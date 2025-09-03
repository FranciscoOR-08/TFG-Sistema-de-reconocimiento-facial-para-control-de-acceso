/*******************************************************
 * Proyecto TFG – Reconocimiento Facial en ESP32-CAM
 * Autor: Francisco
 *
 * Descripción (esta versión):
 *  - Cliente WebSocket que se conecta a FastAPI (/ws/stream).
 *  - Servidor HTTP local con endpoints /send-command y /stream.
 *  - Enrolamiento y reconocimiento facial en modo local o servidor.
 *  - Sincronización de embeddings con FastAPI (GET/POST).
 *  - Envío de eventos de reconocimiento al backend.
 *******************************************************/


// ====================================================
// LIBRERÍAS
// ====================================================
// Librerías para comunicación, cámara y procesamiento
#include <ArduinoWebsockets.h>    // WebSockets en tiempo real
#include "esp_http_server.h"      // Servidor HTTP integrado
#include "esp_timer.h"            // Temporizador
#include "esp_camera.h"           // Control de la cámara OV2640
#include "camera_index.h"         // HTML embebido de ejemplo (no principal en esta versión)
#include "Arduino.h"
#include <HTTPClient.h>           // Cliente HTTP para REST (con FastAPI)
#include "fd_forward.h"           // Detección de rostros
#include "fr_forward.h"           // Reconocimiento facial (embeddings)
#include "fr_flash.h"             // Gestión de memoria flash (rostros)
#include "fb_gfx.h"
#include "mbedtls/base64.h"
#include "Base64.h"
#include "base64.hpp"
#include <ArduinoJson.h>          // Serialización JSON

// ====================================================
// CONFIGURACIÓN Y CONSTANTES
// ====================================================
#define FACE_COLOR_WHITE  0x00FFFFFF
#define FACE_COLOR_BLACK  0x00000000
#define FACE_COLOR_RED    0x000000FF
#define FACE_COLOR_GREEN  0x0000FF00
#define FACE_COLOR_BLUE   0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN   (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)

// Datos de WiFi (sustituir por credenciales propias)
const char *ssid = "Avatel_vUb7";
const char *password = "Ufp4CFUY";
const char* SERVER_IP = "192.168.18.14";

// Configuración de reconocimiento facial
#define ENROLL_CONFIRM_TIMES 5    // Nº de capturas necesarias para enrolar un rostro
#define FACE_ID_SAVE_NUMBER 7     // Nº máximo de rostros a guardar en memoria flash
#define HTTPD_400_BAD_REQUEST "400 Bad Request"

// Selección de cámara: AI Thinker ESP32-CAM
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// =====================================================
// VARIABLES GLOBALES
// =====================================================
using namespace websockets;

WebsocketsClient client;     // Cliente WebSocket hacia FastAPI
camera_fb_t * fb = NULL;     // Frame buffer de la cámara

// Control de tiempos
long current_millis;
long last_detected_millis = 0;

// Estados y banderas de reconocimiento
bool face_recognised = false;
int enroll_samples_left = 0;         // Nº de capturas restantes para enrolamiento
bool recognition_sent = false;
unsigned long last_recognition_time = 0;
const unsigned long recognition_interval = 5000;  // Mínimo 5 s entre envíos de resultados
bool use_server_mode = true;         // true=procesamiento en servidor, false=local

// Control de intervalos de procesado
unsigned long last_processing_time = 0;
const unsigned long processing_interval_default = 10000; // 10s por defecto
const unsigned long processing_interval_enroll = 2000;   // 2s durante enrolamiento
unsigned long processing_interval = processing_interval_default;
bool face_detected_this_interval = false;

// Conexión WebSocket
bool isConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000; // 5 segundos entre intentos

// Resultado procesado de imagen
typedef struct {
  uint8_t *image;
  box_array_t *net_boxes;
  dl_matrix3d_t *face_id;
} http_img_process_result;

// Lista de rostros guardados en memoria (ESP32)
face_id_name_list st_face_list;
//httpd_handle_t camera_httpd = NULL;  // Handler HTTP (no principal en esta versión)

// Máquina de estados (FSM principal)
typedef enum {
  START_STREAM,
  START_DETECT,
  SHOW_FACES,
  START_RECOGNITION,
  START_ENROLL,
  ENROLL_COMPLETE,
  DELETE_ALL,
} en_fsm_state;
en_fsm_state g_state;

// Estructura auxiliar para enrolamiento
typedef struct {
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;
httpd_resp_value st_name;

// Configuración del servidor WebSocket (FastAPI)
const char* ws_server_host = "192.168.18.14";
const uint16_t ws_server_port = 8000;
const char* ws_server_path = "/ws/stream";

// =====================================================
// CONFIGURACIÓN DEL DETECTOR DE CARAS (MTCNN)
// =====================================================
static inline mtmn_config_t app_mtmn_config()
{
  mtmn_config_t mtmn_config = {0};
  mtmn_config.type = FAST;        // Detector rápido
  mtmn_config.min_face = 80;      // Tamaño mínimo de cara en px
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
  return mtmn_config;
}
mtmn_config_t mtmn_config = app_mtmn_config();

// =====================================================
// SETUP – INICIALIZACIÓN
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configuración hardware cámara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Inicializar cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Ajustes de sensor (resolución inicial)
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  // Conexión WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP ESP32: 'http://");
  Serial.println(WiFi.localIP());

  // Iniciar conexión WebSocket con servidor FastAPI
  client.onMessage(onMessageCallback);    // Callback mensajes entrantes
  String wsUrl = "ws://" + String(SERVER_IP) + ":8000/ws/stream";
  bool connected = client.connect(wsUrl.c_str());
  if (connected) {
    Serial.println("Conectado a FastAPI WS");
  } else {
    Serial.println("Error de conexión WS");
  }

  // Iniciar servidor HTTP local (/send-command y /stream)
  start_server();

  // Inicializar FaceNet y cargar embeddings del servidor
  app_facenet_main();
  load_embeddings_from_server(&st_face_list);
}

// ====================================================
// CALLBACK WS – Logging de mensajes recibidos
// ====================================================
/**
 * Callback para mensajes recibidos por WebSocket desde FastAPI.
 * En esta versión, solo se hace logging en el monitor serie.
 */
void onMessageCallback(WebsocketsMessage message) {
  Serial.print("Mensaje recibido: ");
  Serial.println(message.data());
}


// ====================================================
// HTTP SERVER – Handlers y rutas
// ====================================================

/**
 * Handler GET /
 * Devuelve un mensaje de prueba para verificar que el servidor HTTP está activo.
 */
esp_err_t root_get_handler(httpd_req_t *req){
    const char resp[] = "Hola desde ESP HTTP Server";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/**
 * Handler POST /send-command (versión simple).
 * Lee el body de la petición, pero aquí solo devuelve "Comando recibido".
 * En esta versión real se reemplaza por handle_http_command().
 */
esp_err_t send_command_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }
    const char* resp = "Comando recibido";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}


// ====================================================
// HTTP – Procesador de comandos (POST /send-command)
// ====================================================
/**
 * Handler principal para POST /send-command.
 * Procesa el parámetro `cmd` recibido en el body (form-urlencoded).
 * Dependiendo del valor, cambia el estado global del sistema.
 *
 * Comandos soportados:
 *   - stream → activa streaming de vídeo
 *   - detect → inicia detección de rostros
 *   - capture:<nombre> → enrola un rostro con el nombre dado
 *   - recognise → inicia reconocimiento facial
 *   - remove:<nombre> → borra un rostro por nombre
 *   - esp32_mode → activa modo local (procesamiento en el ESP32)
 *   - server_mode → activa modo servidor (procesamiento en FastAPI)
 */
esp_err_t handle_http_command(httpd_req_t *req) {
    char buf[200];
    int total_len = req->content_len;
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    // Buscar parámetro cmd en el body
    char *cmd_ptr = strstr(buf, "cmd=");
    if (!cmd_ptr) {
        const char* resp = "No data received";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    cmd_ptr += 4;

    // Extraer valor de cmd
    char cmd[100];
    int i = 0;
    while (cmd_ptr[i] != '\0' && cmd_ptr[i] != '&' && i < sizeof(cmd) - 1) {
        cmd[i] = cmd_ptr[i];
        i++;
    }
    cmd[i] = '\0';

    Serial.print("📨 Comando HTTP recibido: ");
    Serial.println(cmd);

    String cmd_str = String(cmd);
    cmd_str = urlDecode(cmd_str);  // Decodificar por seguridad

    String response;

    if (cmd_str == "stream") {
        g_state = START_STREAM;
        response = "{\"response\": \"streaming\"}";
    } else if (cmd_str == "detect") {
        g_state = START_DETECT;
        response = "{\"response\": \"DETECTING\"}";
    } else if (cmd_str.startsWith("capture")) {
        g_state = START_ENROLL;
        enroll_samples_left = ENROLL_CONFIRM_TIMES;
        String person = cmd_str.substring(8);
        person.toCharArray(st_name.enroll_name, ENROLL_NAME_LEN);
        response = "{\"response\": \"CAPTURING\"}";
    } else if (cmd_str == "recognise") {
        g_state = START_RECOGNITION;
        response = "{\"response\": \"RECOGNISING\"}";
    } else if (cmd_str.startsWith("remove:")) {
        String person = cmd_str.substring(7);
        char name[ENROLL_NAME_LEN * FACE_ID_SAVE_NUMBER];
        person.toCharArray(name, sizeof(name));
        delete_face_id_in_flash_with_name(&st_face_list, name);
        response = "{\"response\": \"REMOVED\"}";
    } else if (cmd_str == "esp32_mode") {
        use_server_mode = false;
        response = "{\"response\": \"ESP32_MODE_ON\"}";
    } else if (cmd_str == "server_mode") {
        use_server_mode = true;
        response = "{\"response\": \"SERVER_MODE_ON\"}";
    } else {
        response = "{\"response\": \"UNKNOWN_COMMAND\"}";
        httpd_resp_set_status(req, "400 Bad Request");
    }

    // Respuesta en formato JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, response.c_str(), strlen(response.c_str()));
    if (err != ESP_OK) {
        Serial.printf("Error enviando respuesta HTTP: %d\n", err);
    }

    return ESP_OK;
}


// ====================================================
// RUTAS HTTP
// ====================================================

// POST /send-command
httpd_uri_t send_command_uri = {
    .uri = "/send-command",
    .method = HTTP_POST,
    .handler = handle_http_command,
    .user_ctx = NULL
};

// GET /stream (MJPEG streaming de la cámara)
httpd_uri_t stream_uri = {
  .uri       = "/stream",
  .method    = HTTP_GET,
  .handler   = stream_handler,   
  .user_ctx  = NULL
};


// ====================================================
// INICIALIZACIÓN DEL SERVIDOR HTTP
// ====================================================
/**
 * Inicia el servidor HTTP del ESP32 en el puerto por defecto.
 * Registra las rutas:
 *   - /send-command → para recibir comandos desde la app móvil.
 *   - /stream → para streaming MJPEG de la cámara (opcional).
 */
void start_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &send_command_uri);
        //httpd_register_uri_handler(server, &stream_uri);
        Serial.println("HTTP Server started, handlers registered.");
    } else {
        Serial.println("Failed to start HTTP server");
    }
}


// ====================================================
// UTILIDADES – Gestión de lista de rostros, URL decode
// ====================================================

/**
 * Libera memoria y vacía la lista enlazada de rostros guardados.
 * Importante para mantener la RAM limpia cuando se borran usuarios.
 */
void clear_face_list(face_id_name_list *list) {
    face_id_node *current = list->head;
    while (current != NULL) {
        face_id_node *next = current->next;
        free(current);  // libera memoria de cada nodo
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;  // reset contador
}

/*
// Ejemplo de borrado total de rostros en flash y RAM
void delete_all_faces(websockets::WebsocketsClient* ws_client = nullptr) {
    delete_face_all_in_flash_with_name(&st_face_list);
    if (ws_client && ws_client->available()) {
        ws_client->send("Todos los rostros han sido eliminados.");
    }
    clear_face_list(&st_face_list);
    Serial.printf("Modo RECOGNITION activo, rostros guardados: %d\n", st_face_list.count);
}
*/

/**
 * Decodifica cadenas URL (ejemplo: "Juan+Perez%20Test" → "Juan Perez Test").
 * Útil para nombres enviados desde la app móvil en comandos HTTP.
 */
String urlDecode(const String &input) {
  String decoded = "";
  char c;
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      if (i + 2 < input.length()) {
        c = strtol(input.substring(i + 1, i + 3).c_str(), NULL, 16);
        decoded += c;
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}


// ====================================================
// STREAM MJPEG (GET /stream) – opcional
// ====================================================
/**
 * Handler GET /stream
 * Devuelve el stream de vídeo en formato MJPEG (multipart).
 * Se usa para debug visual en navegador, aunque la app móvil normalmente
 * obtiene la imagen vía WebSocket o peticiones HTTP.
 */
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=123456789000000000000987654321";
  static const char* _STREAM_BOUNDARY = "\r\n--123456789000000000000987654321\r\n";
  static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
      return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      _jpg_buf = fb->buf;
      _jpg_buf_len = fb->len;

      sprintf((char *)part_buf, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, strlen((char *)part_buf));
      if(res == ESP_OK){
        res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
      }
      if(res == ESP_OK){
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
      }
      esp_camera_fb_return(fb);
      if(res != ESP_OK){
        break;
      }
    }
    vTaskDelay(30 / portTICK_PERIOD_MS); // ~30 fps
  }

  return res;
}

/*
void app_httpserver_init() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    Serial.println("Servidor HTTP iniciado solo con /stream");
  }
}
*/

// ====================================================
// FACENET – Inicialización y helpers de embeddings
// ====================================================

/**
 * Inicializa la lista de rostros y prepara estructuras para embeddings.
 */
void app_facenet_main() {
  face_id_name_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
  //aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  //read_face_id_from_flash_with_name(&st_face_list);
}

/**
 * Codifica un embedding (vector de floats) a Base64.
 * Útil si quieres transmitirlo como cadena compacta.
 */
String encode_embedding_to_base64(const float* embedding, size_t length) {
  const unsigned char* bytes = (const unsigned char*)embedding;
  unsigned int byte_length = length * sizeof(float);
  unsigned int base64_buffer_len = 4 * ((byte_length + 2) / 3) + 1;
  unsigned char base64_encoded[base64_buffer_len];
  unsigned int encoded_length = encode_base64((unsigned char*)bytes, byte_length, base64_encoded);
  return String((char*)base64_encoded);
}

/**
 * Serializa un embedding a JSON (ejemplo: "[0.12, 0.34, ...]").
 */
String encode_embedding_to_json(float* embedding, size_t length) {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < length; i++) {
    arr.add(embedding[i]);
  }
  String output;
  serializeJson(doc, output);
  return output;
}

/**
 * Convierte un vector float[] en una estructura dl_matrix3d_t.
 * Esto es necesario para trabajar con el stack de reconocimiento de ESP-WHO.
 */
dl_matrix3d_t* create_dl_matrix3d_from_vector(const float* src, int len) {
    dl_matrix3d_t* m = dl_matrix3d_alloc(1, 1, 1, len);
    if (!m) return nullptr;
    memcpy(m->item, src, len * sizeof(float));
    return m;
}

/**
 * Inserta un nuevo rostro en la lista enlazada local.
 * Cada nodo guarda el nombre y su embedding correspondiente.
 */
void insert_face_id_to_list_with_name(face_id_name_list* list, float* vec, const char* name) {
    face_id_node* new_face = new face_id_node;
    strncpy(new_face->id_name, name, ENROLL_NAME_LEN);
    new_face->id_name[ENROLL_NAME_LEN - 1] = '\0';
    new_face->id_vec = create_dl_matrix3d_from_vector(vec, FACE_ID_SIZE);
    new_face->next = NULL;

    if (list->tail) {
        list->tail->next = new_face;
    } else {
        list->head = new_face;
    }
    list->tail = new_face;
    list->count++;
}

/**
 * Crea un cuerpo JSON con embedding y nombre asociado.
 * Ejemplo:
 * {
 *   "embedding": [0.12, 0.34, ...],
 *   "nombre": "Francisco"
 * }
 */
String create_embedding_post_body(float* embedding, size_t length, const char* nombre) {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.createNestedArray("embedding");
  for (size_t i = 0; i < length; i++) {
    arr.add(embedding[i]);
  }
  doc["nombre"] = nombre;
  String output;
  serializeJson(doc, output);
  return output;
}

// ====================================================
// HTTP -> BACKEND FASTAPI (enroll, obtener embeddings, result)
// ====================================================

/**
 * Envía al servidor FastAPI el resultado de un reconocimiento facial.
 * Se guarda en la base de datos del backend.
 *
 * Parámetros:
 *  - face_id: ID local del rostro (si aplica, -1 si no)
 *  - status: "success" | "error"
 *  - message: nombre del usuario o "Usuario desconocido"
 */
void send_face_recognition_result(int face_id, const char* status, const char* message) {
  HTTPClient http;
  WiFiClient client;

  String serverURL = "http://" + String(SERVER_IP) + ":8000/recognition-result/";

  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  // Construir JSON manualmente
  String jsonBody = "{\"status\":\"" + String(status) + 
                    "\", \"message\":\"" + String(message) + 
                    "\", \"face_id\":" + (face_id >= 0 ? String(face_id) : "null") + "}";

  Serial.println("Enviando JSON: " + jsonBody);
  int httpResponseCode = http.POST(jsonBody);

  if (httpResponseCode > 0) {
    Serial.println("HTTP Response code: " + String(httpResponseCode));
    Serial.println("Server response: " + http.getString());
  } else {
    Serial.println("Error en la solicitud HTTP: " + String(httpResponseCode));
  }

  http.end();
}


/**
 * Envía un embedding generado en el ESP32 al servidor (modo enroll).
 * Esto se usa cuando el enrolamiento se hace en local y se sincroniza con FastAPI.
 */
void send_embedding_to_server(float* embedding, size_t length, const char* nombre) {
  HTTPClient http;
  WiFiClient client;

  String url = "http://" + String(SERVER_IP) + ":8000/upload-embedding?modo=enroll";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // Construcción manual del JSON con nombre y embedding (512 floats)
  String jsonBody = "{\"nombre\":\"" + String(nombre) + "\",\"embedding\":[";
  for (size_t i = 0; i < length; i++) {
    jsonBody += String(embedding[i], 6);
    if (i != length - 1) jsonBody += ",";
  }
  jsonBody += "]}";

  Serial.println("Enviando JSON al servidor:");
  Serial.println(jsonBody);

  int code = http.POST(jsonBody);
  if (code > 0) {
    Serial.printf("HTTP Response code: %d\n", code);
    Serial.println("Server response: " + http.getString());
  } else {
    Serial.println("Fallo en HTTP POST: " + String(code));
  }

  http.end();
}


/**
 * Carga los embeddings almacenados en el servidor FastAPI
 * y los inserta en la lista local del ESP32.
 */
void load_embeddings_from_server(face_id_name_list *list) {
  HTTPClient http;
  WiFiClient client;

  String url = "http://" + String(SERVER_IP) + ":8000/get-embeddings";
  http.begin(client, url);

  int code = http.GET();
  if (code > 0) {
    String payload = http.getString();
    Serial.println("Embeddings recibidos:");
    Serial.println(payload);

    DynamicJsonDocument doc(17000);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Error al parsear JSON");
      return;
    }

    for (JsonPair kv : doc.as<JsonObject>()) {
      const char* name = kv.key().c_str();
      JsonArray arr = kv.value().as<JsonArray>();

      if (arr.size() != FACE_ID_SIZE) {
        Serial.printf("Embedding inválido para %s (tamaño: %d)\n", name, arr.size());
        continue;
      }

      float* embedding = (float*)malloc(FACE_ID_SIZE * sizeof(float));
      if (!embedding) {
        Serial.println("Error: no se pudo asignar memoria.");
        continue;
      }

      for (size_t i = 0; i < FACE_ID_SIZE; i++) {
        if (!arr[i].is<float>() && !arr[i].is<int>()) {
          Serial.printf("Valor inválido en embedding de %s en índice %d\n", name, i);
          free(embedding);
          embedding = nullptr;
          break;
        }
        embedding[i] = arr[i].as<float>();
      }

      if (embedding) {
        Serial.printf("Insertando embedding para: %s\n", name);
        insert_face_id_to_list_with_name(list, embedding, name);
        free(embedding); // ya se copió en insert_face_id_to_list_with_name
      }
    }

  } else {
    Serial.println("Fallo al obtener embeddings");
  }

  http.end();
}


/**
 * Envía la lista de caras registradas al cliente WebSocket.
 * Primero manda "delete_faces" y luego cada nombre en formato "listface:<nombre>".
 */
static esp_err_t send_face_list(WebsocketsClient &client) {
  client.send("delete_faces"); 
  face_id_node *head = st_face_list.head;
  char add_face[64];
  for (int i = 0; i < st_face_list.count; i++) {
    sprintf(add_face, "listface:%s", head->id_name); 
    client.send(add_face);
    head = head->next;
  }
}


/**
 * Envía un frame capturado al backend para procesar (detect, recognize o enroll).
 * Retorna el mensaje que FastAPI devuelve para mostrar en la app móvil.
 */
String sendCapturedImageToServer(camera_fb_t *fb) {
  if (!WiFi.isConnected()) {
    Serial.println("WiFi no conectado");
    return "SERVER ERROR";
  }

  HTTPClient http;
  WiFiClient client;

  // Determinar modo según estado global
  String mode_str = "detect";
  if (g_state == START_RECOGNITION) mode_str = "recognize";
  else if (g_state == START_ENROLL) mode_str = "enroll";

  // Construir URL con parámetros
  String serverUrl = "http://" + String(SERVER_IP) + ":8000/upload-image?modo=" + mode_str;
  if (mode_str == "enroll") {
    serverUrl += "&nombre=" + String(st_name.enroll_name);
  }

  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "image/jpeg");

  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Respuesta servidor: %d - %s\n", httpResponseCode, response.c_str());

    // Extraer campo "message" del JSON
    String message = "UNKNOWN";
    int idx_message = response.indexOf("\"message\":\"");
    if (idx_message >= 0) {
      int start = idx_message + 11;
      int end = response.indexOf("\"", start);
      message = response.substring(start, end);
      message.replace("\\n", "\n");
    }

    http.end();

    // Normalizar mensajes para la app Flutter
    if (message == "Face detected") {
      return "FACE DETECTED";
    } else if (message == "Unknown face") {
      return "FACE NOT RECOGNISED";
    } else if (message.startsWith("Face '") && message.indexOf("enrolled") >= 0) {
      return message;  // mensajes de enrolamiento se devuelven tal cual
    }

    return message;
  } else {
    Serial.printf("Error en POST: %d\n", httpResponseCode);
    http.end();
    return "SERVER ERROR";
  }
}


// ====================================================
// ENROLAMIENTO Y RECONOCIMIENTO
// ====================================================

/**
 * Realiza el proceso de enrolamiento de un nuevo rostro.
 * Cuando se completan las muestras requeridas, envía el embedding al servidor.
 */
static inline int do_enrollment(face_id_name_list *face_list, dl_matrix3d_t *new_id) {
  Serial.println("START ENROLLING");

  int left_sample_face = enroll_face_id_to_flash_with_name(face_list, new_id, st_name.enroll_name);
  Serial.printf("Samples left: %d\n", left_sample_face);
  Serial.printf("Face ID %s Enrollment: Sample %d\n", 
                st_name.enroll_name, ENROLL_CONFIRM_TIMES - left_sample_face);

  if (left_sample_face == -2) { // enrolamiento completo
    Serial.println("Enroll completo, enviando embedding al servidor...");
    if (face_list->tail && face_list->tail->id_vec) {
      float* embedding_ptr = face_list->tail->id_vec->item;
      send_embedding_to_server(embedding_ptr, FACE_ID_SIZE, st_name.enroll_name);
    } else {
      Serial.println("No se pudo acceder al embedding del rostro enrolado");
    }
  }

  return left_sample_face;
}


// =====================================================
// LOOP PRINCIPAL
// =====================================================

/**
 * Bucle principal del ESP32-CAM.
 * Gestiona:
 *   - Conexión y reconexión WebSocket al backend FastAPI.
 *   - Procesamiento de frames en modo local (ESP32) o remoto (servidor).
 *   - Flujo de enrolamiento y reconocimiento.
 *   - Envío de imágenes y mensajes a la app móvil vía WS.
 */
void loop() {
  static bool already_warned_no_face = false;

  // ================================
  // 1. Conexión / Reconexión WS
  // ================================
  if (!isConnected) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Intentando conectar WS...");
      String wsUrl = "ws://" + String(SERVER_IP) + ":8000/ws/stream";
      if (client.connect(wsUrl.c_str())) {
        Serial.println("🟢 Cliente WebSocket conectado");
        isConnected = true;
        send_face_list(client);   // Enviar lista inicial de caras
        recognition_sent = false;
      } else {
        Serial.println("Error al conectar WS");
      }
      lastReconnectAttempt = now;
    }
  }

  // ================================
  // 2. Si está conectado
  // ================================
  if (isConnected) {
    if (!client.available()) {
      Serial.println("WebSocket desconectado");
      isConnected = false;
      return;
    }

    client.poll(); // Mantener WS activo

    fb = esp_camera_fb_get();
    if (!fb) return;

    // Ajustar intervalo según estado
    processing_interval = (g_state == START_ENROLL) ? processing_interval_enroll : processing_interval_default;
    bool process_now = (millis() - last_processing_time >= processing_interval);

    // ================================
    // 3. PROCESAMIENTO LOCAL (ESP32)
    // ================================
    if (!use_server_mode &&
        (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) &&
        process_now) {

      last_processing_time = millis();
      face_detected_this_interval = false;

      // Reservar buffer para conversión RGB888
      dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, 320, 240, 3);
      if (!image_matrix) {
        esp_camera_fb_return(fb);
        return;
      }

      http_img_process_result out_res = {0};
      out_res.image = image_matrix->item;

      // Convertir frame a RGB y detectar caras
      fmt2rgb888(fb->buf, fb->len, fb->format, out_res.image);
      out_res.net_boxes = face_detect(image_matrix, &mtmn_config);

      // Si no hay rostros registrados y se intenta reconocer
      if (g_state == START_RECOGNITION && st_face_list.count == 0) {
        client.send("NO HAY ROSTROS REGISTRADOS");
        already_warned_no_face = true;
      }

      // Si se detectó alguna cara
      if (out_res.net_boxes) {
        already_warned_no_face = false;

        dl_matrix3du_t *aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
        if (!aligned_face) {
          Serial.println("❌ No se pudo asignar aligned_face");
          dl_matrix3du_free(image_matrix);
          esp_camera_fb_return(fb);
          return;
        }

        if (align_face(out_res.net_boxes, image_matrix, aligned_face) == ESP_OK) {
          out_res.face_id = get_face_id(aligned_face);
          dl_matrix3du_free(aligned_face);

          if (out_res.face_id) {
            last_detected_millis = millis();
            face_detected_this_interval = true;

            // --- ENROLAMIENTO LOCAL ---
            if (g_state == START_ENROLL && enroll_samples_left > 0) {
              int left_sample_face = do_enrollment(&st_face_list, out_res.face_id);
              enroll_samples_left = left_sample_face;

              // Notificar muestras restantes
              if (enroll_samples_left >= 0 && enroll_samples_left <= ENROLL_CONFIRM_TIMES) {
                int current_sample = ENROLL_CONFIRM_TIMES - enroll_samples_left;
                char enrolling_message[64];
                sprintf(enrolling_message, "SAMPLE NUMBER %d FOR %s", current_sample, st_name.enroll_name);
                client.send(enrolling_message);
              }

              // Cuando el enrolamiento finaliza
              if (enroll_samples_left <= 0) {
                strncpy(st_face_list.tail->id_name, st_name.enroll_name, ENROLL_NAME_LEN);
                char added_message[64];
                sprintf(added_message, "Added %s a la lista", st_face_list.tail->id_name);
                client.send(added_message);

                g_state = START_STREAM;
                client.send("{\"response\": \"STREAMING\"}");
                send_face_list(client);
              }
            }

            // --- RECONOCIMIENTO LOCAL ---
            if (g_state == START_RECOGNITION && st_face_list.count > 0) {
              face_id_node *f = recognize_face_with_name(&st_face_list, out_res.face_id);
              static String last_recognition_result = "";
              if (f) {
                String result = String("Bienvenido ") + f->id_name;
                if (result != last_recognition_result) {
                  client.send(result.c_str());
                  send_face_recognition_result(-1, "success", f->id_name);
                  last_recognition_result = result;
                }
              } else {
                String result = "FACE NOT RECOGNISED";
                if (result != last_recognition_result) {
                  client.send(result.c_str());
                  send_face_recognition_result(-1, "error", "Unknown face");
                  last_recognition_result = result;
                }
              }
            }

            dl_matrix3d_free(out_res.face_id);
          }
        } else {
          Serial.println("❌ align_face falló");
        }

      } else {
        // No se detectaron caras
        if ((g_state == START_ENROLL || g_state == START_RECOGNITION) && !already_warned_no_face) {
          client.send("NO FACE DETECTED");
          already_warned_no_face = true;
        }
      }

      dl_matrix3du_free(image_matrix);

      // --- DETECCIÓN SIMPLE ---
      if (g_state == START_DETECT) {
        client.send(face_detected_this_interval ? "FACE DETECTED" : "NO FACE DETECTED");
      }
    }

    // ================================
    // 4. PROCESAMIENTO EN SERVIDOR
    // ================================
    if (use_server_mode && process_now) {
      last_processing_time = millis();

      if (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) {
        String server_response = sendCapturedImageToServer(fb);
        client.send(server_response);

        // Si se completó el enrolamiento remoto, volver a streaming
        if (g_state == START_ENROLL && server_response.indexOf("enrolled") >= 0) {
          client.send("STATE:START_STREAM");
          g_state = START_STREAM;
          send_face_list(client);
        }
      }
    }

    // ================================
    // 5. Enviar frame en binario (stream)
    // ================================
    if (fb->len > 0 && fb->len < 100000) {
      client.sendBinary((const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);
    fb = NULL;
  }
}







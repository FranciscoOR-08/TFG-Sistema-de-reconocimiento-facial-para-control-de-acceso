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
 *
 *******************************************************/

 // ====================================================
 // LIBRERÍAS
 // ====================================================
#include <ArduinoWebsockets.h>    // WebSockets en tiempo real
#include "esp_http_server.h"      // Servidor HTTP integrado
#include "esp_timer.h"            // Temporizador
#include "esp_camera.h"           // Control cámara OV2640
#include "camera_index.h"         // HTML de ejemplo
#include "Arduino.h"
#include <HTTPClient.h>           // Cliente HTTP (REST)
#include "fd_forward.h"           // Detección de rostros
#include "fr_forward.h"           // Reconocimiento (embeddings)
#include "fr_flash.h"             // Gestión de rostros en flash
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


// Datos de WiFi
const char *ssid = "Avatel_vUb7";
const char *password = "Ufp4CFUY";
const char* SERVER_IP = "192.168.18.14";

// Configuraciones de reconocimiento
#define ENROLL_CONFIRM_TIMES 5    // Veces que se debe capturar una cara para enrolarla
#define FACE_ID_SAVE_NUMBER 7     // Número máximo de caras que se pueden guardar
#define HTTPD_400_BAD_REQUEST "400 Bad Request"

// Selección de modelo de cámara
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// =====================================================
// VARIABLES GLOBALES
// =====================================================

// Usar el namespace de websockets
using namespace websockets;
WebsocketsClient client;  // Servidor WebSocket

camera_fb_t * fb = NULL;  // Frame buffer de la cámara

// Control de tiempos
long current_millis;
long last_detected_millis = 0;

// Control de estados
bool face_recognised = false;
int enroll_samples_left = 0; // Veces restantes para capturar para un enrolamiento
bool recognition_sent = false;
unsigned long last_recognition_time = 0;
const unsigned long recognition_interval = 5000;  // mínimo 5 segundos entre envíos

// Bandera para indicar detección en este intervalo
bool use_server_mode = true;  

// Control de intervalos de procesado
unsigned long last_processing_time = 0;
const unsigned long processing_interval_default = 10000; // 10 segundos para otros modos
const unsigned long processing_interval_enroll = 2000;  // 2 segundos para enrolamiento rápido
unsigned long processing_interval = processing_interval_default;  // variable global
bool face_detected_this_interval = false; // MODIFICADO: solo una bandera
bool isConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000; // 5 segundos

// Resultado procesado
typedef struct {
  uint8_t *image;
  box_array_t *net_boxes;
  dl_matrix3d_t *face_id;
} http_img_process_result;

// Lista de rostros en memoria
face_id_name_list st_face_list; // Estructuras para guardar las caras reconocidas
//static dl_matrix3du_t *aligned_face = NULL; //Variable para guardar la imagen alineada
httpd_handle_t camera_httpd = NULL;  // Handler del servidor HTTP

// Máquina de estados principal
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

// Estructura para guardar nombre de enrolamiento
typedef struct {
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;
httpd_resp_value st_name;


// WebSocket server (por ejemplo tu FastAPI escuchando en 192.168.18.14 puerto 8000)
const char* ws_server_host = "192.168.18.14";
const uint16_t ws_server_port = 8000;
const char* ws_server_path = "/ws/stream";

// =====================================================
// CONFIGURACIÓN DETECTOR DE CARAS (mtmn)
// =====================================================
static inline mtmn_config_t app_mtmn_config()
{
  mtmn_config_t mtmn_config = {0};
  mtmn_config.type = FAST;
  mtmn_config.min_face = 80;
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

// Función de inicialización
// Inicialización
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configuración de la cámara
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

  // Configuración del sensor
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

  // Conectar a WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP ESP32: 'http://");
  Serial.println(WiFi.localIP());
  // Iniciar servidor HTTP solo con /stream
  //app_httpserver_init();
  // Configura callback de mensajes WS entrantes
  client.onMessage(onMessageCallback);
  String wsUrl = "ws://" + String(SERVER_IP) + ":8000/ws/stream";
  bool connected = client.connect(wsUrl.c_str());

  if (connected) {
    Serial.println("Conectado a FastAPI WS");
  } else {
    Serial.println("Error de conexión WS");
  }

  // Conecta con servidor WebSocket
  String ws_url = String("ws://") + ws_server_host + ":" + ws_server_port + ws_server_path;
  Serial.print("Conectando WS a ");
  Serial.println(ws_url);
  client.connect(ws_url);
  // Iniciar WebSocket en puerto 82 (revisa si usas librería Async o personalizada)
  //socket_server.listen(82);
  start_server(); // Inicia el servidor con el endpoint
  app_facenet_main();  // ✅ Inicializa la lista de rostros
  load_embeddings_from_server(&st_face_list);
}

// ====================================================
// CALLBACK WS – Solo logging de mensajes recibidos
// ====================================================
void onMessageCallback(WebsocketsMessage message) {
  Serial.print("Mensaje recibido: ");
  Serial.println(message.data());
}


// ====================================================
// HTTP SERVER – Handlers y rutas
// ====================================================
// Handler para la ruta raíz
esp_err_t root_get_handler(httpd_req_t *req){
    const char resp[] = "Hola desde ESP HTTP Server";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Handler POST /send-command (procesa cmd en body)
esp_err_t send_command_post_handler(httpd_req_t *req) {
    // Aquí procesas la petición POST
    // Por ejemplo, leer el body:
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        // Error al leer
        return ESP_FAIL;
    }
    // Procesar buf...
    const char* resp = "Comando recibido";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// ====================================================
// HTTP – Procesador de comandos (POST /send-command)
// ====================================================
esp_err_t handle_http_command(httpd_req_t *req) {
    // Buffer para leer datos POST
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

    // Buscamos el parámetro cmd= en el body (asumiendo que es application/x-www-form-urlencoded)
    char *cmd_ptr = strstr(buf, "cmd=");
    if (!cmd_ptr) {
        const char* resp = "No data received";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    cmd_ptr += 4; // Apunta al valor después de "cmd="

    // Extraemos valor de cmd hasta & o fin de string
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
    cmd_str = urlDecode(cmd_str);  // ✅ SIEMPRE APLICAR ESTO

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
    } /*else if (cmd_str == "delete_all") {
        delete_all_faces();
        response = "{\"response\": \"ALL_DELETED\"}";
    } */else if (cmd_str == "esp32_mode") {
        use_server_mode = false;
        response = "{\"response\": \"ESP32_MODE_ON\"}";
    } else if (cmd_str == "server_mode") {
        use_server_mode = true;
        response = "{\"response\": \"SERVER_MODE_ON\"}";
    } else {
        response = "{\"response\": \"UNKNOWN_COMMAND\"}";
        httpd_resp_set_status(req, "400 Bad Request");
    }

    // Enviar respuesta JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, response.c_str(), strlen(response.c_str()));
    if (err != ESP_OK) {
        Serial.printf("Error enviando respuesta HTTP: %d\n", err);
    }

    return ESP_OK;
}

// Ruta POST /send-command
httpd_uri_t send_command_uri = {
    .uri = "/send-command",
    .method = HTTP_POST,
    .handler = handle_http_command,
    .user_ctx = NULL
};

// Ruta GET /stream (MJPEG)
httpd_uri_t stream_uri = {
  .uri       = "/stream",
  .method    = HTTP_GET,
  .handler   = stream_handler,
  .user_ctx  = NULL
};


// Arrancar servidor HTTP con handlers registrados
void start_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        // Registrar URI handlers
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
void clear_face_list(face_id_name_list *list) {
    face_id_node *current = list->head;
    while (current != NULL) {
        face_id_node *next = current->next;
        free(current);  // o dl_matrix3d_free si usas otro tipo
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;  // ¡Muy importante actualizar este contador!
}

/*void delete_all_faces(websockets::WebsocketsClient* ws_client = nullptr) {
    // Aquí llamas a la función que borra las caras en la flash
    delete_face_all_in_flash_with_name(&st_face_list);

    if (ws_client && ws_client->available()) {
        ws_client->send("📂 Todos los rostros han sido eliminados.");
    }
    clear_face_list(&st_face_list);
    Serial.printf("Modo RECOGNITION activo, rostros guardados: %d\n", st_face_list.count);
    Serial.println("📂 Todos los rostros han sido eliminados.");
}
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
    vTaskDelay(30 / portTICK_PERIOD_MS);
  }

  return res;
}


// Registrar handler /stream
void app_httpserver_init() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    Serial.println("Servidor HTTP iniciado solo con /stream");
  }
}


// ====================================================
// FACENET – Inicialización y helpers de embeddings
// ====================================================
// Inicialización de FaceNet(reconocimiento)
void app_facenet_main() {
  face_id_name_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES); //Inicializa la lista de rostros vacia 
  //aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3); //Reserva memoria para una imagen de un rostro
  //read_face_id_from_flash_with_name(&st_face_list); //Carga desde la flash los rostros registrados
}

// Encode embedding a base64 (usa función encode_base64 externa)
String encode_embedding_to_base64(const float* embedding, size_t length) {
  const unsigned char* bytes = (const unsigned char*)embedding;
  unsigned int byte_length = length * sizeof(float);

  // Calcula tamaño necesario para la salida base64 (aprox)
  unsigned int base64_buffer_len = 4 * ((byte_length + 2) / 3) + 1;
  unsigned char base64_encoded[base64_buffer_len];

  unsigned int encoded_length = encode_base64((unsigned char*)bytes, byte_length, base64_encoded);

  // base64_encoded ya tiene terminador nulo, así que puedes hacer un String directamente
  return String((char*)base64_encoded);
}

// Encode embedding a JSON array (ArduinoJson)
String encode_embedding_to_json(float* embedding, size_t length) {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.to<JsonArray>();

  for (size_t i = 0; i < length; i++) {
    arr.add(embedding[i]);
  }

  String output;
  serializeJson(doc, output);
  return output;  // Ejemplo: "[0.123, 0.456, ...]"
}

// Crear dl_matrix3d_t desde vector float[]
dl_matrix3d_t* create_dl_matrix3d_from_vector(const float* src, int len) {
    dl_matrix3d_t* m = dl_matrix3d_alloc(1, 1, 1, len);  // c = len
    if (!m) return nullptr;

    memcpy(m->item, src, len * sizeof(float));
    return m;
}

// Insertar rostro en lista con nombre y embedding
void insert_face_id_to_list_with_name(face_id_name_list* list, float* vec, const char* name) {
    face_id_node* new_face = new face_id_node;
    strncpy(new_face->id_name, name, ENROLL_NAME_LEN);
    new_face->id_name[ENROLL_NAME_LEN - 1] = '\0';  // asegurar terminador

    new_face->id_vec = create_dl_matrix3d_from_vector(vec, FACE_ID_SIZE);

    new_face->next = NULL;

    // Insertar en la lista enlazada
    if (list->tail) {
        list->tail->next = new_face;
    } else {
        list->head = new_face;
    }
    list->tail = new_face;
    list->count++;
}

// Crear cuerpo JSON para envío embedding + nombre
String create_embedding_post_body(float* embedding, size_t length, const char* nombre) {
  StaticJsonDocument<2048> doc;  // Ajusta el tamaño según el tamaño del embedding

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
// Enviar resultado reconocimiento al servidor
void send_face_recognition_result(int face_id, const char* status, const char* message) {
  HTTPClient http;
  
  // Usar directamente WiFiClient como parte de HTTPClient
  WiFiClient client;
  

  // Dirección del servidor FastAPI
  String serverURL = "http://" + String(SERVER_IP) + ":8000/recognition-result/";

  // Iniciar la solicitud HTTP
  http.begin(client, serverURL);  // Pasar el WiFiClient al HTTPClient
  http.addHeader("Content-Type", "application/json");

  // Crear el JSON con los resultados del reconocimiento
  String jsonBody = "{\"status\":\"" + String(status) + "\", \"message\":\"" + String(message) + "\", \"face_id\":" + (face_id >= 0 ? String(face_id) : "null") + "}";

  Serial.println("Enviando JSON: " + jsonBody);
  int httpResponseCode = http.POST(jsonBody);

  if (httpResponseCode > 0) {
    Serial.println("HTTP Response code: " + String(httpResponseCode));
    String response = http.getString();
    Serial.println("Server response: " + response);
  } else {
      Serial.println("Error en la solicitud HTTP: " + String(httpResponseCode));
  }

  http.end();  // Finalizar la solicitud
}


// Enviar embedding al servidor (modo enroll)
void send_embedding_to_server(float* embedding, size_t length, const char* nombre) {
  HTTPClient http;
  WiFiClient client;

  String url = "http://" + String(SERVER_IP) + ":8000/upload-embedding?modo=enroll";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // Crear el JSON manualmente
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
    String response = http.getString();
    Serial.println("Server response: " + response);
  } else {
    Serial.println("Fallo en HTTP POST: " + String(code));
  }

  http.end();
}

// Cargar embeddings desde servidor, llenar lista local
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

    DynamicJsonDocument doc(17000);  // Estima un tamaño mayor si tienes muchos embeddings
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Error al parsear JSON");
      return;
    }

    for (JsonPair kv : doc.as<JsonObject>()) {
      const char* name = kv.key().c_str();
      JsonArray arr = kv.value().as<JsonArray>();

      if (arr.size() != FACE_ID_SIZE) {
        Serial.printf("Embedding inválido para %s (tamaño incorrecto: %d)\n", name, arr.size());
        continue;
      }

      float* embedding = (float*)malloc(FACE_ID_SIZE * sizeof(float));
      if (!embedding) {
        Serial.println("Error: no se pudo asignar memoria para embedding.");
        continue;
      }

      for (size_t i = 0; i < FACE_ID_SIZE; i++) {
        if (!arr[i].is<float>() && !arr[i].is<int>()) {
          Serial.printf("Valor inválido en el embedding de %s en índice %d\n", name, i);
          free(embedding);
          embedding = nullptr;
          break;
        }
        embedding[i] = arr[i].as<float>();
      }

      if (embedding) {
        Serial.printf("Insertando embedding para: %s\n", name);
        insert_face_id_to_list_with_name(list, embedding, name);
        free(embedding);  // asumiendo que insert_face_id_to_list_with_name hace una copia
      }
    }

  } else {
    Serial.println("Fallo al obtener embeddings");
  }

  http.end();
}


// Enviar la lista de caras registradas al cliente
static esp_err_t send_face_list(WebsocketsClient &client) {
  client.send("delete_faces"); 
  face_id_node *head = st_face_list.head; //Puntero al primer nodo de la lista
  char add_face[64]; //Cadena para mensajes
  for (int i = 0; i < st_face_list.count; i++) { //Se ejecuta por cada rostro
    sprintf(add_face, "listface:%s", head->id_name); 
    client.send(add_face);
    head = head->next; //Se avanza al siguiente nodo de la lista
  }
}

String sendCapturedImageToServer(camera_fb_t *fb) {
  if (!WiFi.isConnected()) {
    Serial.println("WiFi no conectado");
    return "SERVER ERROR";
  }

  HTTPClient http;
  WiFiClient client;

  // Definir modo segun estado global
  String mode_str = "detect";
  if (g_state == START_RECOGNITION) mode_str = "recognize"; // Fíjate que en el backend usas "recognize" (no recognition)
  else if (g_state == START_ENROLL) mode_str = "enroll";

  // Construir URL con query param modo
  String serverUrl = "http://" + String(SERVER_IP) + ":8000/upload-image?modo=" + mode_str;

  // Si es modo enroll, añade parámetro nombre
  if (mode_str == "enroll") {
    String enroll_name = String(st_name.enroll_name);  // <-- Cambia "Juan" por tu variable o nombre real a enviar
    serverUrl += "&nombre=" + enroll_name;
  }

  http.begin(client, serverUrl);  // Iniciar HTTP con URL que incluye query string
  http.addHeader("Content-Type", "image/jpeg");

  // Ya NO se añade header X-Mode
  // http.addHeader("X-Mode", mode_str);   <-- eliminar esta línea

  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("Respuesta servidor: %d - %s\n", httpResponseCode, response.c_str());

    String message = "UNKNOWN";
    int idx_message = response.indexOf("\"message\":\"");
    if (idx_message >= 0) {
      int start = idx_message + 11;
      int end = response.indexOf("\"", start);
      message = response.substring(start, end);
      message.replace("\\n", "\n");
    }

    http.end();

    // Normalizar mensaje para que Flutter lo entienda
    if (message == "Face detected") {
      return "FACE DETECTED";
    } else if (message == "Unknown face") {
      return "FACE NOT RECOGNISED";
    } else if (message.startsWith("Face '") && message.indexOf("enrolled") >= 0) {
      return message;  // Deja los mensajes de enrolamiento como están
    }

    return message;  // Otros mensajes
  } else {
    Serial.printf("Error en POST: %d\n", httpResponseCode);
    http.end();
    return "SERVER ERROR";
  }
}

// =====================================================
// ENROLAMIENTO Y RECONOCIMIENTO
// =====================================================
// Enrolar nueva cara, enviar embedding a servidor cuando completo
static inline int do_enrollment(face_id_name_list *face_list, dl_matrix3d_t *new_id) {
  Serial.println("START ENROLLING");

  int left_sample_face = enroll_face_id_to_flash_with_name(face_list, new_id, st_name.enroll_name);
  Serial.print("Samples left to enroll: ");
  Serial.println(left_sample_face);
  Serial.printf("Face ID %s Enrollment: Sample %d\n", st_name.enroll_name, ENROLL_CONFIRM_TIMES - left_sample_face);

  if (left_sample_face == -2) {
    Serial.println("Enroll completo, enviando embedding al servidor...");
    if (face_list->tail && face_list->tail->id_vec) {
      float* embedding_ptr = face_list->tail->id_vec->item;

      // Crear JSON con embedding y nombre
      String postBody = create_embedding_post_body(embedding_ptr, FACE_ID_SIZE, st_name.enroll_name);

      // Cambia la función para enviar el POST con el cuerpo JSON completo
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

void loop() {
  static bool already_warned_no_face = false;

  if (!isConnected) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Intentando conectar WS...");
      String wsUrl = "ws://" + String(SERVER_IP) + ":8000/ws/stream";
      if (client.connect(wsUrl.c_str())) {
        Serial.println("🟢 Cliente WebSocket conectado");
        isConnected = true;
        //client.send("STATE:START_SERVER");
        send_face_list(client);
        //client.send("STREAMING");
        recognition_sent = false;
      } else {
        Serial.println("Error al conectar WS");
      }
      lastReconnectAttempt = now;
    }
  }

  if (isConnected) {
    if (!client.available()) {
      Serial.println("WebSocket desconectado");
      isConnected = false;
      return;
    }

    client.poll();

    fb = esp_camera_fb_get();
    if (!fb) return;

    processing_interval = (g_state == START_ENROLL) ? processing_interval_enroll : processing_interval_default;
    bool process_now = (millis() - last_processing_time >= processing_interval);

    if (!use_server_mode &&
        (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) &&
        process_now) {

      last_processing_time = millis();
      face_detected_this_interval = false;

      dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, 320, 240, 3);
      if (!image_matrix) {
        esp_camera_fb_return(fb);
        return;
      }

      http_img_process_result out_res = {0};
      out_res.image = image_matrix->item;

      fmt2rgb888(fb->buf, fb->len, fb->format, out_res.image);
      out_res.net_boxes = face_detect(image_matrix, &mtmn_config);

      if (g_state == START_RECOGNITION && st_face_list.count == 0) {
              //static bool warned_no_faces = false;
              //if (!warned_no_faces) {
                printf("DEBUG: g_state=%d, st_face_list.count=%d\n", g_state, st_face_list.count);
                client.send("NO HAY ROSTROS REGISTRADOS");
                already_warned_no_face = true;  // Para no mandar también "NO FACE DETECTED"
              //}
      }

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
          aligned_face = NULL;

          

          if (out_res.face_id) {
            last_detected_millis = millis();
            face_detected_this_interval = true;

            if (g_state == START_ENROLL && enroll_samples_left > 0) {
              Serial.printf("HOla");
              int left_sample_face = do_enrollment(&st_face_list, out_res.face_id);
              Serial.printf("🔢 Resultado do_enrollment: %d\n", left_sample_face);
              enroll_samples_left = left_sample_face;

              ESP_LOGI(TAG, "Enroll samples left: %d", enroll_samples_left);
              if (enroll_samples_left >= 0 && enroll_samples_left <= ENROLL_CONFIRM_TIMES) {
                int current_sample = ENROLL_CONFIRM_TIMES - enroll_samples_left;
                Serial.printf("📸 Voy a mandar SAMPLE %d para %s\n",current_sample, st_name.enroll_name);
                char enrolling_message[64];
                sprintf(enrolling_message, "SAMPLE NUMBER %d FOR %s", current_sample, st_name.enroll_name);
                bool ok = client.send(enrolling_message);
                if (!ok) Serial.println("❌ client.send() falló");
              }

              if (enroll_samples_left <= 0) {
                strncpy(st_face_list.tail->id_name, st_name.enroll_name, ENROLL_NAME_LEN);
                ESP_LOGI(TAG, "Enrolled Face ID: %s", st_face_list.tail->id_name);
                char added_message[64];
                sprintf(added_message, "Added %s a la lista", st_face_list.tail->id_name);
                client.send(added_message);

                g_state = START_STREAM;

                // 🔄 Notificar a la app que se cambió al modo streaming
                client.send("{\"response\": \"STREAMING\"}");

                send_face_list(client);
              }
            }
            /*
            if (g_state == START_RECOGNITION && st_face_list.count == 0) {
              //static bool warned_no_faces = false;
              //if (!warned_no_faces) {
                client.send("⚠️ NO HAY ROSTROS REGISTRADOS");
                
              //}
            }
            */
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
        // 🚫 No se detectaron caras en absoluto
        if ((g_state == START_ENROLL || g_state == START_RECOGNITION) && !already_warned_no_face) {
          client.send("NO FACE DETECTED");
          already_warned_no_face = true;
        }
      }

      dl_matrix3du_free(image_matrix);

      if (g_state == START_DETECT) {
        client.send(face_detected_this_interval ? "FACE DETECTED" : "NO FACE DETECTED");
      }
    }

    if (use_server_mode && process_now) {
      last_processing_time = millis();

      if (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) {
        String server_response = sendCapturedImageToServer(fb);
        client.send(server_response);

        if (g_state == START_ENROLL && server_response.indexOf("enrolled") >= 0) {
          client.send("STATE:START_STREAM");
          g_state = START_STREAM;
          send_face_list(client);
        }
      }
    }

    if (fb->len > 0 && fb->len < 100000) {
      client.sendBinary((const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);
    fb = NULL;
  }
}






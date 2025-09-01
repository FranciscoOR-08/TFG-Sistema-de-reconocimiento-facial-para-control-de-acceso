/*******************************************************
 * Proyecto TFG – Sistema de Control de Acceso Facial
 * Dispositivo: ESP32-CAM
 * Autor: Francisco
 * 
 * Descripción:
 *  - Captura de imágenes y detección facial con ESP32-CAM.
 *  - Enrolamiento y reconocimiento facial (modo local y remoto).
 *  - Envío y recepción de embeddings con servidor FastAPI.
 *  - Comunicación en tiempo real con WebSocket.
 *  - Gestión de resultados y sincronización con servidor.
 *******************************************************/

// =====================================================
// LIBRERÍAS
// =====================================================
#include <ArduinoWebsockets.h>    // Comunicación WebSockets
#include "esp_http_server.h"      // Servidor HTTP integrado
#include "esp_timer.h"            // Temporizadores
#include "esp_camera.h"           // Control de la cámara
#include "camera_index.h"         // Página HTML embebida
#include "Arduino.h"
#include <HTTPClient.h>           // Cliente HTTP para peticiones al backend
#include "fd_forward.h"           // Forwarding detección de rostros
#include "fr_forward.h"           // Forwarding reconocimiento facial
#include "fr_flash.h"             // Gestión de memoria flash (caras)
#include "fb_gfx.h"               // Gráficos frame buffer
#include "mbedtls/base64.h"
#include "Base64.h"
#include "base64.hpp"
#include <ArduinoJson.h>          // Serialización JSON


// =====================================================
// CONFIGURACIÓN Y CONSTANTES
// =====================================================
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
#define ENROLL_CONFIRM_TIMES 5    // Nº de capturas necesarias para enrolar
#define FACE_ID_SAVE_NUMBER 7     // Nº máximo de rostros a guardar

// Selección del modelo de cámara
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// =====================================================
// VARIABLES GLOBALES
// =====================================================
using namespace websockets;
WebsocketsServer socket_server;   // Servidor WebSocket

camera_fb_t * fb = NULL;          // Frame buffer de la cámara

// Control de tiempos
long current_millis;
long last_detected_millis = 0;

// Control de estados
bool face_recognised = false;
int enroll_samples_left = 0;      // Veces restantes para enrolar
bool recognition_sent = false;
unsigned long last_recognition_time = 0;
const unsigned long recognition_interval = 5000;  // Min. 5s entre envíos

bool use_server_mode = true;      // true=modo servidor, false=modo ESP32

// Control de intervalos de procesado
unsigned long last_processing_time = 0;
const unsigned long processing_interval_default = 1000; // Intervalo por defecto (ms)
const unsigned long processing_interval_enroll = 2000;  // Intervalo más rápido para enrolamiento (ms)
unsigned long processing_interval = processing_interval_default;  // Variable dinámica

// Bandera para indicar detección en este intervalo
bool face_detected_this_interval = false;

// Resultado procesado
typedef struct {
  uint8_t *image;
  box_array_t *net_boxes;
  dl_matrix3d_t *face_id;
} http_img_process_result;

// Lista de rostros en memoria
face_id_name_list st_face_list;         
static dl_matrix3du_t *aligned_face = NULL; 
httpd_handle_t camera_httpd = NULL;    // Handler HTTP

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

// Estructura para guardar nombre en enrolamiento
typedef struct {
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;
httpd_resp_value st_name;


// =====================================================
// CONFIGURACIÓN DETECTOR DE CARAS (mtmn)
// =====================================================
static inline mtmn_config_t app_mtmn_config() {
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

// Inicializar la config del detector
mtmn_config_t mtmn_config = app_mtmn_config();


// =====================================================
// SETUP – INICIALIZACIÓN
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Configuración cámara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG;

  // Ajuste calidad según PSRAM
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

  // Conexión WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Iniciar HTTP + FaceNet + WebSocket
  app_httpserver_init();
  app_facenet_main();
  socket_server.listen(82);
  load_embeddings_from_server(&st_face_list);

  Serial.printf("Camera Ready! Use 'http://%s' to connect\n", WiFi.localIP().toString().c_str());
}

// =====================================================
// HTTP SERVER
// =====================================================

// Handler para la página principal del servidor web
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

// URI para el index
httpd_uri_t index_uri = {
  .uri       = "/",
  .method    = HTTP_GET,
  .handler   = index_handler,
  .user_ctx  = NULL
};

// Inicialización servidor HTTP
void app_httpserver_init() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
    Serial.println("httpd_start");
  {
    httpd_register_uri_handler(camera_httpd, &index_uri);
  }
}

// =====================================================
// FACENET Y EMBEDDINGS
// =====================================================

// Inicialización principal FaceNet y lista de rostros
void app_facenet_main() {
  face_id_name_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES); //Inicializa la lista de rostros vacia 
  aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3); //Reserva memoria para una imagen de un rostro
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


// Enviar lista de rostros registrados al cliente WebSocket
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


// =====================================================
// BORRADO DE ROSTROS
// =====================================================

static esp_err_t delete_all_faces_esp32(WebsocketsClient &client) {
  HTTPClient http;
  WiFiClient wifiClient;

  String url = "http://" + String(SERVER_IP) + ":8000/clear-embeddings-esp32";
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST("");
  if (code > 0) {
    String response = http.getString();
    Serial.printf("ESP32 -> Borrado OK: %d - %s\n", code, response.c_str());

    face_id_node* node = st_face_list.head;
    while (node) {
      face_id_node* next = node->next;
      if (node->id_vec) dl_matrix3d_free(node->id_vec);
      delete node;
      node = next;
    }
    st_face_list.head = NULL;
    st_face_list.tail = NULL;
    st_face_list.count = 0;

    Serial.println("Lista de caras en RAM eliminada");

    client.send("delete_faces");
  } else {
    Serial.printf("ESP32 -> Error al hacer POST: %d\n", code);
  }

  http.end();
  return ESP_OK;
}

// Borrar todas las caras del servidor remoto (post)
static esp_err_t delete_all_faces_servidor(WebsocketsClient &client) {
  HTTPClient http;
  WiFiClient wifiClient;

  String url = "http://" + String(SERVER_IP) + ":8000/clear-embeddings-servidor";
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST("");
  if (code > 0) {
    String response = http.getString();
    Serial.printf("Servidor -> Borrado OK: %d - %s\n", code, response.c_str());
    // Aquí **NO** se envía delete_faces, porque es para el modo ESP32
  } else {
    Serial.printf("Servidor -> Error al hacer POST: %d\n", code);
  }

  http.end();
  return ESP_OK;
}

// Borrar rostro por nombre esp32 (local y servidor)
static esp_err_t delete_face_by_name(face_id_name_list* st_face_list, const char* person) {
  // 1. Eliminar del servidor FastAPI
  HTTPClient http;
  WiFiClient wifiClient;

  String url = "http://" + String(SERVER_IP) + ":8000/delete-embedding-esp32/" + String(person); 
  http.begin(wifiClient, url);

  int code = http.sendRequest("DELETE");
  if (code > 0) {
    String response = http.getString();
    Serial.printf("Servidor respondió: %d - %s\n", code, response.c_str());
  } else {
    Serial.printf("Fallo en DELETE a /delete-embedding/%s: %d\n", person, code);
  }
  http.end();

  // 2. Eliminar de la flash y lista local
  delete_face_id_in_flash_with_name(st_face_list, (char*)person);

  // 3. Notificar por WebSocket
  //client.send(String("deleted:") + person);

  return ESP_OK;
}

// Borrar rostro en servidor remoto (modo servidor)
void delete_face_by_name_remote(const char* person) {
  HTTPClient http;
  WiFiClient wifiClient;

  String url = "http://" + String(SERVER_IP) + ":8000/delete-embedding-servidor/" + String(person);
  http.begin(wifiClient, url);

  int code = http.sendRequest("DELETE");
  if (code > 0) {
    Serial.printf("[SERVER] Eliminado '%s': %d - %s\n", person, code, http.getString().c_str());
  } else {
    Serial.printf("[SERVER] Fallo al eliminar '%s': %d\n", person, code);
  }

  http.end();
}

// =====================================================
// WEBSOCKET – MENSAJES
// =====================================================

void handle_message(WebsocketsClient &client, WebsocketsMessage msg) {
  if (msg.data() == "stream") {
    g_state = START_STREAM; //Cambio de estado
    client.send("STREAMING");
  }
  if (msg.data() == "detect") {
    g_state = START_DETECT; //Cambio de estado
    client.send("DETECTANDO");
  }
  if (msg.data().substring(0, 8) == "capture:") { //Si el mensaje empieza con capture:
    g_state = START_ENROLL; //Cambia estado
    enroll_samples_left = ENROLL_CONFIRM_TIMES; //Reinicia el contador de muestras
    char person[FACE_ID_SAVE_NUMBER * ENROLL_NAME_LEN] = {0,}; // Array para guardar el nombre
    msg.data().substring(8).toCharArray(person, sizeof(person)); //Coge el nombre, lo convierte en cadena y lo guarda en el array anterior
    memcpy(st_name.enroll_name, person, strlen(person) + 1); //Copia el nombre almacenado en la cadena en la estrucura 
    client.send("REGISTRANDO");
  }
  if (msg.data() == "recognise") {
    g_state = START_RECOGNITION; //Cambio de estado
    client.send("RECONOCIENDO");
  }
  if (msg.data().startsWith("remove_esp32:")) {
    char person[ENROLL_NAME_LEN * FACE_ID_SAVE_NUMBER];
    msg.data().substring(13).toCharArray(person, sizeof(person));
    delete_face_by_name(&st_face_list, person);
    send_face_list(client);
  }
  if (msg.data() == "delete_all_esp32") {
    delete_all_faces_esp32(client);
  }
  if (msg.data() == "esp32_mode") {
    use_server_mode = false;
    client.send("MODO_ESP32_ACTIVADO");
  }
  if (msg.data() == "server_mode") {
    use_server_mode = true;
    client.send("MODO_SERVIDOR_ACTIVADO");
  }
}

// =====================================================
// HTTP CON FASTAPI
// =====================================================

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
        insert_face_id_to_list_with_name(list, embedding, name);
        free(embedding);  // asumiendo que insert_face_id_to_list_with_name hace una copia
      }
    }

  } else {
    Serial.println("Fallo al obtener embeddings");
  }

  http.end();
}

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
    return message;
  } else {
    Serial.printf("Error en POST: %d\n", httpResponseCode);
    http.end();
    return "SERVER ERROR";
  }
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================


void loop() {
  auto client = socket_server.accept();
  client.onMessage(handle_message);

  // Inicialización frame
  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, 320, 240, 3);
  http_img_process_result out_res = {0};
  out_res.image = image_matrix->item;

  // Estado inicial
  client.send("STATE:START_SERVER");
  send_face_list(client);
  client.send("STREAMING");
  recognition_sent = false;

  // Bucle principal
  while (client.available()) {
    client.poll();
    fb = esp_camera_fb_get();

    // Intervalos distintos para enrolamiento
    if (g_state == START_ENROLL) {
      processing_interval = processing_interval_enroll;
    } else {
      processing_interval = processing_interval_default;
    }
    bool process_now = (millis() - last_processing_time >= processing_interval);

    // --- MODO LOCAL (ESP32) --- //
    if (!use_server_mode && (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) && process_now) {
      last_processing_time = millis();
      face_detected_this_interval = false;

      unsigned long start_time = millis();  // ⬅️ INICIO

      out_res.net_boxes = NULL;
      out_res.face_id = NULL;

      fmt2rgb888(fb->buf, fb->len, fb->format, out_res.image);
      out_res.net_boxes = face_detect(image_matrix, &mtmn_config);

      if (out_res.net_boxes) {
        if (align_face(out_res.net_boxes, image_matrix, aligned_face) == ESP_OK) {
          out_res.face_id = get_face_id(aligned_face);
          last_detected_millis = millis();
          face_detected_this_interval = true;

          if (g_state == START_ENROLL && enroll_samples_left > 0) {
            int left_sample_face = do_enrollment(&st_face_list, out_res.face_id);
            enroll_samples_left = left_sample_face;

            ESP_LOGI(TAG, "Muestras de registro restantes: %d", enroll_samples_left);
            char enrolling_message[64];
            if (enroll_samples_left >= 0 && enroll_samples_left <= ENROLL_CONFIRM_TIMES) {
              int current_sample = ENROLL_CONFIRM_TIMES - enroll_samples_left;
              sprintf(enrolling_message, "Muestra número %d para %s", current_sample, st_name.enroll_name);
              client.send(enrolling_message);
            }

            if (enroll_samples_left <= 0) {
              ESP_LOGI(TAG, "ID de rostro: %s", st_face_list.tail->id_name);
              char added_message[64];
              if (st_face_list.tail && strlen(st_face_list.tail->id_name) > 0) {
              sprintf(added_message, "Añadido %s a la lista", st_face_list.tail->id_name);
              } else {
                sprintf(added_message, "Añadido [NOMBRE_INVALIDO]");
              }
              client.send(added_message);
              g_state = START_STREAM;
              send_face_list(client);
            }
          }

          if (g_state == START_RECOGNITION && st_face_list.count > 0) {
            face_id_node *f = recognize_face_with_name(&st_face_list, out_res.face_id);

             unsigned long end_time = millis();  // ⬅️ FIN
             unsigned long elapsed = end_time - start_time;
             Serial.printf("Tiempo de reconocimiento local: %lu ms\n", elapsed);  // Puedes también enviar esto al cliente si quieres log remoto

            if (f) {
              char msg[64];
              sprintf(msg, "Bienvenido %s", f->id_name);
              client.send(msg);
              if (!recognition_sent || (millis() - last_recognition_time > processing_interval)) {
                send_face_recognition_result(-1, "success", f->id_name);
                recognition_sent = true;
                last_recognition_time = millis();
              }
            } else {
              client.send("Rostro no reconocido");
              if (!recognition_sent || (millis() - last_recognition_time > processing_interval)) {
                send_face_recognition_result(-1, "error", "Usuario desconocido");
                recognition_sent = true;
                last_recognition_time = millis();
              }
            }
          }

          dl_matrix3d_free(out_res.face_id);
        }
      }

      if (g_state == START_DETECT) {
        if (face_detected_this_interval) {
          client.send("Rostro detectado");
        } else {
          client.send("Ningún rostro detectado");
        }
      }
    }

    // --- MODO SERVIDOR --- //
    if (use_server_mode && process_now) {
      last_processing_time = millis();

      if (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION) {
        unsigned long start_time = millis();  

        String server_response = sendCapturedImageToServer(fb);
        client.send(server_response);

        unsigned long end_time = millis();    
        unsigned long elapsed = end_time - start_time;
        Serial.printf("Tiempo de reconocimiento en servidor: %lu ms\n", elapsed);

        // Cambio automático si se ha completado el enroll
        if (g_state == START_ENROLL && server_response.indexOf("registrado") >= 0) {
          client.send("STATE:START_STREAM");
          g_state = START_STREAM;
          send_face_list(client);
        }

      }
    }

    client.sendBinary((const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    fb = NULL;
  }
}





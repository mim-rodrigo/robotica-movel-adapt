#include "websocket_server.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <math.h>
#include <ctype.h>

#include "motor_control.h"

// =======================
// Defaults (pode editar aqui)
// =======================
static const char* DEF_WIFI_SSID = "Projeto Adapt";
static const char* DEF_WIFI_PASS = "adaptufjf";
static constexpr float DEF_YAW_DEADBAND = 8.0f;  // graus

// =======================
// Estado atual (ajustável via setters)
// =======================
static const char* g_wifi_ssid = DEF_WIFI_SSID;
static const char* g_wifi_pass = DEF_WIFI_PASS;
static float g_yaw_deadband = DEF_YAW_DEADBAND;

// =======================
// Objetos globais do módulo
// =======================
static AsyncWebServer g_http_server(80);
static AsyncWebSocket g_ws("/ws");

// =======================
// Prototypes internos
// =======================
static void setup_wifi();
static void handle_ws_event(AsyncWebSocket* server,
                            AsyncWebSocketClient* client,
                            AwsEventType type,
                            void* arg,
                            uint8_t* data,
                            size_t len);
static void handle_command_message(AsyncWebSocketClient* client, const String& payload);
static bool parse_command_payload(const String& payload,
                                  float& yawDeg,
                                  String& nonce,
                                  String& timestamp);
static bool execute_yaw_command(float yawDeg, String& executedCommand);
static void send_pong(AsyncWebSocketClient* client,
                      const String& nonce,
                      const String& timestamp,
                      unsigned long executed_at,
                      float yawDeg,
                      const String& executedCommand,
                      bool success);

// =======================
// Implementação dos setters
// =======================
void net_set_wifi(const char* ssid, const char* password) {
  g_wifi_ssid = ssid;
  g_wifi_pass = password;
}

void net_set_yaw_deadband(float deadband_deg) {
  if (!isnan(deadband_deg) && deadband_deg > 0.0f) {
    g_yaw_deadband = deadband_deg;
  }
}

void net_ws_broadcast(const String& message) {
  g_ws.textAll(message);
}

// =======================
// WiFi + WebSocket
// =======================
static void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print(F("Conectando-se a "));
  Serial.println(g_wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(g_wifi_ssid, g_wifi_pass);

  const uint32_t timeout_ms = 30000;
  const uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > timeout_ms) {
      Serial.println(F("\n[WiFi] Timeout ao conectar."));
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi conectado!"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
  }
}

void net_ws_begin() {
  setup_wifi();

  g_ws.onEvent(handle_ws_event);
  g_http_server.addHandler(&g_ws);
  g_http_server.begin();

  Serial.println(F("WebSocket disponível em ws://<ip>/ws"));
}

void net_ws_loop() {
  g_ws.cleanupClients();
}

// =======================
// Tratamento de mensagens
// =======================
static void handle_ws_event(AsyncWebSocket* server,
                            AsyncWebSocketClient* client,
                            AwsEventType type,
                            void* arg,
                            uint8_t* data,
                            size_t len) {
  (void)server;
  switch (type) {
    case WS_EVT_CONNECT: {
      IPAddress ip = client->remoteIP();
      Serial.print(F("[WS] Cliente conectado: "));
      Serial.print(ip);
      Serial.print(':');
      Serial.println(client->remotePort());
      break;
    }
    case WS_EVT_DISCONNECT: {
      Serial.print(F("[WS] Cliente desconectado: #"));
      Serial.println(client->id());
      break;
    }
    case WS_EVT_DATA: {
      AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
      if (!info) return;

      if (info->opcode != WS_TEXT) {
        Serial.println(F("[WS] Ignorando frame não-texto."));
        return;
      }

      if (!(info->final && info->index == 0)) {
        Serial.println(F("[WS] Mensagem fragmentada ignorada."));
        return;
      }

      String message;
      message.reserve(len);
      for (size_t i = 0; i < len; ++i) {
        message += static_cast<char>(data[i]);
      }
      handle_command_message(client, message);
      break;
    }
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
    default:
      break;
  }
}

static void handle_command_message(AsyncWebSocketClient* client, const String& payload) {
  float yawDeg = 0.0f;
  String nonce;
  String timestamp;
  if (!parse_command_payload(payload, yawDeg, nonce, timestamp)) {
    Serial.print(F("[WS] Payload inválido recebido: "));
    Serial.println(payload);
    return;
  }

  Serial.print(F("[WS] Yaw recebido: "));
  Serial.print(yawDeg, 2);
  Serial.print(F("° | nonce="));
  Serial.print(nonce);
  Serial.print(F(" | t0="));
  Serial.println(timestamp);

  String executedCommand;
  bool success = execute_yaw_command(yawDeg, executedCommand);
  if (!success && executedCommand.length() == 0) {
    executedCommand = F("noop");
  }

  Serial.print(F("[WS] Ação derivada: "));
  Serial.print(executedCommand);
  Serial.print(F(" | sucesso="));
  Serial.println(success ? F("sim") : F("não"));

  unsigned long executed_at = millis();
  send_pong(client, nonce, timestamp, executed_at, yawDeg, executedCommand, success);
}

static bool parse_command_payload(const String& payload,
                                  float& yawDeg,
                                  String& nonce,
                                  String& timestamp) {
  int first_sep = payload.indexOf('|');
  if (first_sep < 0) return false;

  int second_sep = payload.indexOf('|', first_sep + 1);
  if (second_sep < 0) return false;

  String yawStr = payload.substring(0, first_sep);
  yawStr.trim();

  nonce = payload.substring(first_sep + 1, second_sep);
  nonce.trim();

  timestamp = payload.substring(second_sep + 1);
  timestamp.trim();

  if (yawStr.length() == 0 || nonce.length() == 0 || timestamp.length() == 0) {
    return false;
  }

  String yawLower = yawStr;
  yawLower.toLowerCase();
  if (yawLower == F("nan")) {
    yawDeg = NAN;
    return true;
  }

  bool hasDigit = false;
  for (size_t i = 0; i < static_cast<size_t>(yawStr.length()); ++i) {
    char c = yawStr.charAt(i);
    if (isdigit(static_cast<unsigned char>(c))) {
      hasDigit = true;
      break;
    }
    if (c != '-' && c != '+' && c != '.') {
      return false;
    }
  }

  if (!hasDigit) {
    return false;
  }

  yawDeg = yawStr.toFloat();
  if (isnan(yawDeg) || isinf(yawDeg)) {
    return false;
  }

  return true;
}

static bool execute_yaw_command(float yawDeg, String& executedCommand) {
  if (isnan(yawDeg)) {
    set_remote_motion_command(MOTION_STOP);
    executedCommand = F("stop");
    Serial.println(F("[WS] Yaw inválido -> Stop"));
    return true;
  }

  if (yawDeg <= -g_yaw_deadband) {
    set_remote_motion_command(MOTION_TURN_LEFT);
    executedCommand = F("left");
    return true;
  }
  if (yawDeg >= g_yaw_deadband) {
    set_remote_motion_command(MOTION_TURN_RIGHT);
    executedCommand = F("right");
    return true;
  }

  set_remote_motion_command(MOTION_STOP);
  executedCommand = F("stop");
  return true;
}

static void send_pong(AsyncWebSocketClient* client,
                      const String& nonce,
                      const String& timestamp,
                      unsigned long executed_at,
                      float yawDeg,
                      const String& executedCommand,
                      bool success) {
  if (!client) return;

  String payload = nonce;
  payload += '|';
  payload += timestamp;
  payload += '|';
  payload += String(executed_at);
  payload += '|';
  if (isnan(yawDeg)) {
    payload += F("nan");
  } else {
    payload += String(yawDeg, 1);
  }
  payload += '|';
  payload += executedCommand;
  payload += '|';
  payload += success ? F("ok") : F("erro");

  client->text(payload);
}

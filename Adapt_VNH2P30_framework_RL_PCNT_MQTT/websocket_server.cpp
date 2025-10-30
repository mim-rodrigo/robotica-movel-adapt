#include "websocket_server.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ctype.h>
#include <math.h>
#include <type_traits>
#include <utility>

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
static const char DEFAULT_TLS_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDxTCCAq2gAwIBAgIURCaR/PJuG85CVtG1Zm7kNLEu2TswDQYJKoZIhvcNAQEL\n"
    "BQAwcjELMAkGA1UEBhMCQlIxCzAJBgNVBAgMAk1HMRUwEwYDVQQHDAxKdWl6IGRl\n"
    "IEZvcmExFjAUBgNVBAoMDVByb2pldG8gQWRhcHQxETAPBgNVBAsMCFJvYm90aWNz\n"
    "MRQwEgYDVQQDDAtlc3AzMi5sb2NhbDAeFw0yNTEwMzAxOTE1NDBaFw0yNjEwMzAx\n"
    "OTE1NDBaMHIxCzAJBgNVBAYTAkJSMQswCQYDVQQIDAJNRzEVMBMGA1UEBwwMSnVp\n"
    "eiBkZSBGb3JhMRYwFAYDVQQKDA1Qcm9qZXRvIEFkYXB0MREwDwYDVQQLDAhSb2Jv\n"
    "dGljczEUMBIGA1UEAwwLZXNwMzIubG9jYWwwggEiMA0GCSqGSIb3DQEBAQUAA4IB\n"
    "DwAwggEKAoIBAQCKUWsGIobq/mbrOMXXjJ8fiABroaYyFUDrBOrM1Wr1C/LLMqIF\n"
    "bKxdH3F0mR1rTiwjVM0Qn5lIVQ/ciTqaxqNEnr+Yq6l3r7NkReNF7f3ViVMY73Dl\n"
    "zbjD/JF6qgjz6WodoW4XnNSTtVSpNRSxJ9QWhmltIiQnMXn8WBuzejMEGSMU1pzT\n"
    "u3H/JdWK38IP9quOJnS18mIbFsc6NF8kGUEC+B5chmVw22K8yWeTkSjEwSDJOf2H\n"
    "pnfJtr/RVUI/0VIr/nTRlwK92Y9NfigaYixQch9EvAPW22uXnI0AOY7v14cKbg3t\n"
    "7VSzWLT/r9KwckZzu0rvyM3jYPEwTowBWa87AgMBAAGjUzBRMB0GA1UdDgQWBBTX\n"
    "bQ56nexEby5gIoxnzcNkFR/zNzAfBgNVHSMEGDAWgBTXbQ56nexEby5gIoxnzcNk\n"
    "FR/zNzAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQBwBWKgIUJq\n"
    "bpZ0/xQZ5sn4LHliBU5D2m1i00VEPqAtWZLay1/pHVb8IE4Jxsj6kqKsQcOZ2WEm\n"
    "dC6YnnUkvXuJFjeUJ4qTrXg7fLXGsooyM0ykhA4e02gouN7tIK1I8gdPGfEWANDO\n"
    "Vg9Jb3BKj6mIrIsOs19UKZ20SP8FzwdOBNw15iFW7vCUkuo3ei9hsgn+/tqlTD3g\n"
    "wbBsHEny6ZpeVTRWTcSlQIS/CViolfLtAoEJDLup3UN3RIpbB+uBATMpEhjxG0M+\n"
    "5k9ZveV0TtRHNZJIBjrw0Ho3q8kLyZYquVKfSX/51jF8rIw2xny3bkU8dzFe+o/Y\n"
    "jCY0zckIKXso\n"
    "-----END CERTIFICATE-----\n";

static const char DEFAULT_TLS_KEY[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCKUWsGIobq/mbr\n"
    "OMXXjJ8fiABroaYyFUDrBOrM1Wr1C/LLMqIFbKxdH3F0mR1rTiwjVM0Qn5lIVQ/c\n"
    "iTqaxqNEnr+Yq6l3r7NkReNF7f3ViVMY73DlzbjD/JF6qgjz6WodoW4XnNSTtVSp\n"
    "NRSxJ9QWhmltIiQnMXn8WBuzejMEGSMU1pzTu3H/JdWK38IP9quOJnS18mIbFsc6\n"
    "NF8kGUEC+B5chmVw22K8yWeTkSjEwSDJOf2HpnfJtr/RVUI/0VIr/nTRlwK92Y9N\n"
    "figaYixQch9EvAPW22uXnI0AOY7v14cKbg3t7VSzWLT/r9KwckZzu0rvyM3jYPEw\n"
    "TowBWa87AgMBAAECggEAPOnFLaL38rZNocpTSm1JyEuPD9dVBxpYCAgW1VUpcLgt\n"
    "2PG365ajw7DsuJITpCV9h6O5WVhH21Rmk1M15WKUFUyqCPSUQbq1UHP8tlesYSVE\n"
    "XKdZ+0IhW+I3OSN3pN8G1fy5LJnq/g+ttITFU408OB1CgYa3EDGYTJqKvHwUynkX\n"
    "uw+iMCRfIY34YD4fIWFa395ufmwURspfLuZ0EiibRdRlRO6IOUoV9+2sLENyOIh7\n"
    "Tf61JMYX7Ql6MEDneOrdV3UqHoYM6qYKIucCrwY3OF3xPK6j0D3hMP9c94P9zrVt\n"
    "+LiJQ2NrsK5UmC7V5Xrusip6ZDNG6gCN8v0dlRAOgQKBgQC+YjNSkru0QurGGfFi\n"
    "VNjuNH8mzEaPIH6g0rpa7CROMdoYp5V0caheExwXBiEoTfnBrcu++pZ6gBec5EPU\n"
    "mGa71DL+Gt8XGGyJecWhTLTpZ8FRxS3nB7HxlPluRzzdZ5BQmi+gKFVeXAw8w+U+\n"
    "Aj0+zy6u8+zamZesuYs3WJruuwKBgQC5/WdieImip+5RcelK534eRSgkPRaE26Qu\n"
    "rW2iYKw5cuYxp4ygFjfvLyDpTo6xJQgUR81tMHrzSRqFFC8IwoiB+8qLhV51b0Qv\n"
    "+2LLF8Sz1N6KKTPtaMZFcZK5Zvr4SB2Eko6jzujsCaRi2jj3cc8KqF3A2ILNSpSR\n"
    "K7xpWMN5gQKBgEE2wB/L1XI07di38EBfkgNehiOTG6RRXC7YoC8e7ny+hNenKAHA\n"
    "IQ1AfIHCfr8gnqniT4V2ru79S5lZc4ayQZabZHA4Yiy2GA+rX7AV526ANO8+nK+j\n"
    "qid3gU1uJ4IrxHpnpmK1DjEJVMPH0pHAEJygOXyCX6KttA/darulpUSbAoGAJsH4\n"
    "vltyCxRFpHFBdVuCO5qbv9l/DNacgyGe3BybJymbcLOCqYWXyF8g052MPLwDz/4a\n"
    "f+t/Y51TSnInTwMC4VtwHN0BDyXNptYTA1GDqxnr+gyWBp4z2xrMwZgFKqIUjKDh\n"
    "2p7uiOmFeRgSkPYFeCoXx20W7OLizNG5ZJabvYECgYBCnNhEDfNDEpaF3KJeR03c\n"
    "ASGDrdo5JJF3iPTzYZW9yO0zfxin4f/YIgZLzldZbzKQgVAXWAaNI2kHIyDy5u0N\n"
    "o33Kut6jeDfWi5WWRaPWBtRGz381BHR6mFyZV7H8BYO1f+WGSiStYQ8g5OQOLzVb\n"
    "Pl0oBcJCz1Jjht7+aXN61w==\n"
    "-----END PRIVATE KEY-----\n";

static AsyncWebServer* g_http_server = nullptr;
static AsyncWebSocket g_ws("/ws");

static bool g_tls_enabled = true;
static const char* g_tls_cert_override = nullptr;
static const char* g_tls_key_override = nullptr;
static String g_tls_cert_buffer;
static String g_tls_key_buffer;

namespace {
template <typename T>
class has_begin_secure {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(std::declval<U&>().beginSecure("", "", ""), std::true_type{});

  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};
}  // namespace

static constexpr bool SERVER_HAS_BEGIN_SECURE = has_begin_secure<AsyncWebServer>::value;

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

void net_enable_tls(bool enabled) {
  g_tls_enabled = enabled;
}

void net_set_tls_credentials(const char* cert_pem, const char* key_pem) {
  g_tls_cert_override = cert_pem;
  g_tls_key_override = key_pem;
}

static bool prepare_tls_buffers() {
  const char* cert_source = g_tls_cert_override ? g_tls_cert_override : DEFAULT_TLS_CERT;
  const char* key_source = g_tls_key_override ? g_tls_key_override : DEFAULT_TLS_KEY;

  if (!cert_source || !key_source) {
    return false;
  }

  g_tls_cert_buffer = cert_source;
  g_tls_key_buffer = key_source;

  return g_tls_cert_buffer.length() > 0 && g_tls_key_buffer.length() > 0;
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

  if (g_http_server) {
    delete g_http_server;
    g_http_server = nullptr;
  }

  bool tls_ready = false;
  if (g_tls_enabled) {
    tls_ready = prepare_tls_buffers();
    if (!tls_ready) {
      Serial.println(F("[TLS] Certificado ou chave ausentes. Voltando para WS sem criptografia."));
    }
  }

#if defined(ASYNC_TCP_SSL_ENABLED) && ASYNC_TCP_SSL_ENABLED
  const bool tls_supported = true;
#else
  const bool tls_supported = false;
#endif

  if (tls_ready && !tls_supported) {
    Serial.println(F("[TLS] A biblioteca AsyncTCP foi compilada sem suporte a SSL. Voltando para WS sem criptografia."));
    tls_ready = false;
  }

  if (tls_ready && !SERVER_HAS_BEGIN_SECURE) {
    Serial.println(F("[TLS] A versão instalada do ESPAsyncWebServer não oferece beginSecure(). Voltando para WS sem criptografia."));
    tls_ready = false;
  }

  const uint16_t port = tls_ready ? 443 : 80;
  g_http_server = new AsyncWebServer(port);

  g_ws.onEvent(handle_ws_event);
  g_http_server->addHandler(&g_ws);

  if (tls_ready) {
    g_http_server->beginSecure(g_tls_cert_buffer.c_str(), g_tls_key_buffer.c_str(), nullptr);
    Serial.println(F("WebSocket disponível em wss://<ip>/ws (porta 443)"));
  } else {
    g_http_server->begin();
    Serial.println(F("WebSocket disponível em ws://<ip>/ws (porta 80)"));
  }
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

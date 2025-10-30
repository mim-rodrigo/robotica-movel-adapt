#include "mqtt_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <math.h>
#include <ctype.h>

#include "motor_control.h"

// =======================
// Defaults (pode editar aqui)
// =======================
static const char* DEF_WIFI_SSID     = "Rinobont Brasil Não 5GHz";
static const char* DEF_WIFI_PASS     = "autonomos";

static const char* DEF_MQTT_HOST     = "0e51aa7bffcf45618c342e30a71338e8.s1.eu.hivemq.cloud";
static int         DEF_MQTT_PORT     = 8883;  // TLS
static const char* DEF_MQTT_USER     = "hivemq.webclient.1761227941253";
static const char* DEF_MQTT_PASS     = "&a9<Vzb3sC0A!6ZB>xTm";
static bool        DEF_INSECURE_TLS  = true;  // true = setInsecure() (teste rápido)

// Tópico de subscribe
static const char* DEF_SUB_TOPIC     = "facemesh/cmd";
static const char* DEF_PUB_TOPIC     = "facemesh/pong";

// Root CA (opcional). Exemplo:
// static const char* DEF_ROOT_CA_PEM = R"EOF(
// -----BEGIN CERTIFICATE-----
// ... cole o root CA da sua instância HiveMQ Cloud ...
// -----END CERTIFICATE-----
// )EOF";
static const char* DEF_ROOT_CA_PEM   = nullptr;

// =======================
// Estado atual (ajustável via setters)
// =======================
static const char* g_wifi_ssid   = DEF_WIFI_SSID;
static const char* g_wifi_pass   = DEF_WIFI_PASS;

static const char* g_mqtt_host   = DEF_MQTT_HOST;
static int         g_mqtt_port   = DEF_MQTT_PORT;
static const char* g_mqtt_user   = DEF_MQTT_USER;
static const char* g_mqtt_pass   = DEF_MQTT_PASS;
static bool        g_insecureTLS = DEF_INSECURE_TLS;

static const char* g_sub_topic   = DEF_SUB_TOPIC;
static const char* g_pub_topic   = DEF_PUB_TOPIC;
static const char* g_root_ca_pem = DEF_ROOT_CA_PEM;

// =======================
// Objetos globais do módulo
// =======================
static WiFiClientSecure g_secure_client;
static PubSubClient     g_mqtt_client(g_secure_client);

// =======================
// Prototypes internos
// =======================
static void setup_wifi();
static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length);
static void mqtt_reconnect();
static void handle_command_message(const String& payload);
static bool parse_command_payload(const String& payload,
                                  float& yawDeg,
                                  String& nonce,
                                  String& timestamp);
static bool execute_yaw_command(float yawDeg, String& executedCommand);
static void publish_pong(const String& nonce,
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

void net_set_broker(const char* host, int port,
                    const char* username, const char* password,
                    bool insecureTLS) {
  g_mqtt_host   = host;
  g_mqtt_port   = port;
  g_mqtt_user   = username;
  g_mqtt_pass   = password;
  g_insecureTLS = insecureTLS;
}

void net_set_topic(const char* topic) {
  g_sub_topic = topic;
}

void net_set_pub_topic(const char* topic) {
  g_pub_topic = topic;
}

void net_set_root_ca(const char* root_ca_pem) {
  g_root_ca_pem = root_ca_pem;
}

// =======================
// WiFi + MQTT
// =======================
static void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print(F("Conectando-se a "));
  Serial.println(g_wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(g_wifi_ssid, g_wifi_pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - t0 > 30000) {  // timeout 30s
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

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length) {
  Serial.print(F("Mensagem recebida em "));
  Serial.println(topic);

  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    msg += static_cast<char>(payload[i]);
  }

  Serial.print(F("Payload: "));
  Serial.println(msg);
  Serial.println(F("-----------------------"));

  handle_command_message(msg);
}

static void mqtt_reconnect() {
  while (!g_mqtt_client.connected()) {
    Serial.print(F("Tentando MQTT... "));

    String clientId = String("ESP32Client-") + String((uint32_t)esp_random(), HEX);

    // Conecta com usuário/senha (HiveMQ Cloud)
    if (g_mqtt_client.connect(clientId.c_str(), g_mqtt_user, g_mqtt_pass)) {
      Serial.println(F("conectado!"));

      if (g_sub_topic && *g_sub_topic) {
        g_mqtt_client.subscribe(g_sub_topic);
        Serial.print(F("Inscrito em: "));
        Serial.println(g_sub_topic);
      }
    } else {
      Serial.print(F("falhou, rc="));
      Serial.print(g_mqtt_client.state());
      Serial.println(F(" — nova tentativa em 5s"));
      delay(5000);
    }
  }
}

void net_mqtt_begin() {
  setup_wifi();

  // TLS: inseguro para testes OU valida root CA
  if (g_insecureTLS) {
    g_secure_client.setInsecure();
    Serial.println(F("[TLS] setInsecure() habilitado (teste)."));
  } else if (g_root_ca_pem && *g_root_ca_pem) {
    g_secure_client.setCACert(g_root_ca_pem);
    Serial.println(F("[TLS] Root CA configurado (validação ativa)."));
  } else {
    Serial.println(F("[TLS] Aviso: validação pedida sem Root CA — caindo para setInsecure()."));
    g_secure_client.setInsecure();
  }

  g_mqtt_client.setServer(g_mqtt_host, g_mqtt_port);
  g_mqtt_client.setCallback(mqtt_callback);

  // (Opcional) ajuste de performance
  // g_mqtt_client.setKeepAlive(30);
  // g_mqtt_client.setBufferSize(2048);
}

void net_mqtt_loop() {
  if (!g_mqtt_client.connected()) mqtt_reconnect();
  g_mqtt_client.loop();
}

bool net_mqtt_publish(const char* topic, const char* payload) {
  if (!g_mqtt_client.connected()) return false;
  return g_mqtt_client.publish(topic, payload);
}

static void handle_command_message(const String& payload) {
  float yawDeg = 0.0f;
  String nonce;
  String timestamp;
  if (!parse_command_payload(payload, yawDeg, nonce, timestamp)) {
    Serial.println(F("[MQTT] Payload inválido (esperado: yaw|nonce|timestamp)."));
    return;
  }

  Serial.print(F("[MQTT] Yaw recebido: "));
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
  Serial.print(F("[MQTT] Ação derivada: "));
  Serial.print(executedCommand);
  Serial.print(F(" | sucesso="));
  Serial.println(success ? F("sim") : F("não"));
  unsigned long executed_at = millis();
  publish_pong(nonce, timestamp, executed_at, yawDeg, executedCommand, success);
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
  const float yawDeadband = 8.0f;

  if (isnan(yawDeg)) {
    Stop();
    executedCommand = F("stop");
    Serial.println(F("[MQTT] Yaw inválido -> Stop"));
    return true;
  }

  if (yawDeg <= -yawDeadband) {
    TurnLeft();
    executedCommand = F("left");
    return true;
  }
  if (yawDeg >= yawDeadband) {
    TurnRight();
    executedCommand = F("right");
    return true;
  }

  Stop();
  executedCommand = F("stop");
  return true;
}

static void publish_pong(const String& nonce,
                         const String& timestamp,
                         unsigned long executed_at,
                         float yawDeg,
                         const String& executedCommand,
                         bool success) {
  if (!g_pub_topic || !*g_pub_topic) {
    Serial.println(F("[MQTT] Tópico de pong não configurado."));
    return;
  }

  String payload = nonce;
  payload += '|';
  payload += timestamp;
  payload += '|';
  payload += String(executed_at);
  payload += '|';
  payload += String(yawDeg, 2);
  payload += '|';
  payload += executedCommand;
  payload += '|';
  payload += (success ? F("ok") : F("error"));

  if (!g_mqtt_client.connected()) {
    Serial.println(F("[MQTT] Não conectado – não é possível enviar pong."));
    return;
  }

  bool published = g_mqtt_client.publish(g_pub_topic, payload.c_str());
  if (published) {
    Serial.print(F("[MQTT] Pong publicado em "));
    Serial.print(g_pub_topic);
    Serial.print(F(" | payload="));
    Serial.println(payload);
  } else {
    Serial.println(F("[MQTT] Falha ao publicar pong."));
  }
}

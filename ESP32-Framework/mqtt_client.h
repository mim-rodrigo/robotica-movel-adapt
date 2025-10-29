#pragma once
#include <Arduino.h>

// Inicialização e loop do módulo de comunicação
void net_mqtt_begin();     // Conecta WiFi, configura TLS/MQTT e prepara callback
void net_mqtt_loop();      // Mantém conexões (chame em loop())

// --------- Setters (opcionais) ---------
// Se não usar, valores padrão do .cpp serão utilizados.
void net_set_wifi(const char* ssid, const char* password);
void net_set_broker(const char* host, int port,
                    const char* username, const char* password,
                    bool insecureTLS);
// Define o tópico de subscribe (ex.: "facemesh/offset")
void net_set_topic(const char* topic);

// (Opcional) definir Root CA (PEM) para validação TLS.
// Se definido E insecureTLS=false em net_set_broker, usará setCACert(rootCA).
void net_set_root_ca(const char* root_ca_pem);

// (Opcional) publicar algo, caso integre com outros módulos depois.
bool net_mqtt_publish(const char* topic, const char* payload);

#pragma once

#include <Arduino.h>

// Inicialização e manutenção da conectividade Wi-Fi/WebSocket
void net_ws_begin();    // Conecta ao Wi-Fi e inicia o servidor WebSocket
void net_ws_loop();     // Mantém clientes WebSocket (chame em loop())

// --------- Setters opcionais ---------
// Se não forem chamados, os valores padrão definidos no .cpp serão utilizados.
void net_set_wifi(const char* ssid, const char* password);

// Define o intervalo de deadband em graus (|yaw| abaixo do valor vira STOP).
void net_set_yaw_deadband(float deadband_deg);

// Envia texto bruto para todos os clientes conectados (debug opcional).
void net_ws_broadcast(const String& message);

// Ativa/desativa TLS (WSS). Deve ser chamado antes de net_ws_begin().
void net_enable_tls(bool enabled);

// Substitui o certificado/chave padrão em PEM (strings terminadas em \n).
void net_set_tls_credentials(const char* cert_pem, const char* key_pem);

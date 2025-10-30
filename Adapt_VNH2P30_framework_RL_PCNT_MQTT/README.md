# Firmware ESP32 (WebSocket)

Este firmware controla dois motores via VNH2P30 e recebe comandos de direção por WebSocket direto do navegador.

## Comunicação de rede
- Modo STA na mesma rede Wi-Fi configurada nas constantes `DEF_WIFI_SSID` e `DEF_WIFI_PASS` em `websocket_server.cpp`.
- Servidor HTTP/WebSocket na porta 80 com endpoint `ws://<IP_DO_ESP>/ws`.
- Mensagens recebidas: `yaw|nonce|timestamp` (graus).
- Resposta enviada: `nonce|timestamp|execMillis|yaw|acao|status`.

A lógica de decisão converte o yaw recebido em comandos `stop`, `left` ou `right` considerando o deadband configurável via `net_set_yaw_deadband` (padrão 8°).

## Estrutura principal
- `websocket_server.cpp/.h`: gerenciam Wi-Fi e o servidor WebSocket (ESPAsyncWebServer).
- `motor_control.cpp/.h`: controle dos motores, encoders e comandos remotos.
- `Adapt_VNH2P30_framework_RL_PCNT_MQTT.ino`: ponto de entrada, integra controle de motores e rede.

> **Dependências**: instalar as bibliotecas [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) e [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) na IDE Arduino ou PlatformIO antes da compilação.

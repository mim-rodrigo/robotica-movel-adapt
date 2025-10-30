# Firmware ESP32 (WebSocket)

Este firmware controla dois motores via VNH2P30 e recebe comandos de direção por WebSocket direto do navegador.

## Comunicação de rede
- Modo STA na mesma rede Wi-Fi configurada nas constantes `DEF_WIFI_SSID` e `DEF_WIFI_PASS` em `websocket_server.cpp`.
- Servidor WebSocket seguro (`wss://`) na porta 443 por padrão, endpoint `/ws`.
- Caso o certificado TLS não esteja configurado, o firmware faz fallback para `ws://` na porta 80.
- Mensagens recebidas: `yaw|nonce|timestamp` (graus).
- Resposta enviada: `nonce|timestamp|execMillis|yaw|acao|status`.

A lógica de decisão converte o yaw recebido em comandos `stop`, `left` ou `right` considerando o deadband configurável via `net_set_yaw_deadband` (padrão 8°).

## Estrutura principal
- `websocket_server.cpp/.h`: gerenciam Wi-Fi e o servidor WebSocket (ESPAsyncWebServer).
- `motor_control.cpp/.h`: controle dos motores, encoders e comandos remotos.
- `Adapt_VNH2P30_framework_RL_PCNT_MQTT.ino`: ponto de entrada, integra controle de motores e rede.

> **Dependências**: instalar as bibliotecas [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) e [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) na IDE Arduino ou PlatformIO antes da compilação.

## TLS / WSS

- O arquivo `websocket_server.cpp` inclui um certificado e chave RSA autoassinados (CN `esp32.local`) apenas como exemplo.
- Gere e instale um certificado próprio para evitar alertas de segurança. Exemplo usando OpenSSL:
  ```bash
  openssl req -x509 -nodes -newkey rsa:2048 \
    -keyout esp32.key -out esp32.crt -days 365 \
    -subj "/C=BR/ST=MG/L=Juiz de Fora/O=Projeto Adapt/OU=Robotics/CN=esp32.local"
  ```
- Copie o conteúdo PEM para substituir `DEFAULT_TLS_CERT` e `DEFAULT_TLS_KEY` **ou** carregue-os em runtime chamando `net_set_tls_credentials(cert, key)` antes de `net_ws_begin()`.
- Certifique-se de confiar no certificado no sistema operacional/navegador para permitir a conexão `wss://` sem erros.
- Se preferir desativar TLS (uso interno), chame `net_enable_tls(false)` antes de `net_ws_begin()`; o endpoint ficará disponível em `ws://<IP>:80/ws`.

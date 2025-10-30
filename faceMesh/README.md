# FaceMesh WebSocket Controller

Aplicação p5.js/ml5.js que detecta o rosto e envia comandos de yaw diretamente para o ESP32 via WebSocket.

## Configuração rápida

1. Ajuste o IP padrão do ESP32 na constante `DEFAULT_ESP32_IP` dentro de `sketch.js`, ou abra a página informando o host na URL, por exemplo:
   ```
   http://localhost:8080/?esp32=192.168.0.42
   ```
   Também é possível informar a URL completa (`?esp32=ws://192.168.0.42/ws`).
2. Certifique-se de que o ESP32 e o computador estejam na mesma rede Wi-Fi.
3. Sirva a pasta `faceMesh/` a partir de um servidor HTTP simples (por exemplo `npx http-server`).
4. Abra `index.html` no navegador (Chrome recomendado), permita o acesso à webcam e aguarde o status "WebSocket conectado" no HUD.

Quando a cabeça gira para a esquerda/direita, o yaw calibrado é enviado no formato `yaw|nonce|timestamp`. O ESP32 responde com `nonce|t0|execMillis|yaw|acao|status` para medir a latência (RTT).

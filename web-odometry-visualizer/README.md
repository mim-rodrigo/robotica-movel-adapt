# Monitor de odometria em p5.js

Interface pronta para ser copiada no editor online do p5.js. Ela consome os
valores publicados pelo firmware no tópico `robot/odometry` do HiveMQ Cloud,
usando o mesmo host e credenciais da placa.

## Como usar

1. Abra o [editor online do p5.js](https://editor.p5js.org/) e crie um novo
   sketch.
2. Adicione os arquivos `index.html`, `styles.css` e `sketch.js` deste diretório
   (mesmo conteúdo, mesmas referências de CDN).
3. Clique em **Play**. A tela mostrará o carrinho centralizado, seu rastro e os
   indicadores de posição e distância.
4. Use o botão **Conectar** para se inscrever no tópico MQTT.
   O formato esperado é um JSON com `{ "x": 0.0, "y": 0.0, "phi": 0.0 }`
   (metros e radianos). O botão **Resetar trajetória** limpa o rastro e zera a
   distância.

A escala do desenho respeita as dimensões reais do robô (75 × 45 cm de corpo,
rodas conforme tabela) e mantém o carrinho sempre no centro da visualização.

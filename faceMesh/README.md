# FaceMesh + MQTT para controle do robô

Aplicação web em p5.js/ml5.js que usa a webcam para estimar yaw, pitch e roll do
rosto. Ela aplica filtros e calibração (tara) nos ângulos, traduz as leituras em
comandos de movimento e envia para o firmware via MQTT. Também mede a latência
(RTT) com mensagens de "pong" retornadas pelo ESP32.

## Fluxo de execução
1. `index.html` carrega p5.js, ml5.js (FaceMesh) e MQTT over WebSockets.
2. `sketch.js` cria um canvas em tela cheia, captura o vídeo e inicia o modelo
   FaceMesh com `maxFaces=1`.
3. A cada frame:
   - Calcula vetores de referência entre pontos específicos (bochechas, ponte
     nasal e queixo) para obter yaw/pitch/roll desacoplados.
   - Aplica calibração (offsets) definida pelo botão **Zerar ângulos (tara)** e
     um filtro exponencial (`ANGLE_EMA_ALPHA`) para suavização.
   - Envia comandos MQTT para `facemesh/cmd` no formato `yaw|pitch|nonce|timestamp`.
     O `nonce` acompanha a medição de RTT.
4. Ao receber um `pong` em `facemesh/pong`, a app calcula o RTT e exibe nos
   logs, guardando última ação executada e status.

## Lógica de comandos
- **Pitch** controla frente/ré: abaixo de `-10°` envia `forward`, acima de `10°`
  envia `reverse`. Valores intermediários liberam o yaw para decidir.
- **Yaw** controla guinada: menor que `-15°` envia `left`; maior que `15°` envia
  `right`; dentro da zona morta o comando é `stop`.
- Há `COMMAND_DEBOUNCE_MS` e `COMMAND_REPEAT_MS` para evitar saturar o broker e
  ainda assim reenviar periodicamente para medir RTT.
- Alternar o botão **Pausar envio MQTT** interrompe o streaming e envia um
  comando de parada.

## Conexão MQTT
- Broker HiveMQ Cloud via WebSockets seguros (`wss://...:8884/mqtt`).
- Credenciais definidas em `connectOptions` e tópicos padrão:
  - Publicação de ângulo: `facemesh/angle`.
  - Comando para o robô: `facemesh/cmd`.
  - Resposta do robô: `facemesh/pong`.
- Você pode alterar host, usuário ou senha diretamente nas constantes do
  arquivo ou usar outro broker compatível com WebSockets.

## Como executar
1. Sirva os arquivos com qualquer servidor estático (por exemplo `npx serve .`)
   ou abra `index.html` direto no navegador com permissão de webcam.
2. Aceite o acesso à câmera. Os botões de tara e pausa aparecem no canto
   superior esquerdo.
3. Com o ESP32 online, os comandos começarão a chegar ao firmware; use o
   visualizador de odometria para validar o movimento.

## Pontos de personalização
- Ajuste `yawDeadbandLeftDeg`/`yawDeadbandRightDeg` para calibrar a sensibilidade
  do giro.
- Modifique `pitchForwardThresholdDeg`/`pitchReverseThresholdDeg` se precisar de
  maior tolerância para frente/ré.
- Troque a função `sendMotionCommand` (em `sketch.js`) caso queira um formato de
  payload diferente.

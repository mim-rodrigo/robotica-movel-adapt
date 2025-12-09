# Monitor de odometria em p5.js

Interface em p5.js para visualizar, em tempo real, a pose estimada do robô
(publicada pelo firmware). O desenho mantém o carrinho centralizado na tela,
renderiza rastro, orientação e estatísticas num painel lateral.

## Componentes principais
- **`index.html`**: estrutura a interface com campos para host/porta/credenciais
  MQTT, tópico de odometria e botões de Conectar e Resetar trajetória.
- **`styles.css`**: define o tema escuro, layouts responsivos e destaques para os
  rótulos de posição/heading/distância.
- **`sketch.js`**: lógica p5.js responsável por desenhar grid, corpo do robô e
  rastro; também administra a conexão MQTT via `mqtt.js` (CDN).

## Fluxo de execução
1. O `setup()` cria o canvas (960×640), preenche campos com valores padrão do
   HiveMQ Cloud e associa handlers aos botões.
2. Ao clicar **Conectar**, `connectToBroker()` monta a URL `wss://host:port/mqtt`
   com `clean` session e `reconnectPeriod` de 3 s, inscrevendo no tópico
   configurado (default `robot/odometry`).
3. Cada mensagem JSON com `{ x, y, phi }` é processada em `applyPoseUpdate()`,
   que:
   - Atualiza `pose` e acumula distância percorrida (somatório de Δx/Δy).
   - Armazena o histórico em `trail` (limite de 8.000 pontos) para desenhar a
     trajetória multicolorida.
   - Atualiza o painel textual com metros e heading em graus.
4. O `draw()` roda a 60 FPS, pinta o grid e desenha o robô dimensionado conforme
   as medidas reais (75×45 cm, bitolas e raios de roda definidos em `ROBOT_DIM`).

## Ajustes úteis
- Mude `SCALE` em `sketch.js` para variar a proporção pixels/metro na tela.
- Troque as credenciais padrão se estiver usando outro broker ou tópico.
- O botão **Resetar trajetória** limpa o rastro, zera a distância e re-centra a
  pose.

## Formato esperado de mensagem
O payload deve ser um JSON com valores numéricos:

```json
{ "x": 0.123, "y": 0.456, "phi": 1.5708 }
```

`phi` é o heading em radianos; os rótulos exibem também em graus para facilitar
comparações com as leituras do robô.

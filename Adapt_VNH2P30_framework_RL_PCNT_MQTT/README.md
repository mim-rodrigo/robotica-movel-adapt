# Firmware ESP32: VNH2P30 + PCNT + MQTT

Firmware para controlar um robô diferencial com ESP32, duas pontes H VNH2P30 e
encoders quadratura. Ele aceita comandos manuais via botões físicos ou remotos
via MQTT, aplica PWM nos motores, mede a velocidade/odometria com o periférico
PCNT (Pulse Counter) e publica telemetria para um visualizador.

## Arquitetura rápida
- **`Adapt_VNH2P30_framework_RL_PCNT_MQTT.ino`**: configura UART, Wi‑Fi/MQTT e os
  pinos dos quatro botões de controle manual. O `loop()` mantém a conexão MQTT,
  lê encoders/botões e delega a decisão de movimento para `apply_motion_command`.
- **`motor_control.[ch]`**: abstrai comandos de movimento (frente, ré, girar,
  parar), controla PWM/direção das duas pontes H, calcula velocidades a partir
  dos encoders, integra a pose (x, y, phi) e publica odometria.
- **`mqtt_client.[ch]`**: inicializa Wi‑Fi e MQTT (HiveMQ Cloud por padrão),
  processa mensagens no formato `yaw|pitch|nonce|timestamp`, converte em ações
  de movimento e responde com um "pong" contendo eco das leituras.

## Pinos e hardware
- **Motores**: pinos de direção `MOTOR_RA_PIN=4`, `MOTOR_RB_PIN=27`,
  `MOTOR_LA_PIN=32`, `MOTOR_LB_PIN=33`. PWM em `25` (motor R) e `26` (motor L)
  usando `ledc` a 5 kHz, 8 bits.
- **Enable**: `EN_PIN_R=19` e `EN_PIN_L=18` mantêm as pontes H habilitadas.
- **Encoders**: canais A/B em `14/12` (direito) e `16/17` (esquerdo), lidos via
  `pcnt` com limites de ±10.000 contagens.
- **Botões manuais**: frente `36`, ré `34`, esquerda `35`, direita `39`.

## Loop principal
1. `net_mqtt_loop()` mantém a conexão e entrega mensagens ao callback.
2. `encoder()` executa a cada ~50 ms (janela não bloqueante) para zerar o
   contador PCNT, calcular velocidades em rad/s, integrar a pose por cinemática
   diferencial e ajustar o PWM para seguir a velocidade alvo.
3. `leituraBotoes()` determina se há comando manual. Se não houver, busca a
   última ação remota via `get_remote_motion_command()` (timeout de 3 s).
4. `apply_motion_command()` só reaplica o movimento quando muda (evita ficar
   regravando PWM desnecessariamente).

## Cinemática e publicação
- Os contadores são convertidos em voltas (`PULSOS_POR_VOLTA=11`), corrigidos
  pela redução do motor (147,4:1) e multiplicados pelo raio da roda (0,125 m).
- A cinemática diferencial usa base entre rodas de 0,62 m para derivar velocidade
  linear `V` e angular `w`. A pose é integrada e normalizada para `[-π, π]`.
- O firmware publica JSON `{ "x": <m>, "y": <m>, "phi": <rad> }` em
  `robot/odometry` e um payload de debug com contagens e velocidades em
  `robot/odometry/debug`.

## Comandos remotos via MQTT
- **Tópico de subscribe**: `facemesh/cmd` (padrão). O payload deve ser
  `yaw|pitch|nonce|timestamp` (graus). `pitch` pode ser `nan`.
- **Mapeamento**: `pitch <= -10°` → frente; `pitch >= 10°` → ré;
  `yaw <= -8°` → virar à esquerda; `yaw >= 8°` → direita; fora das faixas → stop.
- **Resposta**: um "pong" em `facemesh/pong` com `nonce|timestamp|executed_at|
  yaw|pitch|acao|status`.
- Caso nenhuma mensagem chegue por 3 s, o robô entra em `MOTION_STOP`.

## Ajustes rápidos
- Funções `net_set_wifi`, `net_set_broker` e `net_set_root_ca` permitem trocar
  rede, broker e certificado em tempo de execução (antes de `net_mqtt_begin`).
- Constantes `DEFAULT_PWM_*` controlam a intensidade padrão do comando; o
  regulador incremental em `adjustPwm` suaviza variações de velocidade.
- Flags globais `block_foward` e `block_reverse` podem ser usadas para inibir
  movimento em situações de segurança.

## Fluxo de inicialização
1. `setup()` abre a serial (115200 bps), inicializa Wi‑Fi/MQTT e chama
   `setupMotor()` (pinos, PWM, PCNT e libera motores).
2. O `loop()` mantém a comunicação, atualiza odometria, lê botões e aplica o
   comando decidido.

Com esse README é possível identificar rapidamente pinos, tópicos MQTT e pontos
para ajustar velocidade, segurança ou rede antes de gravar o firmware.

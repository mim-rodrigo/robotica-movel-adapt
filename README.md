# Robótica móvel — conjunto de demos

Este repositório reúne três peças que trabalham em conjunto para controlar e
visualizar um robô diferencial:

- **Adapt_VNH2P30_framework_RL_PCNT_MQTT**: firmware para ESP32 que controla os
  dois motores via ponte H VNH2P30, lê encoders pelo periférico PCNT e expõe
  comandos/telemetria via MQTT.
- **faceMesh**: aplicação p5.js + ml5.js que calcula ângulos de orientação da
  cabeça (yaw/pitch/roll) a partir da webcam e envia comandos MQTT para o
  firmware.
- **web-odometry-visualizer**: painel p5.js que assina o tópico de odometria e
  desenha a pose estimada do robô em tempo real.

Cada pasta contém um README detalhado sobre configuração, fluxo de execução e
pontos de extensão.

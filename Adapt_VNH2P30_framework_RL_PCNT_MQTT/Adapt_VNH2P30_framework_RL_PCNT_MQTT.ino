#include "motor_control.h"
#include "websocket_server.h"

int botao_frente = 36;
int botao_re = 34;
int botao_esquerda = 35;
int botao_direita = 39;

int estado_botoes = 0;

bool block_foward = false;
bool block_reverse = false;

void leituraBotoes() {
  if (digitalRead(botao_frente)) {
    estado_botoes = 1;
  } else if (digitalRead(botao_re)) {
    estado_botoes = 2;
  } else if (digitalRead(botao_esquerda)) {
    estado_botoes = 3;
  } else if (digitalRead(botao_direita)) {
    estado_botoes = 4;
  } else {
    estado_botoes = 0;
  }
}

void setup() {

  Serial.begin(115200);
  net_ws_begin();       // inicializa WiFi + WebSocket
  
  setupMotor();

  pinMode(botao_frente, INPUT);
  pinMode(botao_re, INPUT);
  pinMode(botao_esquerda, INPUT);
  pinMode(botao_direita, INPUT);
}

void loop() {

  net_ws_loop();        // mantém a conexão e processa mensagens
  
  encoder(); // leitura dos encoders

  leituraBotoes(); // leitura dos botões

  MotionCommand commandToExecute = MOTION_STOP;
  bool manualControl = true;

  switch (estado_botoes) {
    case 1:
      commandToExecute = MOTION_FORWARD;
      break;
    case 2:
      commandToExecute = MOTION_REVERSE;
      break;
    case 3:
      commandToExecute = MOTION_TURN_LEFT;
      break;
    case 4:
      commandToExecute = MOTION_TURN_RIGHT;
      break;
    default:
      manualControl = false;
      break;
  }

  if (!manualControl) {
    commandToExecute = get_remote_motion_command();
  }

  apply_motion_command(commandToExecute);
}

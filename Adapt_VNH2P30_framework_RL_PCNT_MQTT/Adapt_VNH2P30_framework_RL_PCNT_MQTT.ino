#include "motor_control.h"
#include "mqtt_client.h"

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
  net_mqtt_begin();     // inicializa WiFi + MQTT
  
  setupMotor();

  pinMode(botao_frente, INPUT);
  pinMode(botao_re, INPUT);
  pinMode(botao_esquerda, INPUT);
  pinMode(botao_direita, INPUT);
}

void loop() {

  net_mqtt_loop();      // mantém a conexão e processa mensagens
  
  encoder(); // leitura dos encoders

  leituraBotoes(); // leitura dos botões

  // Após a sequência, o robô volta a responder aos botões
  switch (estado_botoes) {
    case 0:
      Stop();
      break;
    case 1:
      Forward();
      break;
    case 2:
      Reverse();
      break;
    case 3:
      TurnLeft();
      break;
    case 4:
      TurnRight();
      break;
    default:
      break;
  }
}

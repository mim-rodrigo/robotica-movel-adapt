#include "motor_control.h"

int botao_frente = 36;
int botao_re = 34;
int botao_esquerda = 35;
int botao_direita = 39;

int estado_botoes = 0;

bool block_foward = false;
bool block_reverse = false;

// Variáveis para controle do tempo
unsigned long tempoInicial = 0;
bool iniciouSequencia = false;
bool executandoForward = false;
bool finalizouSequencia = false;

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
  setupMotor();

  pinMode(botao_frente, INPUT);
  pinMode(botao_re, INPUT);
  pinMode(botao_esquerda, INPUT);
  pinMode(botao_direita, INPUT);

  tempoInicial = millis(); // marca o tempo de início da contagem
  iniciouSequencia = true;
}

void loop() {
  encoder(); // leitura dos encoders

  leituraBotoes(); // leitura dos botões

  unsigned long tempoAtual = millis();

  if (iniciouSequencia && !finalizouSequencia) {
    unsigned long tempoDecorrido = tempoAtual - tempoInicial;

    if (tempoDecorrido >= 1000 && tempoDecorrido < 11000) {
      Forward(); // anda por 10 segundos (depois de esperar 1s)
      executandoForward = true;
    } else if (tempoDecorrido >= 11000) {
      Stop(); // para depois de 11s totais (1s parado + 10s andando)
      finalizouSequencia = true;
    } else {
      Stop(); // ainda está no primeiro segundo
    }

    // Ignora leitura dos botões durante a sequência inicial
    return;
  }

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

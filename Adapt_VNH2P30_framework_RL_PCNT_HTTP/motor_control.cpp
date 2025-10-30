#include "motor_control.h"

// Variáveis de velocidade independentes
short usSpeedR = 159; // Velocidade padrão para o Motor R
short usSpeedL = 159; // Velocidade padrão para o Motor L
unsigned short usMotor_Status = BRAKE;
unsigned long last_time = 0;

static MotionCommand g_remote_command = MOTION_STOP;
static MotionCommand g_last_applied_command = MOTION_STOP;

void setupPCNT() {
  pcnt_config_t configR;
  configR.pulse_gpio_num = ENCODER_RA;
  configR.ctrl_gpio_num = PCNT_PIN_NOT_USED;
  configR.channel = PCNT_CHANNEL_0;
  configR.unit = PCNT_UNIT_0;
  configR.pos_mode = PCNT_COUNT_INC;
  configR.neg_mode = PCNT_COUNT_DIS;
  configR.lctrl_mode = PCNT_MODE_KEEP;
  configR.hctrl_mode = PCNT_MODE_KEEP;
  configR.counter_h_lim = 10000;
  configR.counter_l_lim = -10000;
  pcnt_unit_config(&configR);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_resume(PCNT_UNIT_0);

  pcnt_config_t configL = configR;
  configL.pulse_gpio_num = ENCODER_LA;
  configL.unit = PCNT_UNIT_1;
  pcnt_unit_config(&configL);
  pcnt_counter_clear(PCNT_UNIT_1);
  pcnt_counter_resume(PCNT_UNIT_1);
}

void setupMotor() {
  pinMode(MOTOR_RA_PIN, OUTPUT);
  pinMode(MOTOR_RB_PIN, OUTPUT);
  pinMode(MOTOR_LA_PIN, OUTPUT);
  pinMode(MOTOR_LB_PIN, OUTPUT);

  pinMode(EN_PIN_R, OUTPUT);
  pinMode(EN_PIN_L, OUTPUT);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(PWM_MOTOR_R, 0);
  ledcSetup(1, 5000, 8);
  ledcAttachPin(PWM_MOTOR_L, 1);

  setupPCNT();

  //Libera os motores
  digitalWrite(EN_PIN_R, HIGH);
  digitalWrite(EN_PIN_L, HIGH);

  Serial.begin(115200); // Faster baud rate for ESP32
  Serial.println("Begin motor control");

  Stop();
}

void encoder() {
  // --- Janela não-bloqueante e proteção da 1ª amostra ---
  static bool prim = true;                     // primeira chamada?
  static unsigned long last_print = 0;         // para limitar prints (opcional)
  const  unsigned long janela_ms   = 50;       // período de cálculo (~20 Hz)
  const  unsigned long print_ms    = 200;      // período de impressão (~5 Hz)

  unsigned long now = millis();

  if (prim) {                                  // evita dt gigante na primeira vez
    last_time = now;
    prim = false;
    return;
  }

  // Se ainda não passou a janela, não faz nada (não bloqueia o loop)
  if ((now - last_time) < janela_ms) return;

  unsigned long dt = now - last_time;          // dt real da janela
  last_time = now;

  // --- Leitura e zeragem dos contadores ---
  int16_t contagemR = 0, contagemL = 0;
  pcnt_get_counter_value(PCNT_UNIT_0, &contagemR);
  pcnt_get_counter_value(PCNT_UNIT_1, &contagemL);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_1);

  // --- Cálculo de velocidades (rad/s) ---
  float voltasR = contagemR / (float)PULSOS_POR_VOLTA;
  float voltasL = contagemL / (float)PULSOS_POR_VOLTA;
  float velR = (dt > 0) ? (voltasR / (dt / 1000.0f)) * (2.0f * PI) : 0.0f;
  float velL = (dt > 0) ? (voltasL / (dt / 1000.0f)) * (2.0f * PI) : 0.0f;

  // --- Impressão desacoplada (opcional) ---
  if ((now - last_print) >= print_ms) {
    last_print = now;
    Serial.print("VelR: ");
    Serial.println(velR);
    Serial.print("VelL: ");
    Serial.println(velL);
  }
}

void Stop() {
  usMotor_Status = BRAKE;
  motorGo(MOTOR_R, usMotor_Status, 0);
  motorGo(MOTOR_L, usMotor_Status, 0);
  g_last_applied_command = MOTION_STOP;
}

void Forward() {
  if (!block_foward) {
    usMotor_Status = CW;
    motorGo(MOTOR_R, usMotor_Status, usSpeedR); // Usa usSpeedR
    motorGo(MOTOR_L, usMotor_Status, usSpeedL); // Usa usSpeedL
    g_last_applied_command = MOTION_FORWARD;
  }
}

void Reverse() {
  if (!block_reverse) {
    usMotor_Status = CCW;
    motorGo(MOTOR_R, usMotor_Status, usSpeedR); // Usa usSpeedR
    motorGo(MOTOR_L, usMotor_Status, usSpeedL); // Usa usSpeedL
    g_last_applied_command = MOTION_REVERSE;
  }
}

void TurnLeft() {
  motorGo(MOTOR_R, CW, usSpeedR);  // Usa usSpeedR
  motorGo(MOTOR_L, CCW, usSpeedL); // Usa usSpeedL
  g_last_applied_command = MOTION_TURN_LEFT;
}

void TurnRight() {
  motorGo(MOTOR_R, CCW, usSpeedR); // Usa usSpeedR
  motorGo(MOTOR_L, CW, usSpeedL);  // Usa usSpeedL
  g_last_applied_command = MOTION_TURN_RIGHT;
}

void Lock() {
  usMotor_Status = BRAKE;
  motorGo(MOTOR_R, usMotor_Status, 0);
  motorGo(MOTOR_L, usMotor_Status, 0);
  digitalWrite(EN_PIN_R, LOW);
  digitalWrite(EN_PIN_L, LOW);
  Serial.println("Motors locked");
  g_last_applied_command = MOTION_STOP;
}

void motorGo(uint8_t motor, uint8_t direct, uint8_t pwm) {
  if (motor == MOTOR_R) {
    if (direct == CW) {
      digitalWrite(MOTOR_RA_PIN, LOW);
      digitalWrite(MOTOR_RB_PIN, HIGH);
    } else if (direct == CCW) {
      digitalWrite(MOTOR_RA_PIN, HIGH);
      digitalWrite(MOTOR_RB_PIN, LOW);
    } else {
      digitalWrite(MOTOR_RA_PIN, LOW);
      digitalWrite(MOTOR_RB_PIN, LOW);
    }
    ledcWrite(0, pwm);
   }
  if (motor == MOTOR_L) {
    if (direct == CW) {
      digitalWrite(MOTOR_LA_PIN, LOW);
      digitalWrite(MOTOR_LB_PIN, HIGH);
    } else if (direct == CCW) {
      digitalWrite(MOTOR_LA_PIN, HIGH);
      digitalWrite(MOTOR_LB_PIN, LOW);
    } else {
      digitalWrite(MOTOR_LA_PIN, LOW);
      digitalWrite(MOTOR_LB_PIN, LOW);
    }
    ledcWrite(1, pwm);
  }
}

void set_remote_motion_command(MotionCommand command) {
  g_remote_command = command;
}

MotionCommand get_remote_motion_command() {
  return g_remote_command;
}

static void apply_motion_now(MotionCommand command) {
  switch (command) {
    case MOTION_STOP:
      Stop();
      break;
    case MOTION_FORWARD:
      Forward();
      break;
    case MOTION_REVERSE:
      Reverse();
      break;
    case MOTION_TURN_LEFT:
      TurnLeft();
      break;
    case MOTION_TURN_RIGHT:
      TurnRight();
      break;
  }
}

void apply_motion_command(MotionCommand command) {
  if (command == g_last_applied_command) {
    return;
  }

  apply_motion_now(command);
  g_last_applied_command = command;
}

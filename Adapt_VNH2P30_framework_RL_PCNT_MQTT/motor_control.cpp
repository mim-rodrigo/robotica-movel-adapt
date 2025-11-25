#include "motor_control.h"
#include <math.h>

static unsigned short usMotor_Status = BRAKE;
static unsigned long last_time = 0;

static uint8_t currentPwmR = 0;
static uint8_t currentPwmL = 0;
static float targetVelR = 0.0f;
static float targetVelL = 0.0f;
static uint8_t lastDirectionR = BRAKE;
static uint8_t lastDirectionL = BRAKE;

static const uint8_t PWM_STEP = 2;
static const float MAX_TARGET_VELOCITY = 400.0f;
static const uint8_t DEFAULT_PWM_FORWARD = 159;
static const uint8_t DEFAULT_PWM_REVERSE = 159;
static const uint8_t DEFAULT_PWM_TURN = 159;

static MotionCommand g_remote_command = MOTION_STOP;
static MotionCommand g_last_applied_command = MOTION_STOP;
static unsigned long g_remote_command_last_update = 0;
static const unsigned long REMOTE_COMMAND_TIMEOUT_MS = 3000;  // 1s sem mensagens -> STOP
static bool g_pcnt_pins_logged = false;

static float pwmToTargetVelocity(uint8_t pwm) {
  return (pwm / 255.0f) * MAX_TARGET_VELOCITY;
}

static void setTargetVelocities(uint8_t pwmR, uint8_t pwmL, bool oppositeDirections) {
  float baseTarget = pwmToTargetVelocity(static_cast<uint8_t>((pwmR + pwmL) / 2));
  targetVelR = baseTarget;
  targetVelL = oppositeDirections ? -baseTarget : baseTarget;
}

static uint8_t adjustPwm(uint8_t current, float measured, float target) {
  const float tolerance = 0.5f;

  if (target <= tolerance) {
    return (current > PWM_STEP) ? current - PWM_STEP : 0;
  }

  if (measured < (target - tolerance)) {
    return (current + PWM_STEP > 255) ? 255 : current + PWM_STEP;
  }

  if (measured > (target + tolerance)) {
    return (current > PWM_STEP) ? current - PWM_STEP : 0;
  }

  return current;
}

static void synchronizeWheels(MotionCommand command, float velR, float velL) {
  const float syncTolerance = 0.5f;
  float diff = fabs(velR) - fabs(velL);

  if (fabs(diff) < syncTolerance) {
    return;
  }

  if (diff > 0) {
    currentPwmR = (currentPwmR > 0) ? currentPwmR - 1 : 0;
    currentPwmL = (currentPwmL < 255) ? currentPwmL + 1 : currentPwmL;
  } else {
    currentPwmL = (currentPwmL > 0) ? currentPwmL - 1 : 0;
    currentPwmR = (currentPwmR < 255) ? currentPwmR + 1 : currentPwmR;
  }

  if (command == MOTION_STOP) {
    currentPwmR = 0;
    currentPwmL = 0;
  }
}

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

  Serial.println("Begin motor control");
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

  float targetMagR = fabs(targetVelR);
  float targetMagL = fabs(targetVelL);

  currentPwmR = adjustPwm(currentPwmR, fabs(velR), targetMagR);
  currentPwmL = adjustPwm(currentPwmL, fabs(velL), targetMagL);

  synchronizeWheels(g_last_applied_command, velR, velL);

  motorGo(MOTOR_R, lastDirectionR, currentPwmR);
  motorGo(MOTOR_L, lastDirectionL, currentPwmL);

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
  targetVelR = 0.0f;
  targetVelL = 0.0f;
  currentPwmR = 0;
  currentPwmL = 0;
  lastDirectionR = BRAKE;
  lastDirectionL = BRAKE;
  motorGo(MOTOR_R, usMotor_Status, 0);
  motorGo(MOTOR_L, usMotor_Status, 0);
  g_last_applied_command = MOTION_STOP;
}

void Forward(uint8_t usSpeedR, uint8_t usSpeedL) {
  if (!block_foward) {
    usMotor_Status = CW;
    lastDirectionR = CW;
    lastDirectionL = CW;
    currentPwmR = usSpeedR;
    currentPwmL = usSpeedL;
    setTargetVelocities(usSpeedR, usSpeedL, false);
    motorGo(MOTOR_R, lastDirectionR, currentPwmR);
    motorGo(MOTOR_L, lastDirectionL, currentPwmL);
    g_last_applied_command = MOTION_FORWARD;
  }
}

void Reverse(uint8_t usSpeedR, uint8_t usSpeedL) {
  if (!block_reverse) {
    usMotor_Status = CCW;
    lastDirectionR = CCW;
    lastDirectionL = CCW;
    currentPwmR = usSpeedR;
    currentPwmL = usSpeedL;
    setTargetVelocities(usSpeedR, usSpeedL, false);
    motorGo(MOTOR_R, lastDirectionR, currentPwmR);
    motorGo(MOTOR_L, lastDirectionL, currentPwmL);
    g_last_applied_command = MOTION_REVERSE;
  }
}

void TurnLeft(uint8_t usSpeedR, uint8_t usSpeedL) {
  lastDirectionR = CW;
  lastDirectionL = CCW;
  currentPwmR = usSpeedR;
  currentPwmL = usSpeedL;
  setTargetVelocities(usSpeedR, usSpeedL, true);
  motorGo(MOTOR_R, lastDirectionR, currentPwmR);  // Usa usSpeedR
  motorGo(MOTOR_L, lastDirectionL, currentPwmL); // Usa usSpeedL
  g_last_applied_command = MOTION_TURN_LEFT;
}

void TurnRight(uint8_t usSpeedR, uint8_t usSpeedL) {
  lastDirectionR = CCW;
  lastDirectionL = CW;
  currentPwmR = usSpeedR;
  currentPwmL = usSpeedL;
  setTargetVelocities(usSpeedR, usSpeedL, true);
  motorGo(MOTOR_R, lastDirectionR, currentPwmR); // Usa usSpeedR
  motorGo(MOTOR_L, lastDirectionL, currentPwmL);  // Usa usSpeedL
  g_last_applied_command = MOTION_TURN_RIGHT;
}

void Lock() {
  usMotor_Status = BRAKE;
  targetVelR = 0.0f;
  targetVelL = 0.0f;
  currentPwmR = 0;
  currentPwmL = 0;
  lastDirectionR = BRAKE;
  lastDirectionL = BRAKE;
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
  g_remote_command_last_update = millis();
}

MotionCommand get_remote_motion_command() {
  unsigned long now = millis();

  if (g_remote_command_last_update == 0) {
    return MOTION_STOP;
  }

  if ((now - g_remote_command_last_update) > REMOTE_COMMAND_TIMEOUT_MS) {
    if (g_remote_command != MOTION_STOP) {
      g_remote_command = MOTION_STOP;
    }
    return MOTION_STOP;
  }

  return g_remote_command;
}

static void apply_motion_now(MotionCommand command) {
  switch (command) {
    case MOTION_STOP:
      Stop();
      break;
    case MOTION_FORWARD:
      Forward(DEFAULT_PWM_FORWARD, DEFAULT_PWM_FORWARD);
      break;
    case MOTION_REVERSE:
      Reverse(DEFAULT_PWM_REVERSE, DEFAULT_PWM_REVERSE);
      break;
    case MOTION_TURN_LEFT:
      TurnLeft(DEFAULT_PWM_TURN, DEFAULT_PWM_TURN);
      break;
    case MOTION_TURN_RIGHT:
      TurnRight(DEFAULT_PWM_TURN, DEFAULT_PWM_TURN);
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

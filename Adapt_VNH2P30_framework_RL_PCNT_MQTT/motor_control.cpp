#include "motor_control.h"

// Variáveis de velocidade independentes
short usSpeedR = 100; // Velocidade padrão para o Motor R
short usSpeedL = 100; // Velocidade padrão para o Motor L
unsigned short usMotor_Status = BRAKE;
unsigned long last_time = 0;

static MotionCommand g_remote_command = MOTION_STOP;
static MotionCommand g_last_applied_command = MOTION_STOP;
static unsigned long g_remote_command_last_update = 0;
static const unsigned long REMOTE_COMMAND_TIMEOUT_MS = 1000;  // 1s sem mensagens -> STOP
static bool g_pcnt_pins_logged = false;

namespace {

bool configureEncoderUnit(pcnt_unit_t unit,
                          gpio_num_t pulse_gpio,
                          gpio_num_t ctrl_gpio,
                          const char* label,
                          bool invert_ctrl_logic) {
  pcnt_config_t config{};
  config.pulse_gpio_num = pulse_gpio;
  config.ctrl_gpio_num = ctrl_gpio;
  config.channel = PCNT_CHANNEL_0;
  config.unit = unit;
  config.pos_mode = PCNT_COUNT_INC;
  config.neg_mode = PCNT_COUNT_DEC;
  // Quando o sinal de controle estiver no nível configurado, invertemos o sentido da contagem
  // para que o giro "para trás" produza valores negativos.
  if (invert_ctrl_logic) {
    config.lctrl_mode = PCNT_MODE_KEEP;
    config.hctrl_mode = PCNT_MODE_REVERSE;
  } else {
    config.lctrl_mode = PCNT_MODE_REVERSE;
    config.hctrl_mode = PCNT_MODE_KEEP;
  }
  config.counter_h_lim = 10000;
  config.counter_l_lim = -10000;

  esp_err_t err = pcnt_unit_config(&config);
  if (err != ESP_OK) {
    Serial.printf("[PCNT] Falha ao configurar unidade %d (%s): err=%d\n",
                  static_cast<int>(unit), label, static_cast<int>(err));
    return false;
  }

  // Filtro simples para evitar bounces (valor em ticks de clock APB ~80MHz)
  pcnt_set_filter_value(unit, 1023);
  pcnt_filter_enable(unit);

  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);
  pcnt_counter_resume(unit);

  Serial.printf(
      "[PCNT] Unidade %d pronta (%s) – pulse GPIO %d, ctrl GPIO %d%s\n",
      static_cast<int>(unit),
      label,
      static_cast<int>(pulse_gpio),
      static_cast<int>(ctrl_gpio),
      invert_ctrl_logic ? " (invert ctrl)" : "");
  return true;
}

}  // namespace

static void logPcntPinAssignments() {
  if (g_pcnt_pins_logged) {
    return;
  }

  g_pcnt_pins_logged = true;
  Serial.printf("PCNT unidade 0 (Motor R) – pulse GPIO %d, ctrl GPIO %d\n",
                ENCODER_RA,
                ENCODER_RB);
  Serial.printf("PCNT unidade 1 (Motor L) – pulse GPIO %d, ctrl GPIO %d\n",
                ENCODER_LA,
                ENCODER_LB);
  Serial.printf("Encoder R -> canal A GPIO %d | canal B GPIO %d\n", ENCODER_RA, ENCODER_RB);
  Serial.printf("Encoder L -> canal A GPIO %d | canal B GPIO %d\n", ENCODER_LA, ENCODER_LB);
}

void setupPCNT() {
  pinMode(ENCODER_RA, INPUT_PULLUP);
  pinMode(ENCODER_RB, INPUT_PULLUP);
  pinMode(ENCODER_LA, INPUT_PULLUP);
  pinMode(ENCODER_LB, INPUT_PULLUP);

  bool right_ok = configureEncoderUnit(PCNT_UNIT_0,
                                       static_cast<gpio_num_t>(ENCODER_RA),
                                       static_cast<gpio_num_t>(ENCODER_RB),
                                       "Motor R",
                                       false);
  bool left_ok = configureEncoderUnit(PCNT_UNIT_1,
                                      static_cast<gpio_num_t>(ENCODER_LA),
                                      static_cast<gpio_num_t>(ENCODER_LB),
                                      "Motor L",
                                      true);

  if (!right_ok || !left_ok) {
    Serial.println(
        F("[PCNT] Atenção: falha ao configurar alguma unidade. Confira os pinos dos encoders."));
  }
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
  logPcntPinAssignments();

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
    Serial.printf("dt=%lums\n", dt);
    Serial.printf("PCNT U0 (GPIO %d) contagem=%d -> VelR=%.3f rad/s\n",
                  ENCODER_RA, contagemR, velR);
    Serial.printf("PCNT U1 (GPIO %d) contagem=%d -> VelL=%.3f rad/s\n",
                  ENCODER_LA, contagemL, velL);

    static unsigned long last_zero_warn = 0;
    if (contagemR == 0 && contagemL != 0 && (now - last_zero_warn) > 500) {
      last_zero_warn = now;
      Serial.printf(
          "[PCNT] Aviso: contador do Motor R segue em zero. Estado instantâneo A=%d B=%d\n",
          digitalRead(ENCODER_RA),
          digitalRead(ENCODER_RB));
    }
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

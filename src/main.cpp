#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// ==============================================================================
// CANAIS DOS SERVOS NA PLACA PCA9685
// ==============================================================================
#define SERVO_LR 0 // Pan (Esquerda/Direita)
#define SERVO_UD 1 // Tilt (Cima/Baixo)
#define SERVO_TL 2 // Top Left (Pálpebra Superior Esquerda)
#define SERVO_BL 3 // Bottom Left (Pálpebra Inferior Esquerda)
#define SERVO_TR 4 // Top Right (Pálpebra Superior Direita)
#define SERVO_BR 5 // Bottom Right (Pálpebra Inferior Direita)

// ==============================================================================
// LIMITES (BASEADO NO CÓDIGO DO WILL COGLEY)
// ==============================================================================
// MIN = Posição FECHADA | MAX = Posição ABERTA
// NOTA: TL e BR são invertidos fisicamente (90 é fechado, 10 é aberto)
const int LR_MIN = 60, LR_MAX = 120; 
const int UD_MIN = 60, UD_MAX = 120;
const int TL_MIN = 90, TL_MAX = 10;
const int BL_MIN = 10, BL_MAX = 90;
const int TR_MIN = 10, TR_MAX = 90;
const int BR_MIN = 90, BR_MAX = 10;

// ==============================================================================
// VARIÁVEIS DE ESTADO (MODO AUTOMÁTICO)
// ==============================================================================
int last_lr_angle = 90;
int last_ud_angle = 90;
unsigned long auto_next_action = 0;

// ==============================================================================
// FUNÇÕES DE CONTROLE DE OLHOS E PÁLPEBRAS
// ==============================================================================

// Helper para converter ângulo (0-180) para pulso PWM na placa PCA9685
void setServoAngle(uint8_t n, int angle) {
  // Faixa de pulso segura de 1000 a 2000 microssegundos para não forçar fisicamente
  int microsec = map(angle, 0, 180, 1000, 2000);
  pwm.writeMicroseconds(n, microsec);
}

void blink() {
    // Fecha todas as pálpebras mandando para os valores MIN
    setServoAngle(SERVO_TL, TL_MIN);
    setServoAngle(SERVO_BL, BL_MIN);
    setServoAngle(SERVO_TR, TR_MIN);
    setServoAngle(SERVO_BR, BR_MIN);
}

void neutral() {
    // Posição de repouso (olhos pro centro, pálpebras abertas)
    setServoAngle(SERVO_LR, 90);
    setServoAngle(SERVO_UD, 90);
    setServoAngle(SERVO_TL, TL_MAX);
    setServoAngle(SERVO_BL, BL_MAX);
    setServoAngle(SERVO_TR, TR_MAX);
    setServoAngle(SERVO_BR, BR_MAX);
}

// A mágica matemática do Squinting (Pálpebras acompanham o movimento)
void control_ud_and_lids(int ud_angle) {
    // Normaliza a posição de UD para 0.0 (baixo) a 1.0 (cima) usando o range amplo
    float ud_range = 140.0 - 40.0;
    float ud_progress = (ud_angle - 40.0) / ud_range;
    ud_progress = constrain(ud_progress, 0.0f, 1.0f);

    // Quando olha para cima, fecha um pouco a de baixo. Para baixo, fecha a de cima
    float top_close_factor = 0.6 * (1.0 - ud_progress);
    float bottom_close_factor = 0.6 * ud_progress;

    // Interpola entre o aberto (MAX) e fechado (MIN)
    int tl_target = TL_MIN + (TL_MAX - TL_MIN) * (1.0 - top_close_factor);
    int tr_target = TR_MIN + (TR_MAX - TR_MIN) * (1.0 - top_close_factor);
    int bl_target = BL_MIN + (BL_MAX - BL_MIN) * (1.0 - bottom_close_factor);
    int br_target = BR_MIN + (BR_MAX - BR_MIN) * (1.0 - bottom_close_factor);

    setServoAngle(SERVO_UD, ud_angle);
    setServoAngle(SERVO_TL, tl_target);
    setServoAngle(SERVO_TR, tr_target);
    setServoAngle(SERVO_BL, bl_target);
    setServoAngle(SERVO_BR, br_target);
}

void setup() {
    Serial.begin(115200);

    // Inicializa I2C no ESP32 usando os pinos originais (SDA=21, SCL=22)
    Wire.begin(21, 22);

    pwm.begin();
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(50); // Servos analógicos usam 50Hz
    
    Serial.println("Calibrando no Centro...");
    neutral();
    delay(1000);
}

void loop() {
    // MODO TOTALMENTE AUTOMÁTICO (Sem joystick)
    if (millis() > auto_next_action) {
        int command = random(0, 3);
        if (command == 0) {
            blink();
            delay(100);
            neutral();
            auto_next_action = millis() + random(1000, 3000);
        } else if (command == 1) {
            blink();
            delay(100);
            int r_ud = random(UD_MIN, UD_MAX);
            int r_lr = random(LR_MIN, LR_MAX);
            control_ud_and_lids(r_ud);
            setServoAngle(SERVO_LR, r_lr);
            last_ud_angle = r_ud;
            last_lr_angle = r_lr;
            auto_next_action = millis() + random(300, 1000);
        } else {
            int r_ud = random(UD_MIN, UD_MAX);
            int r_lr = random(LR_MIN, LR_MAX);
            control_ud_and_lids(r_ud);
            setServoAngle(SERVO_LR, r_lr);
            last_ud_angle = r_ud;
            last_lr_angle = r_lr;
            auto_next_action = millis() + random(200, 400);
        }
    }

    delay(20); // 50 Hz refresh rate
}

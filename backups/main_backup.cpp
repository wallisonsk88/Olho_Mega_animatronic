#include <Arduino.h>
#include <ESP32Servo.h>

// ==============================================================================
// CONFIGURAÇÕES DO JOYSTICK
// ==============================================================================
#define PIN_JOYSTICK_X 34  
#define PIN_JOYSTICK_Y 35  
#define PIN_JOYSTICK_SW 32 

// Calibração do seu HW-504
const int JOYSTICK_DEADZONE = 250;
const int JOYSTICK_CENTER = 1830;

// ==============================================================================
// PINOS DOS SERVOS
// ==============================================================================
#define PIN_LR 13 // Pan (Esquerda/Direita)
#define PIN_UD 12 // Tilt (Cima/Baixo)
#define PIN_TL 14 // Top Left (Pálpebra Superior Esquerda)
#define PIN_BL 27 // Bottom Left (Pálpebra Inferior Esquerda)
#define PIN_TR 26 // Top Right (Pálpebra Superior Direita)
#define PIN_BR 25 // Bottom Right (Pálpebra Inferior Direita)

Servo servoLR;
Servo servoUD;
Servo servoTL;
Servo servoBL;
Servo servoTR;
Servo servoBR;

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
// VARIÁVEIS DE ESTADO (SUAVIZAÇÃO E MODO AUTOMÁTICO)
// ==============================================================================
int last_lr_angle = 90;
int last_ud_angle = 90;
const int max_speed = 5; // Limita a velocidade dos servos para não dar trancos

unsigned long last_joystick_move = 0;
bool auto_mode = false;
unsigned long auto_next_action = 0;

// ==============================================================================
// FUNÇÕES DE CONTROLE DE OLHOS E PÁLPEBRAS
// ==============================================================================

void blink() {
    // Fecha todas as pálpebras mandando para os valores MIN
    servoTL.write(TL_MIN);
    servoBL.write(BL_MIN);
    servoTR.write(TR_MIN);
    servoBR.write(BR_MIN);
}

void neutral() {
    // Posição de repouso (olhos pro centro, pálpebras abertas)
    servoLR.write(90);
    servoUD.write(90);
    servoTL.write(TL_MAX);
    servoBL.write(BL_MAX);
    servoTR.write(TR_MAX);
    servoBR.write(BR_MAX);
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

    servoUD.write(ud_angle);
    servoTL.write(tl_target);
    servoTR.write(tr_target);
    servoBL.write(bl_target);
    servoBR.write(br_target);
}

int scale_joystick(int pot_value, int out_min, int out_max) {
    if (abs(pot_value - JOYSTICK_CENTER) < JOYSTICK_DEADZONE) {
        return 90;
    }
    
    float scaled_value;
    if (pot_value < JOYSTICK_CENTER) {
        scaled_value = out_min + (float)(pot_value) * (90.0 - out_min) / (JOYSTICK_CENTER - JOYSTICK_DEADZONE);
    } else {
        scaled_value = 90.0 + (float)(pot_value - (JOYSTICK_CENTER + JOYSTICK_DEADZONE)) * (out_max - 90.0) / (4095.0 - (JOYSTICK_CENTER + JOYSTICK_DEADZONE));
    }
    
    return constrain((int)scaled_value, min(out_min, out_max), max(out_min, out_max));
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_JOYSTICK_SW, INPUT_PULLUP);
    
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    servoLR.setPeriodHertz(50);
    servoUD.setPeriodHertz(50);
    servoTL.setPeriodHertz(50);
    servoBL.setPeriodHertz(50);
    servoTR.setPeriodHertz(50);
    servoBR.setPeriodHertz(50);

    servoLR.attach(PIN_LR, 500, 2400);
    servoUD.attach(PIN_UD, 500, 2400);
    servoTL.attach(PIN_TL, 500, 2400);
    servoBL.attach(PIN_BL, 500, 2400);
    servoTR.attach(PIN_TR, 500, 2400);
    servoBR.attach(PIN_BR, 500, 2400);
    
    Serial.println("Calibrando no Centro...");
    neutral();
    delay(1000);
    last_joystick_move = millis();
}

void loop() {
    int joyX = analogRead(PIN_JOYSTICK_X);
    int joyY = analogRead(PIN_JOYSTICK_Y);
    bool btn_pressed = (digitalRead(PIN_JOYSTICK_SW) == LOW);

    // Identifica se estamos mexendo no controle
    if (abs(joyX - JOYSTICK_CENTER) > JOYSTICK_DEADZONE || abs(joyY - JOYSTICK_CENTER) > JOYSTICK_DEADZONE || btn_pressed) {
        last_joystick_move = millis();
        auto_mode = false;
    } else if (millis() - last_joystick_move > 5000) {
        auto_mode = true; // Ativa modo automático após 5 segundos sem uso
    }

    if (auto_mode) {
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
                int r_ud = random(60, 120);
                int r_lr = random(60, 120);
                control_ud_and_lids(r_ud);
                servoLR.write(r_lr);
                last_ud_angle = r_ud;
                last_lr_angle = r_lr;
                auto_next_action = millis() + random(300, 1000);
            } else {
                int r_ud = random(60, 120);
                int r_lr = random(60, 120);
                control_ud_and_lids(r_ud);
                servoLR.write(r_lr);
                last_ud_angle = r_ud;
                last_lr_angle = r_lr;
                auto_next_action = millis() + random(200, 400);
            }
        }
    } else {
        if (btn_pressed) {
            blink();
        } else {
            // Mapeia joystick para ângulos alvos
            int target_lr = scale_joystick(joyX, LR_MIN, LR_MAX);
            int target_ud = scale_joystick(joyY, UD_MIN, UD_MAX);
            
            // Limitador de Velocidade (Suavização)
            int lr_angle = last_lr_angle;
            if (abs(target_lr - last_lr_angle) > max_speed) {
                lr_angle = (target_lr > last_lr_angle) ? last_lr_angle + max_speed : last_lr_angle - max_speed;
            } else {
                lr_angle = target_lr;
            }
            
            int ud_angle = last_ud_angle;
            if (abs(target_ud - last_ud_angle) > max_speed) {
                ud_angle = (target_ud > last_ud_angle) ? last_ud_angle + max_speed : last_ud_angle - max_speed;
            } else {
                ud_angle = target_ud;
            }
            
            last_lr_angle = lr_angle;
            last_ud_angle = ud_angle;
            
            // Aplica os movimentos
            servoLR.write(lr_angle);
            control_ud_and_lids(ud_angle);
        }
    }

    delay(20); // 50 Hz refresh rate
}

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ==============================================================================
// [NOVO] INCLUDES — HERMES VOICE INTEGRATION
// ==============================================================================
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#include <WiFiManager.h>

// ==============================================================================
// [NOVO] CONFIGURAÇÃO DA BRIDGE — edite apenas se mudar o IP da VPS
// ==============================================================================
#define BRIDGE_URL  "http://195.35.19.208:8080/voice"  // VPS bridge

// ==============================================================================
// [NOVO] BOTÃO PUSH-TO-TALK (pressione para falar com Hermes)
// ==============================================================================
#define BTN_PTT 13  // GPIO 13 — possui pull-up interno, resolve o problema do pino flutuando

// ==============================================================================
// [NOVO] INMP441 — MICROFONE I2S (usa I2S_NUM_0)
// ==============================================================================
#define I2S_MIC_SCK  32   // SCK → GPIO 32
#define I2S_MIC_WS   25   // WS  → GPIO 25
#define I2S_MIC_SD   34   // SD  → GPIO 34 (input-only pin)
#define SAMPLE_RATE  16000
#define RECORD_SECS  2.5
#define PCM_SIZE     (int)(SAMPLE_RATE * RECORD_SECS)     // amostras int16
#define WAV_SIZE     (PCM_SIZE * 2 + 44)             // bytes: PCM + header

// ==============================================================================
// [NOVO] MAX98357 — AMPLIFICADOR I2S (pinos para ESP8266Audio)
// ==============================================================================
#define I2S_SPK_BCLK 26   // BCLK → GPIO 26
#define I2S_SPK_LRC  27   // LRC  → GPIO 27
#define I2S_SPK_DIN  14   // DIN  → GPIO 14

// ==============================================================================
// CANAIS DOS SERVOS NA PLACA PCA9685  (CÓDIGO ORIGINAL — INALTERADO)
// ==============================================================================
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVO_LR      0   // Pan (Esquerda/Direita)
#define SERVO_UD      1   // Tilt (Cima/Baixo)
#define SERVO_TL      2   // Top Left (Pálpebra Superior Esquerda)
#define SERVO_BL      3   // Bottom Left (Pálpebra Inferior Esquerda)
#define SERVO_TR      4   // Top Right (Pálpebra Superior Direita)
#define SERVO_BR      5   // Bottom Right (Pálpebra Inferior Direita)
#define SERVO_NECK_UD 6   // Pescoço Tilt (Cima/Baixo)

// ==============================================================================
// LIMITES (BASEADO NO CÓDIGO DO WILL COGLEY) — CÓDIGO ORIGINAL INALTERADO
// ==============================================================================
const int LR_MIN = 60, LR_MAX = 120;
const int UD_MIN = 60, UD_MAX = 120;
const int TL_MIN = 90, TL_MAX = 10;
const int BL_MIN = 10, BL_MAX = 90;
const int TR_MIN = 10, TR_MAX = 90;
const int BR_MIN = 90, BR_MAX = 10;
const int NECK_UD_MIN = 60, NECK_UD_MAX = 120;

// ==============================================================================
// VARIÁVEIS DE ESTADO (MODO AUTOMÁTICO) — CÓDIGO ORIGINAL INALTERADO
// ==============================================================================
int last_lr_angle = 90;
int last_ud_angle = 90;
unsigned long auto_next_action = 0;

float current_neck_ud = 90.0;
float target_neck_ud  = 90.0;
const float neck_smoothing = 0.05;

// ==============================================================================
// [NOVO] VARIÁVEIS DE ESTADO — MODO HERMES
// ==============================================================================
bool hermes_mode_active = false;  // true enquanto o ESP32 está se comunicando

// ==============================================================================
// FUNÇÕES DE CONTROLE DE OLHOS E PÁLPEBRAS — CÓDIGO ORIGINAL INALTERADO
// ==============================================================================

void setServoAngle(uint8_t n, int angle) {
    int microsec = map(angle, 0, 180, 1000, 2000);
    pwm.writeMicroseconds(n, microsec);
}

void blink() {
    setServoAngle(SERVO_TL, TL_MIN);
    setServoAngle(SERVO_BL, BL_MIN);
    setServoAngle(SERVO_TR, TR_MIN);
    setServoAngle(SERVO_BR, BR_MIN);
}

void neutral() {
    setServoAngle(SERVO_LR, 90);
    setServoAngle(SERVO_UD, 90);
    setServoAngle(SERVO_TL, TL_MAX);
    setServoAngle(SERVO_BL, BL_MAX);
    setServoAngle(SERVO_TR, TR_MAX);
    setServoAngle(SERVO_BR, BR_MAX);
    current_neck_ud = 90.0;
    target_neck_ud  = 90.0;
    setServoAngle(SERVO_NECK_UD, 90);
}

void control_ud_and_lids(int ud_angle) {
    float ud_range    = 140.0 - 40.0;
    float ud_progress = (ud_angle - 40.0) / ud_range;
    ud_progress = constrain(ud_progress, 0.0f, 1.0f);

    float top_close_factor    = 0.6 * (1.0 - ud_progress);
    float bottom_close_factor = 0.6 * ud_progress;

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

// ==============================================================================
// [NOVO] FUNÇÕES — EXPRESSÃO DOS OLHOS BASEADA NA EMOÇÃO DO HERMES
// ==============================================================================

// Abre/fecha pálpebras em percentual (0.0 = fechado, 1.0 = aberto)
void setEyelidOpenness(float openness) {
    openness = constrain(openness, 0.0f, 1.0f);
    int tl = (int)(TL_MIN + (TL_MAX - TL_MIN) * openness);
    int bl = (int)(BL_MIN + (BL_MAX - BL_MIN) * openness);
    int tr = (int)(TR_MIN + (TR_MAX - TR_MIN) * openness);
    int br = (int)(BR_MIN + (BR_MAX - BR_MIN) * openness);
    setServoAngle(SERVO_TL, tl);
    setServoAngle(SERVO_BL, bl);
    setServoAngle(SERVO_TR, tr);
    setServoAngle(SERVO_BR, br);
}

// Aplica uma expressão recebida do Hermes via JSON
void applyExpression(int lr, int ud, float eyelid_open) {
    lr = constrain(lr, LR_MIN, LR_MAX);
    ud = constrain(ud, UD_MIN, UD_MAX);
    setServoAngle(SERVO_LR, lr);
    setServoAngle(SERVO_UD, ud);
    setEyelidOpenness(eyelid_open);
    target_neck_ud = (float)ud;
}

// Animação "ouvindo" — olhos arregalados, olha ligeiramente para cima
void expressionListening() {
    applyExpression(90, 85, 1.0);
}

// Animação "pensando" — olha para cima-esquerda, pálpebras semi-fechadas
void expressionThinking() {
    applyExpression(70, 105, 0.7);
}

// ==============================================================================
// [NOVO] FUNÇÕES — MICROFONE INMP441 (I2S_NUM_0)
// ==============================================================================

void setupMic() {
    i2s_config_t mic_cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441: 24-bit em frame 32-bit
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 1024,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t mic_pins = {
        .bck_io_num   = I2S_MIC_SCK,
        .ws_io_num    = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_MIC_SD
    };
    i2s_driver_install(I2S_NUM_0, &mic_cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &mic_pins);
    i2s_zero_dma_buffer(I2S_NUM_0);
    Serial.println("[MIC] INMP441 I2S pronto.");
}

// Grava áudio e preenche buf com WAV (16kHz, 16-bit, mono)
// Retorna o tamanho total do buffer WAV em bytes
int recordWAV(uint8_t* wav_buf) {
    int16_t* pcm = (int16_t*)(wav_buf + 44);  // PCM começa depois do header
    size_t bytes_read;
    int samples_read = 0;

    Serial.println("[MIC] Gravando...");
    while (samples_read < PCM_SIZE) {
        int32_t raw32[256];
        int chunk = min((int)(PCM_SIZE - samples_read), 256);
        i2s_read(I2S_NUM_0, raw32, chunk * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        int n = bytes_read / sizeof(int32_t);
        for (int i = 0; i < n; i++) {
            // INMP441: dados nos bits 31..8 (24-bit left-justified)
            // Shift de 14 para obter 16-bit com boa amplitude
            pcm[samples_read++] = (int16_t)(raw32[i] >> 14);
        }
    }
    Serial.println("[MIC] Gravação concluída.");

    // Monta o cabeçalho WAV
    uint32_t data_size    = PCM_SIZE * 2;
    uint32_t file_size    = data_size + 36;
    uint32_t byte_rate    = SAMPLE_RATE * 2;
    uint16_t block_align  = 2;

    memcpy(wav_buf,      "RIFF", 4);
    memcpy(wav_buf +  4, &file_size,    4);
    memcpy(wav_buf +  8, "WAVE", 4);
    memcpy(wav_buf + 12, "fmt ", 4);
    uint32_t subchunk1_size = 16;
    memcpy(wav_buf + 16, &subchunk1_size, 4);
    uint16_t audio_format = 1;  // PCM
    memcpy(wav_buf + 20, &audio_format, 2);
    uint16_t channels = 1;
    memcpy(wav_buf + 22, &channels, 2);
    uint32_t sample_rate = SAMPLE_RATE;
    memcpy(wav_buf + 24, &sample_rate, 4);
    memcpy(wav_buf + 28, &byte_rate, 4);
    memcpy(wav_buf + 32, &block_align, 2);
    uint16_t bits_per_sample = 16;
    memcpy(wav_buf + 34, &bits_per_sample, 2);
    memcpy(wav_buf + 36, "data", 4);
    memcpy(wav_buf + 40, &data_size, 4);

    return WAV_SIZE;
}

// ==============================================================================
// [NOVO] FUNÇÕES — COMUNICAÇÃO COM A BRIDGE (HTTP) E PLAYBACK
// ==============================================================================

String sendToHermes(uint8_t* wav_buf, int wav_len, String& out_audio_url, JsonDocument& out_expr) {
    WiFiClient wifi_client;
    HTTPClient http;

    Serial.printf("[HTTP] Enviando %d bytes para %s\n", wav_len, BRIDGE_URL);

    http.begin(wifi_client, BRIDGE_URL);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Session-ID", "esp32_head");
    http.setTimeout(30000);  // 30s timeout para STT + Hermes + TTS

    int httpCode = http.POST(wav_buf, wav_len);

    if (httpCode != 200) {
        Serial.printf("[HTTP] Erro HTTP: %d\n", httpCode);
        http.end();
        return "";
    }

    String payload = http.getString();
    http.end();

    // Parse do JSON de resposta
    DeserializationError err = deserializeJson(out_expr, payload);
    if (err) {
        Serial.printf("[HTTP] Erro JSON: %s\n", err.c_str());
        return "";
    }

    out_audio_url = out_expr["audio_url"].as<String>();
    String reply_text = out_expr["text"].as<String>();

    Serial.printf("[Hermes] '%s'\n", reply_text.substring(0, 60).c_str());
    Serial.printf("[Audio]  URL: %s\n", out_audio_url.c_str());

    return reply_text;
}

// Toca MP3 via HTTP stream no MAX98357 (I2S_NUM_1, gerenciado pelo ESP8266Audio)
void playAudioFromUrl(const String& url) {
    if (url.isEmpty()) return;

    Serial.println("[AUDIO] Iniciando playback...");

    AudioFileSourceHTTPStream* source = new AudioFileSourceHTTPStream(url.c_str());
    AudioGeneratorMP3* mp3            = new AudioGeneratorMP3();
    AudioOutputI2S* out               = new AudioOutputI2S(1);  // I2S_NUM_1

    out->SetPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
    out->SetGain(0.8);

    if (mp3->begin(source, out)) {
        while (mp3->isRunning()) {
            if (!mp3->loop()) {
                mp3->stop();
                break;
            }
        }
    }

    delete mp3;
    delete source;
    delete out;

    Serial.println("[AUDIO] Playback concluído.");
}

// ==============================================================================
// [NOVO] FUNÇÃO PRINCIPAL — INTERAÇÃO COM HERMES
// ==============================================================================

// O buffer de áudio será alocado dinamicamente para não estourar a RAM (BSS)

void handleHermesInteraction() {
    Serial.println("\n=== MODO HERMES ATIVADO ===");
    hermes_mode_active = true;

    // 1. Animação "ouvindo"
    blink();
    delay(100);
    expressionListening();

    // 2. Grava áudio (aloca buffer no heap)
    uint8_t* wav_buffer = (uint8_t*)malloc(WAV_SIZE);
    if (!wav_buffer) {
        Serial.println("[Erro] Memoria RAM insuficiente para gravar audio!");
        neutral();
        hermes_mode_active = false;
        return;
    }
    int wav_len = recordWAV(wav_buffer);

    // 3. Animação "pensando" enquanto processa
    expressionThinking();

    // 4. Envia para a bridge e recebe resposta
    String audio_url;
    JsonDocument expr_doc;
    String reply = sendToHermes(wav_buffer, wav_len, audio_url, expr_doc);

    // Libera a memória do áudio logo após o envio
    free(wav_buffer);

    if (reply.isEmpty()) {
        Serial.println("[Hermes] Sem resposta. Voltando ao modo autônomo.");
        neutral();
        hermes_mode_active = false;
        return;
    }

    // 5. Aplica a expressão recebida do Hermes nos olhos
    JsonObject expr = expr_doc["expression"];
    int lr          = expr["lr"]           | 90;
    int ud          = expr["ud"]           | 90;
    float eyelid    = expr["eyelid_open"]  | 1.0f;
    applyExpression(lr, ud, eyelid);

    // 6. Toca o áudio da resposta
    playAudioFromUrl(audio_url);

    // 7. Pisca e volta ao neutro
    delay(300);
    blink();
    delay(100);
    neutral();

    Serial.println("=== MODO HERMES ENCERRADO ===\n");
    hermes_mode_active = false;
}

// ==============================================================================
// SETUP — expandido (código original + novas inicializações)
// ==============================================================================
void setup() {
    Serial.begin(115200);

    // I2C original — PCA9685
    Wire.begin(21, 22);
    pwm.begin();
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(50);

    Serial.println("Calibrando no Centro...");
    neutral();
    delay(1000);

    // [NOVO] Botão PTT
    pinMode(BTN_PTT, INPUT_PULLUP);

    // [NOVO] Microfone I2S
    setupMic();

    // [NOVO] Wi-Fi usando WiFiManager (Portal Captivo)
    WiFiManager wm;
    // Tenta conectar. Se falhar, abre um ponto de acesso por 3 minutos (180 seg).
    wm.setConfigPortalTimeout(180);

    Serial.println("[WiFi] Conectando ou abrindo portal 'Olho_Animatronic'...");
    
    // Se não conectar nas redes salvas, ele criará a rede "Olho_Animatronic" (sem senha)
    if (!wm.autoConnect("Olho_Animatronic")) {
        Serial.println("\n[WiFi] Falha na conexão ou timeout. Modo autonomo ativo.");
    } else {
        Serial.printf("\n[WiFi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    }

    Serial.println("[ESP32] Pronto! Segure o botao para falar com o Hermes.");
}

// ==============================================================================
// LOOP — botão PTT verifica primeiro, depois modo autônomo original
// ==============================================================================
void loop() {

    // [NOVO] Botão PTT pressionado → fala com Hermes
    if (digitalRead(BTN_PTT) == LOW && !hermes_mode_active) {
        delay(50);  // debounce
        if (digitalRead(BTN_PTT) == LOW) {
            handleHermesInteraction();
            return;  // pula o modo autônomo nesta iteração
        }
    }

    // ── MODO AUTÔNOMO ORIGINAL (100% inalterado) ─────────────────────────────
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
            target_neck_ud = r_ud;
            auto_next_action = millis() + random(300, 1000);
        } else {
            int r_ud = random(UD_MIN, UD_MAX);
            int r_lr = random(LR_MIN, LR_MAX);
            control_ud_and_lids(r_ud);
            setServoAngle(SERVO_LR, r_lr);
            last_ud_angle = r_ud;
            last_lr_angle = r_lr;
            target_neck_ud = r_ud;
            auto_next_action = millis() + random(200, 400);
        }
    }

    // Atualiza posição do pescoço suavemente
    current_neck_ud += (target_neck_ud - current_neck_ud) * neck_smoothing;
    setServoAngle(SERVO_NECK_UD, (int)current_neck_ud);

    delay(20);  // 50 Hz refresh rate
}

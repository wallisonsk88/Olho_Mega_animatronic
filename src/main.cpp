#include <Arduino.h>
#include <Wire.h>
#ifndef TARGET_ESP32_S3
// PCA9685 só é usado no ESP32 clássico (olho mecânico com servos)
#include <Adafruit_PWMServoDriver.h>
#endif

// ==============================================================================
// INCLUDES — HERMES VOICE INTEGRATION
// ==============================================================================
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <WiFiManager.h>

#ifdef TARGET_ESP32_S3
// Display ST7789 — só ativo no ambiente ESP32-S3
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#endif

// ==============================================================================
// [NOVO] CONFIGURAÇÃO DA BRIDGE — edite apenas se mudar o IP da VPS
// ==============================================================================
#define BRIDGE_URL  "http://195.35.19.208:8080/voice"  // VPS bridge

// ==============================================================================
// SELEÇÃO AUTOMÁTICA DE PINOS POR PLACA
// Compilar com env:esp32dev → pinos do ESP32 clássico
// Compilar com env:esp32s3  → pinos do ESP32-S3 N16R8
// ==============================================================================

#ifdef TARGET_ESP32_S3
  // ── ESP32-S3 N16R8 DevKitC-1 ─────────────────────────────────────────────
  // GPIOs 26–32 são PROIBIDOS no S3 (usados pela PSRAM/Flash interna)
  #define BTN_PTT      13   // Botão PTT → GPIO 13
  // INMP441 — Microfone I2S (I2S_NUM_0)
  #define I2S_MIC_SCK   5   // SCK  → GPIO 5
  #define I2S_MIC_WS    6   // WS   → GPIO 6
  #define I2S_MIC_SD    7   // SD   → GPIO 7
  // MAX98357 — Amplificador I2S (I2S_NUM_1)
  #define I2S_SPK_BCLK 15   // BCLK → GPIO 15
  #define I2S_SPK_LRC  16   // LRC  → GPIO 16
  #define I2S_SPK_DIN  14   // DIN  → GPIO 14
  // I2C — PCA9685 (SCL mudou: GPIO 22 nao existe no S3)
  #define I2C_SDA      21   // SDA  → GPIO 21
  #define I2C_SCL       8   // SCL  → GPIO 8 (GPIO 47 era difícil de localizar)
  // Audio: S3 tem 8MB PSRAM — grava muito mais tempo!
  #define SAMPLE_RATE  16000
  #define RECORD_SECS  10.0  // 10s = ~320KB → vai na PSRAM, sem problema!
  // Alocação na PSRAM externa (ps_malloc)
  #define AUDIO_MALLOC(size) ps_malloc(size)

#else
  // ── ESP32 Clássico (esp32dev) — pinos originais INALTERADOS ──────────────
  #define BTN_PTT      13   // Botão PTT → GPIO 13
  // INMP441 — Microfone I2S (I2S_NUM_0)
  #define I2S_MIC_SCK  32   // SCK  → GPIO 32
  #define I2S_MIC_WS   25   // WS   → GPIO 25
  #define I2S_MIC_SD   34   // SD   → GPIO 34 (input-only)
  // MAX98357 — Amplificador I2S (I2S_NUM_1)
  #define I2S_SPK_BCLK 26   // BCLK → GPIO 26
  #define I2S_SPK_LRC  27   // LRC  → GPIO 27
  #define I2S_SPK_DIN  14   // DIN  → GPIO 14
  // I2C — PCA9685 (pinos originais)
  #define I2C_SDA      21   // SDA  → GPIO 21
  #define I2C_SCL      22   // SCL  → GPIO 22
  // Audio: ESP32 clássico tem RAM limitada
  #define SAMPLE_RATE  16000
  #define RECORD_SECS  3.0   // 3s = limite maximo de RAM com WiFi ativo
  // Alocação na heap interna (malloc normal)
  #define AUDIO_MALLOC(size) malloc(size)

#endif

#define PCM_SIZE  (int)(SAMPLE_RATE * RECORD_SECS)
#define WAV_SIZE  (PCM_SIZE * 2 + 44)

// VAD - Voice Activity Detection
#define SILENCE_THRESHOLD   2000  // Aumentado para ignorar ruído de fundo
#define SILENCE_TIMEOUT_MS  1500

// ==============================================================================
// CANAIS DOS SERVOS NA PLACA PCA9685  (CÓDIGO ORIGINAL — INALTERADO)
// ==============================================================================
#ifndef TARGET_ESP32_S3
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#endif

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
bool hermes_mode_active = false;

#ifdef TARGET_ESP32_S3
// ==============================================================================
// DISPLAY ST7789 — Avatar Mobine
// ==============================================================================
TFT_eSPI tft = TFT_eSPI();

// ==============================================================================
// LÓGICA DE GIF ANIMADO (AnimatedGIF + LittleFS)
// ==============================================================================
AnimatedGIF gif;
File gifFile;
String currentGifPath = "";
String nextGifPath = "/neutral.gif";

// Callbacks para o LittleFS
void* GIFOpenFile(const char* fname, int32_t* pSize) {
    gifFile = LittleFS.open(fname, "r");
    if (gifFile) {
        *pSize = gifFile.size();
        return (void*)&gifFile;
    }
    return NULL;
}
void GIFCloseFile(void* pHandle) {
    File* f = static_cast<File*>(pHandle);
    if (f != NULL) f->close();
}
int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    File* f = static_cast<File*>(pFile->fHandle);
    if ((pFile->iSize - pFile->iPos) < iLen)
        iBytesRead = pFile->iSize - pFile->iPos - 1;
    if (iBytesRead <= 0) return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
}
int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

// Callback de Desenho no TFT
void GIFDraw(GIFDRAW* pDraw) {
    uint8_t* s;
    uint16_t* usPalette;
    uint16_t usTemp[240];
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > 240) iWidth = 240;
    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // linha atual
    s = pDraw->pPixels;

    if (pDraw->ucHasTransparency) {
        uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
        int iCount = 0;
        pEnd = s + iWidth;
        x = 0;
        while(x < iWidth) {
            c = ucTransparent - 1;
            uint16_t* d = usTemp;
            while (c != ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) s--;
                else { *d++ = usPalette[c]; iCount++; }
            }
            if (iCount) {
                tft.pushImage(pDraw->iX + x, y, iCount, 1, usTemp);
                x += iCount;
                iCount = 0;
            }
            c = ucTransparent;
            while (c == ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) iCount++;
                else s--; 
            }
            if (iCount) {
                x += iCount;
                iCount = 0;
            }
        }
    } else {
        for (x = 0; x < iWidth; x++) usTemp[x] = usPalette[*s++];
        tft.pushImage(pDraw->iX, y, iWidth, 1, usTemp);
    }
}

// Tarefa FreeRTOS que roda no Core 0 (paralelo ao áudio) para animar o GIF sem travamentos
void gifTask(void* parameter) {
    gif.begin(LITTLE_ENDIAN_PIXELS);
    while (true) {
        if (nextGifPath != currentGifPath) {
            currentGifPath = nextGifPath;
        }
        if (gif.open(currentGifPath.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            while (nextGifPath == currentGifPath) {
                int result = gif.playFrame(true, NULL); // true = respeita o delay do frame do GIF
                if (result == 0) { 
                    break; // Fim do GIF, sai do while para reabrir e fazer loop
                }
            }
            gif.close();
        } else {
            delay(100); // Falha ao abrir o GIF, aguarda antes de tentar de novo
        }
    }
}

// Define qual GIF a task vai carregar na próxima repetição
void setFaceGif(String gifName) {
    nextGifPath = "/" + gifName + ".gif";
}

void setupDisplay() {
    Serial.println("[Display] Iniciando tft.init()...");
    tft.init();
    Serial.println("[Display] init() OK. Setando rotacao e SwapBytes...");
    tft.setRotation(0);
    tft.setSwapBytes(true); // <--- Mantém correção de cores
    
    Serial.println("[Display] Pintando preto...");
    tft.fillScreen(TFT_BLACK);

    if (!LittleFS.begin()) {
        Serial.println("[Display] FALHA NO LITTLEFS! Formate a placa ou rode Upload File System Image!");
    } else {
        Serial.println("[Display] LittleFS OK!");
    }
    
    Serial.println("[Display] Iniciando tarefa de Animacao GIF...");
    setFaceGif("neutral");
    // Cria a task no Core 0 para não atrapalhar o Wi-Fi/Áudio (Core 1)
    xTaskCreatePinnedToCore(gifTask, "GifTask", 4096, NULL, 1, NULL, 0);
    
    Serial.println("[Display] ST7789 240x240 e GIFs prontos!");
}
#endif  // TARGET_ESP32_S3

// ==============================================================================
// FUNÇÕES DE CONTROLE DE OLHOS E PÁLPEBRAS — CÓDIGO ORIGINAL INALTERADO
// ==============================================================================

void setServoAngle(uint8_t n, int angle) {
#ifndef TARGET_ESP32_S3
    int microsec = map(angle, 0, 180, 1000, 2000);
    pwm.writeMicroseconds(n, microsec);
#endif
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
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,  // TESTE: trocando para RIGHT
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

    int16_t max_amplitude = 0;
    int32_t max_raw = 0;  // DIAGNÓSTICO: valor bruto máximo antes de qualquer shift

    uint32_t last_speech_time = millis();
    bool speech_detected = false;

    Serial.println("[MIC] Gravando...");
    while (samples_read < PCM_SIZE) {
        int32_t raw32[256];
        int chunk = min((int)(PCM_SIZE - samples_read), 256);
        i2s_read(I2S_NUM_0, raw32, chunk * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        int n = bytes_read / sizeof(int32_t);
        for (int i = 0; i < n; i++) {
            // DIAGNÓSTICO: guarda o maior valor bruto (sem shift)
            if (abs(raw32[i]) > abs(max_raw)) max_raw = raw32[i];

            // INMP441: shift de 10 para converter 24-bit → 16-bit com ganho
            int32_t sample32 = raw32[i] >> 10;
            if (sample32 > 32767) sample32 = 32767;
            if (sample32 < -32768) sample32 = -32768;

            int16_t sample = (int16_t)sample32;
            pcm[samples_read++] = sample;

            if (abs(sample) > max_amplitude) max_amplitude = abs(sample);

            // VAD: Reseta o timer de silêncio se detectar fala
            if (abs(sample) > SILENCE_THRESHOLD) {
                last_speech_time = millis();
                speech_detected = true;
            }
        }

        // Verifica VAD: se já detectou fala e passou tempo de silêncio, corta
        if (speech_detected && (samples_read > SAMPLE_RATE)) { // min 1 segundo de gravação
            if (millis() - last_speech_time > SILENCE_TIMEOUT_MS) {
                Serial.println("[MIC] Silencio detectado, encerrando gravacao antecipadamente.");
                break;
            }
        }
        // Se nunca detectar fala após 3 segundos, desiste
        if (!speech_detected && (samples_read > SAMPLE_RATE * 3)) {
            Serial.println("[MIC] Nenhuma fala detectada. Cancelando.");
            break;
        }
    }
    // DIAGNÓSTICO: se max_raw for 0, o I2S não recebeu NADA do microfone (problema físico)
    // Se max_raw for != 0 mas max_amplitude for 0, é bug no shift
    Serial.printf("[MIC] Concluido. Raw Max: %ld | Amplitude Max: %d | Amostras: %d\n", max_raw, max_amplitude, samples_read);

    // Monta o cabeçalho WAV
    uint32_t data_size    = samples_read * 2;
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

    return data_size + 44;
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

    // 1. Expressão OUVINDO
#ifdef TARGET_ESP32_S3
    setFaceGif("listening");
#else
    blink(); delay(100); expressionListening();
#endif

    // 2. Grava áudio (PSRAM no S3, heap no clássico)
    uint8_t* wav_buffer = (uint8_t*)AUDIO_MALLOC(WAV_SIZE);
    if (!wav_buffer) {
        Serial.println("[Erro] Memoria insuficiente para gravar audio!");
#ifdef TARGET_ESP32_S3
        setFaceGif("neutral");
#else
        neutral();
#endif
        hermes_mode_active = false;
        return;
    }
    int wav_len = recordWAV(wav_buffer);

    // 3. Expressão PENSANDO enquanto a VPS processa
#ifdef TARGET_ESP32_S3
    setFaceGif("thinking");
#else
    expressionThinking();
#endif

    // 4. Envia para a bridge e recebe resposta
    String audio_url;
    JsonDocument expr_doc;
    String reply = sendToHermes(wav_buffer, wav_len, audio_url, expr_doc);
    free(wav_buffer);

    if (reply.isEmpty()) {
        Serial.println("[Hermes] Sem resposta. Voltando ao modo autonomo.");
#ifdef TARGET_ESP32_S3
        setFaceGif("neutral");
#else
        neutral();
#endif
        hermes_mode_active = false;
        return;
    }

    // 5. Detecta emoção e atualiza o rosto
#ifdef TARGET_ESP32_S3
    // Lê a emoção do JSON retornado pelo Hermes
    String emotion = expr_doc["emotion"] | "neutral";
    Serial.printf("[Display] Emocao: %s\n", emotion.c_str());
    if      (emotion == "happy")     setFaceGif("happy");
    else if (emotion == "surprised") setFaceGif("surprised");
    else if (emotion == "thinking")  setFaceGif("thinking");
    else                             setFaceGif("talking");
#else
    // ESP32 clássico: aplica expressão nos servos
    JsonObject expr = expr_doc["expression"];
    int lr       = expr["lr"]          | 90;
    int ud       = expr["ud"]          | 90;
    float eyelid = expr["eyelid_open"] | 1.0f;
    applyExpression(lr, ud, eyelid);
#endif

    // 6. Toca o áudio da resposta (boca animada no S3)
    playAudioFromUrl(audio_url);

    // 7. Volta ao neutro
    delay(300);
#ifdef TARGET_ESP32_S3
    setFaceGif("neutral");
#else
    blink(); delay(100); neutral();
#endif

    Serial.println("=== MODO HERMES ENCERRADO ===\n");
    hermes_mode_active = false;
}

// ==============================================================================
// SETUP — expandido (código original + novas inicializações)
// ==============================================================================
void setup() {
    Serial.begin(115200);

#ifdef TARGET_ESP32_S3
    // Diagnóstico da PSRAM
    delay(500);
    Serial.printf("[S3] PSRAM livre : %d KB\n", ESP.getFreePsram() / 1024);
    Serial.printf("[S3] Heap livre  : %d KB\n", ESP.getFreeHeap() / 1024);
    Serial.printf("[S3] Buffer audio: %d KB (%.0fs @ 16kHz)\n", WAV_SIZE/1024, (float)RECORD_SECS);
    // Inicializa display (sem PCA9685 — usando display no lugar dos servos)
    setupDisplay();
#else
    // ESP32 clássico — inicializa PCA9685 e servos
    Wire.begin(I2C_SDA, I2C_SCL);
    pwm.begin();
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(50);
    Serial.println("Calibrando no Centro...");
    neutral();
    delay(1000);
#endif

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

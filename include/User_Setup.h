// ============================================================
// User_Setup.h — Configuração TFT_eSPI para ST7789 240x240
// ESP32-S3 N16R8 DevKitC-1
// Display 7 pinos (sem pino CS fisico)
// ============================================================

// OBRIGATORIO: diz para TFT_eSPI usar ESTE arquivo, nao o padrao da biblioteca
#define USER_SETUP_LOADED

#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// Usa HSPI (SPI3) para evitar conflito com PSRAM Octal que usa SPI2 (FSPI)
#define USE_HSPI_PORT
#define TFT_MOSI 11
#define TFT_SCLK 12
// #define TFT_CS   -1  // Display sem pino CS fisico
#define TFT_DC    9
#define TFT_RST   8
#define TFT_MISO 17  // Pino explicito para evitar erro "HSPI no default pins on S3"
// #define TFT_BL   -1  // Backlight ligado direto no 3.3V — sem controle software

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

#define TFT_INVERSION_ON

#define LOAD_GLCD

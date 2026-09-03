# 📖 Manual Resumido: Olho Animatrônico + Hermes

Este documento serve como um **guia rápido** de como o ecossistema do projeto funciona, dividindo-se entre o cérebro na nuvem (VPS) e o hardware físico (ESP32-S3).

---

## 1. ☁️ Ambiente VPS (O "Cérebro")
A VPS processa o áudio, toma decisões com IA e gera a voz do robô.
* **IP / Host:** `195.35.19.208`
* **Hermes Agent (Docker):** O núcleo de inteligência da IA roda conteinerizado.
* **A "Bridge" (`esp32_bridge.py`):** Um servidor Python (FastAPI na porta 8080) que faz a ponte entre o ESP32 e o Hermes.
  * **Ouvir (`faster-whisper`):** Transcreve o áudio `.wav` recebido do ESP32 para texto.
  * **Pensar:** Envia o texto para a IA do Hermes e recebe a resposta textual + emoção.
  * **Falar (`edge-tts`):** Transforma o texto da resposta em áudio `.mp3` (Voz pt-BR Francisca).
* **Automação:** O serviço `hermes-bridge.service` mantém a bridge rodando automaticamente.

---

## 2. 🤖 Ambiente ESP32-S3 (O "Corpo")
O ESP32-S3 captura comandos, reproduz respostas e exibe as emoções através de um olho digital.
* **Linguagem / Framework:** C++ no PlatformIO (`src/main.cpp`).
* **Dois Modos de Operação (Compilação Condicional):** 
  * **ESP32 Clássico (Olho Mecânico):** Utiliza a placa **PCA9685** (via I2C) para controlar 6 micro-servos (Pan, Tilt, Pálpebras). As reações de emoção se transformam em movimentos físicos pré-programados dos servos.
  * **ESP32-S3 (Olho Digital):** Utiliza uma tela **TFT ST7789**. As emoções são exibidas visualmente na tela através de GIFs animados.
* **Conectividade:** WiFiManager (cria um portal para colocar a senha da rede caso não consiga conectar).
* **Multiprocessamento (FreeRTOS):** 
  * **Core 1:** Lida com o áudio, WiFi e requests HTTP para a VPS.
  * **Core 0 (Task Isolada):** Roda a biblioteca `AnimatedGIF` que lê imagens `.gif` da memória flash (LittleFS) e renderiza na tela de forma contínua e sem travamentos.
* **Interação:**
  * O usuário pressiona (1 clique) o **Botão PTT**.
  * O microfone I2S grava a fala.
  * O **VAD (Detector de Atividade de Voz)** para a gravação sozinho caso haja 1.5s de silêncio.
  * O arquivo é enviado via HTTP POST para a VPS.
  * O ESP32 recebe a resposta em JSON, troca a animação do olho e toca o MP3 da voz usando o amplificador.

---

## 3. 🔌 Pinagem e Ligações de Hardware (ESP32-S3)

### ⚙️ Movimento Físico (Olho Mecânico - Placa PCA9685)
*Essa placa é responsável por dar vida ao olho 3D movendo as pálpebras e globo.*
* **VCC:** 3.3V / 5V (Para a lógica)
* **V+ (Alimentação dos Servos):** Fonte Externa 5V (OBRIGATÓRIO para aguentar a corrente dos motores)
* **GND:** GND
* **SDA:** Pino 21 
* **SCL:** Pino 22 (No ESP32 Clássico) / Pino 8 (No ESP32-S3)
* **Canais Usados:** 
  * Canal 0: Pan (Esquerda/Direita)
  * Canal 1: Tilt (Cima/Baixo)
  * Canais 2 a 5: Pálpebras (Superior/Inferior - Esquerda/Direita)

### 🖥️ Tela / Olho Digital (Display ST7789 - 7 Pinos - Opcional no ESP32-S3)
* **VCC:** 3.3V
* **GND:** GND
* **SCL:** Pino 12
* **SDA (MOSI):** Pino 11
* **RES (Reset):** Pino 8
* **DC:** Pino 9
* **BLK (Luz):** 3.3V (Ligado direto)

### 🎙️ Ouvidos (Microfone INMP441)
* **VDD:** 3.3V
* **GND:** GND
* **L/R:** GND (Configurado para canal direito no código)
* **SCK:** Pino 5
* **WS:** Pino 6
* **SD:** Pino 7

### 🔊 Voz (Amplificador MAX98357A)
* **VIN:** 5V
* **GND:** GND
* **BCLK:** Pino 15
* **LRC:** Pino 16
* **DIN:** Pino 17
* **+ / - :** Conectados diretamente ao Alto-Falante

### 🕹️ Interação (Botão PTT)
* **Terminal 1:** Pino 13 (Com PULLUP interno ativo no código)
* **Terminal 2:** GND


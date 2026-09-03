# 🤖 Integração Hermes + Olho Animatrônico (ESP32-S3)
**Documento de Contexto e Status do Projeto**

Este arquivo foi criado para salvar todo o nosso progresso. Quando precisar continuar, basta abrir um novo chat e dizer: *"Leia o arquivo HERMES_INTEGRATION.md para lembrarmos onde paramos."*

---

## ✅ STATUS ATUAL: SISTEMA COMPLETO E FUNCIONANDO NO ESP32-S3

Em 27 de Agosto de 2026, concluímos com sucesso a migração do projeto para a placa **ESP32-S3-N16R8**, integrando o display ST7789, o microfone INMP441, e o alto-falante MAX98357A. A comunicação com a VPS e o Hermes Agent está operando perfeitamente.

### Conquistas da Migração (ESP32-S3):
1. **Ambiente Isolado:** O código agora compila sob a flag `-DTARGET_ESP32_S3` no PlatformIO, mantendo compatibilidade com o ESP32 antigo caso necessário.
2. **Correção de SPI do Display (ST7789):** Desativamos os pinos de `TFT_CS` e `TFT_BL` no `User_Setup.h`, evitando conflitos de inicialização do GPIO e travamentos ("Guru Meditation Error").
3. **Correção de Cores Psicodélicas:** Adicionamos `tft.setSwapBytes(true);` para alinhar a leitura (Endianness) Little Endian do ESP32 com o Big Endian exigido pelo display TFT.
4. **Resolução de Bug no Desenho do Rosto:** Corrigimos uma otimização no `showFace()` que impedia o rosto de ser desenhado na primeira execução.
5. **VAD (Voice Activity Detection):** Implementamos corte automático de silêncio na gravação. O robô detecta quando o usuário para de falar por 1.5s e envia o áudio imediatamente, economizando banda e acelerando a resposta.
6. **Animações GIF via LittleFS:** Substituímos as imagens estáticas por GIFs animados (usando a biblioteca `AnimatedGIF`). A reprodução roda em uma Task isolada no Core 0 do FreeRTOS, garantindo animações fluidas sem causar gargalos ou estalos no processamento de áudio (I2S).

---

## 🏗️ Arquitetura Atual

1. **A VPS (Servidor Linux na Nuvem)**
   - **IP:** `195.35.19.208`
   - O Hermes Agent está rodando dentro de um container Docker.
   - Um servidor Python (FastAPI) roda na porta `8080`.
   - O script da Bridge (`esp32_bridge.py`) usa `faster-whisper` para converter o áudio WAV em texto.
   - A Bridge chama o Hermes Agent localmente e captura a resposta.
   - O áudio da resposta é gerado usando `edge-tts` (Voz pt-BR Francisca) e salvo como MP3.
   - A Bridge roda como serviço nativo (`hermes-bridge.service`) — reinicia automaticamente com a VPS.

2. **O ESP32-S3 (Olho Animatrônico com Tela)**
   - **Código:** C++ no PlatformIO (`src/main.cpp`).
   - Conectado ao Wi-Fi via WiFiManager (com portal cautivo no primeiro boot).
   - O botão Push-to-Talk está no **GPIO 13** (pull-up interno).
   - Com apenas **1 clique no botão**, a tela exibe expressão "Ouvindo" e o áudio começa a ser gravado (I2S).
   - **VAD Ativo:** Assim que o silêncio durar mais de 1.5s, o ESP32 encerra a gravação sozinho, ajusta o tamanho do arquivo WAV e envia para a Bridge instantaneamente.
   - Recebe a resposta em JSON, aplica a expressão facial na tela e toca o MP3 no alto-falante.

---

## 🔌 Ligações de Hardware (ESP32-S3 DEFINITIVO)

### Display ST7789 (7 Pinos - Sem CS)
* **GND**  -> GND
* **VCC**  -> 3.3V
* **SCL**  -> Pino **12** (Clock SPI)
* **SDA**  -> Pino **11** (MOSI)
* **RES**  -> Pino **8** (Reset)
* **DC**   -> Pino **9** (Data/Command)
* **BLK**  -> 3.3V (Luz de Fundo ligada direto)

### Microfone INMP441 → ESP32-S3
* **VDD**  -> 3.3V
* **GND**  -> GND
* **L/R**  -> GND (Canal esquerdo, processado no código como I2S_CHANNEL_FMT_ONLY_RIGHT)
* **SCK**  -> Pino **5**
* **WS**   -> Pino **6**
* **SD**   -> Pino **7**

### Amplificador MAX98357 → ESP32-S3
* **VIN**  -> 5V
* **GND**  -> GND
* **BCLK** -> Pino **15**
* **LRC**  -> Pino **16**
* **DIN**  -> Pino **17**
* **+ / -**-> Alto-falante

### Botão Push-to-Talk
* **Terminal 1** -> Pino **13**
* **Terminal 2** -> GND

### Placa PCA9685 (Servos) → ESP32-S3
* **SDA**  -> Pino **4**
* **SCL**  -> Pino **3**
* **VCC**  -> 3.3V
* **GND**  -> GND

---

## 🛠️ Próximos Passos (Possíveis Evoluções)
- [ ] Adicionar suporte a um segundo display para o "Olho Esquerdo" (se desejado).
- [ ] Integrar os servos físicos (PCA9685) baseados nas emoções captadas pela tela, mesclando o robô físico com a tela digital.
- [ ] Ajustar a sensibilidade do microfone ou tempo de espera do botão PTT.

---
*Status atualizado em: 03 de Setembro de 2026 — Integração com GIFs animados no display via LittleFS e FreeRTOS concluída com sucesso* 🎉

# 🤖 Integração Hermes + Olho Animatrônico
**Documento de Contexto e Status do Projeto**

Este arquivo foi criado para salvar todo o nosso progresso. Quando os sensores chegarem, basta abrir um novo chat (ou continuar neste mesmo) e me dizer: *"Leia o arquivo HERMES_INTEGRATION.md para lembrarmos onde paramos e continuar a instalação do hardware."*

---

## 🏗️ Arquitetura Atual (Funcionando)

Criamos uma ponte (Bridge) de comunicação entre o ESP32 e a Inteligência Artificial (Hermes Agent).

1. **A VPS (Servidor Linux na Nuvem)**
   - **IP:** `195.35.19.208`
   - O Hermes Agent está rodando lá dentro de um container Docker (`hermes-agent-wv9p-hermes-agent-1`).
   - Criamos um servidor Python (FastAPI) na porta `8080`.
   - O script da Bridge (`esp32_bridge.py`) usa a biblioteca `faster-whisper` (modelo *tiny*) para converter a sua voz (WAV) em texto.
   - A Bridge usa `docker exec` para passar seu texto pro Hermes Agent localmente e captura a resposta.
   - O áudio da resposta é gerado usando `edge-tts` (Voz pt-BR Francisca) e salvo como MP3.
   - A Bridge roda como um serviço nativo do sistema (`hermes-bridge.service`), ou seja, se a VPS reiniciar, a ponte volta a funcionar automaticamente.

2. **O ESP32 (Olho Animatrônico)**
   - **Código:** C++ no PlatformIO (`main.cpp`).
   - O código principal do ESP32 agora usa **WiFiManager** (Portal Cativo). Se não encontrar rede Wi-Fi, ele cria a rede "Olho_Animatronic" para configuração pelo celular.
   - O loop principal aguarda o botão (Agora no **GPIO 13**, com pull-up interno) ser pressionado.
   - Ao pressionar, os olhos arregalam (Expressão Ouvindo), e o ESP32 captura áudio I2S (quando o hardware chegar) e envia um pacote HTTP POST para `http://195.35.19.208:8080/voice`.
   - O ESP32 recebe a resposta em JSON, extrai a "Expressão Facial" baseada no sentimento do Hermes, move os servos e dá Play no áudio recebido.

---

## 🛠️ Próximos Passos (O que fazer quando o hardware chegar)

As peças que estamos aguardando são:
- **INMP441** (Microfone Omnidirecional I2S)
- **MAX98357** (Amplificador de Áudio I2S) + Alto-falante

### 1. Ligações do Microfone (INMP441) no ESP32:
* VDD  --> 3.3V
* GND  --> GND
* L/R  --> GND (Para definir como canal Esquerdo)
* SCK  --> GPIO 32
* WS   --> GPIO 25
* SD   --> GPIO 34

### 2. Ligações do Amplificador (MAX98357) no ESP32:
* VIN  --> 5V (ou 3.3V, depende do volume desejado)
* GND  --> GND
* BCLK --> GPIO 26
* LRC  --> GPIO 27
* DIN  --> GPIO 14
* Terminais + e - --> Ligados no Alto-falante

### 3. Finalização
Quando soldar tudo, a única coisa que precisaremos fazer é testar se o áudio está saindo limpo no alto-falante e se o microfone não está captando muito ruído dos servos. Todo o código (C++ e Python) já está escrito, gravado e pronto para lidar com eles!

---
### Histórico de Correções:
- **Problema de Memória (dram0_0_seg):** Resolvido movendo o buffer de gravação para a memória dinâmica (Heap) no momento de uso.
- **Botão Fantasma:** O pino 35 flutuante estava disparando a IA sozinho. Trocado para o **Pino 13** usando PULL_UP interno.
- **Flexibilidade Wi-Fi:** Adicionado `WiFiManager` para conectar em novas redes sem precisar regravar o ESP32.

---
*Status atualizado em: 21 de Agosto de 2026*

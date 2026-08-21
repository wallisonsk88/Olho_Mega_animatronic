#!/usr/bin/env python3
"""
esp32_bridge.py — Bridge ESP32 Animatrônico ↔ Hermes
=====================================================
Roda na VPS Hostinger.

Fluxo REAL (confirmado pelo próprio Hermes):
  1. ESP32 envia áudio WAV  →  POST /voice
  2. Bridge transcreve com Whisper (STT) → texto
  3. Bridge chama:  hermes chat -q "texto" --quiet
  4. Hermes responde no stdout
  5. Bridge converte resposta em MP3 (Edge TTS)
  6. Bridge retorna: { audio_url, expression, text }
  7. ESP32 toca o MP3 no MAX98357 e anima os servos

Como rodar na VPS:
  pip install -r bridge_requirements.txt
  python esp32_bridge.py

Testar sem ESP32 (via curl):
  curl -X POST http://localhost:8080/test \
       -H "Content-Type: application/json" \
       -d '{"message": "Olá Hermes, tudo bem?"}'
"""

import os
import uuid
import asyncio
import tempfile
from pathlib import Path

import edge_tts
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, FileResponse
from faster_whisper import WhisperModel

# ==============================================================================
# CONFIGURAÇÃO — edite ou use variáveis de ambiente
# ==============================================================================
# Voz Edge TTS em português brasileiro
# Outras vozes: pt-BR-AntonioNeural (masculina), pt-PT-RaquelNeural
TTS_VOICE = os.getenv("TTS_VOICE", "pt-BR-FranciscaNeural")

# Modelo Whisper: tiny (rápido) | small (equilíbrio) | medium (preciso)
WHISPER_MODEL = os.getenv("WHISPER_MODEL", "small")

# IP público da VPS — o ESP32 usa esse IP para buscar o MP3
VPS_PUBLIC_IP = os.getenv("VPS_PUBLIC_IP", "SEU_IP_VPS_AQUI")

# Porta da bridge
PORT = int(os.getenv("BRIDGE_PORT", "8080"))

# Caminho do executável hermes — confirmado na VPS
HERMES_CMD = os.getenv("HERMES_CMD", "/usr/local/bin/hermes")

# Flags extras do hermes chat — confirmados via teste na VPS
HERMES_FLAGS = ["--quiet", "--ignore-rules", "--ignore-user-config"]

# Diretório para os MP3 gerados
AUDIO_DIR = Path("/tmp/hermes_audio")
AUDIO_DIR.mkdir(parents=True, exist_ok=True)

# ==============================================================================
# INICIALIZAÇÃO
# ==============================================================================
app = FastAPI(
    title="ESP32-Hermes Bridge",
    description="Bridge de voz entre o ESP32 animatrônico e o agente Hermes",
    version="2.0.0"
)

print(f"[Bridge] Carregando Whisper '{WHISPER_MODEL}' — aguarde...")
whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
print("[Bridge] Whisper pronto!")

# ==============================================================================
# SISTEMA DE EXPRESSÕES — Emoção detectada → posição dos olhos
# ==============================================================================
EMOTION_KEYWORDS = {
    "happy":     ["ótimo", "legal", "incrível", "feliz", "excelente", "perfeito",
                  "show", "adoro", "maravilha", "consegui", "pronto", "boa", "claro"],
    "thinking":  ["deixa eu", "analisando", "vou verificar", "hmm", "pensando",
                  "vamos ver", "calculando", "processando", "buscando", "verificando"],
    "serious":   ["atenção", "cuidado", "importante", "alerta", "erro",
                  "problema", "aviso", "falha", "não funciona", "impossível"],
    "surprised": ["nossa", "sério", "mesmo", "surpreendente",
                  "não acredito", "uau", "que coisa", "incrível"],
}

EXPRESSIONS = {
    "happy":     {"type": "happy",     "lr": 90, "ud": 80,  "eyelid_open": 1.0},
    "thinking":  {"type": "thinking",  "lr": 70, "ud": 105, "eyelid_open": 0.7},
    "serious":   {"type": "serious",   "lr": 90, "ud": 85,  "eyelid_open": 0.5},
    "surprised": {"type": "surprised", "lr": 90, "ud": 75,  "eyelid_open": 1.0},
    "neutral":   {"type": "neutral",   "lr": 90, "ud": 90,  "eyelid_open": 1.0},
}

def detect_emotion(text: str) -> dict:
    text_lower = text.lower()
    for emotion, keywords in EMOTION_KEYWORDS.items():
        if any(kw in text_lower for kw in keywords):
            return EXPRESSIONS[emotion]
    return EXPRESSIONS["neutral"]

# ==============================================================================
# FUNÇÃO PRINCIPAL — Chama o Hermes via CLI
# ==============================================================================

async def ask_hermes(message: str) -> str:
    """
    Chama: /usr/local/bin/hermes chat -q "mensagem" --quiet --ignore-rules --ignore-user-config
    Retorna o texto de resposta da IA extraído do stdout.

    Stdout confirmado na VPS:
      <texto da resposta do agente>
      session_id: XXXXXXXX
    """
    print(f"[Hermes] Enviando: '{message}'")

    cmd = [HERMES_CMD, "chat", "-q", message] + HERMES_FLAGS
    print(f"[Hermes] Comando: {' '.join(cmd)}")

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=30.0)

        raw_output = stdout.decode("utf-8", errors="ignore").strip()
        print(f"[Hermes] stdout:\n---\n{raw_output}\n---")

        # Filtra linhas de metadado e avisos conhecidos:
        #   session_id: XXXX
        #   TERMINAL_CWD=... found in .env — this is deprecated.
        SKIP_PREFIXES = (
            "session_id:",
            "TERMINAL_CWD=",
            "found in .env",
        )
        lines = raw_output.split("\n")
        response_lines = [
            l for l in lines
            if l.strip()
            and not any(l.strip().startswith(p) for p in SKIP_PREFIXES)
            and "found in .env" not in l
        ]
        reply = "\n".join(response_lines).strip()

        # Fallback: se não sobrou nada útil, devolve o raw completo
        if not reply:
            reply = raw_output

        print(f"[Hermes] Resposta final: '{reply[:120]}'")
        return reply

    except asyncio.TimeoutError:
        return "Desculpe, demorei demais para responder. Tente novamente."
    except FileNotFoundError:
        return f"Hermes nao encontrado em {HERMES_CMD}. Verifique o caminho."
    except Exception as e:
        print(f"[Hermes] Erro: {e}")
        return f"Ocorreu um erro interno: {str(e)}"

# ==============================================================================
# ENDPOINTS
# ==============================================================================

@app.get("/health")
async def health():
    """Verifica se a bridge está rodando."""
    # Testa se o hermes está acessível
    try:
        proc = await asyncio.create_subprocess_exec(
            HERMES_CMD, "--version",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=5.0)
        hermes_version = stdout.decode().strip().split("\n")[0]  # primeira linha
    except Exception:
        hermes_version = f"nao encontrado em {HERMES_CMD}"

    return {
        "status": "ok",
        "bridge_version": "2.0.0",
        "whisper_model": WHISPER_MODEL,
        "tts_voice": TTS_VOICE,
        "hermes_cmd": HERMES_CMD,
        "hermes_version": hermes_version,
        "vps_ip": VPS_PUBLIC_IP,
    }


@app.post("/voice")
async def voice_endpoint(request: Request):
    """
    Endpoint principal — recebe áudio WAV binário do ESP32.

    Headers:
      Content-Type: application/octet-stream
      X-Session-ID: esp32_head   (opcional)

    Body: arquivo WAV (16kHz, 16-bit, mono)

    Retorna:
      {
        "audio_url":  "http://IP:8080/audio/hermes_XXXX.mp3",
        "expression": {"type": "neutral", "lr": 90, "ud": 90, "eyelid_open": 1.0},
        "text":       "Resposta do Hermes...",
        "transcript": "O que o usuário disse..."
      }
    """
    session_id = request.headers.get("x-session-id", str(uuid.uuid4())[:8])

    try:
        # ── 1. Recebe o áudio WAV ──────────────────────────────────────────────
        audio_bytes = await request.body()
        print(f"\n[Bridge] === Nova requisição === (sessao={session_id})")
        print(f"[Bridge] Audio recebido: {len(audio_bytes):,} bytes")

        if len(audio_bytes) < 1000:
            return JSONResponse(
                {"error": "Audio muito curto. Segure o botao e fale por pelo menos 2 segundos!"},
                status_code=400
            )

        # ── 2. Whisper STT — transcreve o áudio ───────────────────────────────
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp.write(audio_bytes)
            tmp_path = tmp.name

        segments, _ = whisper.transcribe(tmp_path, language="pt", beam_size=1)
        transcript = " ".join(seg.text for seg in segments).strip()
        os.unlink(tmp_path)

        print(f"[Bridge] Transcricao: '{transcript}'")

        if not transcript:
            return JSONResponse(
                {"error": "Nao entendi. Fale mais perto do microfone."},
                status_code=400
            )

        # ── 3. Hermes — chama via CLI e obtém resposta ────────────────────────
        reply_text = await ask_hermes(transcript)

        # ── 4. Edge TTS — converte resposta em MP3 ────────────────────────────
        mp3_filename = f"hermes_{session_id}.mp3"
        mp3_path = AUDIO_DIR / mp3_filename

        communicate = edge_tts.Communicate(reply_text, TTS_VOICE)
        await communicate.save(str(mp3_path))

        print(f"[Bridge] MP3 gerado: {mp3_path.stat().st_size:,} bytes")

        # ── 5. Expressão dos olhos + resposta para o ESP32 ────────────────────
        expression = detect_emotion(reply_text)
        audio_url  = f"http://{VPS_PUBLIC_IP}:{PORT}/audio/{mp3_filename}"

        return JSONResponse({
            "audio_url":  audio_url,
            "expression": expression,
            "text":       reply_text,
            "transcript": transcript,
            "session_id": session_id
        })

    except Exception as e:
        print(f"[Bridge] ERRO inesperado: {e}")
        return JSONResponse({"error": str(e)}, status_code=500)


@app.get("/audio/{filename}")
async def serve_audio(filename: str):
    """Serve o MP3 gerado para o ESP32 fazer streaming."""
    mp3_path = AUDIO_DIR / filename
    if not mp3_path.exists():
        return JSONResponse({"error": "Arquivo nao encontrado."}, status_code=404)
    return FileResponse(str(mp3_path), media_type="audio/mpeg")


@app.post("/test")
async def test_text(request: Request):
    """
    Teste direto via texto — sem precisar do ESP32.
    Útil para validar se o Hermes está respondendo antes de ligar o hardware.

    Exemplo:
      curl -X POST http://localhost:8080/test \\
           -H "Content-Type: application/json" \\
           -d '{"message": "Ola Hermes!"}'
    """
    body        = await request.json()
    message     = body.get("message", "Ola Hermes, voce esta conectado ao olho animatronico?")
    session_id  = f"test_{str(uuid.uuid4())[:6]}"

    # Chama o Hermes
    reply_text = await ask_hermes(message)

    # Gera TTS
    mp3_filename = f"hermes_{session_id}.mp3"
    mp3_path     = AUDIO_DIR / mp3_filename
    communicate  = edge_tts.Communicate(reply_text, TTS_VOICE)
    await communicate.save(str(mp3_path))

    audio_url = f"http://{VPS_PUBLIC_IP}:{PORT}/audio/{mp3_filename}"

    return JSONResponse({
        "message_sent": message,
        "hermes_reply": reply_text,
        "expression":   detect_emotion(reply_text),
        "audio_url":    audio_url,
        "tip":          f"Abra {audio_url} no navegador para ouvir a voz do Hermes!"
    })


# ==============================================================================
# MAIN
# ==============================================================================
if __name__ == "__main__":
    import uvicorn
    print("=" * 58)
    print("  ESP32-Hermes Bridge v2.0")
    print("=" * 58)
    print(f"  Porta:    {PORT}")
    print(f"  Hermes:   {HERMES_CMD}")
    print(f"  Flags:    {' '.join(HERMES_FLAGS)}")
    print(f"  Whisper:  {WHISPER_MODEL}")
    print(f"  TTS:      {TTS_VOICE}")
    print(f"  VPS IP:   {VPS_PUBLIC_IP}")
    print("=" * 58)
    print(f"  Health:   http://0.0.0.0:{PORT}/health")
    print(f"  Teste:    POST http://0.0.0.0:{PORT}/test")
    print("=" * 58)
    uvicorn.run(app, host="0.0.0.0", port=PORT, log_level="info")

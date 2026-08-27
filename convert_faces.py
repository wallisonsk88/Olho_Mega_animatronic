"""
Converte as 6 imagens do avatar (240x240 JPG) para arrays C++ RGB565
para uso no display ST7789 via TFT_eSPI no ESP32-S3.

Uso: python convert_faces.py
Dependência: pip install Pillow
"""

from PIL import Image
import os

FACES_DIR = r"m:\Projeto Robotico\Animatronic Olho\data\faces"
OUTPUT_H  = r"m:\Projeto Robotico\Animatronic Olho\src\face_sprites.h"

EXPRESSIONS = [
    ("face_neutral",   "FACE_NEUTRAL"),
    ("face_listening", "FACE_LISTENING"),
    ("face_thinking",  "FACE_THINKING"),
    ("face_happy",     "FACE_HAPPY"),
    ("face_surprised", "FACE_SURPRISED"),
    ("face_talking",   "FACE_TALKING"),
]

TARGET_W, TARGET_H = 240, 240

def rgb_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def image_to_rgb565_array(path):
    img = Image.open(path).convert("RGB")
    img = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)
    pixels = []
    for y in range(TARGET_H):
        for x in range(TARGET_W):
            r, g, b = img.getpixel((x, y))
            pixels.append(rgb_to_rgb565(r, g, b))
    return pixels

def write_header(expressions_data):
    lines = []
    lines.append("// ============================================================")
    lines.append("// face_sprites.h — Sprites RGB565 do Avatar Mobine")
    lines.append("// Gerado automaticamente por convert_faces.py")
    lines.append("// ============================================================")
    lines.append("")
    lines.append("#pragma once")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append(f"#define FACE_WIDTH  {TARGET_W}")
    lines.append(f"#define FACE_HEIGHT {TARGET_H}")
    lines.append(f"#define FACE_PIXELS ({TARGET_W} * {TARGET_H})")
    lines.append("")
    for name, const, pixels in expressions_data:
        lines.append(f"// {name}.jpg")
        lines.append(f"const uint16_t {const}[FACE_PIXELS] PROGMEM = {{")
        chunks = [pixels[i:i+12] for i in range(0, len(pixels), 12)]
        for chunk in chunks:
            lines.append("  " + ", ".join(f"0x{v:04X}" for v in chunk) + ",")
        lines.append("};")
        lines.append("")
    lines.append("enum FaceState {")
    for i, (_, const, _) in enumerate(expressions_data):
        lines.append(f"  STATE_{const} = {i},")
    lines.append("  FACE_STATE_COUNT")
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t* const FACE_SPRITES[] = {")
    for _, const, _ in expressions_data:
        lines.append(f"  {const},")
    lines.append("};")
    return "\n".join(lines)

def main():
    print("=== Conversor de Sprites RGB565 ===")
    expressions_data = []
    missing = []
    for filename, const in EXPRESSIONS:
        path = os.path.join(FACES_DIR, filename + ".jpg")
        if not os.path.exists(path):
            print(f"  [FALTA] {filename}.jpg")
            missing.append(filename)
            continue
        print(f"  Convertendo {filename}.jpg ...", end=" ", flush=True)
        try:
            pixels = image_to_rgb565_array(path)
            expressions_data.append((filename, const, pixels))
            print(f"OK ({len(pixels)*2/1024:.1f} KB)")
        except Exception as e:
            print(f"ERRO: {e}")
            missing.append(filename)
    if missing:
        print(f"\n[AVISO] Faltam: {', '.join(missing)}")
    if not expressions_data:
        print("Nenhuma imagem encontrada. Abortando.")
        return
    print(f"\nGerando face_sprites.h ...", end=" ", flush=True)
    header = write_header(expressions_data)
    with open(OUTPUT_H, "w", encoding="utf-8") as f:
        f.write(header)
    print(f"OK ({os.path.getsize(OUTPUT_H)/1024:.0f} KB)")
    print(f"\nPronto! {len(expressions_data)} sprites convertidos.")
    total_kb = sum(len(p) for _,_,p in expressions_data)*2/1024
    print(f"Espaco em flash: ~{total_kb:.0f} KB")

if __name__ == "__main__":
    main()

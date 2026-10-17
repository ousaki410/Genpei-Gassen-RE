#!/usr/bin/env python3
"""
KOEI Palette Extractor for 《源平合战》 (Genpei Kassen)
Decodes Mainpal.pld and Losepal.pld (4-bit per channel RGB, 4 banks x 16 colors)
Generates JSON palette definitions and palette preview PNGs.
"""

import os
import json
from PIL import Image, ImageDraw

def decode_pld(pld_path: str):
    with open(pld_path, 'rb') as f:
        data = f.read()

    if len(data) != 192:
        print(f"[!] Warning: {pld_path} size is {len(data)}, expected 192")

    banks = []
    # 4 banks of 16 colors = 64 color triplets
    for b in range(4):
        bank_colors = []
        for c in range(16):
            idx = (b * 16 + c) * 3
            # KOEI PC-98 PLD channel order: data[idx]=Blue, data[idx+1]=Red, data[idx+2]=Green
            b_raw, r_raw, g_raw = data[idx], data[idx + 1], data[idx + 2]
            r = (r_raw << 4) | r_raw
            g = (g_raw << 4) | g_raw
            b_val = (b_raw << 4) | b_raw
            bank_colors.append({
                "index": c,
                "raw": [r_raw, g_raw, b_raw],
                "rgb": [r, g, b_val],
                "hex": f"#{r:02x}{g:02x}{b_val:02x}"
            })
        banks.append(bank_colors)
    return banks

def render_palette_swatch(banks, out_png_path: str):
    swatch_w, swatch_h = 32, 32
    img = Image.new("RGB", (16 * swatch_w, 4 * swatch_h), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    for b, bank in enumerate(banks):
        for c, col in enumerate(bank):
            x0 = c * swatch_w
            y0 = b * swatch_h
            x1 = x0 + swatch_w
            y1 = y0 + swatch_h
            draw.rectangle([x0, y0, x1, y1], fill=tuple(col["rgb"]))

    img.save(out_png_path)

def extract_palettes(game_dir: str, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    results = {}

    for name in ["Mainpal.pld", "Losepal.pld"]:
        pld_path = os.path.join(game_dir, name)
        if not os.path.exists(pld_path):
            continue
        banks = decode_pld(pld_path)
        base_name = os.path.splitext(name)[0]
        results[base_name] = banks

        png_path = os.path.join(out_dir, f"{base_name}.png")
        render_palette_swatch(banks, png_path)
        print(f"[+] Exported palette swatch: {png_path}")

    json_path = os.path.join(out_dir, "palettes.json")
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=2)
    print(f"[+] Exported palette JSON: {json_path}")

if __name__ == "__main__":
    extract_palettes("games/yphz", "extracted_assets/palettes")

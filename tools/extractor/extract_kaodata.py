#!/usr/bin/env python3
"""
KOEI Kaodata Graphics Extractor for 《源平合战》 (Genpei Kassen, 1994)
Disassembled directly from Main.exe routine 0x728-0x966:

Features:
- Decodes all 90 historical officers (offsets from table at Main.exe:0x4d3bb)
- Emulates 12-bit nibble RLE/LZ sliding-window decompressor (routine 0x728)
- Decompresses Block A (Normal Portrait, 128x160)
- Decompresses Block B (1-bit Alpha Clipping Mask, 128x160)
- Applies verified PC-98 GBR palette channel order from Mainpal.pld
- Exports indexed PNGs, transparent RGBA PNGs, and a 10x9 unified sprite sheet
"""

import os
import json
import struct
from PIL import Image

def load_palette(pld_path: str):
    with open(pld_path, "rb") as f:
        data = f.read()[:48]  # Bank 0 (16 colors x 3 bytes)

    pal_rgb = []
    for c in range(16):
        b_raw = data[c * 3]
        r_raw = data[c * 3 + 1]
        g_raw = data[c * 3 + 2]
        r = (r_raw << 4) | r_raw
        g = (g_raw << 4) | g_raw
        b = (b_raw << 4) | b_raw
        pal_rgb.append((r, g, b))
    return pal_rgb

def decompress_kao_block(data: bytes, offset: int):
    """
    Decompresses a single 128x160 3-bitplane block using x86 routine 0x728.
    Returns: (width, height, dx, units, bytes_consumed)
    """
    w = int.from_bytes(data[offset : offset + 2], "little")
    h = int.from_bytes(data[offset + 2 : offset + 4], "little")
    dx = (w + 3) // 4  # 32 units per line
    units = [[0, 0, 0] for _ in range(h * dx)]
    si = offset + 4

    for y in range(h):
        line_start = y * dx
        rem = dx
        cur_x = 0
        while rem > 0:
            ctrl = data[si]
            si += 1

            if (ctrl & 0x80) == 0:
                # Type A (0x8bb): RLE constant 3-nibble value
                b2 = data[si]
                si += 1
                count = ((ctrl >> 4) & 7) + 1
                n0 = b2 & 0x0F
                n1 = (b2 >> 4) & 0x0F
                n2 = ctrl & 0x0F
                for i in range(count):
                    units[line_start + cur_x + i] = [n0, n1, n2]
                cur_x += count
                rem -= count
            elif (ctrl & 0x40) == 0:
                # Type B (0x77e): Copy from horizontal offset
                count = (ctrl & 0x0F) + 1
                step = ((ctrl >> 4) & 3) + 1
                for i in range(count):
                    src_u = line_start + cur_x + i - step
                    units[line_start + cur_x + i] = list(units[src_u])
                cur_x += count
                rem -= count
            else:
                # Type C (0x83c): Copy from vertical scanline offset
                step = ((ctrl >> 4) & 3) + 1
                count = (ctrl & 0x0F) + 1
                for i in range(count):
                    src_u = (y - step) * dx + cur_x + i
                    units[line_start + cur_x + i] = list(units[src_u])
                cur_x += count
                rem -= count

    return w, h, dx, units, si

def units_to_pixels(units, w, h, dx):
    """Converts 3-bitplane units to 8-color pixel buffer."""
    pixels = bytearray()
    for y in range(h):
        for x_u in range(dx):
            u = units[y * dx + x_u]
            p0, p1, p2 = u[0], u[1], u[2]
            for bit_idx in range(4):
                shift = 3 - bit_idx
                b0 = (p0 >> shift) & 1
                b1 = (p1 >> shift) & 1
                b2 = (p2 >> shift) & 1
                c = (b2 << 2) | (b1 << 1) | b0
                pixels.append(c)
    return pixels

def extract_all_officers(game_dir: str, out_dir: str):
    main_exe_path = os.path.join(game_dir, "Main.exe")
    kaodata_path = os.path.join(game_dir, "Kaodata.gp")
    mainpal_path = os.path.join(game_dir, "Mainpal.pld")

    with open(main_exe_path, "rb") as f:
        exe = f.read()

    with open(kaodata_path, "rb") as f:
        kaodata = f.read()

    pal_rgb = load_palette(mainpal_path)
    flat_pal = []
    for rgb in pal_rgb:
        flat_pal.extend(rgb)
    flat_pal += [0] * (768 - len(flat_pal))

    # Read 90 officer offsets from Main.exe:0x4d3bb
    offsets = [0]
    ptr = 0x4D3BB
    while len(offsets) < 90:
        val = struct.unpack_from("<I", exe, ptr)[0]
        offsets.append(val)
        ptr += 4

    os.makedirs(out_dir, exist_ok=True)
    officers_dir = os.path.join(out_dir, "kaodata")
    os.makedirs(officers_dir, exist_ok=True)

    manifest = []
    sheet_cols = 10
    sheet_rows = 9
    card_w, card_h = 128, 160
    sheet_img = Image.new("RGBA", (sheet_cols * card_w, sheet_rows * card_h), (0, 0, 0, 0))

    print(f"[*] Extracting all 90 officers from {kaodata_path}...")
    for idx in range(90):
        start_off = offsets[idx]
        w_a, h_a, dx_a, u_a, end_a = decompress_kao_block(kaodata, start_off)
        w_b, h_b, dx_b, u_b, end_b = decompress_kao_block(kaodata, end_a)

        pix_a = units_to_pixels(u_a, w_a, h_a, dx_a)
        pix_b = units_to_pixels(u_b, w_b, h_b, dx_b)

        # 1. Indexed Opaque Image
        img_opaque = Image.frombytes("P", (w_a, h_a), bytes(pix_a))
        img_opaque.putpalette(flat_pal)
        opaque_path = os.path.join(officers_dir, f"kao_{idx:02d}.png")
        img_opaque.save(opaque_path)

        # 2. RGBA Transparent Image (Block B mask: 7 is transparent, 0 is opaque)
        rgba = bytearray()
        for pa, pb in zip(pix_a, pix_b):
            r, g, b = pal_rgb[pa]
            a = 0 if pb == 7 else 255
            rgba.extend([r, g, b, a])

        img_tr = Image.frombytes("RGBA", (w_a, h_a), bytes(rgba))
        tr_path = os.path.join(officers_dir, f"kao_{idx:02d}_tr.png")
        img_tr.save(tr_path)

        # Paste onto sprite sheet
        grid_x = (idx % sheet_cols) * card_w
        grid_y = (idx // sheet_cols) * card_h
        sheet_img.paste(img_tr, (grid_x, grid_y))

        manifest.append({
            "index": idx,
            "offset": start_off,
            "block_a_bytes": end_a - start_off,
            "block_b_bytes": end_b - end_a,
            "total_bytes": end_b - start_off,
            "width": w_a,
            "height": h_a,
            "opaque_file": f"kaodata/kao_{idx:02d}.png",
            "transparent_file": f"kaodata/kao_{idx:02d}_tr.png",
        })

    # Save sprite sheets
    sheet_path = os.path.join(out_dir, "kaodata_sheet.png")
    sheet_img.save(sheet_path)

    # 2x nearest-neighbor scaled version
    sheet_2x = sheet_img.resize((sheet_cols * card_w * 2, sheet_rows * card_h * 2), Image.NEAREST)
    sheet_2x_path = os.path.join(out_dir, "kaodata_sheet_2x.png")
    sheet_2x.save(sheet_2x_path)

    manifest_path = os.path.join(out_dir, "kaodata_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print(f"[+] Successfully extracted 90 officers!")
    print(f"[+] Sprite sheet: {sheet_path}")
    print(f"[+] 2x Sprite sheet: {sheet_2x_path}")
    print(f"[+] Manifest: {manifest_path}")

if __name__ == "__main__":
    chmod_x = os.chmod("tools/extractor/extract_kaodata.py", 0o755)
    extract_all_officers("games/yphz", "extracted_assets/images")

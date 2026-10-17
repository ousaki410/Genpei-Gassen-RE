#!/usr/bin/env python3
"""
KOEI NPK016 / NUP4 Graphics Decoder & Extractor for 《源平合战》 (Genpei Kassen, 1994)
Disassembled and reverse-engineered directly from Open.exe / Grpdrv.exe:
"nup4 nibble mamory unpack routine by T.Tabata (1991), T.Yazaki (1992), T.Fujinuma (1993)"

Features:
- Full LSB-first bitstream sliding-window ring buffer decompression (1600 bytes = 5 scanlines)
- Interleaved planar nibble transformation (0x258c packing emulation)
- PC-98 16-color palette decoding (0x0GRB 12-bit RGB)
- Multi-image container parsing (Logo.gp, Mainitem.gp, Hikback.gp, etc.)
- Automated sprite sheet & composite screen generator
"""

import os
import glob
import json
import struct
from PIL import Image

def decompress_nup4_block(data: bytes, offset: int = 0):
    """
    Decompresses a single NPK016 / NUP4 image block at the given byte offset.
    Returns: (PIL.Image, bytes_consumed, metadata_dict)
    """
    if offset + 48 > len(data):
        raise ValueError(f"Offset {offset} out of bounds for header (len={len(data)})")

    magic = data[offset : offset + 6]
    if magic != b"NPK016":
        raise ValueError(f"Invalid magic at {hex(offset)}: {magic}")

    planes = int.from_bytes(data[offset + 6 : offset + 8], "little")
    screen_w = int.from_bytes(data[offset + 8 : offset + 10], "little")
    screen_h = int.from_bytes(data[offset + 10 : offset + 12], "little")
    img_w = int.from_bytes(data[offset + 12 : offset + 14], "little")
    img_h = int.from_bytes(data[offset + 14 : offset + 16], "little")

    # Read 16-color PC-98 palette (0x0GRB)
    palette_raw = []
    palette_rgb = []
    for i in range(16):
        w = int.from_bytes(data[offset + 16 + i * 2 : offset + 18 + i * 2], "little")
        palette_raw.append(w)
        g_val = (w >> 8) & 0x0F
        r_val = (w >> 4) & 0x0F
        b_val = w & 0x0F
        palette_rgb.extend([(r_val << 4) | r_val, (g_val << 4) | g_val, (b_val << 4) | b_val])

    # Decompression parameters
    words_per_line = (img_w + 3) // 4
    RING_SIZE = 1600  # 5 scanlines * 320 bytes (640-pixel PC-98 row width)
    ring = bytearray(RING_SIZE)
    ring_di = 0

    payload = data[offset + 48 :]
    si = 0
    bit_buf = 0
    bits_left = 0
    decompressed_bytes = bytearray()

    def transform_4bytes(w1: int, w2: int) -> list:
        """Emulates x86 routine 0x25a8-0x25e6 (4-byte planar rotation packing)."""
        bx = w1
        ax = w2
        bh = (bx >> 8) & 0xFF
        bl = bx & 0xFF
        ah = (ax >> 8) & 0xFF
        al = ax & 0xFF
        bh, al = al, bh
        bx = (bh << 8) | bl
        ax = (ah << 8) | al

        bx = ((bx << 4) | (bx >> 12)) & 0xFFFF
        bl = ((bx & 0xFF) << 4 | (bx & 0xFF) >> 4) & 0xFF
        bx = (bx & 0xFF00) | bl
        bx = ((bx << 4) | (bx >> 12)) & 0xFFFF

        ax = ((ax << 4) | (ax >> 12)) & 0xFFFF
        al = ((ax & 0xFF) << 4 | (ax & 0xFF) >> 4) & 0xFF
        ax = (ax & 0xFF00) | al
        ax = ((ax << 4) | (ax >> 12)) & 0xFFFF

        return [ax & 0xFF, (ax >> 8) & 0xFF, bx & 0xFF, (bx >> 8) & 0xFF]

    def transform_odd_word(w: int) -> list:
        """Emulates 0x25ec-0x2626 (odd trailing word unpacking)."""
        out = []
        ax = w
        for _ in range(4):
            ax = ((ax >> 4) | (ax << 12)) & 0xFFFF
            out.append(ax & 0xF0)
        return out

    for line in range(img_h):
        line_start = ring_di
        words_needed = words_per_line
        while words_needed > 0:
            if bits_left == 0:
                bit_buf = payload[si]
                si += 1
                bits_left = 8
            bit = bit_buf & 1
            bit_buf >>= 1
            bits_left -= 1

            if bit == 0:
                # Literal 16-bit word
                ring[ring_di : ring_di + 2] = payload[si : si + 2]
                si += 2
                ring_di = (ring_di + 2) % RING_SIZE
                words_needed -= 1
            else:
                # LZ match copy
                ctrl = payload[si]
                si += 1
                count = (ctrl & 0x1F) + 1
                offset_idx = ((ctrl >> 5) & 3) + 1
                offset_val = (offset_idx * 320) if (ctrl & 0x80) else (offset_idx * 2)
                src_pos = (ring_di - offset_val) % RING_SIZE
                for _ in range(count):
                    ring[ring_di] = ring[src_pos]
                    ring[ring_di + 1] = ring[(src_pos + 1) % RING_SIZE]
                    ring_di = (ring_di + 2) % RING_SIZE
                    src_pos = (src_pos + 2) % RING_SIZE
                words_needed -= count

        # Apply planar packing
        p = line_start
        pairs = words_per_line // 2
        for _ in range(pairs):
            w1 = ring[p] | (ring[(p + 1) % RING_SIZE] << 8)
            p = (p + 2) % RING_SIZE
            w2 = ring[p] | (ring[(p + 1) % RING_SIZE] << 8)
            p = (p + 2) % RING_SIZE
            decompressed_bytes.extend(transform_4bytes(w1, w2))
        if words_per_line % 2 != 0:
            w = ring[p] | (ring[(p + 1) % RING_SIZE] << 8)
            p = (p + 2) % RING_SIZE
            decompressed_bytes.extend(transform_odd_word(w))

        ring_di = (line_start + 320) % RING_SIZE

    # Convert 4-plane chunky words to standard indexed 8-bit bitmap
    pixels = bytearray()
    line_bytes = (img_w + 7) // 8 * 4
    for y in range(img_h):
        line_raw = decompressed_bytes[y * line_bytes : (y + 1) * line_bytes]
        for x_chunk in range(0, len(line_raw), 4):
            b0, b1, b2, b3 = line_raw[x_chunk : x_chunk + 4]
            for bit in range(8):
                shift = 7 - bit
                bit0 = (b0 >> shift) & 1
                bit1 = (b1 >> shift) & 1
                bit2 = (b2 >> shift) & 1
                bit3 = (b3 >> shift) & 1
                color = (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0
                pixels.append(color)

    # Crop to exact dimensions
    actual_pixels = bytearray()
    pitch = (img_w + 7) // 8 * 8
    for y in range(img_h):
        actual_pixels.extend(pixels[y * pitch : y * pitch + img_w])

    img = Image.frombytes("P", (img_w, img_h), bytes(actual_pixels))
    img.putpalette(palette_rgb + [0] * (768 - len(palette_rgb)))

    total_block_bytes = 48 + si
    meta = {
        "offset": offset,
        "planes": planes,
        "screen_dims": [screen_w, screen_h],
        "image_dims": [img_w, img_h],
        "consumed_bytes": total_block_bytes,
        "palette_raw": palette_raw,
    }
    return img, total_block_bytes, meta

def extract_npk_file(fpath: str, out_dir: str):
    """Extracts all NPK016 sub-images inside a container file."""
    with open(fpath, "rb") as f:
        data = f.read()

    base_name = os.path.splitext(os.path.basename(fpath))[0].lower()
    file_out_dir = os.path.join(out_dir, base_name)
    os.makedirs(file_out_dir, exist_ok=True)

    import re
    offsets = [m.start() for m in re.finditer(b"NPK016", data)]
    if not offsets:
        return []

    results = []
    for idx, off in enumerate(offsets):
        try:
            img, consumed, meta = decompress_nup4_block(data, off)
            out_name = f"{base_name}_{idx:02d}.png" if len(offsets) > 1 else f"{base_name}.png"
            out_path = os.path.join(file_out_dir, out_name)
            img.save(out_path)
            results.append({
                "index": idx,
                "file": out_path,
                "dims": [img.size[0], img.size[1]],
                "offset": off,
                "bytes": consumed,
            })
            print(f"[+] {base_name}: sub-image {idx:2d} -> {out_name} ({img.size[0]}x{img.size[1]})")
        except Exception as e:
            print(f"[-] {base_name}: sub-image {idx} failed at offset {hex(off)}: {e}")

    return results

def build_item_sheet(items_dir: str, out_path: str):
    """Builds a unified 5x4 sprite sheet for all 19 items."""
    cols = 5
    rows = 4
    cell_w = 64
    cell_h = 64
    pad = 4

    sheet_w = cols * (cell_w + pad) + pad
    sheet_h = rows * (cell_h + pad) + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), (24, 28, 36))

    for idx in range(19):
        fpath = os.path.join(items_dir, f"mainitem_{idx:02d}.png")
        if not os.path.exists(fpath):
            continue
        r = idx // cols
        c = idx % cols
        x = pad + c * (cell_w + pad)
        y = pad + r * (cell_h + pad)
        item_img = Image.open(fpath).convert("RGB")
        sheet.paste(item_img, (x, y))

    sheet.save(out_path)
    sheet_2x = sheet.resize((sheet_w * 2, sheet_h * 2), Image.NEAREST)
    sheet2x_path = out_path.replace(".png", "_2x.png")
    sheet_2x.save(sheet2x_path)
    print(f"[+] Generated item sheets: {out_path} & {sheet2x_path}")

def build_intro_screens(logo_dir: str, out_dir: str):
    """Reconstructs the original PC-98 640x400 intro screens from Logo sub-images."""
    # Screen 1: KOEI Logo Screen
    screen1 = Image.new("RGB", (640, 400), (0, 0, 0))
    p0 = os.path.join(logo_dir, "logo_00.png")
    p1 = os.path.join(logo_dir, "logo_01.png")
    if os.path.exists(p0) and os.path.exists(p1):
        logo0 = Image.open(p0).convert("RGB")
        logo1 = Image.open(p1).convert("RGB")
        screen1.paste(logo0, (216, 96))
        screen1.paste(logo1, (216, 224))
        s1_path = os.path.join(out_dir, "screen_koei_logo_640.png")
        screen1.save(s1_path)
        print(f"[+] Reconstructed Koei Logo Screen: {s1_path}")

    # Screen 2: Title / Copyright Screen
    screen2 = Image.new("RGB", (640, 400), (0, 0, 0))
    p2 = os.path.join(logo_dir, "logo_02.png")
    p3 = os.path.join(logo_dir, "logo_03.png")
    if os.path.exists(p2) and os.path.exists(p3):
        logo2 = Image.open(p2).convert("RGB")
        logo3 = Image.open(p3).convert("RGB")
        screen2.paste(logo2, (136, 144))
        screen2.paste(logo3, (120, 224))
        s2_path = os.path.join(out_dir, "screen_title_copyright_640.png")
        screen2.save(s2_path)
        print(f"[+] Reconstructed Copyright Screen: {s2_path}")

def batch_extract_all(game_dir: str, out_dir: str):
    """Batch unpacks all NPK016 files in game directory."""
    os.makedirs(out_dir, exist_ok=True)
    all_targets = [
        "Logo.gp",
        "Mainitem.gp",
        "Hikback.gp",
        "Mainstl.gp",
        "Maincmd2.gp",
        "Mainevt.gp",
        "Hkumi.gp",
        "Hkeshiki.gp",
    ]

    summary = {}
    for target in all_targets:
        fpath = os.path.join(game_dir, target)
        if os.path.exists(fpath):
            print(f"\n--- Extracting {target} ---")
            res = extract_npk_file(fpath, out_dir)
            summary[target] = res

    # Generate composite assets
    items_dir = os.path.join(out_dir, "mainitem")
    if os.path.exists(items_dir):
        build_item_sheet(items_dir, os.path.join(out_dir, "items_sheet.png"))

    logo_dir = os.path.join(out_dir, "logo")
    if os.path.exists(logo_dir):
        build_intro_screens(logo_dir, out_dir)

    summary_file = os.path.join(out_dir, "extracted_manifest.json")
    with open(summary_file, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    print(f"\n[+] Extraction manifest saved to {summary_file}")

if __name__ == "__main__":
    batch_extract_all("games/yphz", "extracted_assets/images")


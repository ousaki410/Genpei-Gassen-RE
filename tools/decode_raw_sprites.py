#!/usr/bin/env python3
"""
Decode raw DOS Genpei Kassen 4‑plane sprite sheets for subtask 10 part 2.

主要樣式（根據原生算術驗證）：
  - Hunitpat.gp: 151,936 B = 1,187 × 128 B × 16×16 單元 = 1,187 × 16×16 像素
  - Hchikei.gp:  10,060 B = 78 × 128 B + 60 B 表頭（或偏移 0：78 × 128 B + 0？每槽先檢測）
  - Mainobj.gp:  24,544 B = 208 × 236 / 2 = 24,544 B（4‑plane，每行 4 × (208/8) = 104 B）

用法：python3 tools/decode_raw_sprites.py <名> [輸出路徑] [--single]
"""

import os
import sys
from PIL import Image
import argparse

SND_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "games", "yphz"))

def decode_4plane(data, w, name):
    """將 4‑plane 原始數據轉為 1b‑pp 索引像素陣列（0 = 透明）。
    支援 Jae 主式：4 個 plane 相鄰，每 4 字節代表 8 像素。
    輸出：NumPy 陣列
    """
    import numpy as np

    row_bytes = 4 * ((w + 7) // 8)  # 包含 4 個 plane
    if len(data) < row_bytes * ((w + 7) // 8):  # 假設高為 w（方形）
        return None
    # 實際應根據文件算出 h。對 Mainobj 用 w=208，每行 104 B，共 236 行
    h = len(data) // row_bytes
    if w * h != len(data):
        h = ((len(data) + row_bytes - 1) // row_bytes) * 1

    img = np.zeros((h, w), dtype=np.uint8)
    src = data
    for y in range(h):
        for row_start in range((w + 7) // 8):
            for chunk in range(4):
                b0 = src[0]; b1 = src[1]; b2 = src[2]; b3 = src[3]
                src += 4
                for bit in range(8):
                    x = row_start * 8 + chunk * 8 + (7 - bit)
                    if x >= w: continue
                    c = ((b3 >> bit) & 1) | (((b2 >> bit) & 1) << 1) | (((b1 >> bit) & 1) << 2) | ((b0 >> bit) & 1)
                    if c:
                        img[y, x] = c
    return img

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("name", help="GP 檔案名稱（不含 .gp）")
    parser.add_argument("name", help="GP 檔案名稱（不含 .gp）")
    parser.add_argument("out", nargs="?", help="輸出 PNG 路徑（可為目录）")
    parser.add_argument("--single", action="store_true", help="僅輸出一張預覽圖")
    args = parser.parse_args()

    path = os.path.join(SND_ROOT, args.name + ".gp")
    if not os.path.exists(path):
        print(f"[ERROR] not found: {path}", file=sys.stderr)
        return 1

    raw = open(path, "rb").read()
    print(f"[INFO] {args.name}.gp: {len(raw)} B")

    if args.name in ("Mainobj", "Hunitpat", "Hchikei"):
        if args.name == "Mainobj":
            w, h = 208, 236
            row_bytes = 4 * ((w + 7) // 8)
            if len(raw) % row_bytes != 0:
                print(f"[WARN] Mainobj.length={len(raw)} % {row_bytes} != 0, skip")
                return 1
            img = decode_4plane(raw, w, args.name)
            img_pil = Image.fromarray(img * 17).resize((w * 2, h * 2), Image.NEAREST)
            out_path = os.path.join(args.out, "mainobj-preview.png") if args.out else "/tmp/mainobj-gp.png"
            img_pil.save(out_path)
            print(f"[INFO] decoded 208×236, wrote {out_path}")
        elif args.name in ("Hunitpat", "Hchikei"):
            # 每個 tile 128 B，16×16
            tile_sz = 128
            cnt = len(raw) // tile_sz
            # Hchikei 後面 60 B 為布陣表，只取前面的 tile
            cnt = cnt - (60 if args.name == "Hchikei" and len(raw) > 10000 else 0)
            print(f"[INFO] {args.name}.gp: {cnt} tiles × 16×16")
            # 每個 tile 寬高為 16
            out_dir = args.out or "/tmp"
            for i in range(min(cnt, 24)):
                tile = raw[i * tile_sz : (i + 1) * tile_sz]
                img = decode_4plane(tile, 16, args.name)
                if img is not None:
                    img_pil = Image.fromarray(img * 17)
                    tile_out = os.path.join(out_dir, f"{args.name}_{i:03d}.png")
                    img_pil.save(tile_out)
    else:
        print(f"[WARN] unknown format {args.name}, decode only for Mainobj/Hunitpat/Hchikei")
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
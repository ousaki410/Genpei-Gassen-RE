#!/usr/bin/env python3
"""Decode the raw PC-98 4-plane sprite banks of Genpei Kassen.

子任务 10 part 2.  The battle assets that are *not* wrapped in NPK016
containers store their pixels as raw 4-plane bitmaps:

  * a scanline is a sequence of 8-pixel groups;
  * each group is 4 bytes, one byte per colour plane (plane 0 = LSB);
  * inside a byte, bit 7 is the leftmost pixel;
  * a pixel's colour index is  b0 | b1<<1 | b2<<2 | b3<<3.

Sizes that follow from that layout:

  Hunitpat.gp  151936 B = 1187 x 128 B -> 1187 tiles of 16x16
  Hchikei.gp    10060 B = 76 + 78 x 128 B (or 100x100 tile ids + 60 B)
  Mainobj.gp    24544 B = 104 x 236 B   -> 208x236 sheet

Usage:
    python3 tools/decode_raw_sprites.py                 # report + previews
    python3 tools/decode_raw_sprites.py --out /tmp/x    # preview directory
    python3 tools/decode_raw_sprites.py --tile 3        # dump one tile
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("[ERROR] Pillow is required (pip install Pillow)", file=sys.stderr)
    raise

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "games", "yphz")

# PC-98 16 colour fallback palette (the engine loads Mainpal.pld at runtime).
PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0x5A), (0x00, 0x5A, 0x00), (0x00, 0x5A, 0x5A),
    (0x5A, 0x00, 0x00), (0x5A, 0x00, 0x5A), (0x5A, 0x5A, 0x00), (0x5A, 0x5A, 0x5A),
    (0x80, 0x80, 0x80), (0x00, 0x00, 0xFF), (0x00, 0xFF, 0x00), (0x00, 0xFF, 0xFF),
    (0xFF, 0x00, 0x00), (0xFF, 0x00, 0xFF), (0xFF, 0xFF, 0x00), (0xFF, 0xFF, 0xFF),
]


def decode_4plane(data, width, height, offset=0):
    """Decode a raw 4-plane bitmap into a list of rows of colour indices.

    Layout (per row): [ plane 0 ][ plane 1 ][ plane 2 ][ plane 3 ], each
    segment being ceil(width/8) bytes, bit 7 = leftmost pixel.
    """
    plane_bytes = (width + 7) // 8
    row_bytes = plane_bytes * 4
    rows = []
    pos = offset
    for _ in range(height):
        row = [0] * width
        for plane in range(4):
            bits = data[pos:pos + plane_bytes]
            pos += plane_bytes
            for g, byte in enumerate(bits):
                if byte == 0:
                    continue
                for bit in range(8):
                    x = g * 8 + bit
                    if x >= width:
                        break
                    if (byte >> (7 - bit)) & 1:
                        row[x] |= 1 << plane
        rows.append(row)
    return rows, pos


def to_image(rows, scale=1):
    h = len(rows)
    w = len(rows[0]) if h else 0
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = PALETTE[rows[y][x] & 0x0F]
    if scale > 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    return img


def report(name, size, out_dir):
    path = os.path.join(DATA, name + ".gp")
    if not os.path.exists(path):
        print("[ERROR] missing %s" % path, file=sys.stderr)
        return False
    data = open(path, "rb").read()
    print("[INFO] %s.gp: %d bytes" % (name, len(data)))

    if name == "Hunitpat":
        tw, th = 16, 16
        tile_bytes = tw * th // 2
        count = len(data) // tile_bytes
        print("       -> %d tiles of %dx%d (%d B each), %d B left over"
              % (count, tw, th, tile_bytes, len(data) % tile_bytes))
        # contact sheet of the first tiles
        cols, rows_n, scale = 16, 4, 3
        sheet = Image.new("RGB", (cols * tw * scale, rows_n * th * scale))
        for i in range(min(count, cols * rows_n)):
            rows, _ = decode_4plane(data, tw, th, i * tile_bytes)
            tile = to_image(rows, scale)
            sheet.paste(tile, ((i % cols) * tw * scale, (i // cols) * th * scale))
        out = os.path.join(out_dir, "Hunitpat_sheet.png")
        sheet.save(out)
        print("       wrote %s" % out)
    elif name == "Hchikei":
        # Two readings fit the size: a 76 B header + 78 tiles of 128 B, and a
        # 100x100 grid of one byte tile ids + a 60 B formation table.
        print("       -> 100x100 tile-id grid + 60 B trailer (13 distinct ids)")
        ids = data[:10000]
        distinct = sorted(set(ids))
        print("       tile ids: %d distinct, max %d" % (len(distinct), max(ids)))
        print("       trailer (60 B): %s" % data[10000:].hex(" "))
        grid = Image.new("RGB", (100, 100))
        px = grid.load()
        for y in range(100):
            for x in range(100):
                px[x, y] = PALETTE[ids[y * 100 + x] % 16]
        out = os.path.join(out_dir, "Hchikei_grid.png")
        grid.resize((400, 400), Image.NEAREST).save(out)
        print("       wrote %s" % out)
    elif name == "Mainobj":
        w, h = 208, 236
        row_bytes = 4 * ((w + 7) // 8)
        print("       -> %dx%d sheet, %d B/row x %d rows = %d B"
              % (w, h, row_bytes, h, row_bytes * h))
        rows, _ = decode_4plane(data, w, h)
        out = os.path.join(out_dir, "Mainobj_sheet.png")
        to_image(rows, 2).save(out)
        print("       wrote %s" % out)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/yphz_raw_sprites",
                    help="preview output directory")
    ap.add_argument("--tile", type=int, default=-1,
                    help="dump a single Hunitpat tile as PNG")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    if args.tile >= 0:
        data = open(os.path.join(DATA, "Hunitpat.gp"), "rb").read()
        rows, _ = decode_4plane(data, 16, 16, args.tile * 128)
        out = os.path.join(args.out, "Hunitpat_tile%03d.png" % args.tile)
        to_image(rows, 8).save(out)
        print("[INFO] wrote %s" % out)
        return 0

    ok = True
    for name in ("Hunitpat", "Hchikei", "Mainobj"):
        if not report(name, os.path.getsize(os.path.join(DATA, name + ".gp"))
                      if os.path.exists(os.path.join(DATA, name + ".gp")) else 0,
                      args.out):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

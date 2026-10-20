#!/usr/bin/env python3
"""Decode the raw PC-98 4-plane battle sheets of Genpei Kassen.

子任务 11.  The raw battle assets (Hunitpat.gp / Hchikei.gp / Mainobj.gp) are
not NPK016 containers: they store their pixels directly in the game's native
planar order, the same one the NPK/NUP4 decoder in decompress.c produces:

  * a scanline is a sequence of 8-pixel groups;
  * each group is 4 bytes, one byte per colour plane (plane 0 = LSB);
  * inside a byte, bit 7 is the leftmost pixel;
  * a pixel's colour index is  b0 | b1<<1 | b2<<2 | b3<<3.

The solved geometry (see tools/solve_raw_layout.py for the analysis):

  Hunitpat.gp  151936 B  stride 12 B -> 24 px wide, 12661 rows.
                             Unit sprites are 24x32 px on a 36-row pitch;
                             the twelve sprite types are the factions.
  Mainobj.gp    24544 B  stride 12 B -> 24 px wide, 2045 rows.
                             The same 24x32 unit sprites plus terrain art.
  Hchikei.gp    10060 B  not a bitmap: a 100x100 grid of one-byte terrain
                             tile ids (13 distinct ids) + a 60 byte table.

Usage:
    python3 tools/decode_raw_sprites.py                  # report + previews
    python3 tools/decode_raw_sprites.py --tile 3         # one unit sprite
    python3 tools/decode_raw_sprites.py --out /tmp/x     # preview directory
"""

import argparse
import os
import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:
    print("[ERROR] Pillow is required (pip install Pillow)", file=sys.stderr)
    raise

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "games", "yphz")

# --- solved geometry --------------------------------------------------------
WIDTH = 24            # pixels per scanline
ROW_BYTES = 12        # 3 groups x 4 plane bytes
UNIT_W = 24           # unit sprite width
UNIT_H = 32           # unit sprite height
UNIT_PITCH = 36       # rows between sprite origins

# PC-98 16 colour fallback palette (the engine loads Mainpal.pld at runtime).
PALETTE = [
    (0x00, 0x00, 0x00), (0x11, 0x44, 0xAA), (0xDD, 0x44, 0x00), (0xEE, 0x88, 0x55),
    (0x00, 0x99, 0x33), (0x77, 0xBB, 0xBB), (0xFF, 0xBB, 0x44), (0xFF, 0xFF, 0xDD),
    (0x33, 0x22, 0x00), (0x33, 0x44, 0x00), (0x99, 0x55, 0x33), (0xCC, 0x99, 0x44),
    (0x77, 0x88, 0x55), (0x11, 0x22, 0x44), (0xAA, 0xAA, 0xBB), (0xAA, 0xBB, 0x44),
]


def decode_4plane(data, width, height, offset=0):
    """Decode a raw 4-plane bitmap into a list of rows of colour indices."""
    groups = (width + 7) // 8
    row_bytes = groups * 4
    rows = []
    pos = offset
    for _ in range(height):
        row = [0] * width
        for g in range(groups):
            b0, b1, b2, b3 = data[pos], data[pos + 1], data[pos + 2], data[pos + 3]
            pos += 4
            for bit in range(8):
                x = g * 8 + bit
                if x >= width:
                    break
                shift = 7 - bit
                c = ((b0 >> shift) & 1) | (((b1 >> shift) & 1) << 1) \
                    | (((b2 >> shift) & 1) << 2) | (((b3 >> shift) & 1) << 3)
                if c:
                    row[x] = c
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


def byte_stride(data, max_stride=260):
    """Row stride: the lag with the smallest mean absolute byte difference."""
    best = []
    n = len(data)
    for s in range(2, min(max_stride, n)):
        diff = sum(abs(a - b) for a, b in zip(data[:-s], data[s:])) / (n - s)
        best.append((diff, s))
    best.sort()
    return best


def report(name, out_dir):
    path = os.path.join(DATA, name)
    if not os.path.exists(path):
        print("[SKIP] %s not found" % name)
        return False
    data = open(path, "rb").read()
    print("[INFO] %s: %d bytes" % (name, len(data)))
    print("       head: %s" % data[:24].hex(" "))

    strides = byte_stride(data)
    print("       byte autocorrelation: " +
          "  ".join("s=%d:%.2f" % (s, m) for m, s in strides[:4]))

    if name == "Hchikei.gp":
        ids = data[:10000]
        distinct = sorted(set(ids))
        print("       -> 100x100 tile-id grid (%d distinct ids, max %d) "
              "+ %d byte formation table" % (len(distinct), max(ids),
                                            len(data) - 10000))
        print("       trailer: %s" % data[10000:].hex(" "))
        grid = Image.new("RGB", (100, 100))
        px = grid.load()
        for y in range(100):
            for x in range(100):
                px[x, y] = PALETTE[ids[y * 100 + x] % 16]
        out = os.path.join(out_dir, "Hchikei_grid.png")
        grid.resize((400, 400), Image.NEAREST).save(out)
        print("       wrote %s" % out)
        return True

    rows = len(data) // ROW_BYTES
    print("       -> %d px wide, %d rows (stride %d B)" % (WIDTH, rows, ROW_BYTES))
    sheet, _ = decode_4plane(data, WIDTH, rows)
    types = rows // UNIT_PITCH
    print("       -> %d unit sprite slots of %dx%d px on a %d-row pitch"
          % (types, UNIT_W, UNIT_H, UNIT_PITCH))

    # Contact sheet of the unit sprites.
    cols = 8
    cell = Image.new("RGB", (cols * UNIT_W, 4 * UNIT_H), (0, 0, 0))
    for i in range(min(types, cols * 4)):
        top = i * UNIT_PITCH
        sub = sheet[top:top + UNIT_H]
        if len(sub) < UNIT_H:
            break
        im = to_image(sub, 1)
        cell.paste(im, ((i % cols) * UNIT_W, (i // cols) * UNIT_H))
    out = os.path.join(out_dir, "%s_units.png" % name.replace(".gp", ""))
    cell.resize((cols * UNIT_W * 3, 4 * UNIT_H * 3), Image.NEAREST).save(out)
    print("       wrote %s" % out)

    # Full sheet preview.
    out = os.path.join(out_dir, "%s_sheet.png" % name.replace(".gp", ""))
    to_image(sheet[:512], 2).save(out)
    print("       wrote %s" % out)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/yphz_raw_sprites",
                    help="preview output directory")
    ap.add_argument("--tile", type=int, default=0,
                    help="dump the n-th unit sprite (1-based) as PNG")
    ap.add_argument("--files", nargs="*",
                    default=["Hunitpat.gp", "Mainobj.gp", "Hchikei.gp"])
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    if args.tile > 0:
        data = open(os.path.join(DATA, "Hunitpat.gp"), "rb").read()
        top = (args.tile - 1) * UNIT_PITCH
        rows, _ = decode_4plane(data, WIDTH, UNIT_H, top * ROW_BYTES)
        out = os.path.join(args.out, "Hunitpat_unit%02d.png" % args.tile)
        to_image(rows, 8).save(out)
        print("[INFO] wrote %s" % out)
        return 0

    ok = True
    for name in args.files:
        if not report(name, args.out):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

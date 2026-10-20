#!/usr/bin/env python3
"""PC-98 4bpp layout solver for the raw Genpei Kassen battle sheets.

子任务 11.  The raw battle assets (Hunitpat.gp / Hchikei.gp / Mainobj.gp) are
not NPK016 containers, so their plane interleaving has to be recovered from
the data itself.

Method
------
1. *Byte autocorrelation* finds the row stride: the mean absolute difference
   between byte *i* and byte *i + s* is minimal at the true stride, because
   consecutive scanlines of a picture are similar.
2. *Vertical coherence* confirms the pixel width: with the correct width, an
   adjacent row pair matches far better than a random row pair.
3. *Spatial coherence* scores the plane layout: real pixel art has large
   connected regions of one colour, a mis-ordered byte stream is noise.

Results (see the README for the discussion):

  Hunitpat.gp  151936 B  stride 12 B -> 24 px wide, 4 planes, chunk8 order
                             12661 rows; sprites are 24x32 on a 36-row pitch
                             (12 unit types, one per faction)
  Mainobj.gp    24544 B  stride 24 B -> 48 px wide, 4 planes, chunk8 order
                             1022 rows; 16x16 banner/flag tiles, 3 per row
  Hchikei.gp    10060 B  a 100x100 grid of one-byte terrain tile ids
                             (13 distinct ids) + a 60 byte formation table
"""

import os
import sys
from collections import Counter

import numpy as np

DATA = os.path.join(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")),
                    "games", "yphz")

SH = np.array([7, 6, 5, 4, 3, 2, 1, 0], dtype=np.uint16)


# ---------------------------------------------------------------------------
# Decoders.  Each returns an (h, w) uint8 array of colour indices.
# ---------------------------------------------------------------------------


def dec_chunk8(d, off, w, h):
    """4 bytes per 8 pixels, plane 0 = LSB.

    This is the order the engine's NPK/NUP4 decoder already produces (see
    decompress.c), i.e. the game's native planar order.
    """
    wb = (w + 7) // 8
    a = np.ascontiguousarray(d[off:off + wb * 4 * h]).reshape(h, wb, 4).astype(np.uint16)
    out = np.zeros((h, wb, 8), np.uint8)
    for p in range(4):
        out |= (((a[:, :, p][:, :, None] >> SH[None, None, :]) & 1).astype(np.uint8)) << p
    return out.reshape(h, wb * 8)[:, :w]


def dec_rowplanar(d, off, w, h):
    """Per row: plane 0 bytes, then plane 1, plane 2, plane 3."""
    wb = (w + 7) // 8
    a = np.ascontiguousarray(d[off:off + wb * 4 * h]).reshape(h, 4, wb).astype(np.uint16)
    out = np.zeros((h, wb, 8), np.uint8)
    for p in range(4):
        out |= (((a[:, p][:, :, None] >> SH[None, None, :]) & 1).astype(np.uint8)) << p
    return out.reshape(h, wb * 8)[:, :w]


def dec_blockplanar(d, off, w, h):
    """The whole window's plane 0, then plane 1, plane 2, plane 3."""
    wb = (w + 7) // 8
    a = np.ascontiguousarray(d[off:off + wb * 4 * h]).reshape(4, h, wb).astype(np.uint16)
    out = np.zeros((h, wb, 8), np.uint8)
    for p in range(4):
        out |= (((a[p][:, :, None] >> SH[None, None, :]) & 1).astype(np.uint8)) << p
    return out.reshape(h, wb * 8)[:, :w]


def dec_packed(d, off, w, h):
    """4bpp packed, two pixels per byte (high nibble first)."""
    rb = (w + 1) // 2
    a = np.ascontiguousarray(d[off:off + rb * h]).reshape(h, rb)
    out = np.zeros((h, rb * 2), np.uint8)
    out[:, 0::2] = a >> 4
    out[:, 1::2] = a & 0x0F
    return out[:, :w]


LAYOUTS = {
    "chunk8": dec_chunk8,
    "rowplanar": dec_rowplanar,
    "blockplanar": dec_blockplanar,
    "packed": dec_packed,
}


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------


def byte_stride(d, max_stride=260):
    """Row stride: the lag with the smallest mean absolute byte difference."""
    di = d.astype(np.int16)
    best = []
    for s in range(2, min(max_stride, len(d))):
        best.append((float(np.abs(di[:-s] - di[s:]).mean()), s))
    best.sort()
    return best


def vertical_gap(px, far=53, step=7):
    """(adjacent row similarity) - (random row similarity)."""
    n = px.shape[0]
    if n < far + 20:
        return 0.0
    adj = float(np.mean([(px[i] == px[i + 1]).mean() for i in range(0, n - 2, step)]))
    rnd = float(np.mean([(px[i] == px[i + far]).mean() for i in range(0, n - far, step)]))
    return adj - rnd


def largest_blob(px):
    """Size of the largest 4-connected same-colour region (fraction)."""
    h, w = px.shape
    flat = px.ravel()
    seen = np.zeros(h * w, bool)
    best = 0
    for start in range(h * w):
        if seen[start]:
            continue
        col = flat[start]
        seen[start] = True
        stack = [start]
        size = 0
        while stack:
            i = stack.pop()
            size += 1
            y, x = divmod(i, w)
            for j in ((i - 1) if x > 0 else -1, (i + 1) if x < w - 1 else -1,
                      (i - w) if y > 0 else -1, (i + w) if y < h - 1 else -1):
                if j >= 0 and not seen[j] and flat[j] == col:
                    seen[j] = True
                    stack.append(j)
        if size > best:
            best = size
    return best / (h * w)


def flat_ratio(px):
    h, w = px.shape
    a, b, c, e = px[:-1, :-1], px[:-1, 1:], px[1:, :-1], px[1:, 1:]
    return float(np.mean((a == b) & (b == c) & (c == e)))


def entropy(px):
    counts = np.bincount(px.ravel(), minlength=16).astype(np.float64)
    p = counts[counts > 0] / counts.sum()
    return float(-(p * np.log2(p)).sum())


def analyse(d, w, layout, rows=3000, off=0):
    px = LAYOUTS[layout](d, off, w, rows)
    return {"blob": largest_blob(px), "flat": flat_ratio(px),
            "colors": int(len(np.unique(px))), "entropy": entropy(px),
            "vgap": vertical_gap(px)}


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------


def report(name):
    path = os.path.join(DATA, name)
    if not os.path.exists(path):
        return
    d = np.frombuffer(open(path, "rb").read(), dtype=np.uint8)
    print("=" * 78)
    print("%s.gp  (%d bytes)" % (name, len(d)))
    print("  head 32: %s" % bytes(d[:32]).hex(" "))

    strides = byte_stride(d)
    print("  byte autocorrelation (stride: mean |byte[i] - byte[i+s]|):")
    print("    " + "  ".join("s=%d:%.2f" % (s, m) for m, s in strides[:6]))

    # Width candidates that divide the file exactly for each layout.
    print("  vertical coherence gap by width (chunk8):")
    rows = []
    for w in range(8, 1025, 4):
        wb = (w + 7) // 8
        h = len(d) // (wb * 4)
        if h < 200:
            continue
        px = dec_chunk8(d, 0, w, min(h, 3000))
        rows.append((vertical_gap(px), w, h))
    rows.sort(reverse=True)
    for g, w, h in rows[:6]:
        print("    w=%4d h=%6d gap=%+.4f" % (w, h, g))

    # Layout comparison at the best width.
    best_w = rows[0][1]
    print("  layout comparison at w=%d:" % best_w)
    for layout in LAYOUTS:
        m = analyse(d, best_w, layout, min(3000, len(d) // (((best_w + 7) // 8) * 4)))
        print("    %-12s blob=%.3f flat=%.3f colors=%2d entropy=%.3f vgap=%+.4f"
              % (layout, m["blob"], m["flat"], m["colors"], m["entropy"], m["vgap"]))


def palettes():
    print("=" * 78)
    print("PC-98 palettes")
    for fn in ("Mainpal.pld", "Losepal.pld"):
        p = os.path.join(DATA, fn)
        if not os.path.exists(p):
            continue
        raw = open(p, "rb").read()
        print("  %s (%d B = %d banks x 16 colours x 3 bytes)"
              % (fn, len(raw), len(raw) // 48))
        for b in range(len(raw) // 48):
            cols = []
            for c in range(16):
                bl, r, g = raw[b * 48 + c * 3], raw[b * 48 + c * 3 + 1], raw[b * 48 + c * 3 + 2]
                cols.append("%02X%02X%02X" % (r * 17, g * 17, bl * 17))
            print("    bank %d: %s" % (b, " ".join(cols)))


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*",
                    default=["Hunitpat.gp", "Mainobj.gp", "Hchikei.gp",
                             "Maincmd.gp", "Hikuchi.gp"])
    args = ap.parse_args()
    for fn in args.files:
        report(fn)
    palettes()


if __name__ == "__main__":
    main()

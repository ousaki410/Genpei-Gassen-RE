#!/usr/bin/env python3
"""Exhaustive PC-98 4bpp layout sweep with shear + coherence scoring.

子任务 11 helper.  Decoding a raw 4bpp sheet with the wrong row stride shears
the image diagonally, which is easy to detect: for the correct stride the
rows stay aligned, so the *vertical* neighbour equality is much higher than
the horizontal one.  For the correct *plane order* the four planes have very
different densities (a colour plane that is nearly empty stands out).

The script reports, for every (layout, width) whose stride divides the file
exactly, the shear ratio and the spatial coherence.
"""

import os
import sys

DATA = os.path.join(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")),
                    "games", "yphz")


def _bits(byte):
    return [(byte >> (7 - i)) & 1 for i in range(8)]


def decode(data, layout, width, height, off=0):
    """Decode `height` rows of `width` pixels; returns a list of rows."""
    wb = (width + 7) // 8
    rows = []
    if layout == "chunk8":
        # per 8 pixels: 4 plane bytes
        pos = off
        for y in range(height):
            row = []
            for g in range(wb):
                b = data[pos:pos + 4]
                pos += 4
                for i in range(8):
                    x = g * 8 + i
                    if x >= width:
                        break
                    c = 0
                    for p in range(4):
                        if (b[p] >> (7 - i)) & 1:
                            c |= 1 << p
                    row.append(c)
            rows.append(row)
    elif layout == "rowplanar":
        # per row: plane0 (wb bytes), plane1, plane2, plane3
        pos = off
        for y in range(height):
            row = [0] * width
            for p in range(4):
                for g in range(wb):
                    byte = data[pos]
                    pos += 1
                    base = g * 8
                    for i in range(8):
                        x = base + i
                        if x >= width:
                            break
                        if (byte >> (7 - i)) & 1:
                            row[x] |= 1 << p
            rows.append(row)
    elif layout == "blockplanar":
        # plane0 for the whole image, then plane1, plane2, plane3
        plane_bytes = wb * height
        for y in range(height):
            row = [0] * width
            for p in range(4):
                base = off + p * plane_bytes + y * wb
                for g in range(wb):
                    byte = data[base + g]
                    b8 = g * 8
                    for i in range(8):
                        x = b8 + i
                        if x >= width:
                            break
                        if (byte >> (7 - i)) & 1:
                            row[x] |= 1 << p
            rows.append(row)
    elif layout == "packed":
        # 4bpp packed, two pixels per byte
        rb = (width + 1) // 2
        pos = off
        for y in range(height):
            row = []
            for xb in range(rb):
                byte = data[pos]
                pos += 1
                row.append(byte >> 4)
                if len(row) < width:
                    row.append(byte & 0x0F)
            rows.append(row[:width])
    elif layout == "vram_banks":
        # the four planes are separate banks of the whole image
        plane_bytes = wb * height
        for y in range(height):
            row = [0] * width
            for p in range(4):
                base = off + p * plane_bytes + y * wb
                for g in range(wb):
                    byte = data[base + g]
                    b8 = g * 8
                    for i in range(8):
                        x = b8 + i
                        if x >= width:
                            break
                        if (byte >> (7 - i)) & 1:
                            row[x] |= 1 << p
            rows.append(row)
    else:
        raise ValueError(layout)
    return rows


def metrics(rows):
    h = len(rows)
    w = len(rows[0]) if h else 0
    if h < 2 or w < 2:
        return None
    horiz = sum(1 for y in range(h) for x in range(w - 1) if rows[y][x] == rows[y][x + 1])
    horiz /= h * (w - 1)
    vert = sum(1 for y in range(h - 1) for x in range(w) if rows[y][x] == rows[y + 1][x])
    vert /= (h - 1) * w
    # a correct stride keeps rows aligned: vertical equality must exceed
    # horizontal equality (row content is much more similar downwards).
    shear = horiz / vert if vert else 0.0
    flat_n = flat = 0
    for y in range(h - 1):
        for x in range(w - 1):
            a = rows[y][x]
            if a == rows[y][x + 1] == rows[y + 1][x] == rows[y + 1][x + 1]:
                flat += 1
            flat_n += 1
    flat = flat / flat_n if flat_n else 0.0
    colors = len({v for row in rows for v in row})
    bg = sum(1 for row in rows for v in row if v == 0) / (h * w)
    return {"horiz": horiz, "vert": vert, "shear": shear, "flat": flat,
            "colors": colors, "bg": bg}


def plane_densities(data, layout, width, height, off=0):
    """Density of each colour plane, which reveals the plane order."""
    wb = (width + 7) // 8
    dens = [0.0] * 4
    total = 0
    if layout == "chunk8":
        pos = off
        for y in range(height):
            for g in range(wb):
                b = data[pos:pos + 4]
                pos += 4
                for p in range(4):
                    dens[p] += sum((b[p] >> i) & 1 for i in range(8))
                total += 8
    elif layout in ("rowplanar", "blockplanar", "vram_banks"):
        if layout == "rowplanar":
            plane_bytes = None
            pos = off
            for y in range(height):
                for p in range(4):
                    for g in range(wb):
                        byte = data[pos]
                        pos += 1
                        dens[p] += sum((byte >> i) & 1 for i in range(8))
                total += wb * 8
        else:
            plane_bytes = wb * height
            for p in range(4):
                base = off + p * plane_bytes
                for i in range(plane_bytes):
                    dens[p] += sum((data[base + i] >> b) & 1 for b in range(8))
                total += plane_bytes * 8
    else:
        return None
    return [d / total for d in dens] if total else None


def sweep(name, data, max_rows=160):
    n = len(data)
    results = []
    for layout in ("chunk8", "rowplanar", "blockplanar", "vram_banks", "packed"):
        for width in range(4, 1025):
            if layout == "packed":
                rowbytes = (width + 1) // 2
            else:
                rowbytes = ((width + 7) // 8) * 4
            if rowbytes == 0 or n % rowbytes != 0:
                continue
            height = n // rowbytes
            if height < 8:
                continue
            rows = decode(data, layout, width, min(height, max_rows))
            m = metrics(rows)
            if not m:
                continue
            pd = plane_densities(data, layout, width, min(height, 64))
            pspread = (max(pd) - min(pd)) if pd else 0.0
            score = (2.0 * m["vert"] + 1.0 * m["flat"] - 0.5 * m["shear"]
                     - 0.02 * m["colors"] + 2.0 * pspread)
            results.append((score, layout, width, height, m, pd))
    results.sort(reverse=True)
    print("=" * 78)
    print("%s (%d bytes)" % (name, n))
    print("  %-12s %5s %6s %6s %6s %6s %6s %6s  %s"
          % ("layout", "width", "height", "horiz", "vert", "shear", "flat",
             "colors", "plane densities"))
    for score, layout, width, height, m, pd in results[:14]:
        pds = ("[" + " ".join("%.2f" % v for v in pd) + "]") if pd else "-"
        print("  %-12s %5d %6d %6.3f %6.3f %6.3f %6.3f %6.1f  %s  score=%6.3f"
              % (layout, width, height, m["horiz"], m["vert"], m["shear"],
                 m["flat"], m["colors"], pds, score))
    return results


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*",
                    default=["Hunitpat.gp", "Mainobj.gp", "Hchikei.gp",
                             "Hground.gp", "Hkei.gp", "Haikei.gp",
                             "Mainanm.gp", "Mainstl.gp"])
    ap.add_argument("--max-rows", type=int, default=160)
    args = ap.parse_args()
    for fn in args.files:
        p = os.path.join(DATA, fn)
        if not os.path.exists(p):
            continue
        sweep(fn, open(p, "rb").read(), args.max_rows)


if __name__ == "__main__":
    main()

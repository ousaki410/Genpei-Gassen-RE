#!/usr/bin/env python3
"""
analyze_raw_and_exe.py — dimension / reference reconnaissance for 《源平合战》.

Two questions this tool answers:

1. Where does the DOS executable reference each graphics archive, and what
   constants sit next to that reference?
   Main.exe keeps a compact NUL-terminated catalog ("C:MAINCMD.GP", ...) at
   0x51743; Main.ori keeps the same catalog with A:/B: floppy prefixes at
   0x51723.  Open.exe embeds a shorter install-time catalog.  For every
   EXE we dump the catalog, its table offset, and the u16/u32 words that
   immediately precede it (candidate load slots / segment pointers).

2. What (width, height, count) layout does each headerless .gp file use?
   PC-98 16-colour images are 4 bitplanes, i.e. 2 pixels per byte, with rows
   rounded up to 4 bytes (8 pixels).  For a file of S bytes we therefore look
   for   align4(ceil(w/2)) * h * count == S.  We also test 8bpp (1 byte per
   pixel) and 1bpp (8 pixels per byte).  Candidates are scored against common
   PC-98 frame sizes.  When no layout fits, the file is flagged as compressed
   or structured rather than silently given a bogus size.

   A dedicated tile-grid detector recognises the battle terrain format:
   Hchikei.gp is 100x100 one-byte tile ids followed by a 60-byte placement
   table (10000 + 60 = 10060).

Usage:
    python3 tools/analyze_raw_and_exe.py [--dir games/yphz] [--json]

No third-party dependencies.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
from collections import Counter

# --------------------------------------------------------------------------
# Asset catalogue
# --------------------------------------------------------------------------

# 19 files classified as "raw" by probe_gp_files.py, split by intent.
IMAGE_STEMS = [
    "Haikei", "Hchikei", "Hground", "Hikuchi", "Hkei", "Hunitpat",
    "Kisetsu", "Mainanm", "Maincmd", "Mainobj", "Montage",
]
DATA_STEMS = {
    "Kaodata": "portrait bank: 90 x 0x728 records (decoder exists)",
    "Sndata": "scenario table: 4 x 41266 B (parser exists)",
    "Savedata": "save-game table (not parsed)",
    "Opendat": "opening / prologue data (not parsed)",
    "Enddat": "ending / epilogue data (not parsed)",
    "Music": "FM/PMD music data (not parsed)",
    "Opmusic": "FM/PMD opening music (not parsed)",
    "Edmusic": "FM/PMD ending music (not parsed)",
}

# Common PC-98 frame widths / heights, used only for ranking.
COMMON_W = {640, 320, 288, 256, 208, 160, 128, 96, 64, 32, 16, 8}
COMMON_H = {400, 360, 320, 240, 200, 176, 168, 160, 152, 144, 128, 112,
            100, 96, 80, 72, 64, 56, 48, 40, 32, 24, 16, 12, 8}
COMMON_PAIRS = {(640, 400), (320, 200), (640, 200), (320, 400),
                (160, 128), (128, 128), (64, 64), (208, 160), (288, 176),
                (88, 360), (160, 160), (96, 96), (32, 64), (208, 24)}

# Filename tokens such as "C:MAINCMD.GP" or "A:SNDATA1.GP".  Catalogs store
# names back-to-back and sometimes let a trailing "GP" share its final letter
# with the next "C:" prefix, so we anchor on the extension and walk backwards.
EXT_RE = re.compile(rb"\.(?:GP|PLD|DAT|FNT|CHR|COM|EXE)", re.IGNORECASE)
NAME_CHARS = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                 b"0123456789_-")

# Stems worth reporting when they appear in an EXE.
INTERESTING = {s.upper() for s in IMAGE_STEMS} | {
    "MAINCMD", "MAINCMD2", "MAINOBJ", "MAINSTL", "MAINEVT", "MAINITEM", "MAINMAP",
    "MAINANM", "KAODATA", "SNDATA", "SNDATA1", "SAVEDATA", "MONTAGE",
    "OPENDAT", "ENDDAT", "MESSAGE", "MUSIC", "OPMUSIC", "EDMUSIC",
    "MAINPAL", "LOSEPAL", "PCM", "HCHIKEI", "HKESHIKI", "HGROUND",
    "HUNITPAT", "HIKUCHI", "HKUMI", "HIKBACK", "HKEI", "HAIKEI",
    "KISETSU", "LOGO",
}


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------

def align4(n: int) -> int:
    return (n + 3) & ~3


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    total = len(data)
    return -sum((c / total) * math.log2(c / total) for c in counts.values())


def u16s(data: bytes, count: int) -> list[int]:
    return [struct.unpack_from("<H", data, i * 2)[0]
            for i in range(min(count, len(data) // 2))]


def u32s(data: bytes, count: int) -> list[int]:
    return [struct.unpack_from("<I", data, i * 4)[0]
            for i in range(min(count, len(data) // 4))]


# --------------------------------------------------------------------------
# EXE catalog scan
# --------------------------------------------------------------------------

def scan_exe(path: str) -> dict:
    with open(path, "rb") as f:
        data = f.read()

    result = {
        "file": os.path.basename(path),
        "size": len(data),
        "is_mz": data[:2] == b"MZ",
        "tables": [],
    }
    if not result["is_mz"] and not path.lower().endswith(".exe"):
        return result

    hits = []
    seen = set()
    for m in EXT_RE.finditer(data):
        dot = m.start()
        j = dot
        while j > 0 and data[j - 1] in NAME_CHARS:
            j -= 1
        if j < 3 or data[j - 1] != ord(":") or data[j - 2] not in b"ABCabc":
            continue
        start = j - 2
        if start in seen:
            continue
        seen.add(start)
        name = data[start:m.end()].decode("ascii", "replace")
        stem = name.split(":", 1)[1].split(".", 1)[0].upper()
        if stem in INTERESTING:
            hits.append({"offset": start, "name": name, "stem": stem})
    hits.sort(key=lambda h: h["offset"])

    # Group hits separated by <= 4 bytes of padding (NULs) into one catalog.
    tables = []
    current = []
    last_end = None
    for h in hits:
        if last_end is None or h["offset"] - last_end <= 4:
            current.append(h)
        else:
            if len(current) >= 3:
                tables.append(current)
            current = [h]
        last_end = h["offset"] + len(h["name"]) + 1
    if len(current) >= 3:
        tables.append(current)

    for entries in tables:
        base = entries[0]["offset"]
        before = data[max(0, base - 64):base]
        table = {
            "offset": base,
            "count": len(entries),
            # The pointer block just before the strings uses 4-byte slots.
            "preceding_u32": [f"0x{v:08X}" for v in u32s(before, 16)],
            "preceding_u16": [f"0x{v:04X}" for v in u16s(before, 32)],
            "entries": [
                {"index": i, "offset": e["offset"], "name": e["name"]}
                for i, e in enumerate(entries)
            ],
        }
        result["tables"].append(table)
    return result


# --------------------------------------------------------------------------
# Raw image layout inference
# --------------------------------------------------------------------------

def score_layout(w: int, h: int, count: int, trailer: int) -> int:
    score = 0
    if w in COMMON_W:
        score += 20
    if h in COMMON_H:
        score += 20
    if (w, h) in COMMON_PAIRS:
        score += 60
    if count == 1:
        score += 15
    if count in (1, 2, 4, 8, 16, 32, 64):
        score += 8
    if h <= 400:
        score += 10
    if h <= 200:
        score += 4
    aspect = w / max(h, 1)
    if 0.25 <= aspect <= 4.0:
        score += 10
    elif aspect > 8.0 or aspect < 0.125:
        score -= 30
    if trailer == 0:
        score += 5
    # A 640-pixel-wide 4bpp row is 320 bytes, the native PC-98 screen stride.
    if w == 640:
        score += 6
    return score


def layout_candidates(size: int, bits_per_pixel: int,
                      max_count: int = 64) -> list[dict]:
    """Enumerate align4 row stride * height * count == size."""
    if bits_per_pixel == 4:
        px_per_byte = 2
    elif bits_per_pixel == 8:
        px_per_byte = 1
    else:
        px_per_byte = 8

    out = []
    for w in range(8, 641, 8):
        stride = align4((w + px_per_byte - 1) // px_per_byte)
        if stride == 0:
            continue
        max_h = min(2000, size // stride)
        for count in (1, 2, 4, 8, 16, 32, 64):
            if count > max_count or size % (stride * count) != 0:
                continue
            h = size // (stride * count)
            if 8 <= h <= 2000:
                out.append({
                    "w": w, "h": h, "count": count,
                    "stride": stride, "bpp": bits_per_pixel,
                    "score": score_layout(w, h, count, 0),
                })
    out.sort(key=lambda c: (-c["score"], c["count"], c["h"], c["w"]))
    return out


def detect_tile_grid(data: bytes) -> dict | None:
    """Find size == w*h + small_trailer where payload is a small-value grid.

    Battle terrain (Hchikei.gp) is a byte-per-tile grid followed by a compact
    placement table.  We only consider round PC-98 grid dimensions and require
    both the grid and the trailer to contain low-valued tile ids, so ordinary
    bitmaps (256 distinct byte values) never match.
    """
    size = len(data)
    if size < 256:
        return None
    values = set(data)
    if len(values) > 48 or max(values) > 63:
        return None

    round_grid = {16, 20, 24, 32, 40, 48, 50, 56, 64, 80, 96, 100, 112,
                  128, 144, 160, 192, 200, 224, 256, 288, 320, 384, 400,
                  480, 512}
    round_trailer = {0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 32, 40, 48,
                     56, 60, 64, 72, 80, 96, 100, 112, 128, 160, 192, 200,
                     256}

    best = None
    for w in round_grid:
        for h in round_grid:
            area = w * h
            trailer = size - area
            if trailer < 0 or trailer > 256:
                continue
            if not (0.5 <= w / h <= 2.0):
                continue
            payload = data[:area]
            ids = set(payload)
            if len(ids) > 32 or max(ids, default=0) > 31:
                continue
            tail = data[area:]
            if tail and max(tail) > 63:
                continue

            score = 100.0
            if w == h:
                score += 40
            score -= abs(w - h) * 0.5
            score -= trailer * 0.4
            if trailer in round_trailer:
                score += 10
            if area > 2:
                equal = sum(1 for i in range(area - 1)
                            if payload[i] == payload[i + 1])
                score += 20.0 * equal / (area - 1)

            if best is None or score > best["score"]:
                best = {
                    "w": w,
                    "h": h,
                    "tile_ids": len(ids),
                    "max_id": max(ids, default=0),
                    "trailer": trailer,
                    "score": round(score, 1),
                }
    return best


def analyze_image(path: str) -> dict:
    with open(path, "rb") as f:
        data = f.read()
    size = len(data)

    header_hint = None
    if size >= 4:
        a, b = struct.unpack_from("<HH", data, 0)
        if 8 <= a <= 640 and 8 <= b <= 400:
            header_hint = {"w": a, "h": b}

    grid = detect_tile_grid(data)
    candidates = {
        "4bpp": layout_candidates(size, 4)[:5],
        "8bpp": layout_candidates(size, 8)[:5],
        "1bpp": layout_candidates(size, 1)[:5],
    }

    best = None
    for fmt, cands in candidates.items():
        if cands and (best is None or cands[0]["score"] > best["score"]):
            best = dict(cands[0], format=fmt)

    if grid is not None and grid["trailer"] > 0:
        verdict = (f"tile grid {grid['w']}x{grid['h']} "
                   f"({grid['tile_ids']} ids, max {grid['max_id']}) "
                   f"+ {grid['trailer']}-byte trailer")
    elif best is not None and best["score"] >= 95:
        verdict = f"{best['format']} {best['w']}x{best['h']} x{best['count']}"
    elif best is not None and best["score"] >= 75:
        verdict = (f"probable {best['format']} {best['w']}x{best['h']} "
                   f"x{best['count']} (unconfirmed)")
    else:
        verdict = "inconclusive: no clean bitmap layout (likely compressed/structured)"

    return {
        "file": os.path.basename(path),
        "size": size,
        "entropy": round(entropy(data), 2),
        "zero_pct": round(100.0 * data.count(0) / size, 1) if size else 0.0,
        "header_hint": header_hint,
        "tile_grid": grid,
        "candidates": candidates,
        "verdict": verdict,
    }


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def print_report(dirpath: str, as_json: bool) -> int:
    names = sorted(os.listdir(dirpath))
    exes = []
    images = []
    data_files = []

    for name in names:
        path = os.path.join(dirpath, name)
        if not os.path.isfile(path):
            continue
        stem = name.rsplit(".", 1)[0]
        lower = name.lower()
        if lower.endswith((".exe", ".com", ".ori")) or stem in {
                "Main", "Main.ori", "Open", "End", "Grpdrv", "Install"}:
            if lower.endswith((".exe", ".ori")):
                exes.append(scan_exe(path))
        elif stem in IMAGE_STEMS:
            images.append(analyze_image(path))
        elif stem in DATA_STEMS:
            data_files.append({
                "file": name,
                "size": os.path.getsize(path),
                "kind": DATA_STEMS[stem],
            })

    report = {"dir": dirpath, "exes": exes, "images": images,
              "data": data_files}

    if as_json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
        return 0

    # --- EXE catalogs ---
    print("=" * 78)
    print("DOS EXECUTABLE ASSET CATALOGS")
    print("=" * 78)
    for exe in exes:
        print(f"\n{exe['file']}  ({exe['size']} B, MZ={exe['is_mz']})")
        if not exe["tables"]:
            print("  no asset-filename catalog found")
            continue
        for t in exe["tables"]:
            print(f"  catalog @0x{t['offset']:X}: {t['count']} entries")
            print(f"  preceding u32: {' '.join(t['preceding_u32'][:8])}")
            print(f"  preceding u16: {' '.join(t['preceding_u16'][:16])}")
            for e in t["entries"]:
                print(f"    [{e['index']:2d}] 0x{e['offset']:06X}  {e['name']}")

    # --- Raw images ---
    print("\n" + "=" * 78)
    print("HEADERLESS RAW IMAGE LAYOUTS (PC-98 4bpp = 2 px/byte, 4-byte rows)")
    print("=" * 78)
    for img in images:
        print(f"\n{img['file']:<14} {img['size']:>8} B  "
              f"H={img['entropy']:.2f}  zero={img['zero_pct']:.1f}%")
        if img["header_hint"]:
            print(f"  header u16   : {img['header_hint']['w']}x"
                  f"{img['header_hint']['h']}")
        if img["tile_grid"]:
            g = img["tile_grid"]
            print(f"  tile grid    : {g['w']}x{g['h']} ids={g['tile_ids']} "
                  f"max={g['max_id']} + {g['trailer']} B trailer")
        for fmt in ("4bpp", "8bpp", "1bpp"):
            cands = img["candidates"][fmt]
            if not cands:
                continue
            text = ", ".join(f"{c['w']}x{c['h']}x{c['count']}"
                             f"(s{c['score']})" for c in cands[:3])
            print(f"  {fmt:>4} best   : {text}")
        print(f"  verdict      : {img['verdict']}")

    # --- Known data files ---
    print("\n" + "=" * 78)
    print("RAW NON-IMAGE FILES")
    print("=" * 78)
    for d in data_files:
        print(f"  {d['file']:<14} {d['size']:>8} B  {d['kind']}")

    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="games/yphz",
                    help="asset directory (default: games/yphz)")
    ap.add_argument("--json", action="store_true",
                    help="emit JSON instead of the human report")
    args = ap.parse_args()
    if not os.path.isdir(args.dir):
        print(f"error: no such directory: {args.dir}", file=sys.stderr)
        return 2
    return print_report(args.dir, args.json)


if __name__ == "__main__":
    raise SystemExit(main())

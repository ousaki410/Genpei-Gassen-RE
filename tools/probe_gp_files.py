#!/usr/bin/env python3
"""
probe_gp_files.py — asset-header reconnaissance for 《源平合战》(KOEI, 1994).

Scans every file in games/yphz (53 files), identifies the container/format from
its magic and a few structural heuristics, and prints a compact inventory table:

    LS11        KOEI LS11 compressed text stream (Message.gp)
    NPK016      KOEI NPK016/NUP4 graphics container (48-byte block header,
                LZ + planar 4-bitplane payload)
    DOS EXE     MZ executable
    DOS COM     0xE9/0xEB jump opcode (COM binary)
    palette     Mainpal.pld / Losepal.pld (4 banks x 16 x 0x0GRB)
    font        FONT.CHR / FONT.DAT / JIS.FNT
    text        plain DOS text / batch / config
    raw         no recognised magic; reports size, u16 header words,
                zero ratio and byte entropy to guide the next RE step

Usage:
    python3 tools/probe_gp_files.py [--dir games/yphz] [--json] [--csv]

No third-party dependencies. JSON goes to stdout after the human table only when
--json is given (the table is still printed to stderr so stdout stays parseable).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from collections import Counter

NPK_HEADER_SIZE = 48
LS11_RECORD_OFFSET = 0x110
LS11_RECORD_SIZE = 12

# Families that are already understood; used to annotate raw files by name.
KNOWN_FAMILIES = {
    "kaodata.gp": "portrait bank, 90 x 0x728 records (see decompress.c)",
    "sndata.gp": "scenario table: 4 x 41266 B (officers/castles/conns)",
    "savedata.gp": "save-game table (not parsed yet)",
    "enddat.gp": "ending / epilogue data (not parsed yet)",
    "opendat.gp": "opening / prologue data (not parsed yet)",
    "music.gp": "FM/PMD music data (not parsed yet)",
    "opmusic.gp": "FM/PMD opening music (not parsed yet)",
    "edmusic.gp": "FM/PMD ending music (not parsed yet)",
    "maincmd.gp": "command/UI sprite/bitmap data (raw, not parsed yet)",
    "maincmd2.gp": "palace background container (NPK016)",
    "mainanm.gp": "animation frames (raw, not parsed yet)",
    "mainobj.gp": "map object sprites (raw, not parsed yet)",
    "mainitem.gp": "item sprites (NPK016)",
    "mainevt.gp": "event illustrations (NPK016)",
    "mainstl.gp": "still illustrations (NPK016)",
    "mainmap.gp": "strategic map, 20 NPK016 blocks -> 1696x360 panorama",
    "kisetsu.gp": "seasonal background (raw, not parsed yet)",
    "montage.gp": "montage / ending collage (raw, not parsed yet)",
    "haikei.gp": "battle background (raw, not parsed yet)",
    "hchikei.gp": "battle terrain map (raw, not parsed yet)",
    "hground.gp": "battle ground tiles (raw, not parsed yet)",
    "hikuchi.gp": "battle fortress/base graphics (raw, not parsed yet)",
    "hkei.gp": "battle unit/effect graphics (raw, not parsed yet)",
    "hkeshiki.gp": "battle scenery container (NPK016)",
    "hkumi.gp": "battle formation graphics container (NPK016)",
    "hunitpat.gp": "battle unit patterns (raw, not parsed yet)",
    "hikback.gp": "battle backdrop container (NPK016)",
    "logo.gp": "KOEI logo container (NPK016)",
}


def u16(data: bytes, off: int) -> int:
    if off + 2 > len(data):
        return 0
    return struct.unpack_from("<H", data, off)[0]


def be32(data: bytes, off: int) -> int:
    if off + 4 > len(data):
        return 0
    return struct.unpack_from(">I", data, off)[0]


def shannon_entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    n = len(data)
    return -sum((c / n) * math.log2(c / n) for c in counts.values())


def printable_ratio(data: bytes) -> float:
    if not data:
        return 0.0
    text_bytes = b"\r\n\t\x1a\x00"
    good = sum(1 for b in data if 32 <= b < 127 or b in text_bytes)
    return good / len(data)


def is_textish(data: bytes) -> bool:
    """Small files that decode cleanly as Shift-JIS with almost no controls."""
    if not data or len(data) > 8192:
        return False
    try:
        text = data.decode("cp932")
    except UnicodeDecodeError:
        return False
    ctrl = sum(1 for ch in text if ord(ch) < 32 and ch not in "\r\n\t\x1a")
    return ctrl / len(text) < 0.02


def parse_npk_blocks(data: bytes) -> list[dict]:
    """Find every NPK016 header and report its declared dimensions."""
    blocks = []
    start = 0
    while True:
        off = data.find(b"NPK016", start)
        if off < 0:
            break
        start = off + 6
        if off + NPK_HEADER_SIZE > len(data):
            blocks.append({"offset": off, "truncated": True})
            continue
        blocks.append({
            "offset": off,
            "planes": u16(data, off + 6),
            "screen_w": u16(data, off + 8),
            "screen_h": u16(data, off + 10),
            "img_w": u16(data, off + 12),
            "img_h": u16(data, off + 14),
        })
    return blocks


def parse_ls11(data: bytes) -> dict:
    """Count LS11 records using the same stopping rule as message.c."""
    if len(data) < LS11_RECORD_OFFSET + LS11_RECORD_SIZE:
        return {"entries": 0, "truncated": True}
    entries = 0
    off = LS11_RECORD_OFFSET
    while off + LS11_RECORD_SIZE <= len(data):
        clen = be32(data, off)
        dlen = be32(data, off + 4)
        rec_off = be32(data, off + 8)
        if rec_off == 0 or rec_off >= len(data) or clen == 0:
            break
        if rec_off + clen > len(data):
            break
        entries += 1
        off += LS11_RECORD_SIZE
        if entries >= 256:
            break
    return {"entries": entries}


def detect(path: str, data: bytes) -> tuple[str, str]:
    name = os.path.basename(path).lower()
    size = len(data)

    if data[:4] == b"LS11":
        info = parse_ls11(data)
        return "LS11", f"{info['entries']} compressed record(s), 0x110 table"

    if data[:6] == b"NPK016":
        blocks = parse_npk_blocks(data)
        dims = [f"{b['img_w']}x{b['img_h']}" for b in blocks if "img_w" in b]
        uniq = list(dict.fromkeys(dims))
        detail = f"{len(blocks)} block(s)"
        if uniq:
            detail += ": " + ", ".join(uniq[:4])
            if len(uniq) > 4:
                detail += ", ..."
        return "NPK016", detail

    if data[:2] == b"MZ":
        return "DOS EXE", f"MZ, header paragraphs={u16(data, 8)}"

    if name.endswith(".com"):
        return "DOS COM", f"COM binary, first opcode 0x{data[0]:02x}"

    if data[:1] in (b"\xe9", b"\xeb"):
        return "DOS COM", f"jump opcode 0x{data[0]:02x}"

    if name.endswith(".bat"):
        head = data[:48].decode("latin-1", "replace").replace("\r", " ").replace("\n", " ")
        return "batch", head.strip()

    if name.endswith(".pld"):
        return "palette", f"{size // 3} colors ({size // 48} bank(s) x 16)"

    if name in ("font.chr", "font.dat", "jis.fnt"):
        if name == "font.chr":
            return "font", "256 x 8x16 half-width glyphs"
        if name == "jis.fnt":
            return "font", "JIS X 0208 16x16 glyph bank"
        return "font", "DOS/V font data"

    if printable_ratio(data) >= 0.95:
        head = data[:32].decode("latin-1", "replace").replace("\r", " ").replace("\n", " ")
        kind = "text"
        if data[:8] == b"BUFFERS=":
            kind = "config"
        elif data[:5].lower() == b"@echo":
            kind = "batch"
        return kind, head.strip()

    if is_textish(data):
        text = data.decode("cp932", "replace").strip()
        head = text.splitlines()[0] if text else ""
        kind = "config" if data[:3].upper() == b"REM" else "text"
        return kind, head[:60]

    note = KNOWN_FAMILIES.get(name, "")
    entropy = shannon_entropy(data)
    zero_pct = 100.0 * data.count(0) / size if size else 0.0
    words = " ".join(str(u16(data, i)) for i in range(0, min(8, size - size % 2), 2))
    detail = (f"u16[{words}] zero={zero_pct:.1f}% "
              f"H={entropy:.2f}b/B unique={len(set(data))}")
    if note:
        detail += f" | {note}"
    return "raw", detail


def collect(directory: str) -> list[dict]:
    rows = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        category, detail = detect(path, data)
        rows.append({
            "name": name,
            "size": len(data),
            "category": category,
            "detail": detail,
        })
    return rows


def print_table(rows: list[dict], file=None) -> None:
    out = file or sys.stdout
    print(f"{'FILE':<16} {'SIZE':>9}  {'CATEGORY':<10} DETAIL", file=out)
    print("-" * 100, file=out)
    for r in rows:
        print(f"{r['name']:<16} {r['size']:>9}  {r['category']:<10} {r['detail']}", file=out)

    print(file=out)
    print(f"== {len(rows)} files ==", file=out)
    counts = Counter(r["category"] for r in rows)
    for cat, n in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {cat:<10} {n:>2}", file=out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="games/yphz", help="asset directory (default: games/yphz)")
    ap.add_argument("--json", action="store_true", help="emit JSON on stdout")
    ap.add_argument("--csv", action="store_true", help="emit CSV on stdout")
    args = ap.parse_args()

    if not os.path.isdir(args.dir):
        print(f"error: not a directory: {args.dir}", file=sys.stderr)
        return 2

    rows = collect(args.dir)

    if args.json:
        print_table(rows, file=sys.stderr)
        print(json.dumps(rows, ensure_ascii=False, indent=2))
    elif args.csv:
        print("name,size,category,detail")
        for r in rows:
            detail = r["detail"].replace('"', '""')
            print(f'"{r["name"]}",{r["size"]},"{r["category"]}","{detail}"')
    else:
        print_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

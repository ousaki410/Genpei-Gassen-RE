#!/usr/bin/env python3
"""
Generate src/zh_hans_table.c -- the Japanese kanji -> Simplified Chinese table
used by --lang zh.

The engine ships no run-time Unicode normalization data, so the mapping is
compiled in. It is derived from the OpenCC dictionaries:

    JPShinjitaiCharacters.txt   Japanese shinjitai -> Traditional Chinese
    TSCharacters.txt            Traditional Chinese -> Simplified Chinese

Both files are Apache-2.0 data from https://github.com/BYVoid/OpenCC and are
vendored under third_party/opencc/ (see the README there for the exact upstream
commits), so regenerating the table needs no network access.

Only characters that cp932 can represent are emitted, which is exactly the set
of kanji that can appear in Sndata.gp / Message.gp.

Usage:
    tools/gen_zh_hans_table.py              # regenerate the C table
    tools/gen_zh_hans_table.py --check      # verify the committed table is current
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "third_party" / "opencc"
OUT_C = ROOT / "src" / "zh_hans_table.c"

JP_FILE = DATA / "JPShinjitaiCharacters.txt"
TS_FILE = DATA / "TSCharacters.txt"


def load_pairs(path: Path) -> dict:
    """Parse an OpenCC <key>\\t<value>[ <value>...] dictionary."""
    table = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or "\t" not in line:
            continue
        key, values = line.split("\t", 1)
        first = values.split(" ")[0]
        if len(key) == 1 and first:
            table[key] = first[0]
    return table


def cp932_chars() -> list:
    """Every character cp932 can encode as a two-byte sequence."""
    chars = set()
    for hi in range(0x81, 0xFD):
        for lo in range(0x40, 0xFD):
            try:
                ch = bytes((hi, lo)).decode("cp932")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                chars.add(ch)
    return sorted(chars)


def build_table(jp: dict, ts: dict) -> list:
    pairs = []
    for ch in cp932_chars():
        traditional = jp.get(ch, ch)
        simplified = ts.get(traditional, traditional)
        if simplified == ch:
            continue
        if ord(ch) > 0xFFFF or ord(simplified) > 0xFFFF:
            continue
        pairs.append((ord(ch), ord(simplified)))
    pairs.sort()
    return pairs


def render(pairs: list) -> str:
    lines = []
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("// GENERATED FILE -- do not edit by hand.")
    lines.append("//")
    lines.append("// Japanese kanji -> Simplified Chinese, restricted to characters that cp932")
    lines.append("// can represent. Derived with tools/gen_zh_hans_table.py from the OpenCC")
    lines.append("// dictionaries JPShinjitaiCharacters.txt + TSCharacters.txt (Apache-2.0).")
    lines.append(f"// {len(pairs)} entries.")
    lines.append("// ---------------------------------------------------------------------------")
    lines.append('#include "zh_hans_table.h"')
    lines.append("")
    lines.append(f"const int g_zh_hans_count = {len(pairs)};")
    lines.append("")
    lines.append("const uint16_t g_zh_hans_keys[ZH_HANS_TABLE_CAPACITY] = {")
    for i in range(0, len(pairs), 4):
        chunk = pairs[i:i + 4]
        keys = " ".join(f"0x{k:04X}," for k, _ in chunk)
        comment = " ".join(f"{chr(k)}->{chr(v)}" for k, v in chunk)
        lines.append(f"    {keys}  // {comment}")
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t g_zh_hans_vals[ZH_HANS_TABLE_CAPACITY] = {")
    for i in range(0, len(pairs), 4):
        chunk = pairs[i:i + 4]
        vals = " ".join(f"0x{v:04X}," for _, v in chunk)
        comment = " ".join(f"{chr(k)}->{chr(v)}" for k, v in chunk)
        lines.append(f"    {vals}  // {comment}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="do not write; fail if the committed table is stale")
    ap.add_argument("--jp", type=Path, default=JP_FILE, help="JPShinjitaiCharacters.txt")
    ap.add_argument("--ts", type=Path, default=TS_FILE, help="TSCharacters.txt")
    args = ap.parse_args()

    for path in (args.jp, args.ts):
        if not path.exists():
            print(f"[FAIL] missing dictionary: {path}", file=sys.stderr)
            return 2

    jp = load_pairs(args.jp)
    ts = load_pairs(args.ts)
    pairs = build_table(jp, ts)
    text = render(pairs)

    if args.check:
        current = OUT_C.read_text(encoding="utf-8") if OUT_C.exists() else ""
        if current != text:
            print("[FAIL] src/zh_hans_table.c is stale; rerun tools/gen_zh_hans_table.py")
            return 1
        print(f"[OK] zh-Hans table is current ({len(pairs)} entries)")
        return 0

    OUT_C.write_text(text, encoding="utf-8")
    print(f"[+] wrote {OUT_C.relative_to(ROOT)}: {len(pairs)} kanji mappings")
    return 0


if __name__ == "__main__":
    sys.exit(main())

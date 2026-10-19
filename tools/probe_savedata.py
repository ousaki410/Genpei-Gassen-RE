#!/usr/bin/env python3
"""Probe KOEI Genpei Kassen save container (Savedata.gp).

The file is a fixed array of ten 43403-byte slots. This script reports the
per-slot header, the embedded faction list, and where the Kamakura castle name
lands, then compares those offsets against the Sndata.gp scenario block.

It is read-only; the native engine snapshot (src/save.c) is a separate format.

Usage:
    python3 tools/probe_savedata.py [games/yphz/Savedata.gp] [games/yphz/Sndata.gp]
"""

import struct
import sys
from pathlib import Path

SLOT_SIZE = 43403
FACTION_COUNT = 15
KAMAKURA_SJIS = b"\x8a\x99\x91\x71"  # 鎌倉


def read_u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def describe_slot(index, slot):
    year0, month0 = read_u16(slot, 0), slot[2]
    year18, month18 = read_u16(slot, 18), slot[20]
    factions = [read_u16(slot, 24 + 2 * i) for i in range(FACTION_COUNT)]
    kama = slot.find(KAMAKURA_SJIS)
    print(f"slot {index}:")
    print(f"  header@0   year={year0} month={month0} flags=0x{slot[3]:02x}")
    print(f"  header@18  year={year18} month={month18} flags=0x{slot[21]:02x}")
    print(f"  factions@24  {factions}")
    print(f"  鎌倉 SJIS @ {kama if kama >= 0 else 'not found'}")
    return year0, month0, factions, kama


def main():
    root = Path(__file__).resolve().parent.parent
    save_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "games/yphz/Savedata.gp"
    sndata_path = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "games/yphz/Sndata.gp"

    save = save_path.read_bytes()
    print(f"Savedata: {save_path} ({len(save)} bytes)")
    slots = len(save) // SLOT_SIZE
    print(f"slot size: {SLOT_SIZE}, slots: {slots}, remainder: {len(save) % SLOT_SIZE}\n")

    used = 0
    for i in range(slots):
        year, _, _, kama = describe_slot(i, save[i * SLOT_SIZE:(i + 1) * SLOT_SIZE])
        if 1100 <= year <= 1300:
            used += 1
        print()

    print(f"slots with a plausible year: {used}/{slots}")

    if sndata_path.exists():
        sndata = sndata_path.read_bytes()
        print(f"\nSndata: {sndata_path} ({len(sndata)} bytes)")
        print(f"  header@0     year={read_u16(sndata, 0)} month={sndata[2]} "
              f"flags=0x{sndata[3]:02x}")
        factions = [read_u16(sndata, 6 + 2 * i) for i in range(FACTION_COUNT)]
        print(f"  factions@6   {factions}")
        print(f"  鎌倉 SJIS @ {sndata.find(KAMAKURA_SJIS)}")
        print("\nNote: the save payload starts its faction list at 24 (Sndata: 6),")
        print("but internal text offsets drift (鎌倉 lands ~2137 bytes later), so the")
        print("original slots are NOT byte-compatible with the Sndata parser. The")
        print("engine therefore writes a self-describing native snapshot (src/save.c).")


if __name__ == "__main__":
    main()

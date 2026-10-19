#!/usr/bin/env python3
"""Reverse-engineer the KOEI Genpei Kassen save container (Savedata.gp).

子任务 10: locate the exact officer/castle table offsets inside the 43403-byte
slots and explain the +2137-byte drift versus Sndata.gp.

Method
------
The save payload reuses the Sndata.gp record layouts (71-byte officers,
76-byte castles) but inserts save-only blocks. Instead of guessing offsets, the
script performs *sliding-window struct-signature correlation*: for a set of
anchor windows taken from Sndata (officer 0/1/200, castle 0/16/38, the 251-byte
faction-stats gap and the 9544-byte tail) it scans the slot for the first exact
match and derives the implied record start. Repeating this for several records
proves whether a constant shift and the 71/76-byte strides hold.

It is read-only. The engine import/export code lives in src/dos_save.c.

Usage:
    python3 tools/probe_savedata.py [Savedata.gp] [Sndata.gp] [--json out.json]
"""

import json
import struct
import sys
from pathlib import Path

SLOT_SIZE = 43403
SLOT_COUNT = 10
FACTION_COUNT = 15
MAX_OFFICERS = 401
MAX_CASTLES = 39
REC_OFFICER = 71
REC_CASTLE = 76

SND_OFF_OFFICERS = 0x0024
SND_OFF_CASTLES = 0x7056
SND_OFF_GAP = 0x6F5B
SND_GAP_SIZE = 251
SND_OFF_TAIL = 0x7BEA
SND_TAIL_SIZE = 9544

KAMAKURA_SJIS = b"\x8a\x99\x91\x71"  # 鎌倉


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def find_anchor(slot, window):
    """Return the slot offset of an exact window match, or -1."""
    idx = slot.find(window)
    return idx


def probe_used_slot(index, slot, snd, base, step):
    """Correlate one used slot against one Sndata scenario block."""
    year0, month0, flag0 = u16(slot, 0), slot[2], slot[3]
    year18, month18, flag18 = u16(slot, 18), slot[20], slot[21]
    factions = [u16(slot, 24 + 2 * i) for i in range(FACTION_COUNT)]
    summary = {
        "slot": index,
        "used": True,
        "header0": {"year": year0, "month": month0, "flag": flag0,
                    "scenario_hint": u16(slot, 4)},
        "header18": {"year": year18, "month": month18, "flag": flag18},
        "factions": factions,
        "family_sjis": slot[6:13].split(b"\x00")[0].hex(),
        "given_sjis": slot[13:18].split(b"\x00")[0].hex(),
    }

    # -- struct-signature correlation -------------------------------------
    anchors = []
    for oid in (0, 1, 2, 50, 100, 200, 300):
        snd_off = base + SND_OFF_OFFICERS + oid * REC_OFFICER
        window = snd[snd_off + 2:snd_off + 20]
        found = find_anchor(slot, window)
        anchors.append({
            "kind": "officer", "id": oid, "sndata": snd_off,
            "window_at": found, "implied_start": (found - 2) if found >= 0 else None,
        })
    for cid in (0, 5, 16, 29, 30, 38):
        snd_off = base + SND_OFF_CASTLES + cid * REC_CASTLE
        window = snd[snd_off + 2:snd_off + 22]
        found = find_anchor(slot, window)
        anchors.append({
            "kind": "castle", "id": cid, "sndata": snd_off,
            "window_at": found, "implied_start": (found - 2) if found >= 0 else None,
        })
    gap_window = snd[base + SND_OFF_GAP + 64:base + SND_OFF_GAP + 80]
    gap_at = find_anchor(slot, gap_window)
    anchors.append({
        "kind": "gap", "id": -1, "sndata": base + SND_OFF_GAP + 64,
        "window_at": gap_at,
        "implied_start": (gap_at - 64) if gap_at >= 0 else None,
    })
    tail_window = snd[base + SND_OFF_TAIL:base + SND_OFF_TAIL + 24]
    tail_at = find_anchor(slot, tail_window)
    anchors.append({
        "kind": "tail", "id": -1, "sndata": base + SND_OFF_TAIL,
        "window_at": tail_at, "implied_start": tail_at,
    })

    off_officers = next((a["implied_start"] for a in anchors
                         if a["kind"] == "officer" and a["implied_start"] is not None),
                        None)
    off_castles = next((a["implied_start"] for a in anchors
                        if a["kind"] == "castle" and a["implied_start"] is not None),
                       None)
    off_tail = tail_at if tail_at >= 0 else None

    # Verify the strides by re-checking every record's name field.
    stride_ok = {"officers": 0, "castles": 0}
    if off_officers is not None:
        stride_ok["officers"] = sum(
            1 for i in range(MAX_OFFICERS)
            if slot[off_officers + i * REC_OFFICER + 2:
                    off_officers + i * REC_OFFICER + 9]
            == snd[base + SND_OFF_OFFICERS + i * REC_OFFICER + 2:
                   base + SND_OFF_OFFICERS + i * REC_OFFICER + 9])
    if off_castles is not None:
        stride_ok["castles"] = sum(
            1 for i in range(MAX_CASTLES)
            if slot[off_castles + i * REC_CASTLE:
                    off_castles + i * REC_CASTLE + 10]
            == snd[base + SND_OFF_CASTLES + i * REC_CASTLE:
                   base + SND_OFF_CASTLES + i * REC_CASTLE + 10])

    summary["anchors"] = anchors
    summary["tables"] = {
        "officers": off_officers,
        "castles": off_castles,
        "tail": off_tail,
        "stride_matches": stride_ok,
    }

    # -- drift decomposition ---------------------------------------------
    if off_officers is not None and off_castles is not None:
        officer_end = off_officers + MAX_OFFICERS * REC_OFFICER
        header_drift = off_officers - SND_OFF_OFFICERS
        inter_drift = off_castles - officer_end - SND_GAP_SIZE
        total_drift = off_castles - SND_OFF_CASTLES
        summary["drift"] = {
            "header_bytes": header_drift,
            "inter_table_bytes": inter_drift,
            "total_castle_drift": total_drift,
            "officer_end": officer_end,
            "inter_table_size": off_castles - officer_end,
            "tail_drift": (off_tail - SND_OFF_TAIL) if off_tail else None,
        }

        # Which parts of the 251-byte Sndata gap survive inside the save gap?
        snd_gap = snd[base + SND_OFF_GAP:base + SND_OFF_GAP + SND_GAP_SIZE]
        sav_gap = slot[officer_end:off_castles]
        aligned = sum(1 for a, b in zip(snd_gap, sav_gap) if a == b)
        summary["gap"] = {
            "sndata_size": len(snd_gap),
            "save_size": len(sav_gap),
            "extra": len(sav_gap) - len(snd_gap),
            "aligned_prefix_bytes": aligned,
        }
    return summary


def main():
    argv = sys.argv[1:]
    json_out = None
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] == "--json" and i + 1 < len(argv):
            json_out = argv[i + 1]
            i += 2
            continue
        if not argv[i].startswith("--"):
            pos.append(argv[i])
        i += 1
    root = Path(__file__).resolve().parent.parent
    save_path = Path(pos[0]) if len(pos) > 0 else root / "games/yphz/Savedata.gp"
    sndata_path = Path(pos[1]) if len(pos) > 1 else root / "games/yphz/Sndata.gp"

    save = save_path.read_bytes()
    snd = sndata_path.read_bytes()
    step = len(snd) // 4

    print(f"Savedata: {save_path} ({len(save)} bytes)")
    print(f"  slots={len(save) // SLOT_SIZE} slot_size={SLOT_SIZE} "
          f"remainder={len(save) % SLOT_SIZE}")
    print(f"Sndata:   {sndata_path} ({len(snd)} bytes) scenario_step={step}\n")

    results = []
    for i in range(min(SLOT_COUNT, len(save) // SLOT_SIZE)):
        slot = save[i * SLOT_SIZE:(i + 1) * SLOT_SIZE]
        year, month = u16(slot, 0), slot[2]
        used = 1100 <= year <= 1300 and 1 <= month <= 12
        if not used:
            print(f"slot {i}: empty template (header {year}/{month})")
            results.append({"slot": i, "used": False})
            continue

        # Identify the scenario by its 15-entry faction list: the save copies it
        # into its 36-byte second header (offset 24), while the year/month at
        # offsets 18..20 are the *live* date and therefore differ from Sndata.
        match_base = 0
        for sc in range(4):
            b = sc * step
            if slot[24:54] == snd[b + 6:b + 36]:
                match_base = b
                break
        else:
            hint = u16(slot, 4)
            match_base = (hint * step) if hint < 4 else 0

        info = probe_used_slot(i, slot, snd, match_base, step)
        info["sndata_block"] = match_base
        results.append(info)

        t = info["tables"]
        print(f"slot {i}: {year}/{month}  sndata_block={match_base}")
        snd_kama = snd.find(KAMAKURA_SJIS, match_base)
        print(f"  鎌倉 SJIS @ slot {slot.find(KAMAKURA_SJIS)} "
              f"(Sndata block +{snd_kama - match_base})")
        print(f"  officer table @ {t['officers']}  "
              f"castle table @ {t['castles']}  tail @ {t['tail']}")
        print(f"  stride matches: officers {t['stride_matches']['officers']}/401, "
              f"castles {t['stride_matches']['castles']}/39")
        if "drift" in info:
            d = info["drift"]
            print(f"  drift vs Sndata: header +{d['header_bytes']} "
                  f"inter-table +{d['inter_table_bytes']} "
                  f"= total +{d['total_castle_drift']} "
                  f"(tail {d['tail_drift']:+d})")
            g = info["gap"]
            print(f"  inter-table region: {g['save_size']} B "
                  f"(Sndata {g['sndata_size']} B, extra {g['extra']} B), "
                  f"first {g['aligned_prefix_bytes']} B byte-identical")
        print()

    print("Layout (used slots):")
    print("  0x0000   18 B  summary: u16 year, u8 month, u8 flag, u16 scenario,")
    print("                 leader family SJIS[7] + given SJIS[5]")
    print("  0x0012   36 B  duplicate scenario header + 15 x u16 faction list")
    print("  0x0036   49 B  save-only faction turn-order block")
    print("  0x0067  401 x 71 B  officer table")
    print(f"  0x{0x67 + MAX_OFFICERS * REC_OFFICER:04X}  251 B  faction statistics")
    print(f"  0x{0x67 + MAX_OFFICERS * REC_OFFICER + 251:04X} 2070 B  save-only action block")
    print(f"  0x{0x67 + MAX_OFFICERS * REC_OFFICER + 251 + 2070:04X}  39 x 76 B  castle table")
    tail = 0x67 + MAX_OFFICERS * REC_OFFICER + 251 + 2070 + MAX_CASTLES * REC_CASTLE
    print(f"  0x{tail:04X} 9544 B  event/action tail")
    print(f"  total = {tail + SND_TAIL_SIZE} B (slot {SLOT_SIZE})")
    print("\nThe +2137 drift is exactly 67 (bigger save header) + 2070 (save-only")
    print("block); the record strides and the 9544-byte tail are unchanged.")

    if json_out:
        Path(json_out).write_text(json.dumps(results, indent=2, ensure_ascii=False))
        print(f"\nJSON written to {json_out}")


if __name__ == "__main__":
    main()

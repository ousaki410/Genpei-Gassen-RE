#!/usr/bin/env python3
"""Map Main.exe static 4000-segment functions to the V8 runtime memory dump.

The V8 snapshot is a 640 KiB real-mode image captured from the paused
DOSBox-X debugger.  Code that is linked as static ``4000:xxxx`` is not
necessarily executed at ``4000:xxxx``: the original DOS loader places the
4000-segment overlay at different runtime code segments.  This tool uses
masked byte signatures (far-call operands and relocated data-segment
immediates are ignored), then reports the measured, not assumed, location.

Outputs (default):
  re_out/v8/runtime_layout.json
  re_out/v8/runtime_layout.md

The tool is deliberately read-only with respect to the game binaries.  It
does not start an emulator and does not modify re_out/ runtime dumps.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

try:
    from capstone import CS_ARCH_X86, CS_MODE_16, Cs
except ImportError as exc:  # pragma: no cover - environment guard
    raise SystemExit("capstone is required: pip install capstone") from exc


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_PATH = ROOT / "re_out" / "runtime_0000_a0000.bin"
STATIC_PATH = ROOT / "games" / "yphz" / "Main.exe"
DECOMP_INDEX = ROOT / "re_out" / "decompiled" / "Main.exe" / "index.json"
STATIC_BASE = 0x30040  # file offset of Ghidra 4000:0000

# Runtime bases measured in V6/V7.  The overlays are not one contiguous
# 4000 segment; these are the two code groups used by the V8 target set.
OFFICER_CS = 0x3B1A
OFFICER_SHIFT = 0x5A3F  # runtime offset = static offset + 0x5A3F
CASTLE_CS = 0x43B3
CASTLE_SHIFT = -0x2F44  # runtime offset = static offset - 0x2F44
DS_RUNTIME = 0x619F

# static data segment, runtime data segment.  Every observed relocation is
# +0x1055.  The two "primary" entries are confirmed by the immediate inside
# the runtime 1697/4eea bodies; the remaining three are confirmed by the
# corresponding accessor bodies (45ee/4ea6/6140).
DATA_RELOCATIONS = [
    {"static": 0x451A, "runtime": 0x556F, "accessor": "1697", "body_imm": 0x556F,
     "note": "400 x 0x47 officer/unit table"},
    {"static": 0x4C1D, "runtime": 0x5C72, "accessor": "4eea", "body_imm": 0x5C72,
     "note": "39 x 0x4c castle table"},
    {"static": 0x4C09, "runtime": 0x5C5E, "accessor": "45ee", "body_imm": 0x5C5E,
     "note": "16 x 0x14 faction table"},
    {"static": 0x4CD7, "runtime": 0x5D2C, "accessor": "4ea6", "body_imm": 0x5D2C,
     "note": "8 x 0x09 region table"},
    {"static": 0x4F2C, "runtime": 0x5F81, "accessor": "6140", "body_imm": 0x5F81,
     "note": "81 x 0x16 scenario-template table"},
]

# name, static 4000 offset, runtime group, data segment, description.  The
# expected runtime location is recomputed from the group shift; signatures
# are used only to verify/select the real copy when the overlay contains
# duplicate code.
FUNCTIONS: list[dict[str, Any]] = [
    # ---- officer/unit accessors: runtime CS 3B1A ----
    {"name": "15fb", "static": 0x15FB, "group": "officer", "data_static": 0x451A,
     "note": "officer base: index*0x47; index>=400 -> 3"},
    {"name": "1697", "static": 0x1697, "group": "officer", "data_static": 0x451A,
     "note": "bulk copy of 0x47-byte officer records"},
    {"name": "185b", "static": 0x185B, "group": "officer", "note": "read word +0x1a"},
    {"name": "1871", "static": 0x1871, "group": "officer", "note": "read word +0x1c"},
    {"name": "1887", "static": 0x1887, "group": "officer", "note": "compare +0x1a/+0x1c"},
    {"name": "1957", "static": 0x1957, "group": "officer", "note": "set bit +0x21"},
    {"name": "1999", "static": 0x1999, "group": "officer", "note": "test bit +0x21"},
    # ---- castle/strategy accessors: runtime CS 43B3 ----
    {"name": "4e8e", "static": 0x4E8E, "group": "castle", "data_static": 0x4C1D,
     "note": "castle base: index*0x4c; index>=39 -> 0"},
    {"name": "4eea", "static": 0x4EEA, "group": "castle", "data_static": 0x4C1D,
     "note": "bulk copy of 0x4c-byte castle records"},
    {"name": "5396", "static": 0x5396, "group": "castle", "data_static": 0x4C1D,
     "note": "gold +0x2c read/modify/write via helper"},
    {"name": "53c4", "static": 0x53C4, "group": "castle", "data_static": 0x4C1D,
     "note": "rice/5 vs gold threshold; gold +0x2c update"},
    {"name": "5460", "static": 0x5460, "group": "castle", "data_static": 0x4C1D,
     "note": "troops +0x2e write, cap 999"},
    {"name": "5492", "static": 0x5492, "group": "castle", "data_static": 0x4C1D,
     "note": "troops +0x2e via helper, no literal cap"},
    {"name": "5500", "static": 0x5500, "group": "castle", "data_static": 0x4C1D,
     "note": "training +0x30 write, cap 100"},
    {"name": "5530", "static": 0x5530, "group": "castle", "data_static": 0x4C1D,
     "note": "training +0x30 via helper, no literal cap"},
    {"name": "55b4", "static": 0x55B4, "group": "castle", "data_static": 0x4C1D,
     "note": "morale +0x34 write, cap = defense +0x32"},
    {"name": "55ea", "static": 0x55EA, "group": "castle", "data_static": 0x4C1D,
     "note": "morale +0x34 via helper, no literal cap"},
    {"name": "4672", "static": 0x4672, "group": "castle", "data_static": 0x4C1D,
     "note": "enumerate faction castles then shuffle"},
    {"name": "5e1a", "static": 0x5E1A, "group": "castle", "data_static": 0x4C1D,
     "note": "count castle officer chain"},
    {"name": "6006", "static": 0x6006, "group": "castle", "data_static": 0x4C1D,
     "note": "build 39x39 castle distance matrix"},
    {"name": "704e", "static": 0x704E, "group": "castle", "data_static": 0x4C1D,
     "note": "template-city generation/refresh"},
    {"name": "8eba", "static": 0x8EBA, "group": "castle", "data_static": 0x4C1D,
     "note": "battle unit tick / action dispatch"},
    {"name": "8c70", "static": 0x8C70, "group": "castle", "data_static": 0x4C1D,
     "note": "battle target scoring"},
    {"name": "c07e", "static": 0xC07E, "group": "castle", "data_static": 0x4C1D,
     "note": "battle side statistics; runtime has +6 byte padding before entry"},
    # ---- auxiliary data-accessor functions used to verify relocation ----
    {"name": "45ee", "static": 0x45EE, "group": "castle", "data_static": 0x4C09,
     "note": "faction base: index*0x14; index>=16 -> 0", "aux": True},
    {"name": "4ea6", "static": 0x4EA6, "group": "castle", "data_static": 0x4CD7,
     "note": "region base: index*0x09; index>=8 -> 0", "aux": True},
    {"name": "6140", "static": 0x6140, "group": "castle", "data_static": 0x4F2C,
     "note": "template base: index*0x16; index>=81 -> 0", "aux": True},
]

COPY_HELPER = {
    "name": "copy_544f_0290",
    "file_offset": 0x43BB5,
    "runtime_l": 0x544F0 + 0x0290,
    "runtime_cs": 0x544F,
    "runtime_ip": 0x0290,
    "note": "copy helper reached as lcall 544F:0290 (static file 0x43BB5 / Ghidra 5000:3B75)",
}

# Confirmed pointer tables in the 619F DGROUP data segment.  The addresses
# are linear offsets; the DS:offset form is shown in the generated report.
TABLES = [
    {"name": "stat_getters", "ds": DS_RUNTIME, "off": 0x0BF8, "count": 12,
     "note": "mixed stat table; first three entries point into 1055 data, +0x0c is 5e1a"},
    {"name": "stat_setters", "ds": DS_RUNTIME, "off": 0x5358, "count": 12,
     "note": "stat setter table; +0x10=55b4, +0x14=5460, +0x18=5500"},
    {"name": "vtable_base", "ds": DS_RUNTIME, "off": 0x5DF2, "count": 12,
     "note": "base object vtable; +0x18=3B1A:1E88, +0x1c=3B1A:1F66"},
    {"name": "vtable_derived", "ds": DS_RUNTIME, "off": 0x6362, "count": 12,
     "note": "derived vtable; +0x18=1055:3696 override, +0x1c=3B1A:1F66"},
]

# Exact update formulas recovered from the runtime bodies.  `x` is the
# function's second argument; all stores go through the named setter, so the
# setter's clamp is included.  The business names are not inferred here.
FIELD_FORMULAS = [
    {"name": "5362", "field": "+0x2c gold", "role": "write",
     "formula": "gold = min(rice, value)",
     "evidence": "43B3:241E: 5336 (rice) -> lcall 3B1A:596E (min) -> [si+0x2c]"},
    {"name": "5396", "field": "+0x2c gold", "role": "subtract",
     "formula": "gold = min(rice, max(0, gold - x))",
     "evidence": "534c -> 3B1A:5b0c (subtract floor 0) -> 5362"},
    {"name": "53c4", "field": "+0x2c gold", "role": "conditional percentage update",
     "formula": "if rice/5 <= gold: gold = min(rice/5, (gold * x) / 100)",
     "evidence": "rice/5 branch; 3B1A:5a38 (x*gold/100), 3B1A:597e (min), 5362"},
    {"name": "5420", "field": "+0x2e troops", "role": "read",
     "formula": "return [castle+0x2e]", "evidence": "43B3:24dc"},
    {"name": "5436", "field": "+0x2e troops", "role": "write",
     "formula": "troops = clamp(value, 0, 999)",
     "evidence": "43B3:24F2 -> 43B3:203A, cap 0x3e7"},
    {"name": "5460", "field": "+0x2e troops", "role": "add",
     "formula": "troops = clamp(troops + x, 0, 999)",
     "evidence": "5420 -> 3B1A:5af8 (add cap 999) -> 5436"},
    {"name": "5492", "field": "+0x2e troops", "role": "subtract",
     "formula": "troops = clamp(troops - x, 0, 999)",
     "evidence": "5420 -> 3B1A:5b0c (subtract floor 0) -> 5436"},
    {"name": "54c0", "field": "+0x30 training", "role": "read",
     "formula": "return [castle+0x30]", "evidence": "43B3:257c"},
    {"name": "54d6", "field": "+0x30 training", "role": "write",
     "formula": "training = clamp(value, 0, 100)",
     "evidence": "43B3:2592 -> 43B3:2028, cap 0x64"},
    {"name": "5500", "field": "+0x30 training", "role": "add",
     "formula": "training = clamp(training + x, 0, 100)",
     "evidence": "54c0 -> 3B1A:5af8 (add cap 100) -> 54d6"},
    {"name": "5530", "field": "+0x30 training", "role": "subtract",
     "formula": "training = clamp(training - x, 0, 100)",
     "evidence": "54c0 -> 3B1A:5b0c (subtract floor 0) -> 54d6"},
    {"name": "5574", "field": "+0x34 morale", "role": "read",
     "formula": "return [castle+0x34]", "evidence": "43B3:2630"},
    {"name": "558a", "field": "+0x34 morale", "role": "write",
     "formula": "morale = clamp(value, 0, 999)",
     "evidence": "43B3:2646 -> 43B3:203a, cap 0x3e7"},
    {"name": "55b4", "field": "+0x34 morale", "role": "add with defense cap",
     "formula": "morale = clamp(morale + x, 0, min(defense, 999))",
     "evidence": "5574 + 555e -> 3B1A:5af8 (cap=defense) -> 558a"},
    {"name": "55ea", "field": "+0x34 morale", "role": "subtract",
     "formula": "morale = clamp(morale - x, 0, 999)",
     "evidence": "5574 -> 3B1A:5b0c (subtract floor 0) -> 558a"},
]


def md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def load_index() -> list[dict[str, Any]]:
    if not DECOMP_INDEX.exists():
        return []
    data = json.loads(DECOMP_INDEX.read_text(encoding="utf-8"))
    out: list[dict[str, Any]] = []
    for item in data.get("functions", []):
        try:
            seg, off = (int(x, 16) for x in item["entry"].split(":"))
        except (KeyError, ValueError):
            continue
        out.append({"seg": seg, "off": off, "size": int(item.get("size", 0)),
                    "name": item.get("name", ""), "item": item})
    return out


def containing_function(index: list[dict[str, Any]], seg: int, off: int) -> dict[str, Any] | None:
    best = None
    for fn in index:
        if fn["seg"] == seg and fn["off"] <= off < fn["off"] + max(fn["size"], 1):
            if best is None or fn["off"] > best["off"]:
                best = fn
    return best


def signature(blob: bytes, start: int, length: int = 24) -> tuple[bytes, bytes]:
    """Return masked pattern/mask for one static function prefix.

    Far-call operands (``9A`` + 4 bytes) and the 16-bit immediate of
    ``BA``/``68`` are masked.  The latter is how the relocated data segment
    (e.g. static 451A -> runtime 556F) is handled without force-fitting.
    """
    pat = bytearray(blob[start:start + length])
    mask = bytearray(b"\x01" * len(pat))
    for i in range(len(pat)):
        if pat[i] == 0x9A:
            for j in range(1, 5):
                if i + j < len(mask):
                    mask[i + j] = 0
        if pat[i] in (0xBA, 0x68, 0xB8) and i + 2 < len(pat):
            # Only mask the immediate; keep opcode and surrounding bytes.
            mask[i + 1] = 0
            mask[i + 2] = 0
    return bytes(pat), bytes(mask)


def find_masked(blob: bytes, pat: bytes, mask: bytes) -> list[int]:
    hits: list[int] = []
    plen = len(pat)
    for i in range(0, len(blob) - plen + 1):
        ok = True
        for j, (p, m) in enumerate(zip(pat, mask)):
            if m and blob[i + j] != p:
                ok = False
                break
        if ok:
            hits.append(i)
    return hits


def expected_location(fn: dict[str, Any]) -> tuple[int, int]:
    static = fn["static"]
    if fn["group"] == "officer":
        ip = (static + OFFICER_SHIFT) & 0xFFFF
        return OFFICER_CS, ip
    ip = (static + CASTLE_SHIFT) & 0xFFFF
    return CASTLE_CS, ip


def expected_linear(fn: dict[str, Any]) -> int:
    cs, ip = expected_location(fn)
    return cs * 16 + ip


def static_bytes() -> bytes:
    return STATIC_PATH.read_bytes()


def locate_functions(static: bytes) -> list[dict[str, Any]]:
    runtime = RUNTIME_PATH.read_bytes()
    out: list[dict[str, Any]] = []
    for fn in FUNCTIONS:
        src = static
        src_start = STATIC_BASE + fn["static"]
        pat, mask = signature(src, src_start)
        hits = find_masked(runtime, pat, mask)
        expected_l = expected_linear(fn)
        # Prefer the hit exactly at the expected location; otherwise the
        # nearest hit.  This resolves duplicate copies of 5396/5492 and the
        # six-byte-before-c07e padding exception.
        chosen = min(hits, key=lambda x: abs(x - expected_l)) if hits else None
        actual_cs = fn["group"] == "officer" and OFFICER_CS or CASTLE_CS
        actual_ip = (chosen - actual_cs * 16) & 0xFFFF if chosen is not None else None
        actual_off_file = STATIC_BASE + fn["static"]
        item = {
            "name": fn["name"], "static": fn["static"],
            "static_cs_ip": f"4000:{fn['static']:04X}",
            "off_file": actual_off_file,
            "expected_l": expected_l,
            "expected_cs_ip": f"{expected_location(fn)[0]:04X}:{expected_location(fn)[1]:04X}",
            "candidates": hits,
            "l_run": chosen,
            "runtime_cs_ip": f"{actual_cs:04X}:{actual_ip:04X}" if actual_ip is not None else None,
            "group": fn["group"],
            "note": fn["note"],
            "data_static": fn.get("data_static"),
            "aux": bool(fn.get("aux")),
            "delta_from_expected": (chosen - expected_l) if chosen is not None else None,
        }
        out.append(item)
    return out


def disasm_at(blob: bytes, lin: int, cs: int, ip: int, max_bytes: int = 96) -> list[dict[str, Any]]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    lines: list[dict[str, Any]] = []
    for ins in md.disasm(blob[lin:lin + max_bytes], ip):
        lines.append({"addr": f"{cs:04X}:{ins.address:04X}", "bytes": ins.bytes.hex(),
                      "mnemonic": ins.mnemonic, "op_str": ins.op_str})
        if ins.mnemonic in ("ret", "retf", "iret") and ins.address > ip:
            break
    return lines


def body_immediates(blob: bytes, lin: int, cs: int, ip: int) -> list[int]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    vals: list[int] = []
    for ins in md.disasm(blob[lin:lin + 96], ip):
        if ins.mnemonic in ("mov", "push") and ins.op_str:
            parts = ins.op_str.split(",")
            last = parts[-1].strip()
            if last.startswith("0x"):
                try:
                    vals.append(int(last, 16))
                except ValueError:
                    pass
        if ins.mnemonic in ("ret", "retf") and ins.address > ip:
            break
    return vals


def find_copy_helper(static: bytes, runtime: bytes) -> dict[str, Any]:
    src_start = COPY_HELPER["file_offset"]
    pat, mask = signature(static, src_start, 24)
    hits = find_masked(runtime, pat, mask)
    expected = COPY_HELPER["runtime_l"]
    chosen = min(hits, key=lambda x: abs(x - expected)) if hits else None
    return {
        **COPY_HELPER,
        "candidates": hits,
        "l_run_measured": chosen,
        "runtime_cs_ip_measured": f"{COPY_HELPER['runtime_cs']:04X}:{(chosen - COPY_HELPER['runtime_cs'] * 16) & 0xFFFF:04X}" if chosen is not None else None,
    }


def load_tables(runtime: bytes) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for table in TABLES:
        base = table["ds"] * 16 + table["off"]
        entries: list[dict[str, Any]] = []
        for i in range(table["count"]):
            off, seg = struct.unpack_from("<HH", runtime, base + i * 4)
            entries.append({"index": i, "offset": off, "segment": seg,
                            "linear": (seg << 4) + off})
        out.append({**table, "linear": base, "entries": entries})
    return out


def detect_caller_segment(lin: int) -> int | None:
    # The two native code groups overlap in physical memory; nevertheless the
    # call-site address itself identifies the active overlay for the V8 set.
    # A far call can also live in a third overlay (for example the 0x2286x
    # code used by the 5e1a callers).  Do not invent a CS for those: return
    # None and keep the measured linear call-site address.
    if 0x43B30 <= lin < 0x53B30:
        return CASTLE_CS
    if 0x3B1A0 <= lin < 0x4B1A0:
        return OFFICER_CS
    return None


def caller_static(cs: int, ip: int) -> int | None:
    if cs == OFFICER_CS:
        return (ip - OFFICER_SHIFT) & 0xFFFF
    if cs == CASTLE_CS:
        return (ip + 0x2F44) & 0xFFFF
    return None


def scan_callers(runtime: bytes, located: list[dict[str, Any]], index: list[dict[str, Any]]) -> dict[str, Any]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    far_calls: list[tuple[int, int, int, int]] = []
    near_calls: list[tuple[int, int]] = []
    for i in range(len(runtime) - 5):
        if runtime[i] == 0x9A:
            ins = next(md.disasm(runtime[i:i + 5], i, count=1), None)
            if ins and ins.mnemonic == "lcall":
                try:
                    a, b = ins.op_str.split(",")
                    seg, off = int(a, 16), int(b, 16)
                except ValueError:
                    continue
                far_calls.append((i, seg, off, (seg << 4) + off))
        if runtime[i] == 0xE8:
            rel = struct.unpack_from("<h", runtime, i + 1)[0]
            near_calls.append((i, i + 3 + rel))

    out: dict[str, Any] = {}
    for fn in located:
        if fn["l_run"] is None:
            out[fn["name"]] = {"far": [], "near": [], "pointer_tables": []}
            continue
        target_l = fn["l_run"]
        target_cs = OFFICER_CS if fn["group"] == "officer" else CASTLE_CS
        calls: list[dict[str, Any]] = []
        for site, seg, off, lin in far_calls:
            if lin != target_l:
                continue
            cs = detect_caller_segment(site)
            if cs is None:
                calls.append({"kind": "far", "site_l": site, "cs": None, "ip": None,
                              "static_ref": None, "caller_function": None,
                              "caller_entry_static": None,
                              "caller_segment_note": "call site outside measured 3B1A/43B3 ranges"})
                continue
            ip = (site - cs * 16) & 0xFFFF
            st = caller_static(cs, ip)
            cfn = containing_function(index, 0x4000, st) if st is not None else None
            calls.append({"kind": "far", "site_l": site, "cs": cs, "ip": ip,
                          "static_ref": st, "caller_function": cfn["name"] if cfn else None,
                          "caller_entry_static": f"4000:{cfn['off']:04X}" if cfn else None})
        for site, target in near_calls:
            if target != target_l:
                continue
            cs = target_cs
            ip = (site - cs * 16) & 0xFFFF
            st = caller_static(cs, ip)
            cfn = containing_function(index, 0x4000, st) if st is not None else None
            calls.append({"kind": "near", "site_l": site, "cs": cs, "ip": ip,
                          "static_ref": st, "caller_function": cfn["name"] if cfn else None,
                          "caller_entry_static": f"4000:{cfn['off']:04X}" if cfn else None})
        # pointer-table entries whose target is exactly this function
        ptr_hits: list[dict[str, Any]] = []
        for table in load_tables(runtime):
            for entry in table["entries"]:
                if entry["linear"] == target_l:
                    ptr_hits.append({"table": table["name"], "ds": table["ds"],
                                     "off": table["off"], "index": entry["index"],
                                     "entry": f"{entry['segment']:04X}:{entry['offset']:04X}"})
        out[fn["name"]] = {"far": [c for c in calls if c["kind"] == "far"],
                           "near": [c for c in calls if c["kind"] == "near"],
                           "pointer_tables": ptr_hits,
                           "total": len(calls)}
    return out


def representative_disassembly(runtime: bytes, located: list[dict[str, Any]], callers: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for fn in located:
        if fn["l_run"] is None:
            continue
        cs, ip = fn["runtime_cs_ip"].split(":")
        cs_i, ip_i = int(cs, 16), int(ip, 16)
        body = disasm_at(runtime, fn["l_run"], cs_i, ip_i, 112)
        reps: list[dict[str, Any]] = []
        for c in callers[fn["name"]]["far"][:2] + callers[fn["name"]]["near"][:2]:
            if c["cs"] is None:
                reps.append({"kind": c["kind"], "site_l": c["site_l"],
                             "cs_ip": "unknown:unknown",
                             "caller_function": None, "lines": []})
                continue
            start = max(0, c["site_l"] - 12)
            # Disassemble from a nearby prologue if possible, otherwise from
            # the call site itself.
            lines = disasm_at(runtime, start, c["cs"], (start - c["cs"] * 16) & 0xFFFF, 32)
            reps.append({"kind": c["kind"], "site_l": c["site_l"],
                         "cs_ip": f"{c['cs']:04X}:{c['ip']:04X}",
                         "caller_function": c["caller_function"],
                         "lines": lines})
        out[fn["name"]] = {"body": body, "callers": reps}
    return out


def call_scan_totals(runtime: bytes) -> dict[str, int]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    far = near = 0
    for i in range(len(runtime) - 5):
        if runtime[i] == 0x9A:
            ins = next(md.disasm(runtime[i:i + 5], i, count=1), None)
            if ins and ins.mnemonic == "lcall":
                far += 1
        if runtime[i] == 0xE8:
            near += 1
    return {"far_lcall_9a": far, "near_call_e8": near}


def verify_data_relocations(runtime: bytes, located: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_name = {f["name"]: f for f in located}
    out: list[dict[str, Any]] = []
    for rel in DATA_RELOCATIONS:
        fn = by_name.get(rel["accessor"])
        found = False
        if fn and fn["l_run"] is not None:
            cs, ip = fn["runtime_cs_ip"].split(":")
            vals = body_immediates(runtime, fn["l_run"], int(cs, 16), int(ip, 16))
            found = rel["body_imm"] in vals
        out.append({**rel, "delta": rel["runtime"] - rel["static"],
                    "body_immediate_confirmed": found,
                    "accessor_runtime": fn["runtime_cs_ip"] if fn else None})
    return out


def object_tables(runtime: bytes) -> dict[str, Any]:
    # 3B1A:1F25/1FA5 are call sites, not tables.  The actual object vtable is
    # pointed at by the constructor's immediate 0x5DF2.
    base = 0x3B1A * 16
    call_sites = {
        "1F25": {"l": base + 0x1F25, "containing_function": "3B1A:1E88",
                 "instruction": "lcall [bx+0x18]", "slot": 0x18},
        "1FA5": {"l": base + 0x1FA5, "containing_function": "3B1A:1F66",
                 "instruction": "lcall [bx+0x1c]", "slot": 0x1C},
    }
    vtables = []
    for table in load_tables(runtime):
        if table["name"] in ("vtable_base", "vtable_derived"):
            vtables.append({"name": table["name"], "ds": table["ds"],
                            "off": table["off"], "linear": table["linear"],
                            "entries": table["entries"][:12]})
    return {
        "call_sites": call_sites,
        "constructor": {"cs": "3B1A", "ip": "19E8", "l": base + 0x19E8,
                        "writes_vtable_word": 0x5DF2,
                        "bytes": runtime[base + 0x19E8:base + 0x19F8].hex(" ")},
        "vtables": vtables,
    }


def summarize_markdown(result: dict[str, Any]) -> str:
    lines: list[str] = []
    add = lines.append
    add("# V8 runtime layout map\n")
    add(f"- Runtime: `{RUNTIME_PATH.relative_to(ROOT)}` MD5 `{result['runtime_md5']}`")
    add(f"- Static: `{STATIC_PATH.relative_to(ROOT)}` MD5 `{result['static_md5']}`")
    add("- Method: masked signature; relocated far-call operands and data-segment immediates ignored.")
    add(f"- Scan totals: `9A lcall` = {result['scan_totals']['far_lcall_9a']}, "
        f"`E8 near call` = {result['scan_totals']['near_call_e8']}.\n")
    add("## Function map\n")
    add("| function | static | file offset | expected | measured L_run | runtime CS:IP | data | delta |")
    add("|---|---:|---:|---|---:|---|---|---:|")
    for fn in result["functions"]:
        data = f"{fn['data_static']:04X}" if fn["data_static"] else "-"
        delta = "0" if fn["delta_from_expected"] == 0 else str(fn["delta_from_expected"])
        add(f"| `{fn['name']}` | `{fn['static_cs_ip']}` | `0x{fn['off_file']:05X}` | "
            f"`{fn['expected_cs_ip']}` | `0x{fn['l_run']:05X}` | `{fn['runtime_cs_ip']}` | "
            f"`{data}` | {delta} |")
    add("\n## Data-segment relocations\n")
    add("| static | runtime | delta | accessor | body immediate | confirmed |")
    add("|---:|---:|---:|---|---:|---|")
    for rel in result["data_relocations"]:
        add(f"| `{rel['static']:04X}` | `{rel['runtime']:04X}` | `+0x{rel['delta']:X}` | "
            f"`{rel['accessor']}` | `0x{rel['body_imm']:04X}` | "
            f"{'yes' if rel['body_immediate_confirmed'] else 'no'} |")
    helper = result["copy_helper"]
    add(f"\nCopy helper: `{helper['runtime_cs']:04X}:{helper['runtime_ip']:04X}` "
        f"(L=`0x{helper['l_run_measured']:05X}`, static file `0x{helper['file_offset']:05X}`); "
        f"measured entry `{helper['runtime_cs_ip_measured']}`.")
    add("\n## Stat pointer tables\n")
    name_by_l = {f["l_run"]: f["name"] for f in result["functions"] if f["l_run"] is not None}
    for table in result["tables"]:
        if table["name"] not in ("stat_getters", "stat_setters"):
            continue
        add(f"\n### `{table['name']}` at `{table['ds']:04X}:{table['off']:04X}` "
            f"(L=`0x{table['linear']:05X}`, {table['count']} entries)")
        add("| index | slot | entry | linear | target |")
        add("|---:|---:|---|---:|---|")
        for e in table["entries"]:
            target = name_by_l.get(e["linear"], "")
            add(f"| {e['index']} | +0x{e['index'] * 4:02X} | "
                f"`{e['segment']:04X}:{e['offset']:04X}` | `0x{e['linear']:05X}` | "
                f"{('`' + target + '`') if target else '-'} |")
    add("\n## Exact field update formulas\n")
    add("| function | field | role | formula | evidence |")
    add("|---|---|---|---|---|")
    for f in result["field_formulas"]:
        add(f"| `{f['name']}` | `{f['field']}` | {f['role']} | `{f['formula']}` | {f['evidence']} |")
    add("\n## Object tables / vtable\n")
    obj = result["object_tables"]
    add(f"- Constructor: `{obj['constructor']['cs']}:{obj['constructor']['ip']}` "
        f"writes `[bx] = 0x{obj['constructor']['writes_vtable_word']:04X}`.")
    for key, site in obj["call_sites"].items():
        add(f"- `3B1A:{key}` is a **call site inside {site['containing_function']}**, "
            f"not a function entry/table: `{site['instruction']}` (slot +0x{site['slot']:X}).")
    for vt in obj["vtables"]:
        add(f"\n### `{vt['name']}` at `{vt['ds']:04X}:{vt['off']:04X}` (L=`0x{vt['linear']:05X}`)")
        add("| slot | entry | linear |")
        add("|---:|---|---:|")
        for e in vt["entries"]:
            add(f"| +0x{e['index'] * 4:02X} | `{e['segment']:04X}:{e['offset']:04X}` | `0x{e['linear']:05X}` |")
    add("\n## Caller scan\n")
    add("| function | far | near | pointer-table entries |")
    add("|---|---:|---:|---|")
    for fn in result["functions"]:
        c = result["callers"][fn["name"]]
        ptrs = ", ".join(f"{p['table']}[{p['index']}]" for p in c["pointer_tables"]) or "-"
        add(f"| `{fn['name']}` | {len(c['far'])} | {len(c['near'])} | {ptrs} |")
    add("\n## Notes\n")
    add("- `c07e` has six bytes of padding before its runtime entry; its measured")
    add("  offset is `43B3:9140`, six bytes after the uniform formula `43B3:913A`.")
    add("- `1697` and `4eea` have no direct call sites in this snapshot; they are")
    add("  bulk-copy helpers and may only be reached through indirect entries.")
    add("- `5396`/`5492` have duplicate signature copies; the tool selects the copy")
    add("  at the expected runtime location (candidate lists are in the JSON).")
    add("- `3B1A:1F25/1FA5` are interior call sites; see the vtable section.")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", type=Path, default=ROOT / "re_out" / "v8" / "runtime_layout.json")
    ap.add_argument("--md", type=Path, default=ROOT / "re_out" / "v8" / "runtime_layout.md")
    args = ap.parse_args()

    if not RUNTIME_PATH.exists() or not STATIC_PATH.exists():
        raise SystemExit("missing runtime dump or Main.exe")
    runtime = RUNTIME_PATH.read_bytes()
    static = static_bytes()
    index = load_index()

    located = locate_functions(static)
    copy_helper = find_copy_helper(static, runtime)
    callers = scan_callers(runtime, located, index)
    disasm = representative_disassembly(runtime, located, callers)
    relocs = verify_data_relocations(runtime, located)
    obj = object_tables(runtime)
    tables = load_tables(runtime)
    scan_totals = call_scan_totals(runtime)

    failures: list[str] = []
    for fn in located:
        if fn["l_run"] is None:
            failures.append(f"{fn['name']}: no signature hit")
            continue
        if abs(fn["delta_from_expected"]) > 0x20:
            failures.append(f"{fn['name']}: unexpected delta {fn['delta_from_expected']}")
    for rel in relocs:
        if not rel["body_immediate_confirmed"]:
            failures.append(f"data relocation {rel['static']:04X}: immediate not found")

    result = {
        "runtime_md5": md5(RUNTIME_PATH),
        "static_md5": md5(STATIC_PATH),
        "static_base": STATIC_BASE,
        "runtime_size": len(runtime),
        "functions": located,
        "copy_helper": copy_helper,
        "data_relocations": relocs,
        "object_tables": obj,
        "tables": tables,
        "field_formulas": FIELD_FORMULAS,
        "scan_totals": scan_totals,
        "callers": callers,
        "disassembly": disasm,
        "failures": failures,
    }

    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    args.md.write_text(summarize_markdown(result), encoding="utf-8")

    print(f"wrote {args.json.relative_to(ROOT)}")
    print(f"wrote {args.md.relative_to(ROOT)}")
    for fn in located:
        if fn["l_run"] is not None:
            print(f"{fn['name']:>6}  {fn['runtime_cs_ip']}  L=0x{fn['l_run']:05X}  file=0x{fn['off_file']:05X}")
    if failures:
        print("\nFAILURES:")
        for f in failures:
            print(" -", f)
        return 1
    print("\nall targets located; data immediate checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""V9 offline tracer for Main.exe runtime logic.

Reads only re_out/runtime_0000_a0000.bin (640 KiB real-mode image) and the
Ghidra function index.  It never starts an emulator.

Outputs:
  re_out/v9/trace.json
  re_out/v9/trace.md

What it does:
  * scans the code segments 3B1A/43B3/1055/38CD/544F for references to the
    stat dispatch tables 619F:0BF8..0C24 and 619F:5358..5384;
  * clusters every E8 call to 43B3:1F4A (4000:4e8e) by containing 4000
    function and reports the castle/officer offsets touched by the caller;
  * lists every far call to 43B3:2ED6 (4000:5e1a) with a local prologue
    heuristic (the calls are in an as-yet unnamed third overlay);
  * emits full disassembly of the battle cluster 8c70/8eba/c07e/6140.
"""

from __future__ import annotations

import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "re_out" / "runtime_0000_a0000.bin"
INDEX = ROOT / "re_out" / "decompiled" / "Main.exe" / "index.json"
OUTDIR = ROOT / "re_out" / "v9"
STATIC_BASE = 0x30040
CASTLE_CS = 0x43B3
CASTLE_SHIFT = -0x2F44
# The tables are indexed from these two base displacements.  Scanning every
# entry displacement (0x0BF8+4k) mostly matches ordinary immediates such as
# 0x0C00, so V9 reports only exact base-displacement operands; the full entry
# list is decoded separately from memory.
TABLE_TARGETS = {
    "stat_getters": (0x0BF8, 0x0BF8),
    "stat_setters": (0x5358, 0x5358),
}
CODE_SEGMENTS = {
    "3B1A": 0x3B1A0,
    "43B3": 0x43B30,
    "1055": 0x10550,
    "38CD": 0x38CD0,
    "544F": 0x544F0,
}
BATTLE_TARGETS = {
    "8c70": 0x4985C,
    "8eba": 0x49AA6,
    "6140": 0x46D2C,
    "c07e": 0x4CC70,
    "4e8e": 0x45A7A,
    "5e1a": 0x46A06,
}


def load_index():
    if not INDEX.exists():
        return []
    data = json.loads(INDEX.read_text(encoding="utf-8"))
    out = []
    for item in data.get("functions", []):
        try:
            seg, off = (int(x, 16) for x in item["entry"].split(":"))
        except Exception:
            continue
        out.append({"seg": seg, "off": off, "size": int(item.get("size", 0)),
                    "name": item.get("name", ""), "callers": item.get("callers", [])})
    return out


def containing(index, seg, off):
    best = None
    for f in index:
        if f["seg"] == seg and f["off"] <= off < f["off"] + max(f["size"], 1):
            if best is None or f["off"] > best["off"]:
                best = f
    return best


def disasm(blob, lin, cs, ip, length=128):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    lines = []
    for ins in md.disasm(blob[lin:lin + length], ip):
        lines.append({"cs": cs, "ip": ins.address, "lin": (cs << 4) + ins.address,
                      "bytes": ins.bytes.hex(" "), "text": f"{ins.mnemonic} {ins.op_str}".strip()})
        if ins.mnemonic in ("ret", "retf", "iret") and ins.address > ip:
            break
    return lines


def function_disasm(blob, lin, cs, ip, max_bytes=0x400):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    lines = []
    for ins in md.disasm(blob[lin:lin + max_bytes], ip):
        lines.append({"cs": cs, "ip": ins.address, "lin": (cs << 4) + ins.address,
                      "bytes": ins.bytes.hex(" "), "text": f"{ins.mnemonic} {ins.op_str}".strip()})
        if ins.mnemonic in ("ret", "retf", "iret") and ins.address > ip:
            break
    return lines


def find_prologue(blob, lin, max_back=0x120):
    # Search backwards for the usual 55 8B EC / C8 xx xx 00 prologue.
    start = max(0, lin - max_back)
    for p in range(lin, start, -1):
        if blob[p:p + 3] == b"\x55\x8b\xec" or blob[p] == 0xc8:
            return p
    return lin


def scan_table_refs(blob, index):
    """Find real instruction operands equal to a dispatch-table address.

    Every Ghidra-indexed 4000 function is disassembled from its entry, so an
    immediate/displacement is only reported when Capstone decodes it as an
    operand at a true instruction boundary.  Relying on raw ``F8 0B`` byte
    hits produces hundreds of false positives (the bytes occur inside normal
    instructions); V9 therefore reports the exact-operand result only.
    """
    hits = []
    wanted = {}
    for name, (lo, hi) in TABLE_TARGETS.items():
        for v in range(lo, hi + 1, 4):
            wanted[v] = name
    static = (ROOT / "games" / "yphz" / "Main.exe").read_bytes()
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    for f in index:
        off, size = f["off"], max(f["size"], 1)
        start = STATIC_BASE + off
        code = static[start:start + size]
        if not code:
            continue
        for ins in md.disasm(code, off):
            values = []
            for op in ins.operands:
                if op.type == X86_OP_IMM and op.imm in wanted:
                    values.append((op.imm, "imm"))
                elif op.type == X86_OP_MEM and op.mem.disp in wanted:
                    values.append((op.mem.disp, "disp"))
            for value, kind in values:
                hits.append({"table": wanted[value], "value": value,
                             "segment": "4000", "cs": 0x4000,
                             "ip": ins.address, "lin": None,
                             "bytes": ins.bytes.hex(" "),
                             "instruction": f"{ins.mnemonic} {ins.op_str}".strip(),
                             "operand": kind, "method": "indexed",
                             "caller_function": f["name"],
                             "caller_entry": f"4000:{f['off']:04X}"})
    # Deduplicate: one instruction may contain two table operands.
    out, seen = [], set()
    for h in hits:
        k = (h["cs"], h["ip"], h["table"])
        if k in seen:
            continue
        seen.add(k)
        out.append(h)
    return out


def parse_table_entries(blob):
    """Decode the two 12-entry seg:off pointer tables."""
    tables = []
    for name, off in (("getters", 0x0BF8), ("setters", 0x5358)):
        base = 0x619F0 + off
        entries = []
        for i in range(12):
            o, s = struct.unpack_from("<HH", blob, base + i * 4)
            entries.append({"index": i, "offset": o, "segment": s,
                            "linear": (s << 4) + o,
                            "cs_ip": f"{s:04X}:{o:04X}"})
        tables.append({"name": name, "cs": 0x619F, "offset": off,
                       "entries": entries})
    return tables


def scan_calls(blob):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    far, near = [], []
    for i in range(len(blob) - 5):
        if blob[i] == 0x9A:
            ins = next(md.disasm(blob[i:i + 5], i, count=1), None)
            if ins and ins.mnemonic == "lcall":
                try:
                    a, b = ins.op_str.split(",")
                    s, o = int(a, 16), int(b, 16)
                except ValueError:
                    continue
                far.append({"site": i, "seg": s, "off": o, "target": (s << 4) + o})
        if blob[i] == 0xE8:
            rel = struct.unpack_from("<h", blob, i + 1)[0]
            near.append({"site": i, "target_rel": i + 3 + rel})
    return far, near


def cluster_castle_callers(blob, index, near):
    clusters = defaultdict(lambda: {"entry": None, "name": None, "sites": [], "fields": []})
    target = BATTLE_TARGETS["4e8e"]
    for call in near:
        if call["target_rel"] != target:
            continue
        site = call["site"]
        ip = (site - CASTLE_CS * 16) & 0xFFFF
        static = (ip + 0x2F44) & 0xFFFF
        f = containing(index, 0x4000, static)
        key = (f["off"], f["name"], f["size"]) if f else (None, f"linear:{site:05x}", 0)
        c = clusters[key]
        c["entry"] = f"4000:{f['off']:04X}" if f else None
        c["name"] = f["name"] if f else None
        c["sites"].append({"site_l": site, "cs_ip": f"43B3:{ip:04X}",
                           "static_ref": f"4000:{static:04X}"})
        if f:
            # inspect the containing function for field displacements
            lines = function_disasm(blob, CASTLE_CS * 16 + ((f["off"] - 0x2F44) & 0xFFFF),
                                    CASTLE_CS, (f["off"] - 0x2F44) & 0xFFFF,
                                    max_bytes=min(max(f["size"], 1), 0x300))
            for ln in lines:
                for field in ("0x1a", "0x1c", "0x21", "0x2a", "0x2c", "0x2e",
                              "0x30", "0x32", "0x34"):
                    if f"+ {field}" in ln["text"] or f"+{field}" in ln["text"]:
                        c["fields"].append({"field": field, "instruction": ln["text"],
                                            "cs_ip": f"{ln['cs']:04X}:{ln['ip']:04X}"})
    for c in clusters.values():
        # unique fields
        uniq = {}
        for x in c["fields"]:
            uniq[(x["field"], x["instruction"])] = x
        c["fields"] = list(uniq.values())
        c["count"] = len(c["sites"])
    return sorted(clusters.values(), key=lambda c: (-c["count"], c["entry"] or ""))


def aggregate_high_level(index, castle_clusters):
    """Walk one caller edge above the 46 4e8e accessor wrappers."""
    by_name = {f["name"]: f for f in index}
    agg = {}
    for c in castle_clusters:
        w = c.get("name")
        if not w or w not in by_name:
            continue
        for caller in by_name[w].get("callers", []):
            try:
                off = int(caller.split(":")[1], 16)
            except (IndexError, ValueError):
                continue
            f = next((x for x in index if x["off"] == off and x["seg"] == 0x4000), None)
            key = f["name"] if f else caller
            row = agg.setdefault(key, {"name": key, "entry": caller,
                                       "accessors": set(), "fields": set()})
            row["accessors"].add(w)
            for fld in c.get("fields", []):
                row["fields"].add(fld["field"])
    out = []
    for row in agg.values():
        out.append({"name": row["name"], "entry": row["entry"],
                    "accessor_count": len(row["accessors"]),
                    "accessors": sorted(row["accessors"]),
                    "fields": sorted(row["fields"])})
    return sorted(out, key=lambda r: (-r["accessor_count"], r["name"]))


def cluster_5e1a(blob, index, far):
    out = []
    for call in far:
        if call["target"] != BATTLE_TARGETS["5e1a"]:
            continue
        site = call["site"]
        pro = find_prologue(blob, site)
        pcs = pro >> 4
        pip = pro & 0xF
        out.append({"site_l": site, "prologue_l": pro,
                    "prologue_cs_ip": f"{pcs:04X}:{pip:04X}",
                    "prologue_bytes": blob[pro:pro + 8].hex(" "),
                    "lines": function_disasm(blob, pro, pcs, pip, 160)})
    return out


def extract_templates(blob):
    """Decode the 81 x 22-byte table at runtime 5f81:0000."""
    base = 0x5F810
    count, stride = 81, 22
    records = [blob[base + i * stride:base + (i + 1) * stride]
               for i in range(count)]
    return {"segment": 0x5F81, "offset": 0, "linear": base,
            "count": count, "stride": stride,
            "length": count * stride,
            "unique_records": len(set(records)),
            "sha256": __import__("hashlib").sha256(blob[base:base + count * stride]).hexdigest(),
            "records": [r.hex() for r in records]}


def main():
    if not RUNTIME.exists():
        raise SystemExit("missing runtime dump")
    blob = RUNTIME.read_bytes()
    index = load_index()
    far, near = scan_calls(blob)
    result = {
        "runtime_md5": __import__("hashlib").md5(blob).hexdigest(),
        "table_refs": scan_table_refs(blob, index),
        "table_entries": parse_table_entries(blob),
        "templates_5f81": extract_templates(blob),
        "castle_callers": cluster_castle_callers(blob, index, near),
        "high_level_castle_callers": [],
        "officer_5e1a_calls": cluster_5e1a(blob, index, far),
        "battle_disassembly": {},
        "call_counts": {},
    }
    for name, L in BATTLE_TARGETS.items():
        # All V8 battle/strategy targets live in the 43B3 runtime code group.
        cs = 0x43B3
        ip = (L - cs * 16) & 0xFFFF
        result["battle_disassembly"][name] = {
            "l_run": L, "cs_ip": f"{cs:04X}:{ip:04X}",
            "lines": function_disasm(blob, L, cs, ip, 0x400),
        }
    result["high_level_castle_callers"] = aggregate_high_level(index, result["castle_callers"])
    for name, L in BATTLE_TARGETS.items():
        result["call_counts"][name] = {
            "far": sum(1 for c in far if c["target"] == L),
            "near": sum(1 for c in near if c["target_rel"] == L),
        }

    OUTDIR.mkdir(parents=True, exist_ok=True)
    (OUTDIR / "trace.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUTDIR / "templates_5f81.json").write_text(
        json.dumps(result["templates_5f81"], indent=2) + "\n", encoding="utf-8")

    lines = ["# V9 runtime trace", "",
             f"Runtime MD5: `{result['runtime_md5']}`", "",
             "## Stat-table references", "",
             "| table | value | CS:IP | instruction | containing 4000 function |",
             "|---|---:|---|---|---|"]
    for h in result["table_refs"]:
        lines.append(f"| `{h['table']}` | `0x{h['value']:04X}` | `{h['cs']:04X}:{h['ip']:04X}` | "
                     f"`{h['instruction']}` | {h['caller_function'] or '-'} |")
    lines += ["", "## 4e8e caller clusters", "",
              "| function | count | static | fields touched |", "|---|---:|---|---|"]
    for c in result["castle_callers"]:
        fields = ", ".join(sorted({x["field"] for x in c["fields"]})) or "-"
        lines.append(f"| `{c['name'] or '?'}` | {c['count']} | `{c['entry'] or '-'}` | {fields} |")
    lines += ["", "### Cluster call sites", ""]
    for c in result["castle_callers"]:
        if not c["entry"]:
            continue
        lines.append(f"#### `{c['name']}` — {c['count']} call(s)")
        for s in c["sites"]:
            lines.append(f"- `{s['cs_ip']}` / `{s['static_ref']}` (L=`0x{s['site_l']:05X}`)")
        if c["fields"]:
            lines.append("")
            for f in c["fields"][:20]:
                lines.append(f"- field `{f['field']}` at `{f['cs_ip']}`: `{f['instruction']}`")
        lines.append("")
    lines += ["## High-level callers of the castle accessors", "",
              "| function | accessors called | fields touched |", "|---|---:|---|"]
    for c in result["high_level_castle_callers"]:
        lines.append(f"| `{c['name']}` (`{c['entry']}`) | {c['accessor_count']} | " +
                     f"{', '.join(c['fields']) or '-'} |")
        lines.append(f"|  | {', '.join(c['accessors'])} |  |")
    lines += ["", "## 5e1a far-call sites", ""]
    for c in result["officer_5e1a_calls"]:
        lines.append(f"- call L=`0x{c['site_l']:05X}`, local prologue L=`0x{c['prologue_l']:05X}` "
                     f"`{c['prologue_bytes']}`")
        for ln in c["lines"][:12]:
            lines.append(f"  - `{ln['cs']:04X}:{ln['ip']:04X}` `{ln['text']}`")
    t = result["templates_5f81"]
    lines += ["", "## 5f81 template table", "",
              f"Base `5F81:0000` (L=`0x{t['linear']:05X}`), {t['count']} x {t['stride']} B, "
              f"{t['unique_records']} unique record(s), sha256=`{t['sha256']}`.", "",
              "In this snapshot every record is the same empty template:", "",
              "```", t["records"][0], "```", ""]
    lines += ["", "## Battle cluster disassembly", ""]
    for name, d in result["battle_disassembly"].items():
        lines.append(f"### `{name}` at `{d['cs_ip']}` (L=`0x{d['l_run']:05X}`)")
        lines.append("```asm")
        for ln in d["lines"]:
            lines.append(f"{ln['cs']:04X}:{ln['ip']:04X}  {ln['bytes']:<18} {ln['text']}")
        lines.append("```")
        lines.append("")
    (OUTDIR / "trace.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"wrote {OUTDIR / 'trace.json'}")
    print(f"wrote {OUTDIR / 'trace.md'}")
    print("table_refs", len(result["table_refs"]), "castle_clusters", len(result["castle_callers"]),
          "high_level", len(result["high_level_castle_callers"]),
          "5e1a_calls", len(result["officer_5e1a_calls"]))
    for name, c in result["call_counts"].items():
        print(f"  {name}: far={c['far']} near={c['near']}")


if __name__ == "__main__":
    main()

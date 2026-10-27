#!/usr/bin/env python3
"""V10 offline tracer for the original strategic-turn / AI cluster.

Reads only local reverse-engineering artefacts:
  * re_out/runtime_0000_a0000.bin  (640 KiB real-mode image)
  * re_out/bin/Main.exe            (static executable, for hashes)
  * games/yphz/Sndata.gp           (castle adjacency records)

It never starts an emulator and never writes outside re_out/v10/.

Outputs:
  re_out/v10/trace.json
  re_out/v10/trace.md

Scope:
  * overlay 0x226E0  decision-context setup (shared words 0x920A..0x9220)
  * overlay 0x22856  adjacent-target selector (43B3:20C6 / 21A8 / 221E /
                     2ED6 / 30FE, 28FD:7350 / 71B8, 3B1A:5BC8)
  * overlay 0x22F60  per-castle executor gate
  * overlay 0x2303C  six-way action dispatcher and its policy wrappers
  * segment 28FD    route-cost / path enumerators (6FAE, 71B8, 7350, 752C)
  * segment 43B3    distance matrix, nearest friendly/enemy, adjacency
  * Sndata.gp       slot 0 (+0x10) vs the seven neighbour slots (+0x12..+0x1e)

The real CS of the 0x226xx/0x228xx code page is not recorded in the dump, so
labels use the linear address.  The 0x2285:0006 label emitted by trace_v9 is
linear/16 arithmetic, not a confirmed CS:IP pair.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86 import X86_OP_MEM

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "re_out" / "runtime_0000_a0000.bin"
MAIN_EXE = ROOT / "re_out" / "bin" / "Main.exe"
SNDATA = ROOT / "games" / "yphz" / "Sndata.gp"
OUTDIR = ROOT / "re_out" / "v10"

# 43B3 runtime linear base and the static<->runtime shift used by the docs.
CS_43B3_BASE = 0x43B3 * 16
STATIC_4000_TO_RUNTIME = 0x2F44  # 43B3 IP = static IP - 0x2F44

# (name, linear start, linear end or 0 for "stop at retf")
FUNCTIONS = [
    ("ctx_setup_226e0", 0x226E0, 0x227BA),
    ("select_22856", 0x22856, 0x229B0),
    ("exec_22f60", 0x22F60, 0x2303C),
    ("dispatch_2303c", 0x2303C, 0x23196),
    ("policy_morale_23196", 0x23196, 0x231EA),
    ("policy_troops_231ea", 0x231EA, 0x23250),
    ("policy_train_23250", 0x23250, 0x23304),
    ("policy_train_gate_23304", 0x23304, 0x2337E),
    ("policy_recruit_2337e", 0x2337E, 0x2348E),
    ("policy_resource_2348e", 0x2348E, 0x234FA),
    ("route_enum_2f6ae", 0x2F6AE, 0),
    ("value_71b8", 0x30188, 0),
    ("edge_cost_30280", 0x30280, 0),
    ("path_cost_a_302c4", 0x302C4, 0),
    ("path_cost_b_302f2", 0x302F2, 0),
    ("route_cost_7350", 0x30320, 0),
    ("two_step_752c", 0x304FC, 0),
    ("distance_5f60", 0x46B4C, 0),
    ("matrix_6006", 0x46BF2, 0),
    ("nearest_own_6058", 0x46C44, 0),
    ("nearest_enemy_609c", 0x46C88, 0),
    ("adjacency_20c6", 0x45BF6, 0),
    ("owner_21a8", 0x45CD8, 0),
    ("unowned_221e", 0x45D4E, 0),
    ("officers_2ed6", 0x46A06, 0),
    ("matrix_read_30fe", 0x46C2E, 0),
]

GLOBALS = set(range(0x920A, 0x9222, 2))

HELPERS = [
    {
        "name": "min",
        "addr": "3B1A:596E",
        "formula": "min(a, b)",
        "confidence": "B",
        "evidence": "43B3:7350 tail, 43B3:30188 tail",
    },
    {
        "name": "max",
        "addr": "3B1A:597E",
        "formula": "max(a, b)",
        "confidence": "B",
        "evidence": "wrapper 23250 (0x23288)",
    },
    {
        "name": "add_capped",
        "addr": "3B1A:5AF8",
        "formula": "v = min(v + x, cap)",
        "confidence": "B",
        "evidence": "0x3023B, 0x30272, 0x304CE",
    },
    {
        "name": "mul_div",
        "addr": "3B1A:5A0E",
        "formula": "(a * b) / c; 0xFFFF when c == 0 or overflow",
        "confidence": "B",
        "evidence": "0x30254, 0x40D99",
    },
    {
        "name": "percent",
        "addr": "3B1A:5A38",
        "formula": "(a * b) / 100",
        "confidence": "B",
        "evidence": "0x302AE, 0x302B7 branch",
    },
    {
        "name": "ratio_5bc8",
        "addr": "3B1A:5BC8",
        "formula": "(a + b) * 100 / a; 50 when a == 0 and b == 0",
        "confidence": "B",
        "evidence": "0x40D68..0x40DA1: 5AF8(&tmp=a, b, -1) then 5A0E(tmp, 100, a)",
    },
    {
        "name": "rand_mod",
        "addr": "3B1A:5C1C",
        "formula": "rand() % n",
        "confidence": "B",
        "evidence": "0x40DA2; used by 5C02",
    },
    {
        "name": "rand_percent",
        "addr": "3B1A:5C02",
        "formula": "rand() % 100 < x",
        "confidence": "B",
        "evidence": "0x40DA2..: calls 5C1C(100), compares AL with the argument",
    },
]

OPEN_QUESTIONS = [
    "Writer and exact business meaning of the shared word 0x9212.  It is used "
    "as the route-cost source in 7350(owner, 0x9212, castle) and is rejected "
    "when -1, so a castle id is likely but not proven.",
    "The six action routines 31CA:044A/0458/0474/0FFC/100A/145A are not "
    "decoded; only the dispatcher and the policy gates are established.",
    "22D82 (move executor) is not decoded; its state writes are unknown.",
    "18A6:3D86/3D54/2DBA/3856 and 43B3:1C18/1CA2 return semantics are not "
    "confirmed; 0x9218..0x9220 stay unnamed.",
    "The top-level monthly caller of 0x226E0/0x22F60 was not found in this "
    "image; it is probably in an overlay page that was not resident.",
    "30280's third argument is compared against the source castle id and the "
    "intermediate owner; the exact 'from/to/owner' naming is provisional.",
    "No DOSBox-X session was available, so every conclusion here is static "
    "(confidence B), never dynamic (A).",
]


def md5(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def runtime_cs_ip(linear: int) -> str:
    """Best-effort 43B3 label for 43B3 code, otherwise linear/16."""
    if 0x43B30 <= linear < 0x43B30 + 0x10000:
        return f"43B3:{linear - CS_43B3_BASE:04X}"
    return f"linear 0x{linear:05X}"


def static_label(linear: int) -> str | None:
    if 0x43B30 <= linear < 0x43B30 + 0x10000:
        return f"4000:{(linear - CS_43B3_BASE) + STATIC_4000_TO_RUNTIME:04X}"
    if 0x3B1A0 <= linear < 0x3B1A0 + 0x10000:
        # docs: 3B1A IP = static + 0x5A3F
        return f"3B1A-static:{(linear - 0x3B1A0) - 0x5A3F:04X}"
    return None


def decode(img: bytes, start: int, end: int) -> list[dict[str, Any]]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True
    stop = end if end else min(start + 0x400, len(img))
    out: list[dict[str, Any]] = []
    for ins in md.disasm(img[start:stop], start):
        out.append(
            {
                "addr": f"{ins.address:05X}",
                "bytes": ins.bytes.hex(),
                "text": f"{ins.mnemonic} {ins.op_str}".strip(),
            }
        )
        if not end and ins.mnemonic == "retf" and out:
            break
    return out


def call_targets(img: bytes, instructions: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Recover direct near/far call targets from the decoded bytes."""
    calls: list[dict[str, Any]] = []
    for item in instructions:
        raw = bytes.fromhex(item["bytes"])
        addr = int(item["addr"], 16)
        if not raw:
            continue
        if raw[0] == 0xE8 and len(raw) >= 3:
            rel = int.from_bytes(raw[1:3], "little", signed=True)
            target = (addr + 3 + rel) & 0xFFFFF
            calls.append(
                {
                    "site": f"{addr:05X}",
                    "kind": "near",
                    "target_linear": f"{target:05X}",
                    "target": f"linear 0x{target:05X}",
                }
            )
        elif raw[0] == 0x9A and len(raw) >= 5:
            off = raw[1] | (raw[2] << 8)
            seg = raw[3] | (raw[4] << 8)
            target = seg * 16 + off
            calls.append(
                {
                    "site": f"{addr:05X}",
                    "kind": "far",
                    "target_linear": f"{target:05X}",
                    "target": f"{seg:04X}:{off:04X}",
                }
            )
    return calls


def global_refs(instructions: list[dict[str, Any]], where: str) -> list[dict[str, Any]]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True
    refs: list[dict[str, Any]] = []
    for item in instructions:
        raw = bytes.fromhex(item["bytes"])
        addr = int(item["addr"], 16)
        for ins in md.disasm(raw, addr):
            for op in ins.operands:
                if op.type == X86_OP_MEM and (op.mem.disp & 0xFFFF) in GLOBALS:
                    disp = op.mem.disp & 0xFFFF
                    is_store = ins.mnemonic in ("mov", "movzx") and ins.operands
                    store = False
                    if is_store and len(ins.operands) >= 2:
                        dst = ins.operands[0]
                        store = (dst.type == X86_OP_MEM and
                                 (dst.mem.disp & 0xFFFF) in GLOBALS)
                    refs.append(
                        {
                            "where": where,
                            "addr": f"{ins.address:05X}",
                            "global": f"0x{disp:04X}",
                            "access": "write" if store else "read",
                            "text": f"{ins.mnemonic} {ins.op_str}",
                        }
                    )
                    break
    return refs


def parse_castles(data: bytes) -> dict[str, Any]:
    base = 0
    off = base + 0x7056
    rows = []
    for i in range(39):
        rec = data[off + i * 76:off + (i + 1) * 76]
        slot0 = rec[16] | (rec[17] << 8)
        neighbours = []
        for k in range(1, 8):
            v = rec[16 + k * 2] | (rec[16 + k * 2 + 1] << 8)
            if v != 0xFFFF:
                neighbours.append(v)
        rows.append(
            {
                "castle": i,
                "slot0_0x10": slot0,
                "neighbours_0x12_0x1e": neighbours,
                "faction_0x28": rec[40] | (rec[41] << 8),
            }
        )
    edges = sum(1 for r in rows for n in r["neighbours_0x12_0x1e"] if n > r["castle"])
    return {"rows": rows, "edge_count": edges}


def nearest_crosscheck(castles: list[dict[str, Any]], src: int, faction: int,
                       enemy: bool) -> int:
    from collections import deque

    def dist(a: int, b: int) -> int:
        if a == b:
            return 0
        seen = {a}
        q = deque([(a, 0)])
        while q:
            node, d = q.popleft()
            for n in castles[node]["neighbours_0x12_0x1e"]:
                if n == b:
                    return d + 1
                if n not in seen:
                    seen.add(n)
                    q.append((n, d + 1))
        return 0xFF

    best, best_d = -1, 0xFF
    for i, row in enumerate(castles):
        owner = row["faction_0x28"]
        if enemy:
            if owner == 0xFFFF or owner == faction:
                continue
        elif owner != faction:
            continue
        d = dist(i, src)
        if d < best_d:
            best, best_d = i, d
    return best


def main() -> int:
    if not RUNTIME.exists():
        raise SystemExit(f"missing runtime image: {RUNTIME}")
    img = RUNTIME.read_bytes()
    OUTDIR.mkdir(parents=True, exist_ok=True)

    functions = []
    all_globals: list[dict[str, Any]] = []
    for name, start, end in FUNCTIONS:
        ins = decode(img, start, end)
        calls = call_targets(img, ins)
        refs = global_refs(ins, name)
        all_globals.extend(refs)
        functions.append(
            {
                "name": name,
                "linear": f"{start:05X}",
                "cs_ip_best_effort": runtime_cs_ip(start),
                "static_label": static_label(start),
                "instruction_count": len(ins),
                "calls": calls,
                "globals": refs,
                "instructions": ins,
            }
        )

    sndata = SNDATA.read_bytes() if SNDATA.exists() else b""
    adjacency = parse_castles(sndata) if sndata else {"rows": [], "edge_count": 0}
    real_checks: dict[str, Any] = {}
    if adjacency["rows"]:
        rows = adjacency["rows"]
        f30 = rows[30]["faction_0x28"]
        real_checks = {
            "source_castle": 30,
            "faction": f30,
            "nearest_friendly": nearest_crosscheck(rows, 30, f30, enemy=False),
            "nearest_enemy": nearest_crosscheck(rows, 30, f30, enemy=True),
            "slot0_nonzero": [
                {"castle": r["castle"], "slot0": r["slot0_0x10"]}
                for r in rows if r["slot0_0x10"] != 0
            ],
        }

    doc = {
        "runtime_md5": md5(RUNTIME),
        "main_exe_md5": md5(MAIN_EXE) if MAIN_EXE.exists() else None,
        "sndata_md5": md5(SNDATA) if SNDATA.exists() else None,
        "mapping_note": (
            "43B3 labels use linear = 0x43B30 + IP.  The 0x226xx/0x228xx page's "
            "real CS is not stored in the dump; 2285:0006 from trace_v9 is "
            "linear/16 arithmetic, not a confirmed CS:IP."
        ),
        "functions": functions,
        "globals": all_globals,
        "helpers": HELPERS,
        "adjacency": {
            "edge_count": adjacency["edge_count"],
            "slot0_evidence": real_checks.get("slot0_nonzero", []),
            "rows": adjacency["rows"],
        },
        "distance_crosscheck": real_checks,
        "open_questions": OPEN_QUESTIONS,
    }

    (OUTDIR / "trace.json").write_text(
        json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    lines: list[str] = []
    lines.append("# V10 strategic trace")
    lines.append("")
    lines.append(f"Runtime MD5: `{doc['runtime_md5']}`")
    lines.append(f"Main.exe MD5: `{doc['main_exe_md5']}`")
    lines.append("")
    lines.append("## Mapping")
    lines.append("")
    lines.append(doc["mapping_note"])
    lines.append("")
    lines.append("## Function cluster")
    lines.append("")
    lines.append("| name | linear | runtime label | static | insns | calls | global refs |")
    lines.append("|---|---:|---|---|---:|---:|---:|")
    for fn in functions:
        lines.append(
            "| `{name}` | `{linear}` | `{cs_ip_best_effort}` | `{static_label}` | "
            "{instruction_count} | {calls} | {globals} |".format(
                name=fn["name"],
                linear=fn["linear"],
                cs_ip_best_effort=fn["cs_ip_best_effort"],
                static_label=fn["static_label"] or "-",
                instruction_count=fn["instruction_count"],
                calls=len(fn["calls"]),
                globals=len(fn["globals"]),
            )
        )
    lines.append("")
    lines.append("## Call graph")
    lines.append("")
    lines.append("| caller | site | kind | target |")
    lines.append("|---|---|---|---|")
    for fn in functions:
        for call in fn["calls"]:
            lines.append(
                "| `{caller}` | `{site}` | {kind} | `{target}` |".format(
                    caller=fn["name"], site=call["site"],
                    kind=call["kind"], target=call["target"]
                )
            )
    lines.append("")
    lines.append("## Key selector instructions (0x22856)")
    lines.append("")
    lines.append("| addr | instruction | role |")
    lines.append("|---|---|---|")
    key_roles = {
        "228DE": "initial ratio = word 0x9216",
        "228E4": "best castle = -1 (stay put)",
        "228FE": "candidate == -1 ends the 7-slot loop",
        "2292A": "matrix[cand][best_ref]",
        "2293E": "cost = 7350(owner, 0x9212, cand)",
        "22948": "value = 71B8(cand)",
        "2294F": "ratio = 5BC8(value, cost)",
        "22957": "mask ratio to 8 bits",
        "2295C": "reject officers >= 15",
        "22964": "compare distance first",
        "2296D": "then officer count",
        "22975": "then ratio",
    }
    select = next((f for f in functions if f["name"] == "select_22856"), None)
    if select:
        for ins in select["instructions"]:
            if ins["addr"] in key_roles:
                lines.append(
                    "| `{addr}` | `{text}` | {role} |".format(
                        addr=ins["addr"], text=ins["text"],
                        role=key_roles[ins["addr"]]
                    )
                )
    lines.append("")
    lines.append("## Shared words 0x920A..0x9220")
    lines.append("")
    lines.append("| where | addr | global | access | instruction |")
    lines.append("|---|---|---|---|---|")
    for ref in all_globals:
        lines.append(
            "| `{where}` | `{addr}` | `{global}` | {access} | `{text}` |".format(**ref)
        )
    lines.append("")
    lines.append("## Helper semantics (disassembly-derived)")
    lines.append("")
    lines.append("| helper | address | formula | confidence | evidence |")
    lines.append("|---|---|---|---|---|")
    for h in HELPERS:
        lines.append(
            "| {name} | `{addr}` | `{formula}` | {confidence} | {evidence} |".format(**h)
        )
    lines.append("")
    lines.append("## Castle adjacency evidence (Sndata.gp)")
    lines.append("")
    lines.append(
        f"Undirected edges found in slots +0x12..+0x1e: **{adjacency['edge_count']}**."
    )
    if real_checks.get("slot0_nonzero"):
        lines.append("")
        lines.append("Slot 0 (+0x10) is nonzero only for:")
        for row in real_checks["slot0_nonzero"]:
            lines.append(f"- castle {row['castle']}: {row['slot0']}")
        lines.append("")
        lines.append(
            "43B3:20C6 reads `+0x12 + 2*slot` for slot 0..6, so +0x10 is "
            "never part of the neighbour list; +0x12 is the first real "
            "neighbour.  The engine parser already skips +0x10."
        )
    lines.append("")
    lines.append("## Independent distance cross-check (scenario 0)")
    lines.append("")
    if real_checks:
        lines.append(
            f"- castle 30 faction = {real_checks['faction']}, "
            f"nearest friendly = {real_checks['nearest_friendly']}, "
            f"nearest enemy = {real_checks['nearest_enemy']}"
        )
        lines.append(
            "- The C port prints the same values in `--v10-selftest` "
            "(`real d30_29=1 d30_0=13 friend30=30 enemy30=27`)."
        )
    else:
        lines.append("- Sndata.gp not available; skipped.")
    lines.append("")
    lines.append("## Open questions")
    lines.append("")
    for q in OPEN_QUESTIONS:
        lines.append(f"- {q}")
    lines.append("")
    lines.append("## Confidence")
    lines.append("")
    lines.append(
        "- **B (static evidence):** adjacency slots, BFS distance matrix, "
        "nearest friendly/enemy, 0x22856 control flow, helper formulas, "
        "0x2303C dispatcher shape."
    )
    lines.append(
        "- **C (engine-side approximation):** the 7350 route-cost / 71B8 value "
        "score is isolated behind a callback; no original score model is "
        "claimed in C."
    )
    lines.append(
        "- **A (dynamic):** none.  DOSBox-X is not installed in this workspace."
    )
    (OUTDIR / "trace.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"wrote {OUTDIR / 'trace.json'}")
    print(f"wrote {OUTDIR / 'trace.md'}")
    print(f"functions={len(functions)} global_refs={len(all_globals)} "
          f"edges={adjacency['edge_count']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

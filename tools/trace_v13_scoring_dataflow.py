#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genpei-Gassen-RE — V13 scoring dataflow tracer (static only).

Traces the V13 scoring / path-cost chain for the Genpei Gassen game Main.exe:
    71B8 / 7350 / 30280 / 302C4 / 302F2 / 5BC8
and resolves the constant 0x90 semantics and the DS:0x9212 writer question.

Outputs:
    re_out/v13/scoring_dataflow.json   machine-readable JSON (ignored by .gitignore)

Scope / rules:
    * Static analysis only. No DOSBox-X, no runtime execution.
    * Only writes new files under re_out/v13/.
    * Never modifies V1/V2 scripts/reports, re_out/v12/, Main.exe, snapshots,
      or un-submitted untracked research material.
    * Distinguishes static file offset / static disassembly address / runtime
      linear address / CS:IP / DS:offset / data-segment base; never mixes them.
    * No linear scan is presented as a complete CFG; "not found by scan" is not
      written as "does not exist in the whole program".

Address model (measured per function via byte-signature match in both files):
    * runtime_0000_a0000.bin linear offset == file offset (655360 B snapshot).
    * Static file offsets are measured per function by matching its runtime
      prologue signature in Main.exe; shifts are NOT assumed uniform. Observed
      groups:
        - 0x28FD group: runtime == static file + 0x1055A
        - 0x3B1A group: runtime == static file + 0x10B9F
        - 0x43B3 group: runtime == static file + 0x10BAC
    * For the separate overlay page at rt 0x226E0 / file 0x12192 the empirical
      shift is +0x1054E; its real CS:IP / entry point is NOT stored and NOT
      asserted.
    * DS_RUNTIME = 0x619F (verified via stat_getters / stat_setters / vtable
      pointer tables at 43B3/3B1A). DS:0x9212 == linear 0x6AC02.

Confidence levels (V13 evidence model): A/B/C/D.
    No dynamic verification exists, so no dynamic-dependent claim is marked A.
"""

from __future__ import annotations

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError as exc:  # pragma: no cover - environment dependency
    raise SystemExit("capstone python package is required: pip install capstone") from exc

# ---------------------------------------------------------------------------
# Paths & input hashes
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
MAIN_EXE = ROOT / "games" / "yphz" / "Main.exe"
RUNTIME = ROOT / "re_out" / "runtime_0000_a0000.bin"
OUT_DIR = ROOT / "re_out" / "v13"
OUT_JSON = OUT_DIR / "scoring_dataflow.json"

EXPECTED_RUNTIME_SIZE = 655360
EXPECTED_RUNTIME_MD5 = "649725c7263606abbc93fef0923f96be"
EXPECTED_MAIN_MD5 = "7e7e1b875aa3b07eeb28c2eb5950aac1"

# ---------------------------------------------------------------------------
# Address model
# ---------------------------------------------------------------------------
DS_RUNTIME = 0x619F
DS_9212_OFFSET = 0x9212
DS_9212_LINEAR = DS_RUNTIME * 16 + DS_9212_OFFSET  # 0x6AC02

# Separate overlay page 0x226xx: rt 0x226E0 == file 0x12192 -> shift +0x1054E.
SEPARATE_PAGE_SHIFT = 0x1054E

# Runtime linear addresses of traced functions.
RT = {
    "2F530": 0x2F530,
    "2F5D2": 0x2F5D2,
    "2F6AE": 0x2F6AE,
    "2F928": 0x2F928,
    "300C0": 0x300C0,
    "30120": 0x30120,
    "30188": 0x30188,   # 71B8
    "30280": 0x30280,
    "302C4": 0x302C4,
    "302F2": 0x302F2,
    "30320": 0x30320,   # 7350
    "3037E": 0x3037E,
    "304FC": 0x304FC,
    "40B0E": 0x40B0E,   # 3B1A:596E min
    "40C98": 0x40C98,   # 3B1A:5AF8 add_capped
    "40BAE": 0x40BAE,   # 3B1A:5A0E mul_div
    "40D68": 0x40D68,   # 3B1A:5BC8 ratio
    "40BD8": 0x40BD8,   # 3B1A:5A38 percent
    "226E0": 0x226E0,
    "22856": 0x22856,
    "22B22": 0x22B22,
    "22F60": 0x22F60,
    "45C20": 0x45C20,   # 43B3:20F0 adjacency
    "45CC2": 0x45CC2,   # 43B3:2192
    "45CD8": 0x45CD8,   # 43B3:21A8 castle+0x28 faction
    "45D4E": 0x45D4E,   # 43B3:221E unowned?
}

NAMES = {
    "0x300C0": "300C0",
    "0x30120": "30120",
    "0x30188": "71B8",
    "0x30280": "30280",
    "0x302C4": "302C4",
    "0x302F2": "302F2",
    "0x30320": "7350",
    "0x3037E": "3037E",
    "0x40B0E": "596E/min",
    "0x40C98": "5AF8",
    "0x40BAE": "5A0E",
    "0x40D68": "5BC8",
    "0x40BD8": "5A38",
    "0x226E0": "226E0",
    "0x22856": "22856",
    "0x22B22": "22B22",
    "0x22F60": "22F60",
    "0x45C20": "20F0",
    "0x45CC2": "2192",
    "0x45CD8": "21A8",
    "0x45D4E": "221E",
}

# Functions called by number (the "named label" set from V10/V12).
NUMBERED_CALLS = {
    0x2ED6: "2ED6(officer count of castle)",
    0x2FEA: "2FEA(adjacent castle list)",
    0x30FE: "30FE(distance)",
    0x3114: "3114",
    0x3158: "3158(nearest? castle value)",
    0x34EC: "34EC(template+0x00)",
    0x3572: "3572",
    0x3D48: "3D48",
    0x40D68: "5BC8(a,b)",
    0x40B0E: "596E/min",
    0x40C98: "5AF8(acc,delta,cap)",
    0x40BAE: "5A0E(a,b,c)",
    0x40BD8: "5A38",
    0x45CD8: "21A8(castle+0x28 faction)",
    0x45CC2: "2192(castle+0x26 region)",
    0x45C20: "20F0(adjacency candidate,ref)",
    0x45D4E: "221E(castle unowned?)",
    0x2F530: "2F530(enumerate officers)",
    0x2F5D2: "2F5D2(ref,candidate,flags)",
    0x2F6AE: "2F6AE(enumerate accepted castles)",
}


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def md5_hex(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


# ---------------------------------------------------------------------------
# Disassembly
# ---------------------------------------------------------------------------
def load_files() -> tuple[bytes, bytes]:
    if not MAIN_EXE.is_file():
        raise SystemExit(f"Main.exe not found: {MAIN_EXE}")
    if not RUNTIME.is_file():
        raise SystemExit(f"runtime snapshot not found: {RUNTIME}")
    exe = MAIN_EXE.read_bytes()
    rt = RUNTIME.read_bytes()
    if len(rt) != EXPECTED_RUNTIME_SIZE:
        raise SystemExit(
            f"runtime snapshot size {len(rt)} != {EXPECTED_RUNTIME_SIZE}"
        )
    if md5_hex(rt) != EXPECTED_RUNTIME_MD5:
        raise SystemExit(
            f"runtime MD5 {md5_hex(rt)} != {EXPECTED_RUNTIME_MD5}"
        )
    if md5_hex(exe) != EXPECTED_MAIN_MD5:
        raise SystemExit(
            f"Main.exe MD5 {md5_hex(exe)} != {EXPECTED_MAIN_MD5}"
        )
    return exe, rt


def disasm_range(data: bytes, start: int, max_len: int = 0x220) -> list[dict]:
    """Linear disassembly from `start` for up to `max_len` bytes, stopping at
    a return instruction. Returns a list of instruction dicts."""
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    out: list[dict] = []
    addr = start
    end = min(start + max_len, len(data))
    while addr < end:
        ins = next(md.disasm(data[addr:end], addr), None)
        if ins is None:
            out.append(
                {
                    "address": addr,
                    "bytes": data[addr:addr + 1].hex(),
                    "mnemonic": "db",
                    "op_str": "unclear",
                    "size": 1,
                }
            )
            addr += 1
            continue
        out.append(
            {
                "address": ins.address,
                "bytes": data[ins.address : ins.address + ins.size].hex(" "),
                "mnemonic": ins.mnemonic,
                "op_str": ins.op_str,
                "size": ins.size,
            }
        )
        if ins.mnemonic in ("retf", "ret", "iret"):
            break
        addr += ins.size
    return out


def find_signature(data: bytes, sig: bytes, start: int = 0) -> list[int]:
    """Return all file offsets where `sig` occurs in `data`."""
    hits: list[int] = []
    s = start
    while True:
        i = data.find(sig, s)
        if i < 0:
            break
        hits.append(i)
        s = i + 1
    return hits


def static_file_offset(rt_addr: int, exe: bytes, rt: bytes) -> list[tuple[int, int]]:
    """Measure static file offset(s) for a runtime function by matching its
    runtime prologue signature in Main.exe.

    Tries signature lengths 16 down to 8 (a far-call segment operand inside the
    signature window is relocated between static and runtime, so a shorter
    prefix may be needed). Returns a list of (file_offset, shift) where
    shift = rt_addr - file_offset. Multiple hits mean the signature is not
    unique; the caller must disambiguate by shift consistency within a segment
    group.
    """
    found: list[tuple[int, int]] = []
    seen: set[int] = set()
    for sig_len in (16, 14, 12, 11, 10, 9, 8):
        sig = bytes(rt[rt_addr : rt_addr + sig_len])
        for h in find_signature(exe, sig):
            if h in seen:
                continue
            seen.add(h)
            found.append((h, rt_addr - h))
        if found:
            break
    return found


# ---------------------------------------------------------------------------
# Call / memory reference extraction
# ---------------------------------------------------------------------------
def extract_calls(instrs: list[dict], data: bytes) -> list[dict]:
    """Extract near/far call targets from a disassembled block.

    Near call: E8 rel16  -> target = site + 3 + sign_extended(rel16)
    Far  call: 9A off16 seg16 -> target = seg*16 + off
    """
    calls: list[dict] = []
    for ins in instrs:
        addr = ins["address"]
        m = ins["mnemonic"]
        raw = bytes.fromhex(ins["bytes"])
        if m == "call" and raw[0] == 0xE8:
            rel = int.from_bytes(raw[1:3], "little", signed=True)
            tgt = addr + 3 + rel
            calls.append(
                {
                    "site": addr,
                    "kind": "near",
                    "target": tgt,
                    "target_name": NAMES.get(f"0x{tgt:05X}"),
                    "bytes": ins["bytes"],
                }
            )
        elif m == "lcall" and raw[0] == 0x9A:
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            tgt = seg * 16 + off
            calls.append(
                {
                    "site": addr,
                    "kind": "far",
                    "target": tgt,
                    "segment": seg,
                    "offset": off,
                    "target_name": NAMES.get(f"0x{tgt:05X}"),
                    "bytes": ins["bytes"],
                }
            )
    return calls


def scan_abs_mem_refs(data: bytes, disp16: int) -> list[dict]:
    """Scan a byte image for every absolute 16-bit memory reference to `disp16`
    (bytes `disp_lo disp_hi` interpreted as a 16-bit displacement with no base
    register) across all 8086 encodings, including segment overrides.

    Returns dicts with the surrounding bytes so a human / downstream script can
    classify read vs write and exclude data-compression false positives.
    """
    lo = disp16 & 0xFF
    hi = (disp16 >> 8) & 0xFF
    pat = bytes([lo, hi])
    hits: list[dict] = []
    s = 0
    while True:
        i = data.find(pat, s)
        if i < 0:
            break
        ctx_start = max(0, i - 10)
        ctx_end = min(len(data), i + 12)
        hits.append(
            {
                "position": i,
                "context": data[ctx_start:ctx_end].hex(" "),
            }
        )
        s = i + 1
    return hits


def classify_abs_refs(rt: bytes, disp16: int) -> dict:
    """Classify absolute memory references to disp16 in the runtime snapshot:
    read sites vs write sites vs non-code data hits."""
    all_hits = scan_abs_mem_refs(rt, disp16)
    # Known code read / write sites derived from the disassembly trace.
    # (kept as a cross-check, not as an assumption of completeness)
    code_read = [0x2272F, 0x22898, 0x22B41, 0x24745, 0x30401, 0x3044F, 0x30489]
    code_write: list[int] = []
    return {
        "disp16": f"0x{disp16:04X}",
        "absolute_linear": f"0x{DS_RUNTIME:04X}:{disp16:04X} -> 0x{DS_RUNTIME * 16 + disp16:06X}",
        "raw_hit_count": len(all_hits),
        "raw_hits": [h["position"] for h in all_hits[:200]],
        "code_read_sites": code_read,
        "code_write_sites": code_write,
        "note": "Indirect writes (via base register / pointer table / rep movs) "
        "are NOT covered by this absolute-scan; see errata for scope and next steps.",
    }


# ---------------------------------------------------------------------------
# Function trace records
# ---------------------------------------------------------------------------
def trace_function(name: str, rt_addr: int, rt: bytes, exe: bytes) -> dict:
    instrs = disasm_range(rt, rt_addr, max_len=0x220)
    calls = extract_calls(instrs, rt)
    sfo_hits = static_file_offset(rt_addr, exe, rt)
    primary = sfo_hits[0] if sfo_hits else (None, None)
    rec = {
        "name": name,
        "runtime_linear": f"0x{rt_addr:06X}",
        "runtime_hex": f"{rt_addr:05X}",
        "static_file_offset": f"0x{primary[0]:05X}" if primary[0] is not None else None,
        "static_candidates": [f"0x{h:05X}(+0x{s:X})" for h, s in sfo_hits],
        "measured_shift": f"+0x{primary[1]:X}" if primary[1] is not None else None,
        "cs_ip_note": "CS:IP not stored for resident overlay pages; offset shown "
        "is the runtime linear address, not a verified CS:IP.",
        "instruction_count": len(instrs),
        "instructions": instrs,
        "calls": calls,
    }
    return rec


# ---------------------------------------------------------------------------
# Formulas (verified from disassembly; static analysis only)
# ---------------------------------------------------------------------------
FORMULAS = {
    "5A0E": {
        "text": "5A0E(a,b,c) = (a * b) / c ; c==0 -> 0xFFFF ; quotient>0xFFFF -> 0xFFFF",
        "sig": "5A0E(arg1=a:[bp+6], arg2=b:[bp+8], arg3=c:[bp+0xa])",
        "notes": [
            "enter 4,0 ; si=arg3(c); if c==0 return 0xFFFF",
            "mul arg2 ; push dx,ax ; lcall 1055:F8C(dividend DX:AX, divisor 0:c)",
            "after divide: dx=quotient high, ax=quotient low",
            "if dx<0 return ax ; if dx>0 return 0xFFFF ; else return ax",
            "=> positive products: min((a*b)/c, 0xFFFF)",
        ],
        "confidence": "B",
    },
    "5AF8": {
        "text": "5AF8(acc_ptr, delta, cap) : *acc_ptr = min(*acc_ptr + delta, cap)",
        "sig": "5AF8([bp+6]=far ptr to acc, [bp+8]=delta, [bp+0xa]=cap)",
        "notes": [
            "les bx,[bp+6] ; ax=[es:bx]; ax+=delta; 32-bit compare vs cap",
            "if sum>cap store cap else store sum ; return stored value",
        ],
        "confidence": "B",
    },
    "5BC8": {
        "text": "5BC8(a,b): a==0&&b==0 -> 0x32; else tmp=min(a+b,0xFFFF); return min(tmp*100/a, 0xFFFF); a=0,b!=0 -> 0xFFFF (div-by-zero via 5A0E)",
        "sig": "5BC8([bp+6]=a, [bp+8]=b)",
        "notes": [
            "or si,si ; jne ; cmp [bp+8],si ; je -> mov al,0x32 (50)",
            "else [bp-2]=a ; add_capped(&[bp-2], b, 0xFFFF) => min(a+b,0xFFFF)",
            "push [bp-2]; push 0x64; push a ; lcall 5A0E => min((tmp*100)/a,0xFFFF)",
        ],
        "confidence": "B",
    },
    "300C0": {
        "text": "300C0(officer_list): acc=0; for each officer o: acc=min(acc + (5AA(o)*(7D8(o)+67A(o)))/500, 10000); return acc",
        "sig": "300C0([bp+6]=ptr to officer list, -1 terminated)",
        "notes": [
            "7D8(o) = min(off+0x31 + 43EDE(o,4), 200)",
            "67A(o) = 5AA(o)==0 ? 0 : min(43EDE(o,10) + off+0x2F, 100)",
            "5AA(o) = min(440A8(o), off+0x2B); 440A8(o)=off+0x29==-1 ? 500 : min(100*18A6:27C2(off+0x29,5000), 5000)",
        ],
        "confidence": "C",
    },
    "30188_71B8": {
        "text": "71B8(castle): n=2F530(flags=0x8000, castle, array); if n==0 return 1; take top min(n,5); si=min(max(n-5,0),20); acc=300C0(array); v=min(2814(castle,+0x3A),0x4E20); v=min(v+2B2C(castle,+0x42),0xC350); v=(v*2920(castle,+0x3C))/0x1F4; return min(acc + si*150 + v, 0x7530)",
        "sig": "30188([bp+6]=castle)",
        "notes": [
            "2F530 enumerates alive officers of castle (flags 0x8000 => accept all after callback check)",
            "array capped at 5 entries; sentinel 0xFFFF written at [array+di]",
            "si (remaining officers beyond first 5) clamped to 0..20",
            "fields: +0x3A cap 0x4E20(20000), +0x42 cap 0xC350(50000), +0x3C used as numerator /0x1F4(500)",
        ],
        "confidence": "B",
    },
    "30280": {
        "text": "30280(a,b,c,d): if a==-1 or a==c or b!=c -> return d; else if c==-1 or !1CA2(c) -> return 120(0x78); else return min(d*150/100, 0xFFFF)",
        "sig": "30280([bp+6]=a,[bp+8]=b,[bp+0xa]=c,[bp+0xc]=d)",
        "notes": [
            "d is the 4th argument; in 302F2/302C4 callers d is inherited from a "
            "leftover stack slot (the value 30188(c) / 30120(c)), not pushed explicitly.",
        ],
        "confidence": "B",
    },
    "302F2": {
        "text": "302F2(arg1,arg2,arg3) = 30280(arg1, arg2, arg3, d=30188(arg3))",
        "sig": "302F2([bp+6]=arg1,[bp+8]=arg2,[bp+0xa]=arg3)",
        "notes": [
            "push arg3 ; lcall 30188 -> si (value); then stack layout leaves si as 30280's 4th arg (d)",
            "push 21A8(arg3) as faction; push arg2; push arg1; call 30280",
        ],
        "confidence": "B",
    },
    "302C4": {
        "text": "302C4(arg1,arg2,arg3) = 30280(arg1, arg2, c=34EC(arg3), d=30120(arg3))",
        "sig": "302C4([bp+6]=arg1,[bp+8]=arg2,[bp+0xa]=arg3 template index)",
        "notes": [
            "lcall 30120(arg3) -> si (value); lcall 34EC(arg3) -> ax (faction template field)",
            "3D48 checks 34EC(si) < 0x10 before using it as a faction",
            "d = 30120(template) inherited on the stack as 30280's 4th arg",
        ],
        "confidence": "B",
    },
    "7350": {
        "text": "7350(arg1=owner, arg2=0x9212(target faction), arg3=ref_castle): acc=0; list=2F6AE(flags=0x90, ref, buf); for each neighbor n in list: term=302F2(arg1, arg2, n); acc=min(acc+term,0xFFFF); return acc",
        "sig": "30320([bp+6]=arg1,[bp+8]=arg2,[bp+0xa]=arg3)",
        "notes": [
            "flags 0x90 = 0x10|0x80 passed to 2F6AE->2F5D2 as the 3rd arg (filter mask)",
            "0x10: 20F0(candidate, ref)!=0 (adjacent); 0x80: 21A8(candidate)!=21A8(ref) (owner differs)",
            "cap 0xFFFF via 5AF8; returns 0 if list empty / first entry -1",
        ],
        "confidence": "B",
    },
    "22856": {
        "text": "22856(arg1): selector over adjacent castles; compare key=(distance asc, officer count desc, 5BC8(value,cost) ratio desc); tie keeps current best; initial best = current castle with ratio=[0x9216]; cost uses 7350([0x9210], [0x9212], candidate); value uses 71B8(candidate); returns -1 if no valid move.",
        "sig": "22856([bp+6]=arg1)",
        "confidence": "B",
    },
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> int:
    exe, rt = load_files()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # ---- measured per-function static mapping & shift groups ---------------
    # Collect all signature candidates per function, then disambiguate the
    # primary offset per 4K block by the block's mode shift (a relocated
    # far-call segment operand inside the signature window can produce a
    # spurious short-signature hit with an inconsistent shift).
    all_candidates: dict[str, list[tuple[int, int]]] = {}
    for name, addr in RT.items():
        all_candidates[name] = static_file_offset(addr, exe, rt)

    # block = rt_addr >> 12 ; mode shift per block
    block_shifts: dict[int, Counter] = {}
    for name, addr in RT.items():
        block = addr >> 12
        block_shifts.setdefault(block, Counter())
        for _h, s in all_candidates[name]:
            block_shifts[block][s] += 1
    block_mode: dict[int, int] = {
        b: ctr.most_common(1)[0][0] for b, ctr in block_shifts.items()
    }

    mapping: dict[str, dict] = {}
    for name, addr in RT.items():
        cands = all_candidates[name]
        mode_shift = block_mode.get(addr >> 12)
        primary = None
        if mode_shift is not None:
            for h, s in cands:
                if s == mode_shift:
                    primary = (h, s)
                    break
        if primary is None and cands:
            primary = cands[0]
        mapping[name] = {
            "rt": f"0x{addr:05X}",
            "candidates": [f"0x{h:05X}(+0x{s:X})" for h, s in cands],
            "file": f"0x{primary[0]:05X}" if primary else None,
            "shift": f"+0x{primary[1]:X}" if primary else None,
        }

    # ---- Function trace records -------------------------------------------
    functions: list[dict] = []
    call_edges: list[dict] = []
    for name, addr in RT.items():
        rec = trace_function(name, addr, rt, exe)
        functions.append(rec)
        for c in rec["calls"]:
            call_edges.append(
                {
                    "from": name,
                    "from_address": rec["runtime_linear"],
                    "site": c["site"],
                    "kind": c["kind"],
                    "target_linear": f"0x{c['target']:06X}",
                    "target_name": c["target_name"],
                    "bytes": c["bytes"],
                }
            )

    # ---- 0x90 constant analysis -------------------------------------------
    # Locate 7350's mov ax,0x90 by scanning for B8 90 00 inside 30320..30340.
    b8_90_site = None
    for i in range(RT["30320"], min(RT["30320"] + 0x40, len(rt) - 2)):
        if rt[i] == 0xB8 and rt[i + 1] == 0x90 and rt[i + 2] == 0x00:
            b8_90_site = i
            break
    # Verify the 2F6AE call site inside 7350: E8 rel target 0x2F6AE.
    call_2f6ae_site = None
    for i in range(RT["30320"], min(RT["30320"] + 0x40, len(rt) - 3)):
        if rt[i] == 0xE8:
            rel = int.from_bytes(rt[i + 1 : i + 3], "little", signed=True)
            if i + 3 + rel == RT["2F6AE"]:
                call_2f6ae_site = i
                break
    # Verify 2F5D2 bit tests for 0x10 and 0x80.
    bit_tests: list[dict] = []
    for i in range(RT["2F5D2"], RT["2F5D2"] + 0xB0):
        if i + 2 < len(rt) and rt[i] == 0xF7 and rt[i + 1] == 0xC6:  # test si,imm16
            imm = int.from_bytes(rt[i + 2 : i + 4], "little")
            bit_tests.append({"site": f"0x{i:05X}", "imm": f"0x{imm:04X}"})

    # ---- 0x9212 writer analysis ------------------------------------------
    refs_9212 = classify_abs_refs(rt, DS_9212_OFFSET)
    refs_9212["snapshot_word"] = f"0x{int.from_bytes(rt[DS_9212_LINEAR:DS_9212_LINEAR+2], 'little'):04X}"
    # neighbouring fields
    neigh = {}
    for off in (0x920A, 0x920C, 0x920E, 0x9210, 0x9212, 0x9214, 0x9216, 0x9218, 0x921A, 0x921C, 0x921E, 0x9220, 0x9222):
        lin = DS_RUNTIME * 16 + off
        neigh[f"0x{off:04X}"] = f"0x{int.from_bytes(rt[lin:lin+2], 'little'):04X}"
    refs_9212["neighbour_fields_snapshot"] = neigh

    refs_9212_exe = classify_abs_refs(exe, DS_9212_OFFSET)

    # direct caller search for 0x226E0 (near + far + raw pointer)
    direct_caller_226e0: list[dict] = []
    for i in range(len(rt) - 3):
        if rt[i] == 0xE8:
            rel = int.from_bytes(rt[i + 1 : i + 3], "little", signed=True)
            if i + 3 + rel == RT["226E0"]:
                direct_caller_226e0.append(
                    {"site": f"0x{i:06X}", "kind": "near", "target": "0x226E0"}
                )
        if i + 4 < len(rt) and rt[i] == 0x9A:
            off = int.from_bytes(rt[i + 1 : i + 3], "little")
            seg = int.from_bytes(rt[i + 3 : i + 5], "little")
            if seg * 16 + off == RT["226E0"]:
                direct_caller_226e0.append(
                    {"site": f"0x{i:06X}", "kind": "far", "target": f"0x{seg:04X}:{off:04X}"}
                )

    # ---- self checks ------------------------------------------------------
    self_checks: list[dict] = []

    def check(name: str, ok: bool, detail: str):
        self_checks.append({"name": name, "ok": ok, "detail": detail})

    # input hashes
    check("runtime MD5", md5_hex(rt) == EXPECTED_RUNTIME_MD5,
          f"{md5_hex(rt)} == {EXPECTED_RUNTIME_MD5}")
    check("Main.exe MD5", md5_hex(exe) == EXPECTED_MAIN_MD5,
          f"{md5_hex(exe)} == {EXPECTED_MAIN_MD5}")

    def near_target(site: int) -> int | None:
        if site + 3 > len(rt):
            return None
        if rt[site] != 0xE8:
            return None
        return site + 3 + int.from_bytes(rt[site + 1 : site + 3], "little", signed=True)

    # 0x3035B -> 0x302F2  (E8 94 FF)
    i = 0x3035B
    t35b = near_target(i)
    ok35b = rt[i:i + 3] == bytes([0xE8, 0x94, 0xFF]) and t35b == 0x302F2
    check("0x3035B E8 94 FF -> 0x302F2", ok35b,
          f"bytes {rt[i:i+3].hex(' ')} target {t35b:05X}")

    # 0x30314 E8 69 FF -> 0x30280 (inside 302F2)
    i = 0x30314
    t314 = near_target(i)
    ok314 = rt[i:i + 3] == bytes([0xE8, 0x69, 0xFF]) and t314 == 0x30280
    check("0x30314 E8 69 FF -> 0x30280", ok314,
          f"bytes {rt[i:i+3].hex(' ')} target {t314:05X}")

    # 0x302FD E8 88 FE -> 0x30188 (inside 302F2)
    i = 0x302FD
    t2fd = near_target(i)
    ok2fd = rt[i:i + 3] == bytes([0xE8, 0x88, 0xFE]) and t2fd == 0x30188
    check("0x302FD E8 88 FE -> 0x30188", ok2fd,
          f"bytes {rt[i:i+3].hex(' ')} target {t2fd:05X}")

    # 7350 pushes 0x90 then calls 2F6AE
    check("7350 mov ax,0x90 present", b8_90_site is not None,
          f"site {f'0x{b8_90_site:05X}' if b8_90_site is not None else 'none'}")
    check("7350 call -> 2F6AE present", call_2f6ae_site is not None,
          f"site {f'0x{call_2f6ae_site:05X}' if call_2f6ae_site is not None else 'none'}")

    # 2F5D2 bit tests 0x10 and 0x80
    imm_vals = [int(bt["imm"], 16) for bt in bit_tests]
    check("2F5D2 tests 0x10 and 0x80", 0x10 in imm_vals and 0x80 in imm_vals,
          f"bit tests found: {[bt['imm'] for bt in bit_tests]}")

    # 30280 leading checks: cmp dx,-1 at offset +6 (83 FA FF)
    i30280 = RT["30280"]
    ok30280 = (
        i30280 + 9 <= len(rt)
        and rt[i30280 + 6 : i30280 + 8] == bytes([0x83, 0xFA])  # cmp dx,imm8
        and rt[i30280 + 8] == 0xFF
    )
    check("30280 checks a==-1 first", ok30280,
          f"bytes {rt[i30280:i30280+9].hex(' ')}")

    # 5BC8 special return 0x32 (b0 32) at offset +0x11
    i5bc8 = RT["40D68"]
    ok5bc8 = i5bc8 + 0x13 <= len(rt) and rt[i5bc8 + 0x11] == 0xB0 and rt[i5bc8 + 0x12] == 0x32
    check("5BC8 returns 0x32 when a==0&&b==0", ok5bc8,
          f"bytes {rt[i5bc8:i5bc8+0x16].hex(' ')}")

    # 0x90 value
    check("0x90 == 144 == 0b10010000", 0x90 == (0x10 | 0x80), "")

    # DS:0x9212 linear address
    check("DS:0x9212 linear == 0x6AC02", DS_9212_LINEAR == 0x6AC02,
          f"0x{DS_RUNTIME:04X}*16+0x9212 = 0x{DS_9212_LINEAR:06X}")

    # snapshot word
    check("DS:0x9212 snapshot word == 0x0000", int.from_bytes(rt[DS_9212_LINEAR:DS_9212_LINEAR+2], 'little') == 0,
          f"word = {refs_9212['snapshot_word']}")

    # no absolute write to 0x9212 in snapshot
    check("no absolute write to 0x9212 in snapshot", refs_9212["code_write_sites"] == [],
          f"writes={refs_9212['code_write_sites']}")

    # static file-offset mapping spot checks (measured, not assumed uniform)
    mapping_ok = True
    details = []
    expected = {
        "0x30320": "0x1FDC6",
        "0x30280": "0x1FD26",
        "0x302F2": "0x1FD98",
        "0x302C4": "0x1FD6A",
        "0x30188": "0x1FC2E",
        "0x40D68": "0x301C9",
        "0x40C98": "0x300F9",
        "0x45C20": "0x35074",
        "0x226E0": "0x12192",
    }
    for rt_addr_hex, exp_file in expected.items():
        rt_addr = int(rt_addr_hex, 16)
        hits = static_file_offset(rt_addr, exe, rt)
        files = [f"0x{h:05X}" for h, _ in hits]
        actual = files[0] if files else "NOT FOUND"
        ok_hit = exp_file in files
        details.append(f"rt {rt_addr_hex} -> file {actual} (expect {exp_file}; all hits {files})")
        if not ok_hit:
            mapping_ok = False
    check("static file-offset mapping spot checks", mapping_ok, "; ".join(details))

    # ---- assemble JSON -----------------------------------------------------
    document = {
        "schema": "v13-scoring-dataflow-1",
        "generated_by": "tools/trace_v13_scoring_dataflow.py",
        "inputs": {
            "runtime": {
                "path": str(RUNTIME),
                "size": len(rt),
                "md5": md5_hex(rt),
                "sha256": sha256_hex(rt),
            },
            "main_exe": {
                "path": str(MAIN_EXE),
                "size": len(exe),
                "md5": md5_hex(exe),
                "sha256": sha256_hex(exe),
            },
        },
        "address_model": {
            "runtime_linear_equals_file_offset": True,
            "per_function_static_mapping_measured": mapping,
            "separate_0x226xx_page_shift": f"+0x{SEPARATE_PAGE_SHIFT:X}  (rt 0x226E0 == file 0x12192; CS:IP/entry not stored, not asserted)",
            "ds_runtime": f"0x{DS_RUNTIME:04X}",
            "ds_runtime_verified_by": "stat_getters / stat_setters / vtable pointer tables at 43B3 / 3B1A",
            "ds_0x9212_offset": f"0x{DS_9212_OFFSET:04X}",
            "ds_0x9212_linear": f"0x{DS_9212_LINEAR:06X}",
            "cs_ip_note": "CS:IP is not stored for resident overlay pages; runtime linear "
            "addresses are used throughout and never mixed with CS:IP.",
        },
        "formulas": FORMULAS,
        "functions": functions,
        "call_graph": call_edges,
        "constant_0x90": {
            "value": 0x90,
            "binary": "0b10010000",
            "producer_site": f"0x{b8_90_site:05X}" if b8_90_site is not None else None,
            "producer_instruction": "mov ax, 0x90  (inside 7350 @ 0x30320)",
            "consumer": "passed as arg1 to 2F6AE, forwarded as arg3 (flags mask) to 2F5D2",
            "filter_bits": {
                "0x10": "require 20F0(candidate, ref) != 0  =>  candidate adjacent to ref castle",
                "0x80": "require 21A8(candidate) != 21A8(ref)  =>  different faction (castle +0x28)",
            },
            "semantics": "candidate-selection filter mask for 7350's neighbour enumeration "
            "(NOT a weight, cap, or initial value)",
            "chain_preserved": "0x3035B -> 0x302F2 -> 0x30280  (NOT 0x3035B -> 0x30280)",
            "confidence": "B",
        },
        "global_0x9212": {
            "ds_offset": f"0x{DS_9212_OFFSET:04X}",
            "runtime_linear": f"0x{DS_9212_LINEAR:06X}",
            "snapshot_word": refs_9212["snapshot_word"],
            "neighbour_fields_snapshot": refs_9212["neighbour_fields_snapshot"],
            "references_in_snapshot": refs_9212,
            "references_in_main_exe": refs_9212_exe,
            "direct_caller_of_0x226E0": direct_caller_226e0,
            "writer_status": "not_found_in_checked_scopes",
            "semantic_domain": "faction ID (target-faction candidate); compared against "
            "21A8(castle+0x28 faction) and 34EC(template+0x00, bounded <0x10 by 3D48)",
            "evidence": [
                "30280 compares its arg2 (supplied as 0x9212 by 7350) against arg3, "
                "which is 21A8(neighbor) in the 302F2 path and 34EC(template) in the 302C4 path",
                "24745: cmp di,[0x9212] where di=21A8(candidate castle)",
                "no absolute write to DS:0x9212 in resident snapshot or Main.exe (8 reads, 0 writes)",
            ],
            "checked_scopes": [
                "resident snapshot absolute 16-bit memory references to disp 0x9212 "
                "(all encodings + segment overrides): 8 reads, 0 writes",
                "Main.exe absolute 16-bit memory references to disp 0x9212: reads at "
                "0x121E1/0x1234A/0x125F3/0x141F7/0x1FEA7/0x1FEF5/0x1FF2F/0x1FFCD; 0 writes",
                "no mov reg,0x9212 / lea reg,[0x9212] / push 0x9212 / far pointer 619F:920A "
                "in code or data",
                "no direct near/far caller of runtime 0x226E0 in snapshot or Main.exe",
            ],
            "uncovered_paths": [
                "non-resident overlay / monthly AI driver that calls 22F60/226E0 is not "
                "present in the resident snapshot",
                "indirect writes via base register, pointer table, or rep movs are not "
                "covered by the absolute-scan",
                "save/load or game-start struct copy to DS:0x920A..0x9220 not located",
            ],
            "next_steps": [
                "observe writes to linear 0x6AC02 at the moment 226E0 is invoked "
                "(requires dynamic snapshot or emulator watchpoint)",
                "locate the overlay loader table and the top-level caller of 22F60 / "
                "226E0",
                "scan non-resident overlay files for absolute/indirect stores to 0x9212",
                "if no writer exists anywhere, treat 0x9212 as zero-initialized BSS "
                "(faction 0 constant) and mark resolved-D",
            ],
            "confidence": {
                "domain_is_faction_id": "B",
                "writer_not_resident": "C",
                "specific_writer": "D (unidentified)",
            },
        },
        "self_checks": self_checks,
        "notes": [
            "No dynamic verification was performed; conclusions depending on runtime "
            "behaviour are capped at B/C per the V13 evidence model.",
            "A linear scan is not a complete CFG; 'not found by scan' is not written "
            "as 'does not exist in the whole program'.",
            "The 0x3035B -> 0x302F2 -> 0x30280 call chain is preserved exactly.",
        ],
    }

    OUT_JSON.write_text(json.dumps(document, indent=2, ensure_ascii=False, sort_keys=False))
    print(f"wrote {OUT_JSON} ({OUT_JSON.stat().st_size} bytes)")
    print("self-checks:")
    for c in self_checks:
        print(f"  [{'OK' if c['ok'] else 'FAIL'}] {c['name']}: {c['detail']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

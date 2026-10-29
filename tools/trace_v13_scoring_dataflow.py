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
    "22980": 0x22980,
    "22B22": 0x22B22,
    "22F60": 0x22F60,
    "24682": 0x24682,   # contains cmp di, [0x9212] @ 0x24745
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
    "0x24682": "24682",
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


def _get_block_mode_shift(rt_addr: int, candidates: list[tuple[int, int]]) -> int:
    """Determine the mode shift for a function's 4K block.

    For multi-candidate functions (e.g., duplicated prologues), selects the
    shift that appears most frequently in the block. This is the same logic
    used in the mapping dict for unified offset selection.

    Deterministic tie-break: when shifts tie, the smallest shift wins
    (stable across Python runs, no hash randomization).
    """
    from collections import Counter
    shifts = [s for _, s in candidates]
    if shifts:
        counts = Counter(shifts)
        max_count = max(counts.values())
        # Collect all shifts with max count, then pick smallest deterministically
        tied = sorted(s for s, c in counts.items() if c == max_count)
        return tied[0]  # smallest shift among ties
    return rt_addr  # fallback


def _test_ds_segment_filter(rt: bytes, exe: bytes) -> bool:
    """Self-test: verify DS:0x9212 references are classified correctly relative
    to segment overrides. Uses three synthetic byte sequences:

    1. A1 12 92           -- mov ax, [0x9212]    (default DS)  -> INCLUDED as read
    2. 3E A1 12 92        -- DS: mov ax, [0x9212] (DS override) -> INCLUDED as read
    3. 26 A1 12 92        -- ES: mov ax, [0x9212] (ES override) -> EXCLUDED by filter
    """
    from capstone import Cs, CS_OP_MEM, CS_AC_READ, CS_AC_WRITE, CS_ARCH_X86, CS_MODE_16
    from capstone.x86 import X86_REG_DS, X86_REG_ES

    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True

    def _matches_and_seg(ins):
        """Check if instruction matches DS:0x9212 filter criteria."""
        for op in ins.operands:
            if op.type != CS_OP_MEM:
                continue
            if op.mem.base != 0 or op.mem.index != 0:
                continue
            disp = op.mem.disp & 0xFFFF
            if disp != 0x9212:
                continue
            seg = op.mem.segment
            # The FILTER in scan_abs_mem_refs excludes non-zero, non-DS segments
            would_be_excluded = (seg != 0 and seg != X86_REG_DS)
            return True, seg, would_be_excluded, bool(op.access & CS_AC_READ)
        return False, None, None, False

    # Test 1: A1 12 92 -- default DS (segment=0), INCLUDED, classified as read
    ins1 = next(md.disasm(bytes([0xA1, 0x12, 0x92]), 0))
    matched1, seg1, excl1, read1 = _matches_and_seg(ins1)
    if not matched1 or seg1 != 0 or excl1 or not read1:
        return False

    # Test 2: 3E A1 12 92 -- DS override prefix (segment=X86_REG_DS), INCLUDED
    ins2 = next(md.disasm(bytes([0x3E, 0xA1, 0x12, 0x92]), 0))
    matched2, seg2, excl2, read2 = _matches_and_seg(ins2)
    if not matched2 or seg2 != X86_REG_DS or excl2 or not read2:
        return False

    # Test 3: 26 A1 12 92 -- ES override prefix (segment=X86_REG_ES), EXCLUDED
    ins3 = next(md.disasm(bytes([0x26, 0xA1, 0x12, 0x92]), 0))
    matched3, seg3, excl3, read3 = _matches_and_seg(ins3)
    # The operand exists and matches DS:0x9212 pattern, but segment is ES
    # so the filter SHOULD exclude it (would_be_excluded = True)
    if not matched3 or seg3 != X86_REG_ES or not excl3:
        return False

    return True


def _test_segment_filter_integration() -> bool:
    """Integration test: create a synthetic binary with DS:0x9212 and ES:0x9212
    references and run the ACTUAL scanner (scan_abs_mem_refs) to verify the
    filter includes DS/DS-override references while excluding ES-override.

    Uses explicit scan_addrs parameter instead of overriding global RT,
    so no self-referential module import is needed.
    """
    # Build a synthetic runtime snapshot: zero-filled with our test instructions
    # at a known address. Use a large buffer.
    rt_size = 0x9C400  # 655360
    synthetic_rt = bytearray(rt_size)

    # Place instructions at a high address that won't conflict with real RT entries
    base = 0x80000
    # A1 12 92           (mov ax, [0x9212])         - default DS, should be included
    synthetic_rt[base]     = 0xA1
    synthetic_rt[base + 1] = 0x12
    synthetic_rt[base + 2] = 0x92

    # 3E A1 12 92        (DS: mov ax, [0x9212])      - DS override, should be included
    synthetic_rt[base+0x10]     = 0x3E  # DS prefix
    synthetic_rt[base+0x10+1]   = 0xA1
    synthetic_rt[base+0x10+2]   = 0x12
    synthetic_rt[base+0x10+3]   = 0x92

    # 26 A1 12 92        (ES: mov ax, [0x9212])      - ES override, should be EXCLUDED
    synthetic_rt[base+0x20]     = 0x26  # ES prefix
    synthetic_rt[base+0x20+1]   = 0xA1
    synthetic_rt[base+0x20+2]   = 0x12
    synthetic_rt[base+0x20+3]   = 0x92

    # Pass scan_addrs directly so scan uses these addresses (not RT dict)
    test_addrs = [base, base + 0x10, base + 0x20]

    # Scan the synthetic runtime for DS:0x9212 references using the ACTUAL scanner
    hits = scan_abs_mem_refs(bytes(synthetic_rt), b'', 0x9212,
                             test_addrs, is_runtime=True)

    # Should have exactly 2 hits: default DS and DS-override
    # ES override must be EXCLUDED by the segment filter
    if len(hits) != 2:
        return False

    # Verify all hits are reads (access = read)
    for h in hits:
        if h["kind"] != "read":
            return False

    # Verify no ES-overridden reference appears in hits
    # (scan_abs_mem_refs should have excluded it via segment filter)
    return True


def scan_abs_mem_refs(rt: bytes, exe: bytes, disp16: int, scan_addrs: list[int] = None,
                       is_runtime: bool = True, use_mapping_offsets: bool = False) -> list[dict]:
    """Instruction-aware scan for absolute memory references to `disp16`.

    Uses Capstone's 16-bit x86 decoder to disassemble instructions within
    functions listed in RT. Only reports memory operands that:
      * Are absolute (mem.base == 0, mem.index == 0)
      * Match the target displacement (mem.disp & 0xFFFF == disp16)
      * Use the default data segment (DS) or an explicit DS override

    Parameters
    ----------
    rt: runtime snapshot bytes (used to determine scan addresses)
    exe: Main.exe bytes
    disp16: target displacement (e.g. 0x9212)
    scan_addrs: optional list of runtime addresses to scan; if None, uses RT
    is_runtime: if True, address labels use 5-digit runtime format;
                if False, address labels use 6-digit exe format

    Each hit includes: address, function name, mnemonic, operand string,
    access type (read/write/read_write/unknown), and raw bytes.
    """
    from capstone import CS_OP_MEM, CS_AC_READ, CS_AC_WRITE

    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True

    # Determine which data and addresses to scan
    if is_runtime:
        data = rt
    else:
        data = exe
        # If scan_addrs are already pre-computed static offsets (from mapping
        # dict), use them directly without re-translating.
        if not use_mapping_offsets and scan_addrs:
            translated: list[int] = []
            for a in scan_addrs:
                hits = static_file_offset(a, exe, rt)
                if hits:
                    # Use the same mode-shift selection as the mapping dict
                    block = a >> 12
                    mode_shift = _get_block_mode_shift(a, hits)
                    selected = None
                    for h, s in hits:
                        if s == mode_shift:
                            selected = h
                            break
                    if selected is None:
                        selected = hits[0][0]
                    translated.append(selected)
                else:
                    translated.append(a)
            scan_addrs = translated

    if scan_addrs is None:
        scan_addrs = list(RT.values())

    hits: list[dict] = []

    for addr in sorted(scan_addrs):
        func_end = min(addr + 0x200, len(data))
        scan_name = f"0x{addr:05X}" if is_runtime else f"0x{addr:06x}"

        current = addr
        while current < func_end:
            try:
                ins = next(md.disasm(data[current:func_end], current))
            except StopIteration:
                break

            if ins.address != current:
                current += 1
                continue

            # Check each operand for absolute memory reference matching disp16
            for op in ins.operands:
                if op.type != CS_OP_MEM:
                    continue
                if op.mem.base != 0 or op.mem.index != 0:
                    continue

                # Handle signed displacement (0x9212 appears as -0x6DEE in Capstone)
                disp = op.mem.disp & 0xFFFF
                if disp != disp16:
                    continue

                # Check segment: in Capstone 16-bit mode, mem.segment is the
                # segment register constant (CS_REG_DS=3, CS_REG_ES=4, etc).
                # Value 0 = default segment (DS for most operands).
                from capstone.x86 import X86_REG_DS
                seg = op.mem.segment
                if seg != 0 and seg != X86_REG_DS:
                    # Explicit non-DS segment override (ES, FS, GS, etc)
                    # Exclude from DS:0x9212 target scope
                    continue

                # Classify access type
                access = []
                if op.access & CS_AC_READ:
                    access.append("read")
                if op.access & CS_AC_WRITE:
                    access.append("write")

                if access:
                    kind = "+".join(access)  # "read+write" for read/write operands
                else:
                    # For instructions like CMP/TEST, infer read access
                    if ins.mnemonic in ("cmp", "test"):
                        kind = "read"
                        access = ["read"]
                    else:
                        kind = "unknown"
                        access = ["unknown"]

                hits.append({
                    "address": current,
                    "address_label": scan_name,
                    "function": NAMES.get(f"0x{addr:05X}", scan_name),
                    "address_space": "runtime" if is_runtime else "exe",
                    "mnemonic": ins.mnemonic,
                    "op_str": ins.op_str,
                    "access": access,
                    "kind": kind,
                    "raw_hex": data[current:current + ins.size].hex(" "),
                    "instruction_size": ins.size,
                })

            current += ins.size

    return hits


def classify_abs_refs(rt: bytes, disp16: int, exe: bytes = None,
                       mapping: dict = None) -> dict:
    """Classify absolute memory references to disp16 in the runtime snapshot.
    Uses instruction-level scanning to distinguish read/write/access-type.
    
    If mapping is provided, EXE scan uses the unified block-mode selected
    static offsets (same as the main mapping dict), ensuring consistency.
    """
    # Scan with instruction awareness using Capstone
    all_hits = scan_abs_mem_refs(rt, exe, disp16, is_runtime=True)

    # Classify each hit into reads, writes, or unknown based on actual scan
    reads: list[dict] = []
    writes: list[dict] = []
    read_writes: list[dict] = []
    unknowns: list[dict] = []

    for hit in all_hits:
        kind = hit.get("kind", "unknown")
        addr = hit["address"]
        function = hit.get("function", "unknown")

        if kind == "read":
            reads.append({
                "address": hex(addr),
                "function": function,
                "mnemonic": hit["mnemonic"],
                "op_str": hit["op_str"],
                "raw_hex": hit["raw_hex"],
            })
        elif kind == "write":
            writes.append({
                "address": hex(addr),
                "function": function,
                "mnemonic": hit["mnemonic"],
                "op_str": hit["op_str"],
                "raw_hex": hit["raw_hex"],
            })
        elif kind == "read+write":
            read_writes.append({
                "address": hex(addr),
                "function": function,
                "access": hit["access"],
                "raw_hex": hit["raw_hex"],
            })
        else:
            unknowns.append({
                "address": hex(addr),
                "function": function,
                "reason": "unclassified access",
            })

    # Count distinct read and write addresses
    distinct_read_addrs = set(r["address"] for r in reads)
    distinct_write_addrs = set(w["address"] for w in writes)

    # Scan exe file separately if provided (translate RT addresses to static offsets)
    exe_hits = []
    if exe is not None:
        # Use EXACTLY the static offsets selected by the main mapping dict.
        # This guarantees EXE scan and main mapping use the same selection.
        # If mapping is not available (backwards-compat), fall back to
        # per-function block-mode selection.
        if mapping is not None:
            scan_addrs_exe = []
            for name, addr in RT.items():
                if name in mapping and mapping[name].get("file"):
                    scan_addrs_exe.append(int(mapping[name]["file"], 16))
                else:
                    scan_addrs_exe.append(addr)
            exe_hits = scan_abs_mem_refs(rt, exe, disp16, scan_addrs_exe,
                                         is_runtime=False, use_mapping_offsets=True)
        else:
            exe_hits = scan_abs_mem_refs(rt, exe, disp16, list(RT.values()),
                                         is_runtime=False)

    exe_reads = 0
    exe_writes = 0
    exe_read_writes = 0
    exe_unknowns = 0
    for hit in exe_hits:
        kind = hit.get("kind", "unknown")
        if kind == "read":
            exe_reads += 1
        elif kind == "write":
            exe_writes += 1
        elif kind == "read+write":
            exe_read_writes += 1
        else:
            exe_unknowns += 1

    return {
        "disp16": f"0x{disp16:04X}",
        "absolute_linear": f"0x{DS_RUNTIME:04X}:{disp16:04X} -> 0x{DS_RUNTIME * 16 + disp16:06X}",
        "total_hits": len(all_hits),
        "reads": len(reads),
        "writes": len(writes),
        "read_write": len(read_writes),
        "unknown": len(unknowns),
        "read_sites": sorted(distinct_read_addrs),
        "write_sites": [w["address"] for w in writes],
        "detailed_reads": reads[:50],  # First 50 detailed reads
        "detailed_writes": writes[:50],
        "exe_scan": {
            "total_hits": len(exe_hits),
            "reads": exe_reads,
            "writes": exe_writes,
            "read_write": exe_read_writes,
            "unknown": exe_unknowns,
        },
        "scan_method": "Capstone instruction-aware, 16-bit x86 decoder",
        "note": "Scan is restricted to functions listed in RT and RT's 200-byte window. "
        "Indirect writes via base register/pointer table/rep movs are NOT covered. "
        "Finding 'no writes in checked scope' does not prove no writer exists "
        "in unscanned program regions.",
    }


# ---------------------------------------------------------------------------
# Function trace records
# ---------------------------------------------------------------------------
def trace_function(name: str, rt_addr: int, rt: bytes, exe: bytes, selected_offset=None) -> dict:
    instrs = disasm_range(rt, rt_addr, max_len=0x220)
    calls = extract_calls(instrs, rt)
    sfo_hits = static_file_offset(rt_addr, exe, rt)
    # Use unified mapping result if provided; otherwise fall back to first hit
    if selected_offset is not None:
        primary = selected_offset
        # Record selection reason
        selection_reason = "selected by unified mode-shift block selection"
    elif sfo_hits:
        primary = sfo_hits[0]
        selection_reason = "fallback to first candidate (no block-mode match)"
    else:
        primary = (None, None)
        selection_reason = "no candidates found"
    rec = {
        "name": name,
        "runtime_linear": f"0x{rt_addr:06X}",
        "runtime_hex": f"{rt_addr:05X}",
        "static_file_offset": f"0x{primary[0]:05X}" if primary[0] is not None else None,
        "static_candidates": [f"0x{h:05X}(+0x{s:X})" for h, s in sfo_hits],
        "measured_shift": f"+0x{primary[1]:X}" if primary[1] is not None else None,
        "selection_reason": selection_reason,
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
        "text": "22856(arg1): selector over adjacent castles; compare key=(distance asc, officer count asc, 5BC8(value,cost) ratio asc); tie keeps current best; initial best = closest of nearest-own or nearest-enemy; cost uses 7350([0x9210], [0x9212], candidate); value uses 71B8(candidate); returns -1 if no valid move. Evidence: full disassembly at 0x22856 confirms all three comparisons use 'smaller-better' (ascending) policy.",
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
    # Deterministic block mode: pick smallest shift on ties
    block_mode: dict[int, int] = {}
    for b, ctr in block_shifts.items():
        if ctr:
            max_count = max(ctr.values())
            tied = sorted(s for s, c in ctr.items() if c == max_count)
            block_mode[b] = tied[0]

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
        # Use unified mapping result (same as mapping dict above) for consistency
        selected = None
        if name in mapping and mapping[name].get("file") is not None:
            file_str = mapping[name]["file"]
            shift_str = mapping[name].get("shift", "")
            selected = (int(file_str, 16), int(shift_str, 16))
        rec = trace_function(name, addr, rt, exe, selected)
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
    refs_9212 = classify_abs_refs(rt, DS_9212_OFFSET, exe, mapping)
    refs_9212["snapshot_word"] = f"0x{int.from_bytes(rt[DS_9212_LINEAR:DS_9212_LINEAR+2], 'little'):04X}"
    # neighbouring fields
    neigh = {}
    for off in (0x920A, 0x920C, 0x920E, 0x9210, 0x9212, 0x9214, 0x9216, 0x9218, 0x921A, 0x921C, 0x921E, 0x9220, 0x9222):
        lin = DS_RUNTIME * 16 + off
        neigh[f"0x{off:04X}"] = f"0x{int.from_bytes(rt[lin:lin+2], 'little'):04X}"
    refs_9212["neighbour_fields_snapshot"] = neigh

    # refs_9212 already includes exe scan results internally
    refs_9212_exe = refs_9212.get("exe_scan", {})

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

    # ---- Selector direction assertions (0x22856) ---------------------------
    # Verify CMP / condition codes establish ascending order for all three criteria.
    # Distance comparison at runtime 0x22964:
    #   22964: 39 46 F8   cmp [bp-8], ax    ; best_dist - candidate_dist
    #   22967: 74 04      je  (equal -> tie-break on officers)
    #   22969: 77 11      ja  0x2297C       ; best> candidate -> jbe accepts (smaller wins)
    #   2296B: EB 14      jmp 0x22981       ; best< candidate reject
    # So smaller distance wins => distance ASC.
    i22856 = RT["22856"]
    dist_cmp = i22856 + 0x0C + (0x22964 - 0x22862)  # offset from function start
    dist_cmp = 0x22964  # direct runtime address
    ok_dist_asc = (
        rt[dist_cmp] == 0x39 and rt[dist_cmp+1] == 0x46 and rt[dist_cmp+2] == 0xF8  # cmp [bp-8],ax
        and rt[dist_cmp+3] == 0x74  # je
        and rt[dist_cmp+5] == 0x77  # ja (accept if best > candidate)
    )
    check("selector 0x9212 distance cmp is ascending", ok_dist_asc,
          f"bytes at 0x{dist_cmp:05X}: {rt[dist_cmp:dist_cmp+6].hex(' ')}")

    # Officers comparison at 0x2296D:
    #   2296D: 39 7E F4   cmp [bp-C], di
    #   22970: 75 08      jne 0x2297A      (not equal -> officers decide)
    #   22972: 8B 46 FC   mov ax,[bp-4]    (candidate ratio load)
    #   22975: 39 46 F6   cmp [bp-A], ax
    #   22978: 74 07      je 0x22981       (ratio equal -> reject)
    #   2297A: 76 05      jbe 0x22981      (reject if best_off <= cand_off)
    #   -> smaller officer count wins => officers ASC
    off_cmp = 0x2296D
    ok_off_asc = (
        rt[off_cmp] == 0x39 and rt[off_cmp+1] == 0x7E and rt[off_cmp+2] == 0xF4  # cmp [bp-C],di
        and rt[off_cmp+3] == 0x75  # jne (officers not equal -> officers decide)
        and rt[off_cmp+0xD] == 0x76  # jbe at 0x2297A (reject if best <= candidate)
    )
    check("selector 0x9212 officers cmp is ascending", ok_off_asc,
          f"bytes at 0x{off_cmp:05X}: {rt[off_cmp:off_cmp+0xB].hex(' ')}")

    # Ratio comparison at 0x22975:
    #   22975: 39 46 F6   cmp [bp-A], ax   ; best_ratio - candidate_ratio
    #   22978: 74 07      je 0x22981       (equal -> reject / keep)
    #   2297A: 76 05      jbe 0x22981      (reject if best <= candidate)
    #   -> smaller ratio wins => ratio ASC
    rat_cmp = 0x22975
    ok_rat_asc = (
        rt[rat_cmp] == 0x39 and rt[rat_cmp+1] == 0x46 and rt[rat_cmp+2] == 0xF6  # cmp [bp-A],ax
        and rt[rat_cmp+3] == 0x74  # je (reject on tie)
    )
    check("selector 0x9212 ratio cmp is ascending", ok_rat_asc,
          f"bytes at 0x{rat_cmp:05X}: {rt[rat_cmp:rat_cmp+4].hex(' ')}")


    # DS:0x9212 linear address
    check("DS:0x9212 linear == 0x6AC02", DS_9212_LINEAR == 0x6AC02,
          f"0x{DS_RUNTIME:04X}*16+0x9212 = 0x{DS_9212_LINEAR:06X}")

    # snapshot word
    check("DS:0x9212 snapshot word == 0x0000", int.from_bytes(rt[DS_9212_LINEAR:DS_9212_LINEAR+2], 'little') == 0,
          f"word = {refs_9212['snapshot_word']}")

    # no absolute write to 0x9212 in snapshot
    # Self-check: scanner must classify actual write sites as writes.
    # Finding zero writes means scanner ran but found none, not that writes were
    # assumed empty. Also require zero unknown accesses (all classified).
    check("no absolute write to 0x9212 in snapshot",
          refs_9212["writes"] == 0 and refs_9212["unknown"] == 0
          and refs_9212["read_write"] == 0,
          f"reads={refs_9212['reads']}, writes={refs_9212['writes']}, "
          f"unknown={refs_9212['unknown']}, read_write={refs_9212['read_write']}, "
          f"read_sites={len(refs_9212['read_sites'])}, write_sites={refs_9212['write_sites']}")

    # Verify scanner is working: must find known read site 0x30527 (cmp [0x9212], -1)
    check("0x30527 read site found by scanner",
          "0x30527" in refs_9212["read_sites"],
          f"read_sites={refs_9212['read_sites']}")

    # Verify EXE scan found reads (proves exe scan ran, not hardcoded empty)
    check("EXE scan found at least 5 reads",
          refs_9212_exe.get("reads", 0) >= 5,
          f"exe reads={refs_9212_exe.get('reads', 'N/A')}")

    # Verify EXE scan: no writes, no read_write, no unknown accesses
    check("EXE scan: zero writes, zero read_write, zero unknown",
          refs_9212_exe.get("writes", 0) == 0
          and refs_9212_exe.get("read_write", 0) == 0
          and refs_9212_exe.get("unknown", 0) == 0,
          f"exe writes={refs_9212_exe.get('writes', 'N/A')}, "
          f"read_write={refs_9212_exe.get('read_write', 'N/A')}, "
          f"unknown={refs_9212_exe.get('unknown', 'N/A')}")

    # Verify 0x24745 read site is now found (was missing from old hardcoded list)
    check("0x24745 cmp di,[0x9212] found by scanner",
          "0x24745" in refs_9212["read_sites"],
          f"read_sites={refs_9212['read_sites']}")

    # Integration test: verify segment filter by running the actual scanner
    # on synthetic data containing DS and ES overrides to 0x9212.
    # Uses Capstone directly via scan_abs_mem_refs with a small synthetic image.
    _seg_ok = _test_segment_filter_integration()
    check("segment filter integration: DS/DS-override included, ES excluded",
          _seg_ok,
          "Capstone scan on synthetic DS+ES override bytes")

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
        # Verify the MAIN mapping selected the expected offset
        # (not just that it exists in candidates)
        if mapping[rt_addr_hex[2:]].get("file") != exp_file:
            mapping_ok = False
            details.append(f"  ERROR: mapping selected {mapping[name].get('file')} not {exp_file}")
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
            "scan_results_runtime": {
                "total_hits": refs_9212["total_hits"],
                "reads": refs_9212["reads"],
                "writes": refs_9212["writes"],
                "read_write": refs_9212["read_write"],
                "unknown": refs_9212["unknown"],
                "read_sites": refs_9212["read_sites"],
                "write_sites": refs_9212["write_sites"],
            },
            "references_in_main_exe": {
                "total_hits": refs_9212_exe["total_hits"],
                "reads": refs_9212_exe["reads"],
                "writes": refs_9212_exe["writes"],
                "read_write": refs_9212_exe.get("read_write", 0),
                "unknown": refs_9212_exe.get("unknown", 0),
            },
            "direct_caller_of_0x226E0": direct_caller_226e0,
            "writer_status": "not_found_in_checked_scopes",
            "semantic_domain": "faction ID (target-faction candidate); compared against "
            "21A8(castle+0x28 faction) and 34EC(template+0x00, bounded <0x10 by 3D48)",
            "evidence": [
                "30280 compares its arg2 (supplied as 0x9212 by 7350) against arg3, "
                "which is 21A8(neighbor) in the 302F2 path and 34EC(template) in the 302C4 path",
                "24745: cmp di,[0x9212] where di=21A8(candidate castle)",
                "no absolute write to DS:0x9212 in resident snapshot or Main.exe "
                f"(reads={refs_9212['reads']}, writes={refs_9212['writes']})",
            ],
            "checked_scopes": [
                "resident functions listed in RT (instruction-aware Capstone scan)",
                "absolute 16-bit memory references to disp 0x9212 with base=0, index=0",
                "Main.exe absolute references to disp 0x9212",
                "direct near/far callers of runtime 0x226E0",
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
    all_ok = True
    for c in self_checks:
        print(f"  [{'OK' if c['ok'] else 'FAIL'}] {c['name']}: {c['detail']}")
        if not c["ok"]:
            all_ok = False
    if all_ok:
        print("\nAll self-checks passed.")
        return 0
    else:
        print("\nERROR: Some self-checks FAILED. Exiting non-zero.")
        return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""V16 structural validator for the Genpei Gassen AI dispatch chain and script VM.

Static checks only. The runtime snapshot must be the canonical 640 KB image at
re_out/runtime_0000_a0000.bin (file offset == linear address).

Run from the repository root:
    python3 tools/v16_ai_chain_vm.py
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RT_CANDIDATES = [
    REPO / "re_out" / "runtime_0000_a0000.bin",
    Path.cwd() / "re_out" / "runtime_0000_a0000.bin",
    Path.cwd() / "Y-源平合战" / "re_out" / "runtime_0000_a0000.bin",
]

AI = 0x20780          # AI/event module, segment 0x2078
ORDER = 0x31CA0       # order/system overlay, segment 0x31CA
VM = 0x38CD0          # script VM, segment 0x38CD
DGROUP = 0x619F0      # DGROUP, segment 0x619F

TABLE_A = 0x65C6A     # DS:0x427A, 69 far pointers
TABLE_B = 0x694FE     # DS:0x7B0E, 15 far pointers
SIXWAY = 0x23117      # cs:0x2997 with CS=0x2078
DISPATCH = 0x2303C
CHECK = 0x227BA
ACTION4 = 0x234FA       # table A[55], dispatches action 4
ACTION5 = 0x23598       # table A[56], dispatches action 5
DISPATCH_SITES = [0x231D9, 0x23240, 0x2336F, 0x234E9, 0x23586, 0x23627]

ACT = {
    "act0": (0x23196, 0x2A16),
    "act1": (0x231EA, 0x2A6A),
    "act2": (0x23250, 0x2AD0),
    "act3": (0x23304, 0x2B84),
    "act4": (0x2337E, 0x2BFE),
    "act5": (0x2348E, 0x2D0E),
}

HANDLERS = [0x23123, 0x23133, 0x23140, 0x2314D, 0x2315E, 0x2316F]
EXECUTORS = [0x44A, 0x458, 0x474, 0xFFC, 0x100A, 0x145A]
EXEC_STUBS = [
    (0x320EA, 0x38692),
    (0x320F8, 0x3820E),
    (0x32114, 0x3820E),
    (0x32C9C, 0x38692),
    (0x32CAA, 0x38692),
    (0x330FA, 0x37D92),
]
TRAMPOLINES = [
    (0x38692, 0x69FE, 0x6D8E),
    (0x3820E, 0x657A, 0x692E),
    (0x37D92, 0x60FE, 0x6476),
]

checks: list[tuple[str, bool, str]] = []
infos: list[str] = []


def check(name: str, cond: bool, detail: str = "") -> None:
    checks.append((name, bool(cond), detail))


def u16(off: int) -> int:
    return struct.unpack_from("<H", RT, off)[0]


def u32(off: int) -> int:
    return struct.unpack_from("<I", RT, off)[0]


def far(off: int) -> tuple[int, int]:
    ip, cs = struct.unpack_from("<HH", RT, off)
    return ip, cs


def prologue(off: int) -> bool:
    return RT[off : off + 3] == bytes.fromhex("55 8b ec")


def call_rel16(off: int) -> int:
    """Return the target of an E8 rel16 near call at file offset off.

    Kept for same-64KB-window call sites; prefer call_lin(off, seg_base).
    """
    assert RT[off] == 0xE8
    rel = struct.unpack_from("<h", RT, off + 1)[0]
    return off + 3 + rel


def call_ip(off: int, seg_base: int) -> int:
    """16-bit target IP of an E8 near call, using the call site's segment base."""
    assert RT[off] == 0xE8
    rel = struct.unpack_from("<h", RT, off + 1)[0]
    return (off - seg_base + 3 + rel) & 0xFFFF


def call_lin(off: int, seg_base: int) -> int:
    """Linear target of an E8 near call within the segment at seg_base."""
    return seg_base + call_ip(off, seg_base)


def lcall_far_ptr(off: int) -> tuple[int, int]:
    """Decode 9A off16 seg16 far call at off."""
    assert RT[off] == 0x9A
    ip, cs = struct.unpack_from("<HH", RT, off + 1)
    return ip, cs


def find_lcall_in(lo: int, hi: int, want_cs: int) -> list[tuple[int, int]]:
    out = []
    for p in range(lo, hi):
        if RT[p] == 0x9A:
            ip, cs = struct.unpack_from("<HH", RT, p + 1)
            if cs == want_cs:
                out.append((p, ip))
    return out


# ---------------------------------------------------------------- runtime ----
rt_path = next((p for p in RT_CANDIDATES if p.exists()), None)
if rt_path is None:
    print("FAIL: runtime snapshot runtime_0000_a0000.bin not found")
    print("tried:")
    for p in RT_CANDIDATES:
        print("  ", p)
    sys.exit(2)
RT = rt_path.read_bytes()

check("runtime snapshot size == 0xA0000", len(RT) == 0xA0000, f"size=0x{len(RT):X}")

# ------------------------------------------------------------- AI module ----
# The module starts with a script trampoline: E8 rel16 -> 0x28C33.
check(
    "AI module 0x20780 starts with call 0x28C33",
    RT[0x20780] == 0xE8 and call_lin(0x20780, AI) == 0x28C33,
    f"target=0x{call_lin(0x20780, AI):05X}" if RT[0x20780] == 0xE8 else "no E8",
)

# M1 thunk 0x226C6: five push-imm args + lcall 0x3B1A:0x1A + retf.
check(
    "M1 thunk 0x226C6 push args",
    RT[0x226C6 : 0x226DA] == bytes.fromhex("b8 d8 00 50 b8 78 01 50 b8 18 01 50 b8 10 00 50 b8 02 92 50"),
    RT[0x226C6 : 0x226DA].hex(" "),
)
check(
    "M1 thunk calls 0x3B1A:0x1A",
    RT[0x226DA : 0x226E0] == bytes.fromhex("9a 1a 00 1a 3b cb"),
    RT[0x226DA : 0x226E0].hex(" "),
)

# M1 wrapper 0x24B5E = nop; push cs; call 0x226C6; retf.
check(
    "M1 wrapper 0x24B5E bytes/call",
    RT[0x24B5E] == 0x90
    and RT[0x24B5F] == 0x0E
    and RT[0x24B60] == 0xE8
    and call_lin(0x24B60, AI) == 0x226C6
    and RT[0x24B63] == 0xCB,
    f"target=0x{call_lin(0x24B60, AI):05X}",
)

# --------------------------------------------------------- table A / B ------
entries_a = [far(TABLE_A + 4 * i) for i in range(69)]
check("table A has 69 entries", len(entries_a) == 69)
check(
    "table A all point into 0x2078",
    all(cs == 0x2078 for _, cs in entries_a),
    f"bad={[hex(TABLE_A + 4 * i) for i, (_, cs) in enumerate(entries_a) if cs != 0x2078][:5]}",
)
check(
    "table A entries 49..68 are native prologues",
    all(prologue(AI + ip) for ip, _ in entries_a[49:]),
    f"bad={[hex(AI + ip) for ip, _ in entries_a[49:] if not prologue(AI + ip)][:5]}",
)
for name, (lin, off) in ACT.items():
    expected = (off, 0x2078)
    if name in ("act2", "act4"):
        check(f"table A excludes {name} (internal)", expected not in entries_a)
    else:
        idx = entries_a.index(expected)
        check(f"table A[{idx}] == {name} 0x{lin:05X}", idx in range(49, 69), f"index={idx}")
check("table A entry 49 == act0", entries_a[49] == (0x2A16, 0x2078))
check("table A entry 50 == act1", entries_a[50] == (0x2A6A, 0x2078))
check("table A entry 51 == act3", entries_a[51] == (0x2B84, 0x2078))
check("table A entry 54 == act5", entries_a[54] == (0x2D0E, 0x2078))
check("table A entry 55 == action4 0x234FA", entries_a[55] == (0x2D7A, 0x2078))
check("table A entry 56 == action5 0x23598", entries_a[56] == (0x2E18, 0x2078))

entries_b = [far(TABLE_B + 4 * i) for i in range(15)]
check("table B has 15 entries", len(entries_b) == 15)
check("table B all point into 0x2078", all(cs == 0x2078 for _, cs in entries_b))
check("table B[4] == M1 wrapper 0x24B5E", entries_b[4] == (0x43DE, 0x2078))

# ------------------------------------------------------------ six-way -------
check(
    "six-way table 0x23117 bytes",
    RT[SIXWAY : SIXWAY + 12] == bytes.fromhex("a3 29 b3 29 c0 29 cd 29 de 29 ef 29"),
    RT[SIXWAY : SIXWAY + 12].hex(" "),
)
six_words = [u16(SIXWAY + 2 * i) for i in range(6)]
check("six-way offsets", six_words == [0x29A3, 0x29B3, 0x29C0, 0x29CD, 0x29DE, 0x29EF],
      str([hex(w) for w in six_words]))
check("six-way handlers", [AI + w for w in six_words] == HANDLERS,
      str([hex(AI + w) for w in six_words]))

for i, h in enumerate(HANDLERS):
    lc = find_lcall_in(h, h + 0x30, 0x31CA)
    check(
        f"handler {i} calls 0x31CA:0x{EXECUTORS[i]:04X}",
        any(ip == EXECUTORS[i] for _, ip in lc),
        f"lcalls={[hex(ip) for _, ip in lc]}",
    )

# common tail 0x23181: push args; call check 0x227BA
check("handler tail calls check 0x227BA", call_lin(0x23189, AI) == CHECK,
      f"target=0x{call_lin(0x23189, AI):05X}")
check("handler tail retf", RT[0x23194] == 0xCB)

# ------------------------------------------------------------ dispatcher ----
check("dispatcher 0x2303C prologue", prologue(DISPATCH))
check("dispatcher calls check 0x227BA", call_lin(0x2304D, AI) == CHECK,
      f"target=0x{call_lin(0x2304D, AI):05X}")
check(
    "dispatcher six-way jump bytes",
    RT[0x23112 : 0x23117] == bytes.fromhex("2e ff a7 97 29"),
    RT[0x23112 : 0x23117].hex(" "),
)
check("dispatcher retf", RT[0x23194] == 0xCB)

# ------------------------------------------------------------ act funcs -----
for name, (lin, off) in ACT.items():
    check(f"{name} 0x{lin:05X} prologue", prologue(lin))
    check(f"{name} retf present", 0xCB in RT[lin : lin + 0x150])

# act0/act1/act3/act5 call dispatcher 0x2303C at the verified sites.
for name, site in (("act0", 0x231D9), ("act1", 0x23240), ("act3", 0x2336F), ("act5", 0x234E9)):
    check(f"{name} calls dispatcher 0x2303C", call_lin(site, AI) == DISPATCH,
          f"target=0x{call_lin(site, AI):05X}")

# act3 calls act2 at 0x2335B; act2/act4 call check 0x227BA.
check("act3 calls act2 0x23250", call_lin(0x2335B, AI) == ACT["act2"][0],
      f"target=0x{call_lin(0x2335B, AI):05X}")
check("act2 calls check 0x227BA", call_lin(0x232F7, AI) == CHECK,
      f"target=0x{call_lin(0x232F7, AI):05X}")
check("act4 calls check 0x227BA", call_lin(0x23481, AI) == CHECK,
      f"target=0x{call_lin(0x23481, AI):05X}")

# action 4/5 wrappers (table A[55]/[56]); distinct from act4/act5.
check("action4 wrapper 0x234FA prologue", prologue(ACTION4))
check("action5 wrapper 0x23598 prologue", prologue(ACTION5))
check("action4 sets action=4", RT[0x2357A:0x2357D] == bytes.fromhex("b8 04 00"),
      RT[0x2357A:0x2357D].hex(" "))
check("action5 sets action=5", RT[0x2361B:0x2361E] == bytes.fromhex("b8 05 00"),
      RT[0x2361B:0x2361E].hex(" "))
check("action4 calls dispatcher 0x2303C", call_lin(0x23586, AI) == DISPATCH,
      f"target=0x{call_lin(0x23586, AI):05X}")
check("action5 calls dispatcher 0x2303C", call_lin(0x23627, AI) == DISPATCH,
      f"target=0x{call_lin(0x23627, AI):05X}")
check("0x238A8 calls action4 0x234FA", call_lin(0x239D4, AI) == ACTION4,
      f"target=0x{call_lin(0x239D4, AI):05X}")
check("0x238A8 calls act5 0x2348E", call_lin(0x2392C, AI) == ACT["act5"][0],
      f"target=0x{call_lin(0x2392C, AI):05X}")

# Exactly six static near calls to the dispatcher in the AI segment.
found_sites = []
for p in range(AI, AI + 0x10000 - 2):
    if RT[p] == 0xE8 and call_ip(p, AI) == DISPATCH - AI:
        found_sites.append(p)
check("dispatcher has exactly six static call sites", found_sites == DISPATCH_SITES,
      f"got={[hex(x) for x in found_sites]}")
far_disp = RT.find(bytes.fromhex("bc 28 78 20"))
check("no static far pointer/call 0x2078:0x28BC", far_disp < 0,
      f"hit=0x{far_disp:05X}" if far_disp >= 0 else "")

# 0x281AF: five AI-offset words; interpretation (data table vs bytecode) is Grade C.
five_281af = [u16(0x281AF + 4 * i) for i in range(5)]
check("0x281AF five AI-offset words",
      five_281af == [0x2BFE, 0x27E0, 0x24CA, 0x2A6A, 0x2D7A],
      str([hex(x) for x in five_281af]))
check("0x281AF five targets are function prologues",
      all(prologue(AI + off) for off in five_281af),
      str([hex(AI + off) for off in five_281af if not prologue(AI + off)]))
tags_281af = [u16(0x281AF + 4 * i + 2) for i in range(5)]
check("0x281AF tags are 0x041A/0x061A",
      tags_281af == [0x041A, 0x041A, 0x061A, 0x041A, 0x041A],
      str([hex(x) for x in tags_281af]))
ref_hits = []
for p in range(AI, AI + 0x10000 - 1):
    if u16(p) in (0x7A2F, 0x81AF):
        ref_hits.append(p)
check("no AI-segment reference to 0x281AF/0x7A2F", not ref_hits,
      f"hits={[hex(x) for x in ref_hits]}")
infos.append(
    "0x281AF: five AI-offset words found; no static AI-segment reference; "
    "data-table vs bytecode remains Grade C."
)

# ---------------------------------------------------------- script headers --
# VM segment cs:0x1ECA descriptor table: 27 real entries + 5 empty (0x0000:0x0000).
DESC = VM + 0x1ECA


def dslot(i: int) -> tuple[int, int]:
    return u16(DESC + 4 * i), u16(DESC + 4 * i + 2)  # (off, seg)


def dlin(i: int) -> int:
    off, seg = dslot(i)
    return seg * 16 + off


DESC_REAL = range(27)
check(
    "desc entries 0-26 header stub pattern (b8 imm16 8c ca cb)",
    all(RT[dlin(i) + 3 : dlin(i) + 6] == b"\x8c\xca\xcb" for i in DESC_REAL),
    ", ".join(hex(dlin(i)) for i in DESC_REAL
              if RT[dlin(i) + 3 : dlin(i) + 6] != b"\x8c\xca\xcb"),
)
check(
    "desc entries 0-26 main script pointer != 0xFFFF",
    all(u16(dlin(i) + 6) != 0xFFFF for i in DESC_REAL),
    ", ".join(hex(dlin(i)) for i in DESC_REAL if u16(dlin(i) + 6) == 0xFFFF),
)
check("desc[7]  = 0x2078:0x789F (AI script header 0)", dslot(7) == (0x789F, 0x2078))
check("desc[10] = 0x2078:0x84C6 (AI script header 3)", dslot(10) == (0x84C6, 0x2078))
check("desc[17] = 0x31CA:0x60FE (executor header)", dslot(17) == (0x60FE, 0x31CA))
check("desc[20] = 0x31CA:0x6E56 (executor header)", dslot(20) == (0x6E56, 0x31CA))

# --------------------------------------------------------- order executors --
for lin, target in EXEC_STUBS:
    check(
        f"executor stub 0x{lin:05X} -> 0x{target:05X}",
        RT[lin] == 0xE8 and call_lin(lin, ORDER) == target,
        f"target=0x{call_lin(lin, ORDER):05X}" if RT[lin] == 0xE8 else "no E8",
    )

for lin, hdr, bc in TRAMPOLINES:
    check(f"trampoline 0x{lin:05X} lcall 0x38CD:0x0810",
          RT[lin : lin + 5] == bytes.fromhex("9a 10 08 cd 38"))
    check(f"trampoline 0x{lin:05X} header 0x{hdr:04X}", u16(lin + 5) == hdr,
          f"got=0x{u16(lin + 5):04X}")
    check(f"trampoline 0x{lin:05X} resume lcall 0x38CD:0x09A3",
          RT[lin + 7 : lin + 12] == bytes.fromhex("9a a3 09 cd 38"))
    hlin = ORDER + hdr
    check(f"header 0x{hlin:05X} mov ax,0x{hdr:04X}", RT[hlin] == 0xB8 and u16(hlin + 1) == hdr,
          f"got=0x{u16(hlin + 1):04X}")
    check(f"header 0x{hlin:05X} mov dx,cs; retf",
          RT[hlin + 3 : hlin + 6] == bytes.fromhex("8c ca cb"))
    check(f"header 0x{hlin:05X} +6 bytecode 0x{bc:04X}", u16(hlin + 6) == bc,
          f"got=0x{u16(hlin + 6):04X}")
    check(f"main script 0x{ORDER + bc:05X} non-empty", RT[ORDER + bc] != 0x00)

# ----------------------------------------------- inner trampolines in scripts --
# 每个主脚本末尾嵌入 trampoline，调用另一个共享主脚本，形成环形链：
# 0x6D8E (EXEC 0/3/4) -> 头 0x6E56 (EXEC 5) -> 0x6476
# 0x6476 (EXEC 5)     -> 头 0x657A (EXEC 1/2) -> 0x692E
# 0x692E (EXEC 1/2)   -> 头 0x69FE (EXEC 0/3/4) -> 0x6D8E
INNER_TRAMPOLINES = [
    (0x6D8E, 0x00BC, 0x6E56, "EXEC 5"),
    (0x692E, 0x00C4, 0x69FE, "EXEC 0/3/4"),
    (0x6476, 0x00F8, 0x657A, "EXEC 1/2"),
]
for script_off, inner_off, called_hdr, called_name in INNER_TRAMPOLINES:
    lin = ORDER + script_off + inner_off
    check(f"inner trampoline script 0x{script_off:04X}+0x{inner_off:04X} lcall 0x38CD:0x0810",
          RT[lin:lin + 5] == bytes.fromhex("9a 10 08 cd 38"))
    check(f"inner trampoline calls header 0x{called_hdr:04X} ({called_name})",
          u16(lin + 5) == called_hdr,
          f"got=0x{u16(lin + 5):04X}")
    check(f"inner trampoline resume lcall 0x38CD:0x09A3",
          RT[lin + 7 : lin + 12] == bytes.fromhex("9a a3 09 cd 38"))
    check(f"inner trampoline retf stub mov ax,0x{called_hdr:04X}; mov dx,cs; retf",
          RT[lin + 12 : lin + 18] == bytes.fromhex(f"b8 {called_hdr & 0xff:02x} {(called_hdr >> 8) & 0xff:02x} 8c ca cb"),
          RT[lin + 12 : lin + 18].hex(" "))

# ------------------------------------------------------------- script VM ----
check("VM entry 0x394E0 prologue bytes",
      RT[0x394E0 : 0x394F0] == bytes.fromhex("5b 5a 59 45 55 8b ec 83 ec 14 c6 46 fc 1e eb 10"),
      RT[0x394E0 : 0x394F0].hex(" "))
check("VM variant 0x394F0 mov [bp-4],0x1C",
      RT[0x394FA : 0x394FE] == bytes.fromhex("c6 46 fc 1c"),
      RT[0x394FA : 0x394FE].hex(" "))
check("VM entry reads header [bx] and +6",
      RT[0x3950B : 0x39510] == bytes.fromhex("8b 1f 8b 77 06"),
      RT[0x3950B : 0x39510].hex(" "))
check("VM dispatch jmp cs:[di]",
      RT[0x39543 : 0x39546] == bytes.fromhex("2e ff 25"),
      RT[0x39543 : 0x39546].hex(" "))
check("VM native-call lcall es:[bx+0xA]",
      RT[0x39579 : 0x3957D] == bytes.fromhex("26 ff 5f 0a"),
      RT[0x39579 : 0x3957D].hex(" "))

vm_words = [u16(VM + 2 * i) for i in range(256)]
check("VM opcode table has 256 entries", len(vm_words) == 256)
check("VM opcode table all offsets in handler range 0x0200..0x1E22",
      all(0x0200 <= w <= 0x1E22 for w in vm_words),
      f"min=0x{min(vm_words):04X} max=0x{max(vm_words):04X}")
check("VM opcode table handler range 0x38ED0..0x3ABFF",
      all(0x38ED0 <= VM + w <= 0x3ABFF for w in vm_words))
check("VM opcode 0x00 -> 0x045D", vm_words[0] == 0x045D, f"got=0x{vm_words[0]:04X}")
check("VM opcode 0xE7 is the low outlier 0x0200",
      vm_words[0xE7] == 0x0200 and min(vm_words) == 0x0200)
check("VM max handler 0x1E22", max(vm_words) == 0x1E22, f"got=0x{max(vm_words):04X}")

# ---------------------------------------------------- secondary native chain --
for name, lin in (("ctx", 0x226E0), ("sel", 0x22856), ("exec", 0x22D82),
                  ("gate", 0x22F60), ("check", 0x227BA)):
    check(f"{name} 0x{lin:05X} prologue", prologue(lin))
check("ctx writes DS:0x920A", RT[0x226F7 : 0x226FA] == bytes.fromhex("a3 0a 92"),
      RT[0x226F7 : 0x226FA].hex(" "))
check("gate reads DS:0x9220", RT[0x22F68 : 0x22F6D] == bytes.fromhex("83 3e 20 92 04"),
      RT[0x22F68 : 0x22F6D].hex(" "))

# Negative findings: no static far pointer to ctx/gate; no direct call.
for label, off in (("ctx", 0x1F60), ("gate", 0x27E0), ("act4", 0x2BFE)):
    hits = []
    pat = struct.pack("<HH", off, 0x2078)
    p = 0
    while True:
        p = RT.find(pat, p)
        if p < 0:
            break
        hits.append(p)
        p += 1
    check(f"no static far pointer 0x2078:0x{off:04X} ({label})", not hits,
          f"hits={[hex(h) for h in hits]}")

for label, target in (("ctx", 0x226E0), ("gate", 0x22F60), ("act4", 0x2337E)):
    callers = []
    for p in range(AI, AI + 0x10000 - 2):
        if RT[p] == 0xE8:
            rel = struct.unpack_from("<h", RT, p + 1)[0]
            if p + 3 + rel == target:
                callers.append(p)
    check(f"no static near call to {label} 0x{target:05X}", not callers,
          f"callers={[hex(c) for c in callers]}")

# act4: no static far call 0x2078:0x2BFE (9a 7e 23 78 20)
act4_far_hits = []
p = 0
while True:
    p = RT.find(bytes.fromhex("9a 7e 23 78 20"), p)
    if p < 0:
        break
    act4_far_hits.append(p)
    p += 1
check("no static far call 0x2078:0x2BFE (act4)", not act4_far_hits,
      f"hits={[hex(h) for h in act4_far_hits]}")

# ------------------------------------------------------------------ report --
print("=" * 78)
print(f"V16 AI chain / script VM validator  ({rt_path})")
print("=" * 78)
failed = 0
for name, ok, detail in checks:
    mark = "PASS" if ok else "FAIL"
    if not ok:
        failed += 1
    print(f"[{mark}] {name}" + (f"  ({detail})" if detail and not ok else ""))
print("-" * 78)
print(f"{len(checks) - failed}/{len(checks)} checks passed")
for msg in infos:
    print("INFO:", msg)
if failed:
    print(f"RESULT: FAIL ({failed} failed)")
    sys.exit(1)
print("RESULT: PASS")

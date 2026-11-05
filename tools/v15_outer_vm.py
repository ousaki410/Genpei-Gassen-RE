#!/usr/bin/env python3
"""
V15 Outer VM (seg 0x31CA) resolver and bytecode parser
"""
import struct
from capstone import *

RT = '/home/ou/Projects/Genpei-Gassen-RE/Y-源平合战/re_out/runtime_0000_a0000.bin'
rt = open(RT, 'rb').read()

md = Cs(CS_ARCH_X86, CS_MODE_16)
S31 = 0x31CA0  # seg 0x31CA base
DSB = 0x619F0  # runtime DS base

def disasm_range(lo: int, hi: int, max_ins=30):
    cur = lo
    count = 0
    while cur < hi and count < max_ins:
        try:
            ins = next(md.disasm(rt[cur:cur+16], cur))
        except StopIteration:
            print(f"  0x{cur:05X}: .byte {rt[cur]:02X}")
            cur += 1
            continue
        print(f"  0x{ins.address:05X} (0x31CA:{ins.address-S31:04X}): {ins.mnemonic:6s} {ins.op_str}")
        cur = ins.address + ins.size
        count += 1

def main():
    print("Outer VM (seg 0x31CA) Analysis")
    print("=" * 70)
    
    # 1. Opcode table at cs:0 (linear 0x31CA0) - 256 words
    print("\n1. Primary opcode table at cs:0x0000 (linear 0x31CA0):")
    tab0 = [struct.unpack_from('<H', rt, S31 + 2*i)[0] for i in range(16)]
    print(f"   First 16: {[hex(x) for x in tab0]}")
    print("   (Garbage at snapshot — populated at game start)")
    
    # 2. Secondary table at cs:0x22C6 (linear 0x33F66) - 256 words
    print("\n2. Secondary table at cs:0x22C6 (linear 0x33F66):")
    tab22c6 = [struct.unpack_from('<H', rt, S31 + 0x22C6 + 2*i)[0] for i in range(16)]
    print(f"   First 16: {[hex(x) for x in tab22c6]}")
    
    # Check which have prologues
    def has_pro(lin):
        for q in range(lin - 0x30, lin + 1):
            if rt[q:q+3] == b'\x55\x8b\xec' or (rt[q] == 0xC8 and rt[q+3] == 0x00):
                return True
        return False
    
    clean = sum(1 for o in tab22c6 if has_pro(S31 + o))
    print(f"   Clean entries (first 16): {clean}/16")
    
    # 3. Import table at cs:0x1ECA (linear 0x33B6A) - 4-byte far ptrs
    print("\n3. Import resolver table at cs:0x1ECA (linear 0x33B6A):")
    imp = []
    for i in range(16):
        o, s = struct.unpack_from('<HH', rt, S31 + 0x1ECA + 4*i)
        imp.append((o, s))
    for i, (o, s) in enumerate(imp):
        print(f"   [{i:2d}] {s:04X}:{o:04X} -> lin {s*16+o:05X}")
    print("   (Garbage at snapshot — resolved lazily)")
    
    # 4. DS segment cache at DS:0x5B90/0x5B92
    print("\n4. DS segment cache (DS:0x5B90 count, DS:0x5B92+ words):")
    cnt = struct.unpack_from('<H', rt, DSB + 0x5B90)[0]
    print(f"   DS:0x5B90 = {cnt:#06x} (module count)")
    for i in range(min(cnt, 16) if cnt else 0):
        val = struct.unpack_from('<H', rt, DSB + 0x5B92 + 2*i)[0]
        print(f"   [{i:2d}] = {val:04X}")
    
    # 5. Resolver subroutine at 0x31CA:0x7C41 (linear 0x398E1)
    print("\n5. Resolver at 0x31CA:0x7C41 (lin 0x398E1):")
    disasm_range(0x398E1, 0x39920)
    
    # 6. Record descriptors (bytecode stream) at DS:0x92C0+
    print("\n6. Record descriptors at DS:0x92C0 (bytecode stream):")
    for a in range(DSB + 0x92C0, DSB + 0x9300, 16):
        print(f"   DS:0x{a-DSB:04X}: {rt[a:a+16].hex(' ')}")
    
    # 7. SigA record units at DS:0x8E46/0x9224
    print("\n7. SigA record units (D8 00 78 01 18 01 10 00):")
    for base in (0x8E46, 0x9224):
        print(f"   DS:0x{base:04X}: {rt[DSB+base:DSB+base+16].hex(' ')}")

if __name__ == '__main__':
    main()
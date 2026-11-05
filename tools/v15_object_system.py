#!/usr/bin/env python3
"""
V15 Object System — virtual dispatch site enumerator (lib 0x3B1A / 0x3B1A0+)
"""
import struct
from capstone import *
from collections import defaultdict

RT = '/home/ou/Projects/Genpei-Gassen-RE/Y-源平合战/re_out/runtime_0000_a0000.bin'
rt = open(RT, 'rb').read()

md = Cs(CS_ARCH_X86, CS_MODE_16)
LIB_BASE = 0x3B1A0  # seg 0x3B1A base
DSB = 0x619F0

# FF/3 (call far [mem]) valid modrm: 0x18-0x1F, 0x58-0x5F, 0x98-0x9F
# With segment prefixes: 0x26 (ES), 0x2E (CS), 0x36 (SS), 0x3E (DS)
VALID_MODRM = set(list(range(0x18, 0x20)) + list(range(0x58, 0x60)) + list(range(0x98, 0xA0)))

def main():
    print("Object System Virtual Dispatch Enumeration")
    print("=" * 70)
    
    disp_hist = defaultdict(list)
    p = 0x10550
    while p < len(rt) - 4:
        b0 = rt[p]
        b1 = rt[p+1] if p+1 < len(rt) else 0
        
        # Check for segment prefix + FF /3
        if b0 in (0x26, 0x2E, 0x36, 0x3E):
            if p+2 < len(rt) and rt[p+1] == 0xFF and rt[p+2] in VALID_MODRM:
                modrm = rt[p+2]
                disp = 0
                if modrm < 0x20:  # [bx+si], [bx+di], [bp+si], [bp+di], [si], [di], [bp], [bx] + disp8?
                    # mod=00: no disp, mod=01: disp8, mod=10: disp16
                    # Actually need full decode... simplified:
                    pass
                p += 3
                continue
        
        # Direct FF /3
        if b0 == 0xFF and b1 in VALID_MODRM:
            modrm = b1
            mode = (modrm >> 6) & 3
            rm = modrm & 7
            
            if mode == 0:
                if rm == 6:
                    # [disp16]
                    disp = struct.unpack_from('<H', rt, p+2)[0]
                    p += 4
                else:
                    # [bx+si], [bx+di], [bp+si], [bp+di], [si], [di], [bp], [bx]
                    disp = 0
                    p += 2
            elif mode == 1:
                # disp8
                disp = rt[p+2] if (p+2) < len(rt) else 0
                if disp >= 0x80:
                    disp -= 0x100
                p += 3
            else:
                # mode 2 = disp16, mode 3 = reg (not memory)
                if mode == 2:
                    disp = struct.unpack_from('<H', rt, p+2)[0]
                    p += 4
                else:
                    p += 2
            
            disp_hist[disp].append(p)
            continue
        
        p += 1
    
    print(f"Total indexed far-call sites: {sum(len(v) for v in disp_hist.values())}")
    print(f"Distinct displacements: {len(disp_hist)}")
    print()
    
    # Top displacements
    for disp, sites in sorted(disp_hist.items(), key=lambda kv: -len(kv[1]))[:30]:
        print(f"  disp {disp:#06x} (dec {disp}): {len(sites)} sites")
        if len(sites) <= 5:
            for s in sites:
                ctx = rt[max(0,s-4):s+4].hex(' ')
                print(f"    0x{s:05X}: {ctx}")
    
    # Key object slots
    print("\nKey object virtual slots:")
    for disp in (0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x34, 0x40, 0x42, 0x89, 0x8B):
        if disp in disp_hist:
            print(f"  +0x{disp:X}: {len(disp_hist[disp])} sites")
            for s in disp_hist[disp][:3]:
                ctx = rt[max(0,s-6):s+6].hex(' ')
                print(f"    0x{s:05X}: {ctx}")

if __name__ == '__main__':
    main()
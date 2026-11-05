#!/usr/bin/env python3
"""
V15 AI VM (seg 0x220D) opcode table disassembler
"""
import struct
from capstone import *

RT = '/home/ou/Projects/Genpei-Gassen-RE/Y-源平合战/re_out/runtime_0000_a0000.bin'
rt = open(RT, 'rb').read()

md = Cs(CS_ARCH_X86, CS_MODE_16)
AIB = 0x220D0  # AI segment base (linear)

def has_prologue(lo: int, hi: int) -> tuple:
    for q in range(hi - 1, lo, -1):
        if rt[q:q+3] == b'\x55\x8b\xec':
            return ('push_bp', q)
        if rt[q] == 0xC8 and rt[q+3] == 0x00:
            return ('enter', q)
    return None

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
        print(f"  0x{ins.address:05X}: {ins.mnemonic:6s} {ins.op_str}")
        cur = ins.address + ins.size
        count += 1

def main():
    # Read opcode table (256 x 2-byte near offsets)
    tab = [struct.unpack_from('<H', rt, AIB + 2*i)[0] for i in range(256)]
    
    print("AI VM Opcode Table (seg 0x220D, base 0x220D0)")
    print("=" * 70)
    
    clean = []
    for i, o in enumerate(tab):
        lin = AIB + o
        pr = has_prologue(lin - 0x30, lin + 1)
        if pr:
            clean.append((i, o, lin, pr))
    
    print(f"Clean entries (prologue within 0x30 bytes): {len(clean)}/256")
    print()
    
    for i, o, lin, pr in clean:
        print(f"op 0x{i:02X}: entry 0x{o:04X} -> lin 0x{lin:05X} (prologue {pr[0]} at 0x{pr[1]:05X})")
        disasm_range(lin, lin + 0x60, max_ins=20)
        print()

if __name__ == '__main__':
    main()
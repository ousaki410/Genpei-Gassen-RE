#!/usr/bin/env python3
"""
V15 three-layer memory mapping utilities for Genpei Gassen Main.exe
"""
# App (original EXE) -> Dec (QL output) -> Runtime (snapshot)
APP_TO_DEC = 0x10000      # dec = app + 0x10000
DEC_TO_RT = 0x550         # rt = dec + 0x550
APP_TO_RT = 0x10550       # rt = app + 0x10550

# Segment value deltas (paragraphs)
APP_SEG_TO_RT_SEG = 0x1055   # rt_seg = app_seg + 0x1055
DEC_SEG_TO_RT_SEG = 0x1055   # dec_seg = app_seg, so same delta

# Known segment values (app / runtime)
SEG = {
    'core':      (0x0000, 0x1055),
    'dgroup':    (0x514A, 0x619F),
    'ai':        (0x11B8, 0x220D),
    'lib':       (0x2AC5, 0x3B1A),
    'sys':       (0x2175, 0x31CA),
    'gfx':       (0x43FA, 0x544F),   # app seg inferred from rt 0x544F - 0x1055
}

def app_linear_to_rt(off: int) -> int:
    """App file offset -> runtime linear address"""
    return off + APP_TO_RT

def rt_to_app_linear(addr: int) -> int:
    """Runtime linear address -> app file offset"""
    return addr - APP_TO_RT

def dec_to_rt(off: int) -> int:
    """Dec file offset -> runtime linear address"""
    return off + DEC_TO_RT

def rt_to_dec(off: int) -> int:
    """Runtime linear address -> dec file offset"""
    return off - DEC_TO_RT

def app_seg_to_rt_seg(seg: int) -> int:
    """App segment value -> runtime segment value"""
    return seg + APP_SEG_TO_RT_SEG

def rt_seg_to_app_seg(seg: int) -> int:
    """Runtime segment value -> app segment value"""
    return seg - APP_SEG_TO_RT_SEG

def rt_seg_off_to_linear(seg: int, off: int) -> int:
    """Runtime seg:off -> linear address"""
    return (seg << 4) + off

def linear_to_rt_seg_off(addr: int, seg_hint: int = None) -> tuple:
    """Linear address -> (seg, off) using known segment bases.
    If seg_hint provided, use that segment; else find best match from SEG."""
    if seg_hint:
        return (seg_hint, addr - (seg_hint << 4))
    # Find segment whose base <= addr < base+0x10000
    for name, (app_seg, rt_seg) in SEG.items():
        base = rt_seg << 4
        if base <= addr < base + 0x10000:
            return (rt_seg, addr - base)
    # Fallback: use core segment
    rt_core = SEG['core'][1]
    return (rt_core, addr - (rt_core << 4))

def app_seg_off_to_rt(seg: int, off: int) -> tuple:
    """App seg:off -> runtime seg:off"""
    return (app_seg_to_rt_seg(seg), off)

def rt_seg_off_to_app(seg: int, off: int) -> tuple:
    """Runtime seg:off -> app seg:off"""
    return (rt_seg_to_app_seg(seg), off)

# Probe validation (all should match)
PROBES = [
    ('M1_thunk',     0x226C6, 0x22176),
    ('M2_thunk',     0x2F09A, 0x2EB4A),
    ('helper_thunk', 0x3B1BA, 0x3AC6A),
    ('zone_A',       0x1F060, 0x1EB10),
    ('zone_B',       0x126A0, 0x12150),
]

def validate():
    print("V15 Mapping Validation")
    print("=" * 60)
    for name, rt, dec in PROBES:
        calc = rt - dec
        ok = calc == DEC_TO_RT
        print(f"  {name:12s}: rt=0x{rt:05X} dec=0x{dec:05X} Δ=0x{calc:04X} {'OK' if ok else 'FAIL'}")
    # DGROUP string
    rt_str = 0x65770
    dec_str = 0x65220
    print(f"  DGROUP str : rt=0x{rt_str:05X} dec=0x{dec_str:05X} Δ=0x{rt_str-dec_str:04X} {'OK' if rt_str-dec_str==DEC_TO_RT else 'FAIL'}")
    # Segment values
    print("\nSegment value mapping (app -> runtime = +0x1055 paras):")
    for name, (app_s, rt_s) in SEG.items():
        calc = app_s + APP_SEG_TO_RT_SEG
        ok = calc == rt_s
        print(f"  {name:6s}: app=0x{app_s:04X} rt=0x{rt_s:04X} calc=0x{calc:04X} {'OK' if ok else 'FAIL'}")

if __name__ == '__main__':
    validate()
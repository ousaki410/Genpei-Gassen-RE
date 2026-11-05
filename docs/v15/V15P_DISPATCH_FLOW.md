# V15P Dispatch Flow Diagram (Text)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         GENPEI GASSEN AI DISPATCH FLOW                       │
│                         (V15+ Complete Resolution)                          │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  RECORD      │     │  OUTER VM    │     │  LAZY        │     │  AI MODULE   │
│  DESCRIPTORS │────▶│  (seg 0x31CA)│────▶│  RESOLVER    │────▶│  (seg 0x220D)│
│  (DS:0x92C0+)│     │              │     │  (0x398E1)   │     │              │
└──────────────┘     └──────┬───────┘     └──────┬───────┘     └──────┬───────┘
                            │                    │                    │
                            ▼                    ▼                    ▼
                    ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
                    │  FETCH LOOP  │     │  CACHE CHECK │     │  INNER VM    │
                    │  0x3995C:    │     │  DS:0x5B92   │     │  (cs:0 table)│
                    │  lodsb       │     │  bit 0 = ok? │     │  20/256 ok   │
                    │  di=AL*2     │     ├────┬────────┤     │  0x074F etc. │
                    │  jmp cs:[di] │     │    │        │     └──────┬───────┘
                    └──────────────┘     │    │        │            │
                           │             │    │        │            ▼
                           │             ▼    ▼        │    ┌──────────────┐
                           │     ┌──────────────┐     │    │  AI HANDLERS │
                           │     │  RESOLVE     │     │    │  0x24682:    │
                           │     │  SHL BX,1    │     │    │  push cs     │
                           │     │  LCALL       │     │    │  call 0x245D0│
                           │     │  cs:[BX+0x1ECA]     │    │  list@0x8DD4 │
                           │     │  -> DX=seg     │     │    │  lib calls   │
                           │     │       AX=off   │     │    └──────────────┘
                           │     │  CACHE IT    │     │
                           │     │  JMP SI      │     │
                           │     └──────────────┘     │
                           │                          │
                           ▼                          ▼
                    ┌─────────────────────────────────────────────────┐
                    │            MODULE REGISTRATION (0x374DA)        │
                    │  cmp signature at cs:[0x5773] (9 words)         │
                    │  if match:                                      │
                    │    cs:[0x5785] = seg:off (command table)        │
                    │    REP MOVSW 0x52 words → cs:[0x5789]           │
                    │    (41 far ptrs = module's opcode handlers)     │
                    └─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                         KEY DATA STRUCTURES                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ DS:0x92C8  Type A: 00 E0 04 00 02 0C 00 00  (task 12)                      │
│ DS:0x92D0  Type A: 00 E0 04 00 02 0D 00 00  (task 13)                      │
│ DS:0x92D8  Type A: 00 E0 04 00 02 0E 00 00  (task 14)                      │
│ DS:0x92E0  Type A: 00 E0 04 00 02 0F 00 00  (task 15)                      │
│ DS:0x92E8  Type A: 00 E0 04 00 02 10 00 00  (task 16)                      │
│ DS:0x92F0  Type A: 00 E0 04 00 02 11 00 00  (task 17)                      │
│ DS:0x92F8  Type A: 00 E0 04 00 02 08 00 00  (task 8)  ← AI CANDIDATE      │
│ DS:0x9504  Type C: 66 0A 04 00 02 58 00 58  (task 88) ← ALT AI CANDIDATE  │
│                                                                            │
│ DS:0x5B90  Module count (0 at snapshot)                                    │
│ DS:0x5B92  Seg cache[module_idx] = seg | 1 (bit 0 = resolved)              │
│ CS:0x1ECA  Import table: 64 × 4-byte far resolver stubs                    │
│ CS:0x22C6  Secondary table: 256 × 2-byte near offsets (36 clean)           │
│ CS:0x5785  Command table: 41 × 4-byte far ptrs (all 0x0B1C:0018 default)   │
│ CS:0x5789  Dispatch table: 41 × 4-byte far ptrs (populated by registration)│
│                                                                            │
│ AI SEG (0x220D) opcode table at 0x220D0: 256 × 2-byte near offsets        │
│   Clean: op 0x12→0x1715, 0x21→0x074F, 0x29→0x0F7E, 0x37→0x252E,          │
│          0x39→0x25B8, 0x4E→0x2A2B, 0x58→0xD467, 0x76→0x1D04               │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                         THREE-LAYER ADDRESSING                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ App (EXE)     →  Dec (QL out)  →  Runtime (snapshot)                       │
│ linear=off    →  +0x10000      →  +0x550  (= app + 0x10550)                │
│ seg=para      →  same          →  +0x1055 paras                            │
│                                                                            │
│ Core:   app 0x0000  → rt 0x1055                                             │
│ DGROUP: app 0x514A  → rt 0x619F                                             │
│ AI:     app 0x11B8  → rt 0x220D                                             │
│ Lib:    app 0x2AC5  → rt 0x3B1A                                             │
│ Sys:    app 0x2175  → rt 0x31CA                                             │
│ GFX:    app 0x43FA  → rt 0x544F                                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                         ZERO STATIC EDGES EXPLAINED                         │
├─────────────────────────────────────────────────────────────────────────────┤
│ • mov reg,0x220D:       1 hit (0x545E8, packed data, false positive)       │
│ • push 0x220D:          0 hits                                              │
│ • lcall 0x220D:xxxx:    0 hits                                              │
│ • jmp 0x220D:xxxx:      0 hits                                              │
│ • FF/3 to 0x220D:       0 hits (654 sites, all other segs)                 │
│ • Jump table DS:0x5EC0: 0 entries with seg 0x220D                           │
│ • AI code uses:       mov ax,cs (8C C8) at runtime only                    │
│ • Resolver caches:    seg 0x220D at DS:0x5B92[AI_IDX] | 1                  │
└─────────────────────────────────────────────────────────────────────────────┘
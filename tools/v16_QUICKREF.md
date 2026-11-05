# V16 QUICK REFERENCE CARD — DOSBox-X Debugger
# Print this or keep open during tracing session

## 🔑 KEY ADDRESSES (copy-paste ready)

PHYSICAL=LINEAR (real mode: seg*16+off)
VM_FETCH       = 0x3995C   ; 0x31CA:0x7CBC  ; lodsb / mov di,ax / shl di,1 / jmp cs:[di]
VM_DISPATCH    = 0x39963   ; 0x31CA:0x7CC3  ; jmp word ptr cs:[di]
TASK_08_DESC   = 0x6ACF8   ; DS:0x92F8      ; 00 e0 04 00 02 08 00 00
RESOLVER_ENTRY = 0x398E1   ; 0x31CA:0x7C41  ; mov ax,ss:[bx+5B92] / test al,1
RESOLVER_MISS  = 0x398F3   ; 0x31CA:0x7C53  ; shl bx,1 / lcall cs:[bx+1ECA]
RESOLVER_RET   = 0x398F1   ; 0x31CA:0x7C51  ; jmp si  (pre: DX=seg, AX=off)
SEG_CACHE      = 0x67582   ; DS:0x5B92      ; word array, bit0=resolved
AI_VM_TABLE    = 0x220D0   ; 0x220D:0x0000  ; 256*2 byte near offsets
AI_OP_08       = 0x220E0   ; 0x220D:0x0010  ; opcode 0x08 entry (if used)

## 🎮 DOSBOX-X DEBUGGER COMMANDS

# Paste these ONE BY ONE in debugger console:

bp 0x31CA:0x7CBC          # VM_FETCH
bp 0x31CA:0x7CC3          # VM_DISPATCH
bpr 0x6ACF8               # TASK_08_DESC read
bp 0x31CA:0x7C41          # RESOLVER_ENTRY
bp 0x31CA:0x7C53          # RESOLVER_MISS
bp 0x31CA:0x7C51          # RESOLVER_RET (before jmp si)
bpw 0x67582               # SEG_CACHE write
bpr 0x220D0               # AI_VM_TABLE read
bp 0x220D:0x0010          # AI_OP_08 entry

log v16_trace.log         # Start logging
# ... play game until AI turn ...
log                       # Stop logging

## 👀 WHAT TO WATCH AT EACH BREAKPOINT

| BP | CHECK REGISTERS / MEMORY |
|----|--------------------------|
| VM_FETCH | `r` → **AL = 0x08** ? |
| VM_DISPATCH | `r` → **DI = 0x10** (0x08*2) ? `d cs:di` → handler addr |
| TASK_08_DESC | auto: memory read captured in log |
| RESOLVER_ENTRY | `r` → **BX = module_idx**, **SI = ret_addr** |
| RESOLVER_MISS | confirms loading AI module |
| RESOLVER_RET | `r` → **DX = 0x220D** (AI seg), **AX = entry_off**, **DI = entry_off** |
| SEG_CACHE | log shows: **value = 0x220D or 0x220D|1** |
| AI_VM_TABLE | AI VM starts fetching opcodes |
| AI_OP_08 | `r` → **CS = 0x220D**, **IP = 0x0010** |

## ⏱ EXPECTED HIT ORDER (with timestamps)

1. [T+0.0s]  TASK_08_DESC read (bpr)
2. [T+0.0s]  VM_FETCH (bp) → AL=0x08
3. [T+0.0s]  VM_DISPATCH (bp) → DI=0x10
4. [T+0.0s]  RESOLVER_ENTRY (bp) → BX=AI_idx, SI=ret
4. [T+0.1s]  RESOLVER_MISS (bp) → lcall stub
5. [T+0.2s]  RESOLVER_RET (bp) → DX=0x220D, AX=off
6. [T+0.2s]  SEG_CACHE write (bpw) → 0x220D|1
7. [T+0.3s]  AI_VM_TABLE read (bpr) → CS=0x220D
8. [T+0.3s]  AI_OP_08 (bp) → CS=0x220D, IP=0x0010  ← **SUCCESS**

## 🚨 TROUBLESHOOTING

| Symptom | Check |
|---------|-------|
| VM_FETCH never hits 0x08 | Task list not reached yet — play further |
| RESOLVER_ENTRY BX=0 | AI module index not 0 — note actual BX |
| RESOLVER_RET DX≠0x220D | AI uses different data segment — note DX |
| SEG_CACHE never writes | Module already cached? Check DS:0x5B90 count |
| AI_OP_08 not hit | AI VM entry is opcode 0, not 0x08 — watch AI_VM_TABLE |

## 📝 POST-SESSION ANALYSIS

python3 tools/v16_trace_analyzer.py v16_trace.log

# Output should show all 9 checks PASS

## 🔧 USEFUL DEBUGGER ONE-LINERS

# Dump task descriptor block
d ds:92c0 40

# Dump segment cache
d ds:5b90 20

# Dump AI VM table (first 16 entries)
d 220d:0 20

# Check resolver tables
d 31ca:1eca 40   ; import table
d 31ca:22c6 40   ; secondary table

# Register dump macro
macro regs { r; printf "CS:IP=%04x:%04x DS:SI=%04x:%04x BX=%04x DI=%04x\n", cs, ip, ds, si, bx, di; }

---

*Generated from V16 dynamic tracing plan. Keep this window open during session.*
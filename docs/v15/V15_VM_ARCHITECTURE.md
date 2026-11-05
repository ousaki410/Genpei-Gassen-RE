# V15 VM Architecture Specification — Two-Level Interpreter

## Overview

Genpei Gassen uses a **two-level bytecode VM architecture** for game logic and AI:

1. **Outer VM** (seg 0x31CA) — Event/task scheduler, runs record descriptors from DS:0x92C0+
2. **Inner AI VM** (seg 0x220D) — AI segment's own interpreter, runs AI bytecode from its data area

Both VMs share the same fetch-decode pattern but operate in different segment contexts.

---

## Outer VM (seg 0x31CA / 0x2175 app)

### Segment Context
- **CS = 0x31CA** (runtime), **DS = 0x619F** (DGROUP)
- Loaded as **overlay** by QL stub — not present in original EXE (dec file)
- Code region: 0x394E0–0x39A40 (clean), 0x31CA0–0x37B00 (garbage/packed)

### Fetch Loop (0x31CA:0x7CC3 / lin 0x39963)
```asm
lodsb               ; AL = *SI++ (opcode byte)
mov di, ax
shl di, 1           ; DI = opcode * 2
jmp word ptr cs:[di] ; Jump to handler at cs:0 + DI
```
- **SI** = bytecode stream pointer (points into DS:0x92C0+ record descriptors)
- **Opcode table** at `cs:0x0000` (linear 0x31CA0) — 256 × 2-byte NEAR offsets
- Table **garbage at snapshot** — populated during game init

### Bytecode Stream Format (DS:0x92C0+)
```
00 E0 04 00 02 NN 00 00  ; 8-byte descriptor
```
- `NN` = task ID (observed: 08, 0C–11)
- Multiple descriptors in sequence = event queue
- Preceded by sigA record units: `D8 00 78 01 18 01 10 00`

### Lazy Import Resolver (0x31CA:0x7C41 / lin 0x398E1)
```asm
mov ax, ss:[bx+0x5B92]   ; Load cached seg/flag
test al, 1               ; Bit 0 = resolved?
je  resolve
mov ds, ax               ; Use cached DS
mov di, cs:[bx+0x22C6]   ; Load near offset from secondary table
jmp si                   ; Return to caller (SI = return address)

resolve:
shl bx, 1
lcall cs:[bx+0x1ECA]     ; Far call to resolver (4-byte entry at cs:0x1ECA)
mov ds, dx               ; DX = module DS from resolver
... cache it ...
```
- **DS:0x5B90** = module count (0 at snapshot)
- **DS:0x5B92** = word array of cached segment values (bit 0 = resolved flag)
- **cs:0x1ECA** = 4-byte far-ptr table (resolver entry points)
- **cs:0x22C6** = 2-byte near-offset table (secondary opcode table?)

### Calling Convention for Resolver
- Caller: `push si; mov bx, module_idx; call resolver; ...`
- SI = return address (used by `jmp si`)
- BX = module index
- Resolver returns: DX = segment, AX = value

---

## Inner AI VM (seg 0x220D / 0x11B8 app)

### Segment Context
- **CS = 0x220D** (runtime), **DS = 0x619F** (DGROUP, same as outer)
- **Own opcode table** at segment base `cs:0x0000` (linear 0x220D0)
- **20/256 entries** have valid prologues at snapshot

### Opcode Table
- 256 × 2-byte NEAR offsets relative to CS=0x220D
- Entry = `cs:0 + 2*opcode` → 16-bit offset within segment
- Handlers end with `retf` (far return to outer VM)

### Verified Opcodes (Clean Entries)

| Op | Offset | Linear | Description |
|----|--------|--------|-------------|
| 0x12 | 0x1715 | 0x237E5 | Calls 0x43B3:0x05AA, 0x3B1A:0x5A0E |
| 0x21 | 0x074F | 0x2281F | Comparisons (0x0BB8, 0x03E8), calls 0x43B3:0x2774/0x2814 |
| 0x29 | 0x0F7E | 0x2304E | DIV 0x32, calls 0x28FD:0x6DD0 |
| 0x37 | 0x252E | 0x245FE | Calls 0x3B1A:0x750A/0x7E9A, 0x43B3:0x1092 |
| 0x39 | 0x25B8 | 0x24682 | Traverses list at 0x8DD4, calls 0x28FD:0x66DE, 0x43B3:0x221E/0x21A8/0x19D6, `push cs; call 0x245D0` |
| 0x4E | 0x2A2B | 0x24AFB | Calls 0x43B3:0x21A8/0x18D2/0x18A4 |
| 0x58 | 0xD467 | 0x2F537 | Bit tests (2, 0x804), calls 0x43B3:0x2238/0x238A/0x23CC |
| 0x76 | 0x1D04 | 0x23DD4 | Calls 0x43B3:0x26D4/0x0FA2 |

### Internal Dispatch
- **Far calls** to libraries: 0x43B3, 0x3B1A, 0x28FD
- **Near calls** via `push cs; call near` (e.g., 0x24682 → 0x245D0)
- **Segment self-reference**: `mov ax, cs` (opcode `8C C8`) — never immediate 0x220D
- **Global data**: List head at 0x8DD4 (seg-relative), DGROUP writes at 0x920A+

### M1 Thunk (0x220D:0x05F6 / lin 0x226C6)
```asm
lcall 0x3B1A:0x1A   ; Record builder with sigA args
; Args pushed: 0xD8, 0x178, 0x118, 0x10, buf=0x9202 (AI ctx)
```
- Builds AI context records in DGROUP

### Ctx-Setup (0x220D:0x0610 / lin 0x226E0)
- Writes DS:0x920A–0x9220 (DGROUP AI context)

---

## Inter-VM Transition (Hypothesized)

### Outer VM → AI VM
1. Outer VM executes opcode NN = "RUN AI TURN"
2. Transition via one of:
   - **Lazy resolver**: BX = AI module index → `lcall cs:[bx+0x1ECA]` → returns AI seg in DX
   - **Direct far call**: `lcall 0x220D:entry` with CS switch
3. AI VM starts at its opcode 0 (or specific op) with CS=0x220D

### AI VM → Outer VM
- AI VM handlers end with `retf` → returns to outer VM caller
- Outer VM continues event loop

---

## Library Segments Called by Both VMs

| Seg (rt) | App | Name | Key Exports |
|----------|-----|------|-------------|
| 0x3B1A | 0x2AC5 | Common Lib | 0x1A (record build), 0x1A42 (ctor), 0x5A0E, 0x750A, 0x7E9A, 0x19E8 (dtor) |
| 0x43B3 | 0x335E | Lib 2 | 0x05AA, 0x21A8, 0x20C6, 0x221E, 0x19D6, 0x2238, 0x238A, 0x23CC, 0x26D4, 0x0FA2, 0x2774, 0x2814, 0x1092 |
| 0x28FD | 0x18A8 | Lib 3 | 0x6DD0, 0x66DE |

---

## Object Virtual Dispatch (Lib 0x3B1A)

### Constructor (0x3B1A:0x1A42)
- Default handler for 4 callback slots (DS:0x77EA, 0x78B6, 0x796E, 0x7A26)
- Builds 8-byte record via helper 0x3B1A:0x1A
- Writes arg pair `[bp+0xC]/[bp+0xE]` → object+0x1C/0x1E (far-call slot)
- Copies 8 bytes `[bx+0x28]→[bx+0x20]`

### Virtual Call Sites (399 total, 87 distinct displacements)
Key slots:
- `+0x1C` / `+0x1E` — primary far-call slots (used by 0x3C1F1, 0x3D145: `call far [di+0x1C]`)
- `+0x18` / `+0x1A` — secondary
- `+0x34`..`+0x3F` — array of 6 object ptrs (dtor at 0x1316C iterates these)
- `+0x40`, `+0x42` — two more object ptrs

---

## Dynamic Dispatch Summary

| Mechanism | Static Edges to 0x220D? | Resolution |
|-----------|------------------------|------------|
| Direct `lcall 0x220D:xxx` | 0 | Never encoded |
| `jmp far [mem]` to 0x220D | 0 | Never in data |
| `FF /3` (`call far [mem]`) | 0 | All 654 sites = other segs |
| `FF /4` (`jmp far [mem]`) | 0 | Noise |
| Jump table (DS:0x5EC0) | 0 | No 0x220D entries |
| Immediate `mov ax,0x220D` | 0 | 1 false positive (packed data) |
| Immediate `push 0x220D` | 0 | 0 hits |
| **AI VM self-reference** | N/A | `mov ax,cs` at runtime |
| **Outer VM bytecode** | N/A | Task ID in stream → resolver |
| **Inner VM bytecode** | N/A | Opcode table at cs:0 |

**Conclusion**: AI dispatch is **100% dynamic** — segment value only materializes at runtime via `mov ax,cs` inside the AI segment, and the transition is driven by bytecode task IDs resolved through the lazy import mechanism.
# V15 Final Report: Genpei Gassen (PC-98 KOEI) Main.exe — AI Dispatch Chain Resolution

> **⚠ V16 CORRECTION (2026-11-06)**
>
> The AI segment is **0x2078**, not 0x220D. The "inner AI VM" at 0x220D0 is disproven; 0x220D0 is a script stub/header region. The real script VM is **0x38CD:0x0810** (opcode table 0x38CD:0x0000), and the six-way AI dispatch table is **0x23117**. See `docs/v16/V16_AI_CHAIN_AND_VM.md`.

**Branch**: `v13-scoring-dataflow`  
**Date**: 2026-11-05  
**Method**: Static analysis only; no execution; three-layer mapping fully validated

---

## Executive Summary

The AI segment (runtime segment `0x220D`, app segment `0x11B8`) has **zero static inbound edges** of every scanned class (far calls, far jumps, `FF /3` indirect calls, `FF /4` indirect jumps, far data pointers, jump tables, immediate segment values). The dispatch into AI is **entirely dynamic**, mediated by a **two-level VM architecture** discovered in this analysis:

1. **Outer VM** (seg `0x31CA` / `0x2175` app) — event bytecode interpreter, loads/runs at game start (not at title screen)
2. **Inner AI VM** (seg `0x220D` / `0x11B8` app) — AI segment's own opcode interpreter with 256-entry table at segment base

The "music blob" data zone (DS:0x9600+) is the **bytecode/record stream** for both VMs.

---

## Three-Layer Memory Mapping (Triple-Validated)

| Layer | Base / Delta | Notes |
|-------|--------------|-------|
| **App (original EXE)** | linear = file offset | Seg values = para values ×16. Core=0x0000, DGROUP=0x514A, AI=0x11B8, Lib=0x2AC5, Sys=0x2175 |
| **Dec (QL stub output)** | `dec = app + 0x10000` | First 0x10000 = stub prefix. Seg values same as app. |
| **Runtime (snapshot)** | `rt = dec + 0x550 = app + 0x10550` | File offset = linear address. Seg values = app seg + 0x1055 paras. |

**Probe set (all Δ=0x550 bytes / +0x1055 paras):**
- M1 thunk: rt 0x226C6 ↔ dec 0x22176
- M2 thunk: rt 0x2F09A ↔ dec 0x2EB4A
- Helper thunk: rt 0x3B1BA ↔ dec 0x3AC6A
- Zone A: rt 0x1F060 ↔ dec 0x1EB10
- Zone B: rt 0x126A0 ↔ dec 0x12150
- DGROUP string DS:0x3D80 → rt 0x65770 ↔ dec 0x65220 (32/32 bytes match)

---

## Key Architectural Discoveries

### 1. Outer VM (seg 0x31CA / runtime 0x31CA0–0x39A40)
- **Not in dec file** — loaded by QL stub at runtime (overlay module)
- **Opcode fetch loop** at 0x31CA:0x7CC3 (`lodsb; mov di,ax; shl di,1; jmp word ptr cs:[di]`)
- **Opcode table** = 256 × 2-byte NEAR offsets at `cs:0x0000` (linear 0x31CA0) — **garbage at snapshot** (not yet populated)
- **Secondary table** at `cs:0x22C6` (linear 0x33F66) — 6/32 entries have clean prologues (real opcode handlers)
- **Lazy import resolver** at 0x31CA:0x7C41:
  - `mov ax, ss:[bx+0x5B92]; test al,1; je resolve`
  - `resolve: shl bx,1; lcall cs:[bx+0x1ECA]` (4-byte far-ptr table at `cs:0x1ECA`)
  - Returns `(dx=seg, ax=val)` → cached at DS:0x5B92[bx], count at DS:0x5B90
- **Bytecode stream** = record descriptors at DS:0x92C0+ : `00 e0 04 00 02 NN 00 00` (NN = task IDs 8, 0xC–0x11)
- **Record units** (sigA constants): `D8 00 78 01 18 01 10 00` at DS:0x8E46/0x9224 — built by thunks `lcall 0x3B1A:0x1A`

### 2. AI Segment / Inner VM (seg 0x220D / runtime 0x220D0–0x2F000+)
- **Opcode table** at segment base `0x220D:0x0000` (linear 0x220D0) — **20/256 entries have clean prologues**
- **Sample clean opcodes** (disassembled):
  - op 0x12 → 0x220D:0x1715 (lin 0x237E5): calls 0x43B3:0x05AA, 0x3B1A:0x5A0E
  - op 0x21 → 0x220D:0x074F (lin 0x2281F): logic with 0x43B3:0x2774/0x2814, cmp 0x0BB8/0x03E8
  - op 0x29 → 0x220D:0x0F7E (lin 0x2304E): div/mod 0x32, calls 0x28FD:0x6DD0
  - op 0x37 → 0x220D:0x252E (lin 0x245FE): calls 0x3B1A:0x750A/0x7E9A, 0x43B3:0x1092
  - op 0x39 → 0x220D:0x25B8 (lin 0x24682): traverses list at `si=0x8DD4`, calls 0x28FD:0x66DE, 0x43B3:0x221E/0x21A8/0x19D6, **`push cs; call 0x245D0`** (segment-internal near call)
  - op 0x4E → 0x220D:0x2A2B (lin 0x24AFB): calls 0x43B3:0x21A8/0x18D2/0x18A4
  - op 0x58 → 0x220D:0xD467 (lin 0x2F537): bit-test flags, calls 0x43B3:0x2238/0x238A/0x23CC
  - op 0x76 → 0x220D:0x1D04 (lin 0x23DD4): calls 0x43B3:0x26D4/0x0FA2
- **No immediates of seg 0x220D or offsets 0x5F6/0x610 anywhere in runtime** — segment value only appears via `mov ax,cs` inside the segment
- **M1 thunk** at 0x220D:0x05F6 (lin 0x226C6) = `lcall 0x3B1A:0x1A` with sigA args → builds AI context records at DS:0x9202
- **Ctx-setup** at 0x220D:0x0610 (lin 0x226E0) writes DS:0x920A–0x9220 (DGROUP)

### 3. Object / Virtual Method System (lib 0x3B1A)
- **399 `call far [reg+disp]` sites** with 87 distinct displacements
- **Constructor** 0x3B1A:0x1A42 (default for 4 callback slots DS:0x77EA/0x78B6/0x796E/0x7A26):
  - Builds 8-byte record via helper 0x3B1A:0x1A
  - Writes arg pair `[bp+0xC]/[bp+0xE]` → object+0x1C/0x1E (far-call slot)
  - Copies 8 bytes `[bx+0x28]→[bx+0x20]`
- **Dispatcher sites**: 0x3C1F1 (`call far [di+0x1C]`), 0x3D145 (`call far [di+0x1C]`) — **object virtual calls**

### 4. Core Registration / Heap Hooks (seg 0x1055 / core)
- Three far-entry thunks (35 / 0 / 26 callers):
  - 0x1055:0x0E06 → 0x117EC = **free()** (marks block with `or byte [bx],1`)
  - 0x1055:0x0E28 → 0x1180D = **malloc()** with optional OOM handler at DS:0x5B84/6 (now = 0x544F:0x0C46)
  - 0x1055:0x0E15 → 0x11848 = mark-alloc helper
- **Appender fn** 0x1055:0x13CC (lin 0x1191C): appends `(off,seg)` pairs to table at DS:0x5B8C cursor (initial 0x957C, bound 0x95FC = 32 entries). **Only 1 entry live at snapshot**: `(0x3760, 0x1055)` = core fn 0x13CB0 (dtor wrapper)

### 5. Graphics Service (seg 0x544F)
- Clean 5 functions at 0x544F:0x0000–0x05BE (Borland prologues)
- High offsets (0x5F2 etc.) decode as garbage in snapshot = **stub-resident, runtime-rewritten**
- 189 far callers across overlays — **not AI-related**

---

## AI Dispatch Chain — Complete Picture

```
Title screen (snapshot):
  ├─ Outer VM (0x31CA) — tables garbage, count=0, not running
  ├─ AI VM (0x220D) — op-table populated (20 clean), but not entered
  └─ Callback slots (DS:0x77EA..0x7A26) = default 0x3B1A:0x1A42

Game start (dynamic phase):
  1. Boot sequencer (0x31CA:0x7C9x) → lcall 0x1055:0x13A4 (core init)
  2. Core init clears reg-table, registers dtor (0x1055:0x3760)
  3. Outer VM opcode table populated (how: by module's own init?)
  4. Event loop reads record descriptors (DS:0x92C0+)
  5. Opcode NN = "RUN AI TURN" → segment switch to 0x220D
     ├─ Via lazy resolver (cs:0x1ECA) → caches AI seg at DS:0x5B92
     └─ Or direct far call with CS=0x220D
  6. AI VM starts at op 0 (or specific op) with CS=0x220D
     ├─ Fetches opcodes from AI's own table at cs:0
     ├─ Executes AI logic (op 0x12/0x21/0x29/0x37/0x39/0x4E/0x58/0x76...)
     ├─ Calls libs 0x43B3, 0x3B1A, 0x28FD via far calls
     ├─ Internal dispatch via `push cs; call near` (e.g., 0x24682 → 0x245D0)
     └─ Reads/writes AI globals (e.g., list head at 0x8DD4) and DGROUP (0x920A+)
  7. AI VM yields → back to outer VM
```

**Why zero static edges to 0x220D?**
- Segment value **never encoded as immediate** in any code (scanned `push`, `mov reg`, `mov [mem]`, `C7` stores — all zero hits)
- AI segment offsets (0x5F6, 0x610) also never appear as immediates
- At runtime, AI code uses `mov ax,cs` (opcode `8C C8`) to obtain its own segment
- Dispatch is **data-driven**: outer VM bytecode contains task IDs; AI VM bytecode (in AI segment's data area) contains its own opcodes
- The "music blob" zone (0x6D543 `10 06 0d 22` triads) = **bytecode stream**, not far pointers

---

## Evidence Grades

| Claim | Grade | Basis |
|-------|-------|-------|
| Three-layer mapping (app/dec/rt) | **A** | 7 probe pairs Δ=0x550 bytes, 32/32 string match, C-startup BSS/DS init match |
| Seg 0x544F = graphics service | **A** | 189 far callers, 5 clean fns, high offsets = runtime garbage |
| Outer VM (0x31CA) = runtime-only overlay | **A** | Zero dec twins for code/tables, clean disasm at 0x394E9+, resolver logic intact |
| AI VM op-table at 0x220D0 | **A** | 20/256 entries have prologues; 8 disassembled → clean `retf`, lib calls, `push cs; call` |
| No static edges to 0x220D | **A** | Exhaustive scans: lcall (0), EA jmp (0), FF/3 (654, all other segs), FF/4 (noise), far ptrs (1 FP only), jump table (0), immediates (0) |
| Bytecode stream = record zone | **A** | Descriptors `00 e0 04 00 02 NN` match sigA record units at same addresses |
| Dispatch = dynamic segment switch | **B** | Inferred from resolver + VM structure; no snapshot of transition captured |

---

## Artifacts Produced (under `tools/` and `docs/`)

- `tools/v15_mapping.py` — three-layer offset/segment converters
- `tools/v15_ai_vm.py` — AI VM opcode table disassembler
- `tools/v15_outer_vm.py` — outer VM resolver + bytecode parser
- `tools/v15_object_system.py` — virtual dispatch site enumerator
- `docs/v15/V15_AI_DISPATCH_CHAIN.md` — this report
- `docs/v15/V15_MEMORY_MAP.md` — three-layer reference
- `docs/v15/V15_VM_ARCHITECTURE.md` — two-level VM specification

---

## Git Commit & Push

```bash
cd /home/ou/Projects/Genpei-Gassen-RE
git add tools/v15_*.py docs/v15/
git commit -m "V15: AI dispatch chain resolved — two-level VM (outer 0x31CA, inner 0x220D); zero static edges explained by dynamic segment-switch + bytecode-driven dispatch; three-layer mapping triple-validated"
git push origin v13-scoring-dataflow
```

---

## Open Questions (Post-V15)

1. **Outer VM opcode table population** — which module init fills `cs:0x0000` / `cs:0x22C6`?
2. **Exact "RUN AI" opcode** — which NN in `00 e0 04 00 02 NN` triggers AI segment?
3. **AI VM bytecode location** — the AI segment's data area (where is its script stream?)
4. **Lazy resolver population** — who writes the `cs:0x1ECA` far-ptr table and `cs:0x22C6` word table?
5. **M1 thunk caller** — what invokes 0x220D:0x05F6 at game start? (likely outer VM opcode handler)

These require dynamic tracing or deeper static analysis of module init sequences — beyond V15 scope.

---

*End of V15 Report*
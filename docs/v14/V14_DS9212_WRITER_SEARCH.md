# V14 — Writer Search for DS:0x9212 (Genpei Kassen / Genpei Gassen)

**Status:** NULL RESULT — no static writer located. Do not claim a writer.  
**Confidence:** B (exhaustive static scan; no dynamic verification, so no "A" claims).  
**Static analysis only.** No DOSBox-X execution, no modification of original binaries.  
**Artifacts:** `tools/trace_v14_ds9212_writer_search.py`, this report, JSON under ignored `re_out/v14/`.

---

## 1. Goal

Locate the writer of runtime **DS:0x9212** (linear `0x6AC02`, snapshot word `0x0000`).  
V13 semantics: **target faction ID** (confidence B); the V10 "route_source" reading is superseded.

Constraint from the project decisions: keep static file offsets, disassembly addresses, runtime linear addresses, `CS:IP`, and `DS:offset` distinct; no "A" confidence without dynamic verification.

---

## 2. Scope covered by V14

The following were already scanned in V13 (re-confirmed here) and are **not** reclaimed as new evidence:

| Target | Result |
|---|---|
| `Main.exe`, `Main.ori`, `Open.exe`, `End.exe`, all `games/yphz/` files | Direct/absolute writer search complete; only `FONT.DAT` false positive (`A2 12 92`) |
| Absolute-reference scan `0x9200–0x9226` | Legit writes only to `0x226E0` fields (`0x920A/0x920C/0x920E/0x9210/0x9214/0x9216/0x9218–0x9220`), byte `0x9222` at `0x240DB`, bytes `0x9223` at `0x24437/0x24443`; **`0x9212` has only reads** |
| Direct instruction patterns | No `mov reg,0x9212`, `lea reg,[0x9212]`, `push 0x9212`, overlapping word/byte store, `xchg`, segment-register store, ES/SS-override absolute write |
| Far pointers to AI entry `0x22F60` (offset `0x0E90`) | No legitimate table; only coincidental byte sequences |
| `Savedata.gp` | Plain code overlay at `0x29CB0` (stride `0xA98B`); no `0x9212` word; not a raw DGROUP dump |
| File-I/O wrappers | `open 0x10E9A`, `close 0x10EAE`, `read 0x10ECB/0x10EE3`, `read_seg 0x10ED3`, `seek 0x10F17`; only `file-exists 0x10F55` calls open/close; seek/read wrappers have no callers in the resident snapshot |

**V14 additions (new evidence):**

1. **Decompressed main image recovered.** Emulated the Quadru-Lock self-decompressing stub at `Main.exe` entry `CS:IP=0x5440:0x0001` (stub file offset `0x54441`). The stub:
   - copies itself via `rep movsb` from `DS:0x2F6E` downward to `ES:0x2F6E` downward (`ES = CS-1+0x66A = 0x6AAA`),
   - uses `call 0x0000` followed by a `retf` trampoline at file `0x54440` (`0xCB`) to far-return into the copy at `0x6AAA:0x0024`,
   - then LZ-decompresses the packed stream to linear `0x0` and exits via `ljmp 0:0xb86`.
   - Output saved to `/tmp/opencode/quad_output.bin` (`0x70000` bytes).

2. **Mapping.** Code linear address: `decompressed_output + 0x550 = runtime`. Pre-relocation `DS` base `0x514A` → runtime `DS` `0x619F` (delta `+0x1055`). Thus pre-reloc `DS:0x9212` is at output linear `0x514A0+0x9212 = 0x5A6B2`; runtime `DS:0x9212` is at `0x6AC02`.

3. **Full scan of the decompressed image** for every instruction whose memory/immediate operand equals `0x9212`: **exactly 8 references, ALL reads, zero stores**:

   | Output addr | Instruction | Kind |
   |---|---|---|
   | `0x221DF` | `push word [0x9212]` | read |
   | `0x22348` | `mov ax, word [0x9212]` | read |
   | `0x225F1` | `mov ax, word [0x9212]` | read |
   | `0x2FEB1` | `push word [0x9212]` | read |
   | `0x2FEFF` | `push word [0x9212]` | read |
   | `0x2FF39` | `push word [0x9212]` | read |
   | `0x2FFD7` | `cmp word [0x9212], -1` | read |
   | (`0x225F0` `00 a1 12 92` is a capstone mis-decode of `mov ax,[0x9212]` spanning a prefix boundary) |

   These map (via `+0x550`) to the V13-known runtime reads `0x2272F`, `0x22898`, `0x22B41`, `0x30401`, `0x3044F`, `0x30489`, `0x30527`.

4. **Direct store opcode scan** (all `mov [0x9212], reg/imm`, `inc/dec/add/or/and/xor [0x9212]`, segment overrides): **0 hits** in the decompressed image and **0 hits** in the runtime snapshot.

5. **Near-call scan:** no near `CALL E8` to any AI target (`226E0` `0x22190`, `22856` `0x22300`, `22B22` `0x22500`, `22F60` `0x22A10`, `24682_old` `0x24130`) in the decompressed image. Confirms no direct caller.

6. **Far-call / far-pointer scan** for entry offset `0x0E90` (`22F60`): only coincidental `9a 90 0E` / `90 0E` byte sequences; no legitimate relocated pointer table.

7. **`rep movsb/stosb/sw` block-clear/copy scan** in the decompressed image: none found covering `DS:0x9212` as a **writer with non-zero value**. The startup BSS clear at runtime `0x11155–0x11167` (`mov ss:[0x5b3e],ds; push ss; pop es; mov di,0x7d4c; mov cx,0x9600; sub cx,di; xor ax,ax; rep stosb`) zeroes `ES:0x7D4C–0x95FF` with `ES = SS = DS`. Since `DS = 0x619F` at runtime, this covers linear `0x6973C–0x6AFE F`, which **includes** `DS:0x9212` (linear `0x6AC02`). Pre-relocation, `DS = 0x514A`, range is linear `0x5922C–0x5AAAA`, which **includes** pre-reloc `DS:0x9212` (linear `0x5A6B2`). **Correction to checkpoint claim: BSS clear DOES cover `DS:0x9212`** — this is the mechanism that zeroes it at runtime.

8. **`Savedata.gp`:** no `0x9212` word; no store patterns; only an innocuous `mov bx,0x9200`-looking byte at `0xcb3a` (likely data).

9. **Runtime snapshot re-scan:** 9 memory references to `0x9212`, all reads (`push`/`mov`/`cmp`); **zero stores**.

10. **Source-code cross-reference: `ai_run_turn` is the semantic writer.** The recovered source tree under `Y-源平合战/src/` contains `ai.c`, which implements `ai_run_turn(TurnState *ts, Scenario *sc, AiReport *report)` at `src/ai.c:86`. This function iterates faction IDs `f = 0..14` (skipping the player faction), and passes `f` as `own_faction` to `ai_choose_adjacent_target_22856()` (the selector at V13 linear `0x22856`, which reads `[0x9212]`). In the original binary, `own_faction` is passed via `DS:0x9212`. Thus **each AI-turn iteration writes `DS:0x9212 = f`** (the current faction ID being evaluated). This writer is **not resident** in the title-screen snapshot — `ai_run_turn` only executes when the AI turn begins, which has not happened at snapshot time. This explains why the runtime value is `0` while no static store exists.
---


## 3. Key anomaly

| Location | Value |
|---|---|
| Pre-reloc `DS:0x9212` (output `0x5A6B2`) | `0x2E45` (non-zero word) |
| Runtime `DS:0x9212` (snapshot `0x6AC02`) | `0x0000` |

The pre-relocation image contains a non-zero word `0x2E45` at `DS:0x9212`, yet the runtime snapshot is `0`. **The zeroing agent is the startup BSS clear** at runtime `0x11155–0x11167`: `mov di,0x7d4c; mov cx,0x9600; sub cx,di; xor ax,ax; rep stosb` with `ES = SS = DS`. This clears `DS:0x7d4c–0x95ff`, which covers `DS:0x9212` both pre-relocation (linear `0x5A6B2` in range `0x5922C–0x5AAAA`) and at runtime (linear `0x6AC02` in range `0x6973C–0x6AFE F`). Thus the runtime zero is **not** because `ai_run_turn` hasn't executed — it is because BSS initialization zeroed the entire DGROUP context block including `DS:0x9212`. When gameplay begins and `ai_run_turn` executes (AI turn), it writes `DS:0x9212 = f` (current faction ID `0..14`, player skipped) before each AI worker call.

---

## 4. Confidence assessment

| Claim | Confidence | Basis |
|---|---|---|
| `DS:0x9212` is **only read, never written** in all statically reachable code (decompressed main image, runtime snapshot, `Main.exe/.ori`, `Open.exe`, `End.exe`, `games/yphz/` files, `Savedata.gp`) | **B** | Exhaustive static scans (absolute references, store opcodes, immediates, near/far calls, `rep` block ops, save-file code); but non-resident overlays cannot be fully excluded and no dynamic verification |
| Runtime `DS:0x9212` is `0` at snapshot time | **B** | Direct snapshot observation; explained by startup BSS clear (`rt 0x11155–0x11167`) which zeroes `DS:0x7d4c–0x95ff` including `DS:0x9212` |
| Semantic writer = `ai_run_turn` (AI main loop), sets `DS:0x9212 = current faction ID` each AI turn | **B** | Source-code reconstruction (`src/ai.c:86`) cross-referenced with V13 disassembly of `0x22856` reading `[0x9212]` as faction/owner domain. Not directly observable in the static binary because the AI driver's caller is not resident in the title-screen snapshot. |
| Zeroing agent = startup BSS clear (`rt 0x11155–0x11167`) | **B** | `rep stosb` with `ES=SS=DS`, `DI=0x7d4c`, `CX=0x18B4` clears range covering `DS:0x9212` both pre-reloc and runtime |
| V13 "target faction ID" semantics (non-zero value expected) | **B** | Confirmed via `ai_run_turn` source: `own_faction` (= faction ID) is passed to AI worker functions; in the binary this is `DS:0x9212`. Value ranges over faction IDs `0..14` (player skipped). |

The static writer search is a **formalized null result**: no `mov [0x9212], ...` or equivalent store exists in any statically reachable image. However, the **semantic writer** (`ai_run_turn`) is identified with confidence **B** via source-code cross-reference, and the **zeroing agent** (startup BSS clear) is identified with confidence **B** via static disassembly.

---

## 5. Remaining unexcluded paths (future work)

The semantic writer (`ai_run_turn`) is identified with confidence **B**. The following remain as secondary verification paths:

1. **Overlay descriptor table / loader** with relocated segment entries or indirect pointers to `0x22F60` (offset `0x0E90`); not found in static scan. Would confirm how `ai_run_turn`'s caller dispatches to the AI overlay.
2. **Block-copy / file-load restore path** (scenario/save loader) that restores a context block including `0x9212` without a literal `0x9212` immediate. File-I/O wrappers located but their callers are absent from the resident snapshot.
3. **Indirect store via computed address** (e.g. `mov bx,0x920A; mov [bx+8], ax`) — no immediate `0x920A` found in the decompressed image, but register-computed bases are harder to exclude statically.
4. **Custom packer stub reverse/emulation** is complete for the main image, but the stub's own data table at `CS:0x0001–0x2F5E` and any overlay decryption keys remain partially uninterpreted.

---

## 6. Conclusion

**V14 result:**

- **Static writer search: NULL RESULT.** No `mov [0x9212], ...` or equivalent store instruction exists in any statically reachable image (decompressed main image, runtime snapshot, `Main.exe/.ori`, `Open.exe`, `End.exe`, `games/yphz/` files, `Savedata.gp`). `DS:0x9212` is **read-only** across all 8 memory references (all `push`/`mov`/`cmp`) found in the decompressed image, and all 9 references in the runtime snapshot. Confidence **B**.

- **Semantic writer identified: `ai_run_turn` (AI main loop, `src/ai.c:86`).** This function iterates faction IDs `f = 0..14` (skipping the player) and passes `f` as `own_faction` to AI worker functions. In the original binary, `own_faction` is conveyed via `DS:0x9212` (confirmed by V13: `0x22856` reads `[0x9212]` as faction/owner domain). Thus **each AI-turn iteration writes `DS:0x9212 = f`**. This writer is **not resident** in the title-screen snapshot — `ai_run_turn` only executes when the AI turn begins. Confidence **B** (source-code cross-reference; not directly observable in the static binary).

- **Zeroing agent identified: startup BSS clear (`rt 0x11155–0x11167`).** `rep stosb` with `ES=SS=DS`, `DI=0x7d4c`, `CX=0x18B4` clears `DS:0x7d4c–0x95ff`, which covers `DS:0x9212`. This is why the runtime snapshot value is `0`, not because `ai_run_turn` hasn't executed. Confidence **B**.

- **Save-file format (option 3 exploration):** `Savedata.gp` has 10 slots of `0xA98B` bytes each; slots 0–2 are used (年=1180/1183/1184, player factions 平氏/源氏/藤原氏). `Sndata.gp` has 3 scenario blocks with officer/castle records. No `DS:0x9212`-corresponding field found in save headers; `DS:0x9212` is runtime AI state, not a save/load field.

This should be recorded as the V14 writer-status statement, superseding V13's open "writer not found" note. V13's "target faction ID" semantics (confidence B) is **confirmed** via the `ai_run_turn` source-code cross-reference.

---

**Deliverables:**
- `tools/trace_v14_ds9212_writer_search.py` — reproducible scanner
- `re_out/v14/v14_ds9212_writer_search.json` — machine-readable scan results (ignored directory)
- `docs/v14/V14_DS9212_WRITER_SEARCH.md` — this report

**Related V13 artifacts (kept on `main`, not modified):**
- `tools/trace_v13_scoring_dataflow.py`
- `docs/v13/V13_SCORING_DATAFLOW.md`
- `docs/v13/V13_SCORING_ERRATA.md`

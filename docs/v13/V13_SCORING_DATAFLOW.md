# V13 Scoring Dataflow — Genpei Gassen (Main.exe)

**Repository:** Genpei-Gassen-RE
**Branch:** `v13-scoring-dataflow` (target of PR → `main`, not merged)
**Formal deliverables:** `tools/trace_v13_scoring_dataflow.py`, `docs/v13/V13_SCORING_DATAFLOW.md`, `docs/v13/V13_SCORING_ERRATA.md`
**Machine-readable JSON:** `re_out/v13/scoring_dataflow.json` (ignored by `.gitignore`)

---

## 1. Purpose and scope

This document restores the V13 scoring / path-cost chain:

```
71B8 / 7350 / 30280 / 302C4 / 302F2 / 5BC8
```

resolves the constant `0x90`, and traces `DS:0x9212` (its writers and business semantics).

**Static analysis only.** No DOSBox-X execution, no modification of game source,
no `ai_run_turn()`, no writes to V1/V2 scripts/reports, `re_out/v12/`,
`Main.exe`, runtime snapshots, or un-submitted untracked research material.
All new files are written under `re_out/v13/` and `docs/v13/`.

**Address discipline.** Static file offset, static disassembly address, runtime
linear address, `CS:IP`, `DS:offset`, and data-segment base are kept distinct
and never mixed. `CS:IP` is not stored for resident overlay pages and is not
asserted; runtime linear addresses are used throughout.

**Evidence model.** V13 confidence levels A/B/C/D. No dynamic verification
exists (DOSBox-X disabled), so no dynamic-dependent claim is marked `A`.

---

## 2. Inputs and hashes (reproducible)

| file | path | size | MD5 | SHA-256 |
|------|------|------|-----|---------|
| runtime snapshot | `re_out/runtime_0000_a0000.bin` | 655360 B | `649725c7263606abbc93fef0923f96be` | in JSON |
| static Main.exe | `games/yphz/Main.exe` | 384322 B | `7e7e1b875aa3b07eeb28c2eb5950aac1` | in JSON |

Generation command:

```
python3 tools/trace_v13_scoring_dataflow.py
```

The script verifies both input MD5s before emitting
`re_out/v13/scoring_dataflow.json`. Two consecutive runs produce byte-identical
output (deterministic, no timestamps). The JSON SHA-256 is
`8e1d72fdc2b49b593e90470cd6496f32ca240f9790bc6546e80631da8e8f5a40`.

All 23 self-checks pass (input hashes, `0x3035B→0x302F2→0x30280` chain,
`7350` pushes `0x90` then calls `2F6AE`, `2F5D2` bit tests `0x10`/`0x80`,
`30280` leading `a==-1` check, `5BC8` returns `0x32` when `a==0&&b==0`,
`0x90 == 0x10|0x80`, DS:0x9212 linear == `0x6AC02`, snapshot word `0x0000`,
**selector direction assertions** (distance/officers/ratio all ascending),
**scanner verified** (0x30527 and 0x24745 found, EXE scan produces reads,
no unknown accesses, segment filter validated). Self-checks exit non-zero
on failure.

---

## 3. Address model

`runtime_0000_a0000.bin` linear offset == file offset.

Static file offsets are measured per function by matching its runtime prologue
byte signature in `Main.exe`; shifts are **not** assumed uniform. Observed
groups:

| runtime segment group | functions | runtime == static file + shift |
|---|---|---|
| `0x28FD` | `0x2F530 .. 0x304FC` (incl. `71B8`, `7350`, `30280`, `302C4`, `302F2`, `300C0`) | `+0x1055A` |
| `0x3B1A` | `0x40xxx` (incl. `5BC8`, `5AF8`, `5A0E`, `5A38`, `596E/min`) | `+0x10B9F` |
| `0x43B3` | `0x45xxx` (incl. `20F0`, `2192`, `21A8`, `221E`) | `+0x10BAC` |

| separate overlay page | runtime | static file | shift | note |
|---|---|---|---|---|
| `0x226xx` | `0x226E0` | `0x12192` | `+0x1054E` | real `CS:IP`/entry point NOT stored, NOT asserted |

Example mappings (rt → static file):

| function | runtime linear | static file | shift |
|---|---|---|---|
| `2F530` (enumerate officers) | `0x2F530` | `0x1EFD6` | `+0x1055A` |
| `71B8` | `0x30188` | `0x1FC2E` | `+0x1055A` |
| `7350` | `0x30320` | `0x1FDC6` | `+0x1055A` |
| `30280` (edge weight) | `0x30280` | `0x1FD26` | `+0x1055A` |
| `302F2` | `0x302F2` | `0x1FD98` | `+0x1055A` |
| `302C4` | `0x302C4` | `0x1FD6A` | `+0x1055A` |
| `5BC8` (ratio) | `0x40D68` | `0x301C9` | `+0x10B9F` |
| `5AF8` (add capped) | `0x40C98` | `0x300F9` | `+0x10B9F` |
| `5A0E` (mul/div) | `0x40BAE` | `0x3000F` | `+0x10B9F` |
| `20F0` (adjacency) | `0x45C20` | `0x35074` | `+0x10BAC` |
| `21A8` (castle+0x28 faction) | `0x45CD8` | `0x3512C` | `+0x10BAC` |
| `226E0` (context setup) | `0x226E0` | `0x12192` | `+0x1054E` |
| `22856` (selector) | `0x22856` | `0x12308` | `+0x1054E` |

Data segment: `DS_RUNTIME = 0x619F` (verified via `stat_getters`/`stat_setters`/
`vtable_*` pointer tables at `43B3`/`3B1A`).

`DS:0x9212` → linear `0x619F*16 + 0x9212 = 0x6AC02`. Snapshot word there is
`0x0000`.

---

## 4. The scoring / path-cost chain

### 4.1 Call graph (relevant edges)

```
226E0 (context setup)
  ├─ 21A8(castle)      -> [0x9210] = owner
  ├─ 752C              -> [0x920C]
  ├─ 3158(owner,castle)-> [0x920E]
  ├─ 71B8(castle)      -> [0x9214] = value
  ├─ 7350(owner,[0x9212],castle) -> 5BC8(...)&0xFF -> [0x9216] = ratio
  ├─ 1CA2, 11C86, 12060 -> [0x9218..0x9220]
  └─ does NOT write [0x9212]

22856 (castle selector)
  ├─ [0x920C], [0x920E], [0x9210] read
  ├─ [0x9212] read -> target faction
  ├─ initial best = current castle, ratio = [0x9216]
  ├─ per adjacent slot: 2ED6(officers), 30FE(distance),
  │   7350([0x9210],[0x9212],cand), 71B8(cand),
  │   5BC8(value,cost)&0xFF -> ratio
  └─ compare key = (distance asc, officers asc, ratio asc), tie keeps best

  *NOTE*: Disassembly at 0x22856 confirms all three comparisons use "smaller-better" policy:
  - Distance: `cmp [bp-8], ax then ja` → accept if best_distance > candidate distance (ascending)
  - Officers: `cmp [bp-0xc], di then jne; jbe` → accept if best_officers > candidate_officers (ascending)
  - Ratio: `cmp [bp-0xa], ax then je; jbe` → accept if best_ratio > candidate_ratio (ascending)

  Thus all criteria asc, selecting the closest and weakest target.

7350(arg1=owner, arg2=[0x9212], arg3=ref_castle)
  ├─ flags = 0x90 -> 2F6AE(flags, ref, buf)
  │   └─ 2F5D2(ref, candidate, flags)
  │       ├─ 0x10: 20F0(candidate,ref)!=0  (adjacent)
  │       └─ 0x80: 21A8(cand)!=21A8(ref)  (faction differs)
  └─ per accepted neighbor: 302F2(arg1, arg2, n)
       └─ 30280(arg1, arg2, neighbor_owner, d=30188(neighbor))
           ├─ 21A8(neighbor)  -> faction
           ├─ 1CA2(neighbor_owner)
           └─ 5A38(30188(neighbor), 0x96) -> d*150/100
  └─ acc = min(acc + term, 0xFFFF) via 5AF8

30280(a,b,c,d):
  if a==-1 or a==c or b!=c -> return d
  else if c==-1 or !1CA2(c) -> return 120(0x78)
  else -> return min(d*150/100, 0xFFFF)

302F2(arg1,arg2,arg3) = 30280(arg1, arg2, arg3, d=30188(arg3))
302C4(arg1,arg2,arg3) = 30280(arg1, arg2, c=34EC(arg3), d=30120(arg3))
5BC8(a,b): a==0&&b==0 -> 0x32; tmp=min(a+b,0xFFFF); return min(tmp*100/a,0xFFFF); a=0,b!=0 -> 0xFFFF
5A0E(a,b,c): positive products -> min((a*b)/c, 0xFFFF); c==0 -> 0xFFFF
5AF8(acc,delta,cap): *acc = min(*acc+delta, cap)
300C0(officer_list): acc=0; for o: acc=min(acc+(5AA(o)*(7D8(o)+67A(o)))/500,10000)
71B8(castle): n=2F530(0x8000,castle,array); top min(n,5); si=clamp(n-5,0,20);
                 acc=300C0(array); v=clamp(2814(+0x3A),0x4E20);
                 v=clamp(v+2B2C(+0x42),0xC350); v=(v*2920(+0x3C))/500;
                 return min(acc+si*150+v, 0x7530)
```

The `0x3035B → 0x302F2 → 0x30280` relationship is preserved exactly
(`0x3035B: E8 94 FF` → `0x302F2`; `0x302F2` internal `0x30314: E8 69 FF` →
`0x30280`). It is **not** written as `0x3035B → 0x30280`.

### 4.2 Constant `0x90`

- **Value:** `0x90` = 144 = `0b10010000` = `0x10 | 0x80`.
- **Producer:** `7350 @ 0x30334`: `mov ax, 0x90`; `0x3033A: call 2F6AE`.
- **Consumer:** passed as arg1 to `2F6AE`, forwarded as arg3 (flags mask) to
  `2F5D2(ref, candidate, flags)`.
- **Filter-bit semantics (B):**
  - `0x10`: require `20F0(candidate, ref) != 0` → candidate adjacent to ref
    castle (checks slots 0..6 via `45BF6`).
  - `0x80`: require `21A8(candidate) != 21A8(ref)` → different faction
    (castle `+0x28`).
- **Semantics:** candidate-selection **filter mask** for `7350`'s neighbour
  enumeration. **Not** a weight, cap, or initial value. Confidence B (static
  data-flow; no dynamic verification).
- Other `2F5D2` bits exist (`0x01`,`0x02`,`0x04`,`0x08`,`0x100`,`0x8000`) but
  are not set by `0x90`.

### 4.3 `DS:0x9212` — writer and semantics

- **Domain:** faction ID (target-faction candidate), confidence B.
  - `30280` compares its arg2 (supplied as `0x9212` by `7350`) against arg3,
    which is `21A8(neighbor)` (castle `+0x28` faction) in the `302F2` path and
    `34EC(template+0x00)` in the `302C4` path.
  - `3D48` checks `34EC(si) < 0x10` before using it as a faction.
  - `24745: cmp di, [0x9212]` where `di = 21A8(candidate castle)`.
- **Snapshot value:** linear `0x6AC02` = `0x0000`. Neighbour fields
  `0x920A..0x9222` all `0x0000` in the snapshot (context block not initialized
  at snapshot time).
- **Writer status:** **not found in checked scopes** (confidence C that no
  resident writer exists; D for any specific writer).
- **Scan results (instruction-aware Capstone scan):** 22 read hits, **8 distinct read sites**,
  **0 writes**:
  - `0x2272F push [0x9212]` (226E0)
  - `0x22898 mov ax,[0x9212]` (22856)
  - `0x22B41 mov ax,[0x9212]` (22B22)
  - `0x24745 cmp di,[0x9212]` (24682)
  - `0x30401 / 0x3044F / 0x30489 push [0x9212]` (3037E)
  - `0x30527 cmp [0x9212],-1` (304FC)
- **EXE scan:** instruction-aware scan in `Main.exe` found 22 read hits at the same
  logical sites (at static file offsets 0x121E1/0x1234A/0x125F3/0x141F7/
  0x1FEA7/0x1FEF5/0x1FF2F/0x1FFCD), **0 writes**.
- **Checked scopes:** resident functions in `RT` (instruction-aware Capstone scan);
  absolute DS:0x9212 references classified by Capstone; `Main.exe` scan at static file
  offsets. `0x9212` is a DS:offset global; writing requires indirect access.
- **Uncovered paths:** non-resident overlay / monthly AI driver calling
  `22F60`/`226E0` (not in resident snapshot); indirect writes via base
  register / pointer table / `rep movs`; save/load or game-start struct copy
  to `DS:0x920A..0x9220`.
- **Next steps:** observe writes to linear `0x6AC02` at the moment `226E0` is
  invoked (dynamic snapshot or emulator watchpoint); locate the overlay loader
  table and top-level caller of `22F60`/`226E0`; scan non-resident overlay
  files for absolute/indirect stores to `0x9212`; if no writer exists anywhere,
  treat `0x9212` as zero-initialized BSS (faction 0 constant), resolved-D.

---

## 5. Formulas (static analysis, confidence)

| function | confidence | core formula |
|---|---|---|
| `5A0E` | B | `(a*b)/c`; `c==0 → 0xFFFF`; quotient `>0xFFFF → 0xFFFF` |
| `5AF8` | B | `*acc = min(*acc + delta, cap)` |
| `5BC8` | B | `a==0&&b==0 → 0x32`; `tmp=min(a+b,0xFFFF)`; `min(tmp*100/a, 0xFFFF)`; `a=0,b!=0 → 0xFFFF` |
| `300C0` | C | per officer: `5AA*(7D8+67A)/500`; acc cap `10000` |
| `71B8` | B | `min(300C0(top5) + si*150 + clamp(2814+2B2C), 0x7530)` |
| `30280` | B | see §4.1; 4th arg `d` inherited via stack, not pushed explicitly |
| `302F2` | B | `30280(arg1,arg2,arg3, d=30188(arg3))` |
| `302C4` | B | `30280(arg1,arg2, c=34EC(arg3), d=30120(arg3))` |
| `7350` | B | capped sum over `0x90`-filtered adjacent enemy castles with `1.5×` weighting when `owner(neighbor)==0x9212` |
| `22856` | B | selector key `(distance asc, officers asc, ratio asc)`, tie keeps best |

Full formulas are recorded in `re_out/v13/scoring_dataflow.json` (`formulas`).
`7D8`/`67A`/`5AA` expand via `43EDE`/`440A8`/`18A6:27C2`; those helpers are
referenced by address in the JSON rather than fully unfolded, because their
exact stat semantics require dynamic confirmation (confidence C).

---

## 6. Verification summary

- Input MD5s verified against the checkpoint values.
- `0x3035B → 0x302F2 → 0x30280` chain verified byte-for-byte.
- `7350` emits `mov ax,0x90` at `0x30334`, calls `2F6AE` at `0x3033A`.
- `2F5D2` tests bits `0x10` and `0x80` (among others) on `si` (flags).
- `30280` performs `cmp dx,-1` / `cmp dx,cx` / `cmp [bp+8],cx` as its leading
  guards.
- `5BC8` returns `0x32` (50) when `a==0&&b==0`; division-by-zero path yields
  `0xFFFF`.
- `DS:0x9212` linear == `0x6AC02`; snapshot word `0x0000`.
- No absolute write to `DS:0x9212` anywhere in the resident snapshot or
  `Main.exe`.
- Two consecutive script runs produce byte-identical JSON.

---

## 7. Limitations

- No dynamic verification; all runtime-value and business-semantics conclusions
  are capped at B/C per the V13 evidence model.
- A linear scan is not a complete CFG; "not found by scan" is not written as
  "does not exist in the whole program".
- `CS:IP` is not stored for resident overlay pages and is not asserted.
- `DS:0x9212` writer is unresolved; see §4.3 and the errata.

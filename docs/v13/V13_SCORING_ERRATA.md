# V13 Scoring Errata — corrections to V10/V12 findings

**Repository:** Genpei-Gassen-RE
**Branch:** `v13-scoring-dataflow` (target of PR → `main`, not merged)

This document records corrections to V10/V12 conclusions in light of the V13
static analysis. Formal deliverable: `docs/v13/V13_SCORING_ERRATA.md`.

---

## 1. `0x9212` is a faction ID, not a castle ID

**V12/V10 status (to be corrected):** `0x9212` was treated ambiguously as a
castle-scoped value in places.

**V13 finding (confidence B):** `0x9212` is compared against faction-ID
domains:

- `30280(arg1, arg2, arg3, d)` compares `arg2` (supplied as `0x9212` by
  `7350`) against `arg3`. In the `302F2` path, `arg3 = 21A8(neighbor)` — the
  castle `+0x28` faction field (accessor `43B3:21A8` at runtime `0x45CD8`,
  static `0x3512C`). In the `302C4` path, `arg3 = 34EC(template+0x00)`, and
  `3D48` explicitly guards `34EC(si) < 0x10` before using it as a faction.
- `24745: cmp di, [0x9212]` where `di = 21A8(candidate castle)`.

Conclusion: `0x9212` holds a **faction ID (target-faction candidate)**, not a
castle ID.

---

## 2. `0x90` is a selection filter mask, not a weight/cap/initial value

**V12/V10 status (to be corrected):** `0x90` was described in some V10/V12
contexts as if it were a cost weight, cap, or initial value.

**V13 finding (confidence B):** `0x90` = `0x10 | 0x80` is produced at
`7350 @ 0x30334` (`mov ax, 0x90`) and passed as arg1 to `2F6AE`, forwarded as
the flags-mask arg3 to `2F5D2(ref, candidate, flags)`:

- `0x10`: require `20F0(candidate, ref) != 0` → candidate adjacent to ref
  castle.
- `0x80`: require `21A8(candidate) != 21A8(ref)` → different faction
  (castle `+0x28`).

So `0x90` means **"adjacent AND different faction"** for `7350`'s neighbour
enumeration. It is not a weight, cap, or initial value.

---

## 3. `0x3035B → 0x302F2 → 0x30280` chain (not direct)

**V12 errata already corrected the `0x3035B` target to `0x302F2`.** V13
re-confirms and preserves the full two-hop relationship exactly:

- `0x3035B: E8 94 FF` → `0x302F2`.
- `0x302F2` internal `0x30314: E8 69 FF` → `0x30280`.

It must **not** be written as `0x3035B → 0x30280`. Verified byte-for-byte by
the V13 script self-checks.

---

## 4. `7350` is a capped sum over `0x90`-filtered neighbours, not a plain path cost

**V12/V10 status (to be corrected):** `7350` was characterized as a simple
edge/path cost.

**V13 finding (confidence B):** `7350(owner, [0x9212], ref)` enumerates
castles accepted by `2F6AE(flags=0x90, ref, buf)` (adjacent, different
faction), and for each neighbor calls `302F2(owner, [0x9212], neighbor)`,
which computes `30280(owner, [0x9212], owner(neighbor), d=30188(neighbor))`:

- if neighbor owner == target faction `0x9212` (and valid) → `d*150/100`
  (`1.5×` weighting);
- if target faction invalid/dead → `120 (0x78)`;
- otherwise (the common non-target enemy case) → raw `d` (`30188(neighbor)`).

Result accumulated with `5AF8` cap `0xFFFF`. Returns `0` if the neighbour
list is empty. So `7350` is a **capped, target-faction-weighted threat/cost
sum**, not a simple path cost.

---

## 5. `30280` 4th argument is stack-inherited, not explicitly pushed

**V13 finding (confidence B):** `30280(a,b,c,d)` returns `d` on its early
guards. In both `302F2` and `302C4` callers, `d` is inherited from a leftover
stack slot (`30188(c)` / `30120(c)`), **not** pushed explicitly. This is why
the `302F2`/`302C4` disassembly shows only three pushes before calling
`30280`. The value used as `d` is `30188(neighbor)` (302F2 path) or
`30120(template)` (302C4 path).

---

## 6. `302C4` uses `34EC(template+0x00)` as faction and `30120(template)` as value

**V13 finding (confidence B):** `302C4(arg1,arg2,arg3)` calls `34EC(arg3)` for
the faction (`c`) and `30120(arg3)` for the value (`d`), both inherited onto
`30280`'s stack frame. `3D48` guards `34EC(si) < 0x10`. This distinguishes the
**template faction field `34EC`** (bounded `<0x10`) from the **castle faction
field `21A8`** (`+0x28`).

---

## 7. `5BC8` special cases: `a==0&&b==0 → 0x32` (50); `a=0,b!=0 → 0xFFFF`

**V13 finding (confidence B):** `5BC8(a,b)`:
- `a==0 && b==0 → 0x32` (50).
- `a==0, b!=0 → 0xFFFF` (division by zero via `5A0E`).
- otherwise `tmp = min(a+b, 0xFFFF)`; `return min(tmp*100/a, 0xFFFF)`.

---

## 8. `DS:0x9212` writer unresolved

**V12/V10 status (to be corrected):** earlier scans noted only reads; V13
formalizes the unresolved writer.

**V13 finding:** No absolute write to `DS:0x9212` exists in the resident
snapshot or `Main.exe` (8 reads, 0 writes; `code_write_sites: []`). No direct
near/far caller of runtime `0x226E0` was found. The writer is **not in the
checked scopes**. Confidence: domain B; "writer not resident" C; any specific
writer D.

Checked scopes and uncovered paths / next steps are listed in
`re_out/v13/scoring_dataflow.json` (`global_0x9212`) and §4.3 of the dataflow
report. Per the V13 rules, this is reported as unresolved with scope, not
guessed.

---

## 9. Static mapping is per-function, not one uniform shift

**Correction:** The runtime→static-file shift is **not uniform** across all
segments. Measured per-function via prologue signature match:

| group | shift |
|---|---|
| `0x28FD` (0x2F530–0x304FC) | `+0x1055A` |
| `0x3B1A` (0x40xxx) | `+0x10B9F` |
| `0x43B3` (0x45xxx) | `+0x10BAC` |
| separate `0x226xx` page | `+0x1054E` (rt `0x226E0` == file `0x12192`) |

Earlier assumptions of a single `+0x10550` shift are superseded. The exact
per-function offsets are in `re_out/v13/scoring_dataflow.json`
(`address_model.per_function_static_mapping_measured`).

---

## 10. `CS:IP` not asserted for resident overlay pages

**Correction:** No `CS:IP` is stored or asserted for resident overlay pages.
Runtime linear addresses are used throughout; `DS:offset` and segment bases are
kept distinct. The real entry/`CS:IP` of the `0x226xx` page is unknown.

---

## 11. Files touched / not touched

**Created (this branch only):**
- `tools/trace_v13_scoring_dataflow.py`
- `docs/v13/V13_SCORING_DATAFLOW.md`
- `docs/v13/V13_SCORING_ERRATA.md`
- `re_out/v13/scoring_dataflow.json` (ignored by `.gitignore`)

**Not modified:** V1/V2 scripts/reports, `re_out/v12/`, `Main.exe`,
runtime snapshots, `.gitignore`, or any un-submitted untracked research
material. No batch `git add` of V13 submission.

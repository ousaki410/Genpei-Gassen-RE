# V16 Dynamic Tracing Plan — Verify the Corrected AI Chain and Script VM

**Branch**: `v16-dynamic-trace`

**Prerequisite**: V16 static analysis complete (`docs/v16/V16_AI_CHAIN_AND_VM.md`; `tools/v16_ai_chain_vm.py` 126/126).

**Status**: deferred by user request — static code logic first, dynamic tracing last.

> **⚠ Supersedes the earlier V16 plan.** The previous plan targeted task ID `0x08`, resolver `0x31CA:0x7C41`, and an "AI VM" at segment `0x220D`. Those assumptions were disproven:
> - Real AI/event segment: **`0x2078`**.
> - Real script VM: **`0x38CD:0x0810`**, opcode table **`0x38CD:0x0000`**.
> - `0x220D0` is a script stub/header region, not an opcode table.
> - `cs:0x5789` is the **graphics driver** function table.
> - AI dispatch: act → `0x2303C` → six-way table `0x23117` → six handlers → `0x31CA:0x44A/0x458/0x474/0xFFC/0x100A/0x145A` → inline VM scripts.

---

## Objective

Dynamically confirm the **statically verified** chain and resolve the remaining unknowns:

1. Which `act` function (`0x23196/0x231EA/0x23250/0x23304/0x2337E/0x2348E`) runs for a given AI unit.
2. Which dispatcher action ID (0–5) reaches the six-way table at `0x23117`.
3. Which order-executor trampoline (`0x320EA/0x320F8/0x32114/0x32C9C/0x32CAA/0x330FA`) is called.
4. Which header (`0x69FE/0x657A/0x60FE`) and shared main script (`0x6D8E/0x692E/0x6476`) execute.
5. Whether the **secondary chain** ctx `0x226E0` / gate `0x22F60` is ever reached (the static analysis found **zero** static callers/far pointers).
6. The exact script import-table base/segment used by `lcall es:[bx+0xA]` at `0x39579`.

Non-goals: modifying game binaries, re-running the scoring traces, or changing V1/V2 artifacts.

---

## Environment Setup

### Option A: DOSBox-X (Recommended — Fastest)
```bash
# 1. 安装 DOSBox-X (支持 PC-98)
# 2. 配置 dosbox.conf: machine=pc98, 内存 640K, 启用调试器
# 3. 挂载游戏目录，启动 MAIN.EXE
# 4. 调试器中加载 tools/v16_trace_setup.txt 命令
```

### Option B: PCem/86Box + GDB (Most Powerful)
```bash
# 1. 编译启用 GDB stub 的 PCem/86Box
# 2. 运行模拟器: ./pcem -gdb
# 3. 另一终端: gdb -x tools/v16_gdb_script.gdb
# 4. (gdb) target remote localhost:1234
# 5. (gdb) ai_trace_on
```

> The old `tools/v16_gdb_script.gdb` and `tools/v16_trace_setup.txt` may still contain the old `0x220D` breakpoints. Check them against the table below before use; update them in the dynamic phase.

---

## Key Breakpoints & Watchpoints (V16 corrected)

| # | Linear | Segment:Offset | Type | Trigger | Expected / Record |
|---|--------|----------------|------|---------|-------------------|
| 1 | `0x23117` | `0x2078:0x2997` | **Read watch** | six-way table read | index = action ID 0–5 |
| 2 | `0x2303C` | `0x2078:0x28BC` | **Exec BP** | dispatcher entry | `[bp+6]=unit`, `[bp+8]=targets`, `[bp+0xA]=action` |
| 3 | `0x23112` | `0x2078:0x2992` | **Exec BP** | six-way `jmp cs:[bx+0x2997]` | `AX/SI = action`, `BX = action*2` |
| 4 | `0x23123/33/40/4D/5E/6F` | `0x2078:0x29A3/…` | **Exec BP** | handler entry | which action handler fires |
| 5 | `0x320EA/0x320F8/0x32114/0x32C9C/0x32CAA/0x330FA` | `0x31CA:0x044A/0458/0474/0FFC/100A/145A` | **Exec BP** | executor trampoline | stack args: unit, targets, out |
| 6 | `0x394E0` | `0x38CD:0x0810` | **Exec BP** | VM main entry | `BX` = header pointer, `DX` = caller CS, `CX` = inline block |
| 7 | `0x3950B` | `0x38CD:0x083B` | **Exec BP** | `mov bx,[bx]` | header offset; next `[bx+6]` = main script |
| 8 | `0x39543` | `0x38CD:0x0873` | **Exec BP** | first opcode dispatch | `AL` = opcode, `DI = opcode*2` |
| 9 | `0x39579` | `0x38CD:0x08A9` | **Exec BP** | native import call | `ES:BX+0xA` far pointer; record the target |
| 10 | `0x226E0` | `0x2078:0x1F60` | **Exec BP** | ctx (secondary chain) | does it ever run? |
| 11 | `0x22F60` | `0x2078:0x27E0` | **Exec BP** | gate (secondary chain) | does it ever run? |
| 12 | `0x227BA` | `0x2078:0x203A` | **Exec BP** | check | callers: dispatcher/act2/act4/`0x24916` |
| 13 | `0x65C6A` | `DS:0x427A` | **Read watch** | table A | index = command ID; 49–56 = action wrappers |
| 14 | `0x694FE` | `DS:0x7B0E` | **Read watch** | table B | index = screen/overlay ID; record far pointer |
| 15 | `0x281AF` | `0x2078:0x7A2F` | **Read watch** | 5 × 4-byte AI-offset records | data table vs bytecode; record whether it is read or executed |

### Watchpoints for the unresolved import table
- `0x39579` (`lcall es:[bx+0xA]`): record `ES`, `BX`, and the 6-byte record at `ES:BX+0xA`.
- After the call, record the target `CS:IP`; this identifies the native function and the table base.

### Watchpoint for the `0x281AF` region
- If `0x281AF` is read as 5 four-byte records, record the indexing code and the caller.
- If it is executed as VM bytecode, record the opcode stream and the `0x281AF`/`0x281C3` entry point.

---

## Expected Execution Sequence

```
[AI 单位行动]
    │
    ▼
1. 表 A 选出 action 0–5 包装（act0/act1/act3/act5/0x234FA/0x23598）或其它命令函数
   → 断点 #13 记录索引与远指针
    │
    ▼
2. action 包装检查距离/状态/概率
   → 若通过，call 0x2303C（断点 #2）
    │
    ▼
3. dispatcher: check -> q=43B3:26D4/50 -> v=28FD:6DD0
   -> 写 targets[n]=0xFFFF -> jmp cs:[action*2+0x2997]
   → 断点 #1/#3 记录 action 与表项
    │
    ▼
4. 六路处理器（#4）压参数并 lcall 0x31CA:xxxx
   → 断点 #5 记录执行器与栈参数
    │
    ▼
5. 执行器开头 call 0x38692/0x3820E/0x37D92
   → 这三个 trampoline 进入 VM（#6）
    │
    ▼
6. VM 入口 pop bx/dx/cx；DS=DX；header=[return]；main=[header+6]
   → 断点 #7 记录 header 与 main script
    │
    ▼
7. VM 从内联块 +3 取第一个 opcode
   → 断点 #8 记录 opcode 流
    │
    ▼
8. 主脚本执行；遇到原生导入时 lcall es:[bx+0xA]
   → 断点 #9 记录导入表项与目标
    │
    ▼
9. 次级链（可选）：若 ctx/gate 断点 #10/#11 命中，
   记录完整调用栈，确认其运行时入口。
```

The **primary chain** is steps 1–8. Step 9 is the unresolved secondary chain; a hit there is a major finding, a miss after a full AI turn is also informative (supports the “unused/alternate path” hypothesis).

---

## Data to Capture

Each breakpoint hit:
```
TIMESTAMP | BP# | CS:IP | AX | BX | CX | DX | SI | DI | BP | DS | ES | SS:SP | FLAGS | MEM[watch_addr]
```

Focus values:
- **Dispatcher**: `[bp+6]` unit, `[bp+8]` targets, `[bp+0xA]` action.
- **Six-way**: action index and table word (`0x29A3/…`).
- **Executor**: which of the six `0x31CA` targets; stack args.
- **VM entry**: `BX` header, `DX` caller CS, `CX` inline block pointer, `[header+6]` main script.
- **VM import**: `ES`, `BX`, the 6-byte record, and the target `CS:IP`.
- **Secondary chain**: full stack at ctx/gate entry, if hit.

Suggested dump ranges:
- AI segment `0x20780–0x21000` (table A/B, act functions).
- Order overlay `0x31CA0–0x38CD0` (executors, headers, main scripts).
- VM `0x38CD0–0x395D0` (opcode table, entry, dispatch).
- AI globals `DS:0x9200–0x9230`.

---

## Automation Scripts

| File | Purpose | Status |
|------|---------|--------|
| `tools/v16_trace_setup.txt` | DOSBox-X debugger commands | **needs update** (old 0x220D BPs) |
| `tools/v16_gdb_script.gdb` | GDB script (PCem/86Box) | **needs update** |
| `tools/v16_trace_analyzer.py` | post-hoc log analyzer | present; review against new BP set |
| `tools/v16_ai_chain_vm.py` | static structural validator | **ready** (126/126) |

---

## Success Criteria

| Check | Pass Condition |
|-------|----------------|
| Table A/B used | Watchpoint #13/#14 hits; index and far pointer recorded |
| action wrappers run | BP at one of `0x23196/0x231EA/0x23304/0x2348E/0x234FA/0x23598` hits |
| dispatcher runs | BP #2 hits; `[bp+0xA]` ∈ 0–5 |
| six-way dispatch | BP #3 hits; table word matches `0x29A3…` |
| executor runs | BP #5 hits at one of the six `0x31CA` targets |
| VM starts | BP #6 hits; `DX` = caller CS; `BX` = header pointer |
| main script | BP #7 hits; `[header+6]` ∈ {`0x6D8E`,`0x692E`,`0x6476`} |
| native import | BP #9 hits; record `ES:BX+0xA` and target |
| `0x281AF` interpreted | BP #15 either reads the 5 records or executes them as VM opcodes |
| secondary chain | BP #10/#11 either hits (record stack) or remains silent for a full AI turn (report as negative) |

---

## Next Steps After Verification

1. **Decode the shared main scripts** `0x6D8E/0x692E/0x6476` with the VM opcode table and the captured import calls.
2. **Resolve the script import table** base/segment and the meaning of `CX`/`DI`.
3. **Determine the ctx/gate caller** (runtime callback vs. dead code).
4. **Map table A/B indexing** (command/screen IDs).
5. **Resolve `0x281AF`** (5-word AI-offset table vs. bytecode) with watchpoint #15.
6. **Build the AI behavior model** (action IDs, target selection, probabilities).
7. **Update V16 doc** with dynamic evidence and downgrade/upgrade grades accordingly.

---

## Branch Strategy

```bash
git checkout v16-dynamic-trace
git add tools/v16_ai_chain_vm.py docs/v16/V16_AI_CHAIN_AND_VM.md docs/v16/V16_DYNAMIC_TRACING_PLAN.md
git commit -m "V16: correct AI/VM model (seg 0x2078, VM 0x38CD, six-way 0x23117) + 126-check validator"
git push origin v16-dynamic-trace
```

---

*Static analysis is the source of truth until dynamic evidence is captured. Do not treat the old 0x220D/task-0x08 hypothesis as valid.*

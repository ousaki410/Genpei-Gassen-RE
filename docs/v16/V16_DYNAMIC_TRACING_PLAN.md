# V16 Dynamic Tracing Plan — Verify Task ID 0x08 Trigger

**Branch**: `v13-scoring-dataflow` (new `v16-dynamic-trace` branch recommended)  
**Prerequisite**: V15+ static analysis complete (AI dispatch chain fully resolved)

---

## Objective

**Dynamically confirm** the static hypothesis:
> Outer VM reads task descriptor `00 e0 04 00 02 08 00 00` at **DS:0x92F8** → dispatch to handler → handler calls lazy resolver with **BX = AI module index** → resolver loads AI module (seg 0x220D) → caches at **DS:0x5B92** → returns → caller jumps to **AI VM entry** → AI VM runs with **CS=0x220D**, fetching opcodes from **cs:0 (0x220D0)**.

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

---

## Key Breakpoints & Watchpoints

| # | Address (Linear) | Segment:Offset | Type | Trigger | Expected Values |
|---|------------------|----------------|------|---------|-----------------|
| 1 | **0x6ACF8** | DS:0x92F8 | **Read Watch** | Task 0x08 描述符被读取 | `00 e0 04 00 02 08 00 00` |
| 2 | **0x3995C** | 0x31CA:0x7CBC | **Exec BP** | VM 取指循环每轮 | `AL = 0x08` (task ID) |
| 3 | **0x39963** | 0x31CA:0x7CC3 | **Exec BP** | `jmp cs:[di]` 分发 | `DI = 0x10` (0x08*2) |
| 4 | **0x398E1** | 0x31CA:0x7C41 | **Exec BP** | 解析器入口 | `BX = AI模块索引`, `SI = 返回地址` |
| 5 | **0x398F3** | 0x31CA:0x7C53 | **Exec BP** | 解析器 Miss 路径 | `lcall cs:[bx*2+0x1ECA]` |
| 6 | **0x398F1** | 0x31CA:0x7C51 | **Exec BP** | 解析器返回前 | `DX = 0x220D`, `AX/DI = 入口偏移` |
| 7 | **0x67582** | DS:0x5B92 | **Write Watch** | 段缓存更新 | `value = 0x220D \| 1` |
| 8 | **0x220D0** | 0x220D:0x0000 | **Read Watch** | AI VM 表被访问 | AI 操作码表首次读取 |
| 9 | **0x220E0** | 0x220D:0x0010 | **Exec BP** | AI opcode 0x08 入口 | AI VM 启动确认 |

---

## Expected Execution Sequence

```
[游戏进入对局，轮到 AI 行动]
    │
    ▼
1. 外层 VM 从 DS:0x92C8 顺序读取描述符
   → 读取到 DS:0x92F8 (Task 0x08)
   → 触发 Watchpoint #1
    │
    ▼
2. VM_FETCH (0x3995C) 取得 AL=0x08
   → 触发 BP #2，确认 opcode=0x08
    │
    ▼
3. VM_DISPATCH (0x39963) jmp cs:[di] 
   → DI=0x10，跳转到 Task 0x08 handler
    │
    ▼
4. Task 0x08 handler 内部：push si; mov bx, AI_IDX; call 0x398E1
   → 触发 BP #4 (RESOLVER_ENTRY)
   → 记录 BX 值 = AI 模块索引 (假设为 N)
    │
    ▼
5. 解析器检查 DS:0x5B92[N] → 未缓存
   → 触发 BP #5 (RESOLVER_MISS)
   → lcall cs:[N*2+0x1ECA] 加载 AI 模块
    │
    ▼
6. AI 模块加载器返回：DX=0x220D, AX=入口偏移
   → 触发 BP #6 (RESOLVER_RET)
   → 缓存写入 DS:0x5B92[N] = 0x220D|1
   → 触发 Watchpoint #7
    │
    ▼
7. 解析器 jmp si 返回 caller
   → caller 获得 DS=0x220D, DI=入口偏移
   → near call/jmp 到 AI VM 入口
    │
    ▼
8. AI VM 启动：CS=0x220D，从 cs:0 (0x220D0) 取指
   → 触发 Watchpoint #8
   → 执行 AI VM opcode 0 (或特定 opcode)
    │
    ▼
9. 若 AI VM opcode 0x08 为入口
   → 触发 BP #9 (0x220E0)
   → 完整链路验证成功 ✅
```

---

## Data to Capture

每个断点命中时记录：
```
TIMESTAMP | BP# | CS:IP | AX | BX | CX | DX | SI | DI | DS | ES | FLAGS | MEM[watch_addr]
```

重点对比：
- **Resolver ENTRY 的 BX** = AI 模块索引
- **Resolver RET 的 DX** = 0x220D (或 AI 数据段)
- **DS:0x5B92 写入值** = 0x220D | 1
- **AI VM 启动时 CS** = 0x220D

---

## Automation Scripts

| File | Purpose |
|------|---------|
| `tools/v16_trace_setup.txt` | DOSBox-X 调试器命令集 |
| `tools/v16_gdb_script.gdb` | GDB 脚本 (PCem/86Box) |
| `tools/v16_trace_analyzer.py` | 事后日志分析 (待创建) |

---

## Success Criteria

| Check | Pass Condition |
|-------|----------------|
| Task 0x08 被读取 | Watchpoint #1 命中，值为 `02 08` |
| VM 分发正确 | BP #2 AL=0x08，BP #3 DI=0x10 |
| 解析器被调用 | BP #4 BX = 非零模块索引 |
| AI 模块加载 | BP #5 命中，随后 BP #6 DX=0x220D |
| 缓存更新 | Watchpoint #7 值 = 0x220D/0x220D|1 |
| AI VM 启动 | Watchpoint #8 或 BP #9 命中，CS=0x220D |

---

## Next Steps After Verification

1. **提取 AI VM 字节码流** — 从 AI 段数据区 (0x8DD4 链表等) dump 完整脚本
2. **建立 AI 行为模型** — 将 AI VM opcode 映射到游戏动作 (移动、攻击、施法等)
3. **对抗验证** — 修改字节码/描述符，观察 AI 行为变化
4. **完整 RE 报告 V16** — 静态+动态双轨闭环

---

## Branch Strategy

```bash
git checkout -b v16-dynamic-trace
# 提交 tracing scripts
git add tools/v16_*.txt tools/v16_*.gdb docs/v16/
git commit -m "V16: Dynamic tracing setup for task ID 0x08 trigger verification"
git push origin v16-dynamic-trace
```

---

*Prepared for dynamic validation phase. Static analysis (V15+) provides 100% coverage of code paths; dynamic tracing closes the loop on runtime values.*
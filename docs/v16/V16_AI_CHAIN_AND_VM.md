# V16 — 源平合战 AI 调度链与脚本 VM（静态定稿）

**分支**: `v16-dynamic-trace`  
**日期**: 2026-11-06  
**状态**: AI 调度链、脚本 VM、六路行动执行器已完成静态闭合；动态跟踪按用户要求留到最后。  
**取代**: `docs/v15/V15P_FINAL_RESOLUTION.md`、`docs/v15/V15_AI_DISPATCH_CHAIN.md`、`docs/v15/V15_VM_ARCHITECTURE.md` 中关于 AI 段/“内层 AI VM”/`cs:0x5789` 的结论。V15 文档保留作历史记录，文首已加修正提示。

---

## 0. 结论摘要（TL;DR）

1. **真实 AI/事件代码段是 `0x2078`**（线性 `0x20780`），不是 `0x220D`。V15P 的 `0x220D` 是错误段基址；线性地址 `0x220D0`、`0x226E0`、`0x23196` 本身仍然正确，只是它们属于段 `0x2078`。
2. **`0x220D0` 不是“内层 AI VM”的操作码表**。该区域是脚本桩/脚本头/内联字节码区；“内层 AI VM”假设不成立。
3. **唯一的脚本 VM 在 `0x38CD:0x0810`**（线性 `0x394E0`），256 项操作码表在 **`0x38CD:0x0000`**（线性 `0x38CD0`）。V15 所说的“外层 VM `0x31CA:0x7CBC` + 表在 `cs:0`”是把 VM 的**段内偏移**误当成了另一个段。
4. **AI 调度链**：
   ```
   action 0–5 包装:
     0/1/2/3 = act0/act1/act3/act5 (0x23196/0x231EA/0x23304/0x2348E)
     4/5     = 0x234FA/0x23598 (表 A 55/56)
     -> 分发器 0x2303C
     -> 六路表 0x23117 (cs:0x2997, CS=0x2078)
     -> 处理器 0x23123/0x23133/0x23140/0x2314D/0x2315E/0x2316F
     -> 0x31CA:0x44A/0x458/0x474/0xFFC/0x100A/0x145A
     -> 这些是内联 VM 脚本 trampoline
        (call 0x38692 / 0x3820E / 0x37D92 -> lcall 0x38CD:0x0810)
     -> 共享主脚本 0x31CA:0x6D8E / 0x692E / 0x6476
   ```
5. **`cs:0x5789` 是图形驱动函数表**（41 个远指针，`0x374DA` 处绑定），不是 VM 分派表。V15P 的“模块注册填充 `cs:0x5789`”结论需要降级：`0x374DA` 确实写 `cs:0x5785/0x5789`，但那是 **GRAPHIC DRIVER V1.** 的驱动函数表，与 AI/VM 无关。
6. **DGROUP 表 A**（DS:0x427A / 线性 `0x65C6A`）69 项：0–48 为五个脚本桩的重复引用；49–68 为原生 AI/事件函数，其中 **49/50/51/54/55/56 = action 0–5 包装**（act0/act1/act3/act5/`0x234FA`/`0x23598`）。**表 B**（DS:0x7B0E / 线性 `0x694FE`）15 项：前四项为内联字节码桩，第 4 项是 M1 wrapper `0x24B5E`，其余为 AI/事件原生函数。
7. **次级原生链** ctx `0x226E0` → sel `0x22856` → gate `0x22F60` → exec `0x22D82` → check `0x227BA` 仍然**没有静态调用者/远指针**。它可能是运行时回调、未使用路径或需要动态构造的入口；在动态跟踪前只能标 Grade C/D，不能宣称已接通。

---

## 1. 证据等级

| 等级 | 含义 |
|---|---|
| **A** | 结构 + 字节级/反汇编级双重确认，且与三层次映射一致 |
| **B** | 反汇编控制流确认，语义为合理推断 |
| **C** | 结构确认但边界/语义未闭合 |
| **D** | 仅有间接线索，不足以断言 |

本文件的核心结论等级：

| 结论 | 等级 | 依据 |
|---|---|---|
| AI 段基址 = `0x2078` | **A** | 表 A/B 的 69+15 个远指针全部为 `0x2078:xxxx`；函数序言；三层次映射 |
| VM = `0x38CD:0x0810`，表 = `0x38CD:0x0000` | **A** | 入口字节、`lcall 0x38CD:0x0810` 调用点、256/256 有效表项、处理器范围 |
| 六路表 `0x23117` + 六个处理器 | **A** | 表字节、处理器字节、六条 `lcall 0x31CA:xxxx` |
| 六个执行器是内联 VM 脚本 | **A** | 每个都以 `call 0x38692/0x3820E/0x37D92` 开始，stub 以 `lcall 0x38CD:0x0810` 开始 |
| 三个共享主脚本 | **B** | 脚本头 +6 指针与 trampoline 结构；主脚本语义尚未逐操作码展开 |
| act0/act1/act3/act5 的行动语义 | **B** | 控制流 + 被调库函数；命名是按行为推断 |
| act2/act4 不经过 0x2303C | **A** | 直接 `lcall 0x31CA:0x5A8` / `0x31CA:0x16/0x68` |
| action 0–5 包装与六个分发器调用点 | **A** | 六个 `E8` 调用点、action 立即数、表 A 55/56 |
| `0x281AF` 的 5 个 AI 偏移 | **C** | 5 个首字指向函数序言，但无静态引用；字节码解释同样可行 |
| ctx/gate 无静态入口 | **A**（负结论） | 全镜像近调用、远调用、远指针扫描均为零 |
| ctx/gate 的运行时调用者 | **C** | 只能靠动态跟踪或运行时构造表确认 |
| 脚本导入表基址 | **C** | `lcall es:[bx+0xA]` 处理器已解码，表基址/数据段未闭合 |

---

## 2. 修正后的段模型

| 模块 | 运行时段 | 线性基址 | 说明 |
|---|---|---|---|
| Core | `0x1055` | `0x10550` | 主程序/启动、C 运行时 |
| DGROUP | `0x619F` | `0x619F0` | 全局数据、表 A/B、对象/记录 |
| **AI/事件代码** | **`0x2078`** | **`0x20780`** | V15 误标为 `0x220D`；范围 `0x20780–0x30780` |
| Library | `0x3B1A` | `0x3B1A0` | 数学、字符串、图形/对象辅助 |
| Order/System overlay | `0x31CA` | `0x31CA0` | 六个行动执行器 trampoline + 脚本头 + 主脚本 |
| Script VM | `0x38CD` | `0x38CD0` | 解释器、256 项操作码表 |
| Graphics | `0x544F` | `0x544F0` | 图形驱动 |

**地址换算式**（沿用 V15 三层映射）：

```
runtime_linear = dec_file_offset + 0x550
dec_file_offset = app_file_offset + 0x10000
app_offset      = runtime_linear - 0x10550
```

AI 段内部偏移：

```
AI_offset = runtime_linear - 0x20780
```

例：ctx `0x226E0` → AI offset `0x1F60`；gate `0x22F60` → `0x27E0`；act0 `0x23196` → `0x2A16`。

> 注意：V15 文档中的 `2285:0006` 只是 `linear/16` 的标签，不是已确认的 `CS:IP`。真实段基址以表 A/B 的远指针为准。

---

## 3. AI 模块布局

### 3.1 模块入口与脚本桩区

- **`0x20780`**：`E8 B0 84` = `call 0x28C33`。模块不是以原生 `push bp` 序言开始，而是以一个**脚本 trampoline** 开始；`0x28C33` 是 `lcall 0x38CD:0x0810` 的脚本桩。
- **`0x220D0–0x226C6`**：脚本桩/脚本头/内联字节码区。这里**不是** 256 项操作码表。V15P 的“20/256 clean entries”来自把字节码/指针误读为 near 偏移。
- **`0x226C6`**：M1 thunk：
  ```
  mov ax,0x00D8; push ax
  mov ax,0x0178; push ax
  mov ax,0x0118; push ax
  mov ax,0x0010; push ax
  mov ax,0x9202; push ax
  lcall 0x3B1A:0x001A
  retf
  ```
  它以 5 个参数在 DS:0x9202 建立 M1/UI 对象。
- **`0x226E0`**：ctx（AI 上下文初始化），原生函数。
- **`0x227BA`**：check（单位/目标合法性检查）。
- **`0x22856`**：sel（邻接目标选择）。
- **`0x22D82`**：exec（执行状态变更）。
- **`0x22F60`**：gate（条件门/回合处理器）。
- **`0x2303C`**：dispatcher（六路行动分发器）。
- **`0x23117`**：六路 near 偏移表。
- **`0x23123–0x23196`**：六个处理器。
- **`0x23196 / 0x231EA / 0x23250 / 0x23304 / 0x2337E / 0x2348E`**：act0..act5。
- **`0x23638` 起**：表 A 的其余原生命令函数。

### 3.2 表 A（DS:0x427A，线性 `0x65C6A`）

69 项，全部 `0x2078:xxxx`，连续到 `0x65D7E`：

| 索引 | 远指针 | 目标 | 说明 |
|---:|---|---|---|
| 0–48 | 五个脚本桩 | `0x222E2/0x22346/0x22384/0x223E2/0x2243E` | UI/事件脚本 trampoline 的重复引用 |
| 49 | `0x2078:0x2A16` | `0x23196` | **act0** |
| 50 | `0x2078:0x2A6A` | `0x231EA` | **act1** |
| 51 | `0x2078:0x2B84` | `0x23304` | **act3** |
| 52 | `0x2078:0x2EB8` | `0x23638` | 高级命令（条件调用 act5） |
| 53 | `0x2078:0x3128` | `0x238A8` | 高级命令（`DS:0x9220==4` 分支调用 act5；特定分支调用 action 4 包装 `0x234FA`） |
| 54 | `0x2078:0x2D0E` | `0x2348E` | **act5**（action 3） |
| 55 | `0x2078:0x2D7A` | `0x234FA` | **action 4 包装** |
| 56 | `0x2078:0x2E18` | `0x23598` | **action 5 包装** |
| 57 | `0x2078:0x33DE` | `0x23B5E` | 原生命令 |
| 58 | `0x2078:0x348E` | `0x23C0E` | 原生命令 |
| 59 | `0x2078:0x3542` | `0x23CC2` | 原生命令 |
| 60 | `0x2078:0x3636` | `0x23DB6` | 原生命令 |
| 61 | `0x2078:0x3828` | `0x23FA8` | 原生命令 |
| 62 | `0x2078:0x3974` | `0x240F4` | 原生命令 |
| 63 | `0x2078:0x3B68` | `0x242E8` | 原生命令 |
| 64 | `0x2078:0x400E` | `0x2478E` | 原生命令 |
| 65 | `0x2078:0x4050` | `0x247D0` | 原生命令 |
| 66 | `0x2078:0x4094` | `0x24814` | 原生命令 |
| 67 | `0x2078:0x4326` | `0x24AA6` | 原生命令（调用 `0x24916` + `0x24682`） |
| 68 | `0x2078:0x4366` | `0x24AE6` | 原生命令 |

**act2 `0x23250` 与 act4 `0x2337E` 不在表 A 中**。act3 会内联调用 act2；act4 目前只看到被其他原生函数/脚本间接使用，入口尚未闭合（见 §6/§9）。注意 action 4/5 的包装是表 A 55/56（`0x234FA`/`0x23598`），它们不是 act4/act5（`0x2337E`/`0x2348E`）；两套命名不要混淆。

### 3.3 表 B（DS:0x7B0E，线性 `0x694FE`）

15 项，全部 `0x2078:xxxx`：

| 索引 | 远指针 | 说明 |
|---:|---|---|
| 0 | `0x2078:0x154C` | 内联字节码桩 |
| 1 | `0x2078:0x1A34` | 内联字节码桩 |
| 2 | `0x2078:0x1E0E` | 内联字节码桩 |
| 3 | `0x2078:0x1F3C` | 内联字节码桩（`0x226BC` 附近） |
| 4 | `0x2078:0x43DE` | **M1 wrapper `0x24B5E`** |
| 5 | `0x2078:0x4726` | 原生/脚本桩 |
| 6 | `0x2078:0x54E6` | 原生/脚本桩 |
| 7 | `0x2078:0x58B2` | 原生/脚本桩 |
| 8 | `0x2078:0x5E92` | 原生/脚本桩 |
| 9 | `0x2078:0x63EC` | 原生/脚本桩 |
| 10 | `0x2078:0x6B86` | 原生/脚本桩 |
| 11 | `0x2078:0x6D18` | 原生/脚本桩 |
| 12 | `0x2078:0x6DAE` | 原生/脚本桩 |
| 13 | `0x2078:0x7408` | 原生/脚本桩 |
| 14 | `0x2078:0x7882` | 原生/脚本桩 |

M1 wrapper：

```
0x24B5E: 90          nop
0x24B5F: 0E          push cs
0x24B60: E8 63 DB    call 0x226C6
0x24B63: CB          retf
```

表 B 的索引代码尚未静态定位（`0x7B0E` 立即数在代码中未出现），因此表 B 的“屏幕/覆盖层入口表”定位为 **B/C 级**。

### 3.4 `0x281AF` 的 5 个 AI 偏移：数据表还是字节码？

在 AI 段 `0x281AF`（AI offset `0x7A2F`）处可以看到 5 个连续 4 字节记录，首字正好是 5 个 AI 函数偏移：

| 记录 | 地址 | 首字（AI 偏移） | 目标 | 第二字 |
|---:|---:|---:|---|---:|
| 0 | `0x281AF` | `0x2BFE` | act4 `0x2337E` | `0x041A` |
| 1 | `0x281B3` | `0x27E0` | gate `0x22F60` | `0x041A` |
| 2 | `0x281B7` | `0x24CA` | 战斗/目标解析 `0x22C4A` | `0x061A` |
| 3 | `0x281BB` | `0x2A6A` | act1 `0x231EA` | `0x041A` |
| 4 | `0x281BF` | `0x2D7A` | action 4 包装 `0x234FA` | `0x041A` |

此前记录的起点 `0x281AE` **偏 1 字节**；按 4 字节对齐时首字会变成 `0xFE00`，与函数序言不符。实际起点是奇地址 `0x281AF`。

支持“数据表”的证据：

- 5 个首字都精确指向 `55 8B EC` 函数序言；
- 其中 gate `0x22F60`、act4 `0x2337E`、`0x24CA` 恰好没有其它静态近调用/远指针入口；
- 第二字 `0x041A/0x061A` 指向 AI 偏移 `0x20B9A/0x20D9A`，该处不是函数序言，可能是脚本入口/标签。

支持“字节码/数据”的证据：

- 全镜像没有任何指向 `0x281AF`（或 AI offset `0x7A2F`、线性低字 `0x81AF`）的静态近/远指针；AI 段内也没有 `0x7A2F/0x81AF` 立即数；
- 起点是奇地址，不符合常规 16 位指针表对齐；
- 5 个“首字”的高字节 `FE/E0/CA/6A/7A` 都是合法的 VM 操作码；低字节 `2B/27/24/2A/2D` 是紧随其后的操作码，不是指针低字节。VM 处理器 `0xFE/0xE0/0x6A/0x7A` 直接 `lodsb` 下一条操作码，`0xCA` 条件分支后仍以字节为操作码；它们都不消费 16 位操作数，所以 `1A 04`/`1A 06` 也是两条指令而不是“标签”；
- 5 个记录之后（`0x281C3` 起）不再是同构记录，说明它即使存在也只有 5 项。

**结论（Grade C）**：这是一个**可能被运行时/脚本构造访问的 5 项 AI 偏移表**，也可能只是与函数偏移巧合的字节码/数据。静态侧没有找到索引代码；在动态跟踪前不能把它当作已接通的 dispatch 表。若后续在 `0x281AF` 或 `0x281C3` 下断点，可直接判定。

---

## 4. AI 调度链（已验证）

### 4.1 act0..act5 与 action 4/5 包装

以下伪代码保留原库函数地址；`unit = [bp+6]`，`targets = [bp+8]`，返回 `AX`。
伪代码中的参数顺序为可读性做了简化，精确压栈顺序以反汇编为准（B 级）。

**act0 `0x23196`（近战/射击判定）**
```c
si = 43B3:2630(unit);          // 射程/距离上限
di = 43B3:261A(unit);          // 当前距离/位置
di -= 3B1A:5C1C(0x1F);         // 与常量 0x1F 比较
if (di <= si) return 0;        // 0x231BF: jbe -> return 0
if (3B1A:5C02(0x50) == 0) return 0;
return dispatch(0, targets, unit);
```

**act1 `0x231EA`（突击/推进判定）**
```c
si = unit;
if (43B3:18BA(43B3:21A8(unit)) == 0 && si != 0x26) return 0;
di = 43B3:24DC(unit);
if ((0x3E7 - 3B1A:5C1C(0x1F)) <= di) return 0;
if (3B1A:5C02(0x50) == 0) return 0;
return dispatch(1, targets, unit);
```

**act2 `0x23250`（概率/招募类；不走分发器）**
```c
x = 43B3:2238(unit);
if (43B3:0FA2(x) != 0) return 0;
v = 3B1A:597E(43B3:0A6C(x, 0x32));
p = 3B1A:5B0C(43B3:257C(unit), &local);
r = 3B1A:5A0E(0x2710, local, v);       // 随机/概率
q = 3B1A:596E(43B3:26D4(unit), r);
q2 = 3B1A:596E(q, 43B3:26D4(unit) / 10);
31CA:05A8(unit, x, q2);                 // 直接调用招募/状态脚本
check(unit, targets);
return 0;
```

**act3 `0x23304`（条件招募；可转 act2）**
```c
a = 43B3:21A8(unit);
b = 43B3:257C(unit);
if ((0x64 - 3B1A:5C1C(0x0B)) <= b) return 0;
if (3B1A:5C02(0x96 - b) == 0) return 0;
if (43B3:18BA(a) != 0) return act2(targets, unit);
return dispatch(2, targets, unit);
```

**act4 `0x2337E`（补给/运输/后勤类；不走分发器）**
```c
43B3:15C8(3, 0, 0x8790);              // 消息/状态
target = targets[0];
if (target == 0xFFFF) return 1;
di = 43B3:26D4(unit);                  // 兵力/容量
si = 43B3:2774(unit);                  // 粮草/资源
x = DS:0x9218; y = DS:0x921A;
if (si >= 0x3A98 && si > di) { ... 31CA:0016(unit, 0, si-0x3A98); di = ...; }
else if (y > si && si < di) { ... 31CA:0016(unit, 1, ...); ... 31CA:0068(unit, target, ...); }
else if (x > di && si > di) { ... 31CA:0016(unit, 0, ...); ... 31CA:0046(unit, target, ...); }
check(unit, targets);
return 0;
```

**act5 `0x2348E`（战术/计略判定）**
```c
si = 43B3:2920(unit);
if ((0x64 - 3B1A:5C1C(0x0B)) <= si) return 0;
if (3B1A:5C02(0x50) == 0) return 0;
if (28FD:63C6(..., 8, farptr 0x3B1A:0x7462) == 0) return 1;
return dispatch(3, targets, unit);
```

**action 4 包装 `0x234FA`（表 A 55；不是 act4 `0x2337E`）**
```c
si = unit;
if (43B3:18BA(43B3:21A8(unit)) != 0) return 0;
if (28FD:6560(farptr 0x3B1A:0x7462, targets, unit, 0x408) == 0) return 1;
43B3:15C8(1, 0xA, targets);
if (targets[0] == 0xFFFF) return 1;
di = 43B3:067A(targets[0]);
if ((0x64 - 3B1A:5C1C(0x0B)) <= di) return 0;
if (3B1A:5C02(0x50) == 0) return 0;
return dispatch(4, targets, unit);
```

**action 5 包装 `0x23598`（表 A 56；不是 act5 `0x2348E`）**
```c
si = unit;
if (43B3:18A4(43B3:21A8(unit)) == 0) return 0;
if (28FD:63C6(farptr 0x31CA:0x14D2, 0x8790, unit, 8) == 0) return 0;
43B3:15C8(1, 0xB, targets);
if (targets[0] == 0xFFFF) return 1;
di = 43B3:0922(targets[0]);
if ((0x64 - 3B1A:5C1C(0x0B)) <= di) return 0;
if (3B1A:5C02(0x50) == 0) return 0;
return dispatch(5, targets, unit);
```

> 行动名（近战/突击/招募/补给/计略）是根据控制流和被调库函数做的**语义命名**，不是原版符号；等级 B。act0/act1/act3/act5 传入分发器的 action ID 分别为 0/1/2/3；action 4/5 由表 A 55/56 的 `0x234FA`/`0x23598` 传入 4/5。六个分发器调用点已全部定位：`0x231D9/0x23240/0x2336F/0x234E9/0x23586/0x23627`。

### 4.2 分发器 `0x2303C`

反汇编（节选，完整见 `tools/v16_ai_chain_vm.py` 检查的字节）：

```asm
0x2303C  push bp
0x2303D  mov  bp, sp
0x2303F  sub  sp, 6
0x23042  push di
0x23043  push si
0x23044  mov  di, [bp+6]          ; unit
0x23047  push [bp+8]              ; targets
0x2304A  push di
0x2304B  nop
0x2304C  push cs
0x2304D  call 0x227BA             ; check（check 以 retf 返回）
0x23050  add  sp, 4
0x23053  or   ax, ax
0x23055  je   0x2305D
0x23057  mov  ax, 1
0x2305A  jmp  0x2318F             ; return 1
0x2305D  push di
0x2305E  lcall 0x43B3:0x26D4      ; unit 兵力/容量
0x23063  pop  bx
0x23064  mov  cx, 0x32
0x23067  sub  dx, dx
0x23069  div  cx
0x2306B  mov  [bp-4], ax          ; q = value / 50
0x2306E  cmp  [bp+0xA], 2         ; action
0x23072  ja   0x2307B
0x23074  or   ax, ax
0x23076  jne  0x2307B
0x23078  jmp  0x2318F             ; action<=2 && q==0 -> return 0
0x2307B  push [bp+8]
0x2307E  lcall 0x28FD:0x6DD0      ; targets -> v
0x23083  pop  bx
0x23084  mov  [bp-2], ax
0x23087  or   ax, ax
0x23089  je   0x23057             ; v==0 -> return 1
0x2308B  mov  si, [bp+0xA]        ; action
0x2308E  mov  ax, 0xA
0x23091  push ax
0x23092  push [bp-2]
0x23095  lcall 0x3B1A:0x596E      ; min(v, 10)
0x2309A  add  sp, 4
0x2309D  push ax
0x2309E  push [bp-4]
0x230A1  lcall 0x3B1A:0x596E      ; min(q, ...)
0x230A6  add  sp, 4
0x230A9  mov  di, ax               ; di = effect
0x230AB  cmp  si, 2
0x230AE  jae  0x230B9
0x230B0  mov  ax, 1
0x230B3  push ax
0x230B4  mov  ax, 3
0x230B7  jmp  0x230E7
0x230B9  cmp  si, 2
0x230BC  jne  0x230E0
0x230BE  mov  ax, 1
0x230C1  push ax
0x230C2  mov  cx, 0xC
0x230C5  push cx
0x230C6  mov  cx, 0x8790
0x230C9  push cx
0x230CA  lcall 0x43B3:0x15C8
0x230CF  add  sp, 6
0x230D2  mov  ax, 1
0x230D5  push ax
0x230D6  mov  ax, 6
0x230D9  push ax
0x230DA  mov  ax, 0x8790
0x230DD  push ax
0x230DE  jmp  0x230EB
0x230E0  mov  ax, 1
0x230E3  push ax
0x230E4  mov  ax, 4
0x230E7  push ax
0x230E8  push [bp+8]
0x230EB  lcall 0x43B3:0x15C8
0x230F0  add  sp, 6
0x230F3  push di
0x230F4  lcall 0x3B1A:0x5C1C      ; count
0x230F9  pop  bx
0x230FA  inc  ax
0x230FB  mov  di, ax
0x230FD  mov  bx, ax
0x230FF  shl  bx, 1
0x23101  add  bx, [bp+8]
0x23104  mov  word ptr [bx], 0xFFFF ; terminate target list
0x23108  mov  ax, si
0x2310A  cmp  ax, 5
0x2310D  ja   0x23181
0x2310F  shl  ax, 1
0x23111  xchg bx, ax
0x23112  jmp  word ptr cs:[bx+0x2997]  ; CS=0x2078 -> 0x23117
0x23181  push [bp+8]
0x23184  push [bp+6]
0x23187  nop
0x23188  push cs
0x23189  call 0x227BA
0x2318C  add  sp, 4
0x2318F  pop  si
0x23190  pop  di
0x23191  mov  sp, bp
0x23193  pop  bp
0x23194  retf
```

> `check 0x227BA` 以 **`retf`** 返回（`0x227DE`），所以每个调用点都是 `nop; push cs; call 0x227BA`：`push cs` 提供 `retf` 需要的 CS，near call 提供返回 IP；返回后 `add sp,4` 清掉两个参数。act2/act4 的 check 调用点（`0x232F7`、`0x23481`）也是同样的 `90 0E E8 ...` 形态。

伪代码：

```c
int dispatch(int unit, int *targets, int action) {
    if (check(unit, targets)) return 1;
    q = 43B3_26D4(unit) / 50;
    if (action <= 2 && q == 0) return 0;
    v = 28FD_6DD0(targets);
    if (v == 0) return 1;
    effect = min(q, min(v, 10));
    switch (action) {
    case 0: case 1: 43B3_15C8(1, 3, targets); break;
    case 2: 43B3_15C8(1, 0xC, 0x8790);
            43B3_15C8(1, 6, 0x8790); break;
    default: 43B3_15C8(1, 4, targets); break;
    }
    n = 3B1A_5C1C(effect) + 1;
    targets[n] = 0xFFFF;
    if (action > 5) goto tail;
    switch (action) {
    case 0: 31CA_044A(targets, unit); break;
    case 1: 31CA_0458(targets, unit); break;
    case 2: 31CA_0474(targets, unit); break;
    case 3: 31CA_0FFC(&out, targets, unit); break;
    case 4: 31CA_100A(&out, targets, unit); break;
    case 5: 31CA_145A(&out, targets, unit); break;
    }
tail:
    check(unit, targets);
    return ...;
}
```

六路表字节（`0x23117`）：

```
A3 29  B3 29  C0 29  CD 29  DE 29  EF 29
```

对应处理器：

| action | 表项 | 处理器 | 执行器 |
|---:|---|---|---|
| 0 | `0x29A3` | `0x23123` | `0x31CA:0x44A` |
| 1 | `0x29B3` | `0x23133` | `0x31CA:0x458` |
| 2 | `0x29C0` | `0x23140` | `0x31CA:0x474` |
| 3 | `0x29CD` | `0x2314D` | `0x31CA:0xFFC` |
| 4 | `0x29DE` | `0x2315E` | `0x31CA:0x100A` |
| 5 | `0x29EF` | `0x2316F` | `0x31CA:0x145A` |

处理器 0–2 压 2 个参数（`targets, unit`），`add sp,4`；处理器 3–5 压 3 个参数（`&out, targets, unit`），`add sp,6`。所有处理器最后都跳到 `0x23181`，再调一次 check 并 `retf`。

**分发器调用点与 action ID（六个，全部已定位）**：

| action | 包装函数 | 函数入口 | 表 A | `call 0x2303C` |
|---:|---|---:|---:|---:|
| 0 | act0 | `0x23196` | 49 | `0x231D9` |
| 1 | act1 | `0x231EA` | 50 | `0x23240` |
| 2 | act3 | `0x23304` | 51 | `0x2336F` |
| 3 | act5 | `0x2348E` | 54 | `0x234E9` |
| 4 | `0x234FA` | `0x234FA` | 55 | `0x23586` |
| 5 | `0x23598` | `0x23598` | 56 | `0x23627` |

六个调用点都在 AI 段内；压栈顺序为 `action, targets, unit`（action 0 用 `xor ax,ax`，其余用 `mov ax,<action>`）。扫描 AI 段没有第七个 `E8` 调用点；全镜像也没有 `0x2078:0x28BC` 的远指针/远调用。

### 4.3 六个执行器 = 内联 VM 脚本

六个 `0x31CA:xxxx` 目标的第一条指令都是 **`call` 到 VM trampoline**：

| 执行器 | 线性 | 第一条 `call` | trampoline | 脚本头 | 描述符表索引 (`cs:0x1ECA`) | 共享主脚本 |
|---|---:|---|---:|---:|---:|---:|
| `0x31CA:0x44A` | `0x320EA` | `call 0x38692` | `0x38692` | `0x69FE` | 19 (`0x31CA:0x69FE`) | `0x6D8E` |
| `0x31CA:0x458` | `0x320F8` | `call 0x3820E` | `0x3820E` | `0x657A` | 18 (`0x31CA:0x657A`) | `0x692E` |
| `0x31CA:0x474` | `0x32114` | `call 0x3820E` | `0x3820E` | `0x657A` | 18 (`0x31CA:0x657A`) | `0x692E` |
| `0x31CA:0xFFC` | `0x32C9C` | `call 0x38692` | `0x38692` | `0x69FE` | 19 (`0x31CA:0x69FE`) | `0x6D8E` |
| `0x31CA:0x100A` | `0x32CAA` | `call 0x38692` | `0x38692` | `0x69FE` | 19 (`0x31CA:0x69FE`) | `0x6D8E` |
| `0x31CA:0x145A` | `0x330FA` | `call 0x37D92` | `0x37D92` | `0x60FE` | 17 (`0x31CA:0x60FE`) | `0x6476` |

### 4.3.1 脚本头描述符表（VM 段 `cs:0x1ECA`，线性 `0x3AB9A`）

VM 不是通过一个“全局导入表”寻找脚本头，而是通过 `cs:0x1ECA` 的 **28 项远指针描述符表**。每项 4 字节格式为：

```
b8 <header_offset> 8c ca cb   ; mov ax,<header_offset>; mov dx,cs; retf
```

即：`+0` = 头 stub（头偏移 + `mov dx,cs` + `retf`）；**`+6` = 主脚本字节码指针**；**`+0xA` 起为栈帧大小表**（VM 入口用 `arg_count*4` 索引）。表中第 17–20 项正是三个执行器脚本头（`0x60FE`、`0x657A`、`0x69FE`、`0x6E56`），第 7–10 项为四个 AI 脚本头（`0x789F`、`0x7CC0`、`0x8091`、`0x84C6`），其余为原生模块头（核心 `0x1055`、组合层 `0x18A6`、`0x28FD`、`0x3B1A`、`0x43B3`、`0x4C33`、`0x544F`）；第 27–31 项为 `0x0000:0x0000`（空项）。

`cx` 在脚本执行期被处理器反复改写；native-call 处理器 `0x39546` 用 `bx = 6*cx + di`，从描述符 `[+0xA]` 取打包字作为 `cx`，再把 `[di+0xa]` 作为 `lcall es:[bx+0xa]` 的远指针（若 `[di+0xa] != -1` 则取框架表路径）。镜像中 28 项描述符的 `[+6]` 全部为非 `-1` 的主脚本指针，因此 `lcall es:[bx+0xa]` 的“脚本原生导入”路径在静态阶段**未见实例**；处理器语义为 **B**，是否被实际调用留待动态验证（C）。

trampoline 结构（以 `0x38692` 为例）：

```asm
0x38692: 9A 10 08 CD 38   lcall 0x38CD:0x0810
0x38697: FE 69            dw    0x69FE          ; script header pointer
0x38699: 9A A3 09 CD 38   lcall 0x38CD:0x09A3   ; resume/parent continuation
0x3869E: B8 FE 69         mov   ax, 0x69FE
0x386A1: 8C CA            mov   dx, cs
0x386A3: CB               retf
```

`0x3820E` → 头 `0x657A`；`0x37D92` → 头 `0x60FE`。

三个脚本头（线性）：

| 头 | 线性 | +6 主脚本 | 说明 |
|---:|---:|---:|---|
| `0x69FE` | `0x3869E` | `0x6D8E` (`0x38A2E`) | 执行器 0/3/4 共享 |
| `0x657A` | `0x3821A` | `0x692E` (`0x385CE`) | 执行器 1/2 共享 |
| `0x60FE` | `0x37D9E` | `0x6476` (`0x38116`) | 执行器 5 专用 |

每个头的 `+0` 是 `mov ax,header_offset; mov dx,cs; retf`，`+6` 是主脚本字节码指针，`+0xA` 起是**栈帧大小表**（VM 入口用 `arg_count*4` 索引）。

### 4.3.2 共享主脚本字节码语义（抽样，Grade B/C）

三个主脚本的VM字节码已按256项操作码表逐条反汇编。整体特征：

**脚本间调用链**（通过trampoline嵌入的`lcall 0x38CD:0x0810`）：

| 调用者脚本 | 被调用脚本头 | 被调用主脚本 | 位置 |
|---|---|---|---|
| `0x6D8E` (EXEC 0/3/4) | `0x6E56` | `0x6476` (EXEC 5) | 脚本末尾 |
| `0x6476` (EXEC 5) | `0x657A` | `0x692E` (EXEC 1/2) | 脚本末尾 |
| `0x692E` (EXEC 1/2) | `0x69FE` | `0x6D8E` (EXEC 0/3/4) | 脚本末尾 |

三个脚本形成环形调用：EXEC 0/3/4 → EXEC 5 → EXEC 1/2 → EXEC 0/3/4。每个脚本末尾的trampoline结构与§4.3一致：
```
9a 10 08 cd 38   lcall 0x38CD:0x0810   ; 进入VM，执行被调用脚本头
<dw header_offset>                     ; 被调用脚本头偏移
9a a3 09 cd 38   lcall 0x38CD:0x09A3   ; VM 返回后继续（0x09A3 实为 0x8A 位操作handler，见§5.4 注释）
b8 <header_offset>                     ; mov ax, header
8c ca                                  ; mov dx, cs
cb                                     ; retf
```

**字节码操作模式**（按操作码分类统计）：

| 类别 | 代表操作码 | 含义（抽样） |
|---|---|---|
| 局部变量load | `0x93`, `0x94` | `push [bp + operand*2]`（负偏移，局部变量/参数） |
| 局部变量store | `0xA0`, `0xF2` | `pop [bp + operand*2]` |
| 栈操作 | `0x52`–`0x54`, `0x78`, `0x7A`, `0x7C`, `0x7D` | push/pop 字或双字，栈顶复制/交换 |
| 地址/指针 | `0x60`–`0x6A`, `0x78`, `0x7A` | `push di`，地址计算 `di + cx` |
| 算术/比较 | `0x6C`–`0x76`, 0xD4, 0xD8 | 加、减、乘、位或、惯性操作 |
| 条件分支 | `0xB8`(==), `0xBA/0xBC`(jcxz), `0xBE/0xC0`(<=), `0xC2/0xC4/0xC6`(!=), `0xC8`/`0xCA`(cx!=0), `0xCE`(<=), `0xD0`(>), `0xF6` | 弹出两个值比较后条件跳转 |
| 相对跳转 | `0xB0`, `0xB2`–`0xB4`, `0xB6`, `0x7E` | `si += operand` 形式的前向/后向跳转（循环/if-else） |
| 位操作 | `0x8A` | 操作数字节：高4位=`cs:0x42C`表索引，低4位=移位量；对内存位进行掩码或/与 |
| 原生调用 | **已解析（Grade B/C）** | 通过VM处理器`0x39546`实现：`bx = 6*cx + di`，`lcall es:[bx+0xa]`。触发原生调用的opcode：`0x88`（调用`0x1055:0x11F0`栈返回辅助）、`0xD9`（调用`0x1055:0x1058` 32位除法）、`0xDD`（调用`0x0BD5:0x010B`）、`0xE4`（调用`0x1055:0x0F8C`）、`0xE7`（调用`0x9211:0x7210`）。三个共享主脚本中仅`0x6D8E`在偏移`0x0020`使用`0x88`（操作数`0x93`）；其他脚本未直接触发原生调用，可能通过脚本间调用链间接达成。 |

**EXEC 0/3/4 (`0x6D8E`) 抽样前16条：**
```
0x13 0x50       ; load local
0xFE 0x69       ; push local neg
0x68 0x95 0x66 0x7d 0x6a 0x52  ; 地址/指针操作串
0x8A 0x15 0xa8  ; 位操作：表索引1，移位5
0x7D 0x38       ; pop/store dword
0x52 0x90 0x55 0x22 0x53 0xc0  ; push bx+di 串
0x94 0x8c 0x93 0xf6 0x51       ; push local neg word
0xF6 0x51       ; 条件跳转
...
```

**EXEC 1/2 (`0x692E`) 抽样：**
```
0x12 0x4c 0x7A 0x65  ; load local, addr op
0x1A 0x93 0x5E 0x94 0xa2 0x94  ; stack/addr ops
0x8A 0x15 0xfa      ; 位操作
...
0x98 0x3c 0xca 0x31 ; 3字节比较操作
0x98 0x3e 0xca 0x31 ; 3字节比较操作
...
0x9a 10 08 cd 38 0x69 0xfe 9a a3 09 cd 38  ; trampoline 调用头 0x69FE
```

**EXEC 5 (`0x6476`) 抽样：**
```
0x11 0x5a 0xFE 0x60 0xFA 0x51 0x90  ; load, push, push dword local
0x8A 0x15 0x90       ; 位操作
...
0x9a 10 08 cd 38 0x7a 0x65 9a a3 09 cd 38  ; trampoline 调用头 0x657A
```

### 4.3.3 原生调用机制与表 A/B 索引代码（静态分析）

**原生调用触发点**：
VM 原生调用通过处理器 `0x39546` 实现：`bx = 6*cx + di`，然后 `lcall es:[bx+0xa]`。触发原生调用的 opcode 有 5 个：

| Opcode | Handler | 触发的原生调用 | 在共享主脚本中的使用 |
|--------|---------|---------------|---------------------|
| `0x88` | `0x3A3E2` | `lcall 0x1055:0x11F0`（栈清理/返回辅助） | `0x6D8E` 偏移 `0x0020`，操作数 `0x93` |
| `0xD9` | `0x3A4BA` | `lcall 0x1055:0x1058`（32位有符号除法 商） | 未在三个共享主脚本中使用 |
| `0xDD` | `0x3958A` | `lcall 0x0BD5:0x010B` | 未在三个共享主脚本中使用 |
| `0xE4` | `0x3A6E8` | `lcall 0x1055:0x0F8C`（32位有符号除法 商变体） | 未在三个共享主脚本中使用 |
| `0xE7` | `0x38ED0` | `lcall 0x9211:0x7210` | 未在三个共享主脚本中使用 |

**结论**：三个共享主脚本中仅 `0x6D8E`（EXEC 0/3/4）在偏移 `0x0020` 使用了 opcode `0x88`，调用 `0x1055:0x11F0`（栈清理/返回辅助）。其他两个脚本（`0x692E`、`0x6476`）未直接触发任何原生调用。原生战斗函数（`43B3:xxxx`、`28FD:xxxx`等）的调用方式仍未在 VM 字节码中找到直接对应，可能通过脚本头描述符表 `cs:0x1ECA` 的框架表，或通过数据段中的函数指针间接实现。标 Grade C，留待动态跟踪。

**表 A/表 B 索引代码（静态搜索结果：未定位）**：
表 A 基址 `DS:0x427A`（线性 `0x65C6A`，69 项）、表 B 基址 `DS:0x7B0E`（线性 `0x694FE`，15 项）已知，但“谁按什么 ID 取表项”的代码在 640KB 运行时快照中**未定位**。已尝试的静态搜索方法（均无结果）：

- 搜索 `mov reg, 0x427A/0x7B0E`（加载表基址到寄存器）：无匹配
- 搜索 `mov ax, [0x427A/0x7B0E]`（直接内存访问）：无匹配
- 搜索段前缀 + 直接访问 `0x427A/0x7B0E`：无匹配
- 搜索 `[reg + disp16]` 访问 `0x427A/0x7B0E`：仅有巧合命中（非表访问）
- 搜索 `lds/les reg, [0x427A/0x7B0E]`（加载远指针）：无匹配
- 搜索 `call/jmp far [reg + 0x427A/0x7B0E]`（间接远调用表）：无匹配
- 搜索表项远指针字节（如 `7A 2D 78 20` 为表 A[55]）：仅在表本身中出现
- 搜索 `shl reg, 2` + `lcall [reg+disp]`（index*4 访问表）：找到其他表（如 `0x43A4`、`0x43B6`），但不是表 A/B

**结论**：表 A/表 B 的索引代码不在 640KB 运行时快照中，可能位于未驻留的覆盖层，或使用了未发现的寻址模式。动态跟踪时应在表 A/B 读监视点（BP#13/#14）下断点，记录调用者和索引值。

---


## 5. 脚本 VM（`0x38CD`）

### 5.1 入口与操作码表

| 项 | 值 |
|---|---|
| VM 段基址 | `0x38CD0` |
| 主入口 | `0x38CD:0x0810`（线性 `0x394E0`） |
| 变体入口 | `0x38CD:0x0820`（线性 `0x394F0`，帧类型 `0x1C`） |
| 恢复/父脚本入口 | `0x38CD:0x09A3`（线性 `0x39A73`） |
| 操作码表 | `0x38CD:0x0000`（线性 `0x38CD0`），256 × 2 字节 |
| 处理器范围 | `0x38ED0–0x3ABFF`（表偏移 `0x0200–0x1E22`） |
| 操作码 0x00 | `0x045D` |
| 最小表项 | `0x0200`（操作码 `0xE7`，紧接表后的第一个处理器） |
| 最大表项 | `0x1E22` |

入口字节（`0x394E0`）：

```
5B 5A 59 45 55 8B EC 83 EC 14 C6 46 FC 1E EB 10
pop bx          ; [sp] = word after lcall (script header pointer)
pop dx          ; [sp] = return CS
pop cx          ; [sp] = caller's near return address (inline block pointer)
inc bp
push bp
mov  bp, sp
sub  sp, 0x14
mov  byte ptr [bp-4], 0x1E
jmp  0x39500
```

关键取指序列（`0x39500` 起）：

```asm
mov  [bp-2], ds
mov  [bp-0x12], si
mov  [bp-0x14], di
mov  ds, dx
mov  bx, [bx]          ; header = [return_address]
mov  si, [bx+6]        ; main bytecode pointer
mov  al, [si]
mov  [bp-3], al
mov  [bp-6], si
xor  ax, ax
mov  si, cx            ; cx = inline block pointer
lodsb                  ; frame index
shl  ax, 1
shl  ax, 1
mov  di, ax
mov  ax, [bx+di+0xA]   ; packed frame size
shr  ax, 1
shr  ax, 1
and  al, 0xF8
sub  ax, 0x14
sub  sp, ax
mov  word ptr [bp-0xA], 0
lodsw                  ; value
mov  bx, ax
mov  [bp-8], bx
xor  ah, ah
lodsb                  ; first opcode
mov  di, ax
shl  di, 1
jmp  word ptr cs:[di]  ; 0x38CD:0
```

### 5.2 调用约定

调用者：

```asm
call <trampoline>       ; pushes near return address = inline block pointer
; 或者 lcall <trampoline>
```

trampoline：

```asm
lcall 0x38CD:0x0810     ; pushes far return address = points at header word
dw    header_offset
lcall 0x38CD:0x09A3     ; resume/parent continuation (fallback path; 0x09A3 实为 0x8A 位操作handler，VM 返回协议见§9 第6项)
mov   ax, header_offset
mov   dx, cs
retf
```

VM 入口：

1. `pop bx` = trampoline 中 `lcall` 之后的字（header pointer）。
2. `pop dx` = trampoline 的 CS。
3. `pop cx` = **调用者的 near return address**，即内联字节码块指针。
4. `DS = header CS`；`bx = [header pointer]` = 头偏移；`si = [header+6]` = 主脚本。
5. `cx` 指向的内联块格式：
   ```
   db frame_index
   dw value
   db first_opcode
   ... bytecode ...
   ```
6. `[header+0xA + 4*frame_index]` 取打包栈帧大小；`sub sp` 分配局部帧。
7. 从内联块 `+3` 开始取第一个 opcode，`jmp cs:[opcode*2]`。

> 说明：trampoline 中 `lcall 0x38CD:0x09A3` 之后的 `mov ax,...; retf` 是**正常返回/父脚本恢复路径**；在“脚本直接返回原调用者”的路径下不会执行。`0x09A3` 本身已确认为 **0x8A opcode 的位操作handler**（位域读取/写入），不是 VM 返回入口。
>
> VM 返回协议目前有两个候选，均需动态验证：
> - **候选 A**：`0x1055:0x11f0`（线性 0x11740）— 栈清理返回例程。入栈 `CX`（返回值高）与 `DX`（返回值低），调整 `SP`，检查栈边界 `[0x5b82]`/`[0x5b7e]`，最后 `retf` 带回 `DX:CX`。
> - **候选 B**：`0x3994A`（0x38CD:0x0C7A，线性 0x3994A）— `lcall 0x1055:0x13a4; retf`。先调用核心初始化函数 `0x1055:0x13a4`，再 `retf` 返回给 trampoline 调用者。
>
> 两者均为 **B/C 级**，trampoline 的字节结构、header/主脚本指针、内联块格式均为 A 级。

### 5.3 栈帧与字节码模型

- 16 位栈式 VM；操作数用 `lodsb`/`lodsw` 内联。
- 局部变量位于 `[bp+di]`，`di` 为负偏移；帧大小来自头表。
- 处理器大量使用 `jmp word ptr cs:[di]` 做线程化分派；`0x39543` 是主分派点。
- 处理器范围内还包含次级表：
  - `cs:0x42C`：位域/移位辅助表；
  - `cs:0x4FA`、`cs:0x4B0`：恢复/续执行向量。
- 操作码分类（**暂定，Grade C**，仅抽样处理器）：
  - `0x00–0x1F`：局部变量 load/store、算术；
  - `0x20–0x3F`：栈/数组/复制；
  - `0x40–0x5F`：控制流、原生调用；
  - `0x60–0x7F`：地址/指针；
  - `0x80–0x9F`：条件分支；
  - `0xA0–0xBF`：位操作；
  - `0xC0–0xDF`：算术/比较；
  - `0xE0–0xFF`：特殊/返回。

### 5.4 原生导入调用（已解析，留待动态确认是否被实际调用）

处理器 `0x39546`/`0x39559` 实现“调用脚本导入函数”：

```asm
; 0x39546: bx = 6*CX + DI
mov  bx, cx
shl  bx, 1
add  bx, cx
shl  bx, 1
add  bx, di
...
; 0x3956E:
mov  si, [bp-0xC]
mov  cx, ds
mov  es, cx
mov  ax, ss
mov  ds, ax
lcall es:[bx+0xA]       ; 0x39579
```

- 表项为 **6 字节记录**，`BX = 6*CX + DI`。
- `ES` = 脚本数据段，`DS` 临时切到 `SS`（VM 栈段）。
- `DI` 用于选择记录内偏移（0/2/4 等）。
- `CX` 来自脚本头描述符 `[+0xA]`（`cs:0x1ECA`，§4.3.1），即描述符表内的记录索引。**注意**：描述符表项本身只有 4 字节（`offset + segment`），不存在 `[+0xA]` 字段；`[+0xA]` 是指**头结构**（描述符表项 `lcall` 后 `retf` 回到的地址）的偏移 `0xA`，那里存放栈帧大小表。`CX` 不是调用者的 near return address。
- `[di+0xa]` 被校验：若不等于 `-1`，则走框架表路径；等于 `-1` 时才到 `0x39561` 再做 `lcall es:[bx+0xA]`（脚本原生导入）。
- 镜像中 `cs:0x1ECA` 的 28 项描述符，`[+6]` 全部为非 `-1` 的主脚本指针，因此 `lcall es:[bx+0xa]` 的“脚本原生导入”路径在静态阶段**未见实例**。处理器语义为 **B**，是否被实际调用（以及 `DI` 的业务含义）留待动态验证（C）。

### 5.5 脚本 VM 调用的原生函数（`0x1055` 段）

VM handler 中通过 `lcall 0x1055:xxxx` 调用的原生函数位于核心段 `0x1055`。经逐条反汇编，确认以下 6 个入口：

| 函数 | 线性地址 | 功能（B/C 级） | 参数（栈） | 返回值 |
|------|----------|----------------|-----------|--------|
| `0x1055:0x13a4` | `0x118F4` | 模块初始化：依次调用 3 个内部子函数（`imm16` = `0x000a`/`0x0016`/`0x0003`） | `push imm16; push cs; call` | 无 |
| `0x1055:0x11f0` | `0x11740` | **栈清理/返回辅助**：调整 `SP`，检查栈边界 `[0x5b82]`/`[0x5b7e]`，`retf` 带回 `DX:CX` | `CX`, `DX`（入栈） | `DX:CX` |
| `0x1055:0x1058` | `0x115A8` | 32 位有符号除法（**商**） | 被除数 32 位 (`[bp+6]`/`[bp+8]`), 除数 32 位 (`[bp+0xa]`/`[bp+0xc]`) | 商 `DX:AX` |
| `0x1055:0x1456` | `0x119A6` | 32 位有符号除法（**余数**，带边界检查） | 同上 | 余数 `DX:AX` |
| `0x1055:0x0f8c` | `0x114DC` | 32 位有符号除法（**商**，变体） | 同上 | 商 `DX:AX` |
| `0x1055:0x13f6` | `0x11946` | 32 位有符号除法（**商**，溢出保护，`retf 8`） | 同上 | 商 `AX` |

**分析要点**：

- **除法家族**（`0x1055:0x1058`/`0x1456`/`0x0f8c`/`0x13f6`）：所有函数都先处理被除数和除数的符号位（32 位取反），再调用 x86 `div` 指令。`0x1456` 和 `0x13f6` 还包含**位移优化路径**（除数是 2 的幂时用 `shr/rcr` 位移加速）和**溢出/范围检查**（乘回商与原被除数比较，溢出时商减 1 或饱和）。这说明 VM 脚本中的 `/` 和 `%` 运算最终都落到这些核心函数。
- **栈返回**（`0x1055:0x11f0`）：这是 VM 脚本返回给 trampoline/原调用者的最可能路径。入栈 `CX`（高 16 位）与 `DX`（低 16 位）作为返回值，调整 `SP` 并检查栈边界，最后 `retf`。这与 trampoline 结构中 `lcall 0x38CD:0x0810` 压入的返回地址相配合。

**VM 原生调用调用约定**（从 handler `0x3A50E`/`0x3A52E`/`0x3A730`/`0x3A750` 推断）：

```asm
mov  di, sp              ; di = 栈指针
push word ptr ss:[di+6]  ; 参数 2（高/除数高）
push word ptr ss:[di+4]  ; 参数 1（低/除数低）
lcall  0x1055, 0xXXXX    ; 调用核心原生函数
pop  cx                  ; 清理栈
pop  cx
push dx                  ; 保存返回值高
push ax                  ; 保存返回值低
...
lodsb  al, byte ptr [si] ; 读取下一个 VM opcode
shl    di, 1
jmp    word ptr cs:[di]  ; 分派到下一个 opcode
```

**结论**：VM 脚本中的算术运算（`/`、`%`）最终通过 `lcall 0x1055:xxxx` 调用核心除法函数；脚本返回通过 `0x1055:0x11f0` 栈清理例程或 `0x3994A: lcall 0x1055:0x13a4; retf` 路径完成。两者均为 **B/C 级**，留待动态验证。

---

## 6. 次级原生链与未决调用者

### 6.1 函数

| 名称 | 线性 | 作用（B/C 级） |
|---|---:|---|
| ctx | `0x226E0` | 初始化 DS:0x920A/0x9210，调用 `43B3:21A8`、`28FD:752C`，写 0x920C–0x921A |
| sel | `0x22856` | 读 0x920C/0x920E/0x9210/0x9212，遍历邻接城，返回最佳目标或 `-1` |
| gate | `0x22F60` | 读 DS:0x9220，按条件调用 sel/exec/check |
| exec | `0x22D82` | 读取单位，调用 `43B3:2238/2128`、`3B1A:7F9A`，写状态 |
| check | `0x227BA` | 单位/目标合法性检查；被分发器、act2、act4、`0x24916` 调用 |

### 6.2 负结论（重要）

全镜像扫描结果：

- **零** 近调用（`E8 rel16`）到 ctx `0x226E0`。
- **零** 近调用到 gate `0x22F60`。
- **零** 远指针 `0x2078:0x1F60` / `0x2078:0x27E0`。
- 远指针进入 AI 段的只有表 A（69 项）和表 B（15 项）；两者都**不包含** ctx/gate。
- 全镜像对字 `0x27E0` 的命中只有 2 处（`0x0B221`、`0x281B3`），均不是远指针/代码入口。
- **act4 `0x2337E`（AI offset `0x2BFE`）：零近调用、零 `call far 0x2078:0x2BFE`、零远指针数据 `0x2078:0x2BFE`。** act4 不是表 A/表 B 项，也不对应任何已知静态入口。

因此：

> **不能**在静态阶段宣称 ctx/gate/act4 已被某条确定路径调用。它们可能是：
> 1. 运行时构造的回调（UI/对象虚表）；
> 2. 早期开发遗留、最终未使用的路径；
> 3. 通过脚本 VM 的导入表/函数 ID 间接调用，但导入表基址尚未闭合。

等级：**C/D**。动态跟踪时优先在 ctx/gate/act4 下断点验证。

### 6.3 与已确认链的关系

已确认的“act → dispatch → 六路 → 执行器脚本”链与 ctx/gate 链是**两条不同的路径**：

- act 链：表 A 导出的命令函数 → 分发器 → 六路 VM 脚本。
- ctx/gate 链：战略/回合级上下文 → sel/exec/check。

V10 文档把两者都称为“战略链”，V16 将其拆开：**act 链是当前静态闭合的主链**；ctx/gate 链是次级/未闭合链。

---

## 7. 对 V15/V15P 的逐条修正

| V15/V15P 结论 | V16 修正 | 证据 |
|---|---|---|
| AI 段 = `0x220D` | **`0x2078`** | 表 A/B 远指针、函数序言 |
| `0x220D0` 是内层 AI VM 操作码表（20/256 clean） | **不是表**；该区域是脚本桩/头/内联字节码 | 字节结构、`call 0x28C33` 桩、header 布局 |
| `cs:0x5789` = 41 远指针 VM 分派表 | **图形驱动函数表**（`GRAPHIC DRIVER V1.`），`0x374DA` 绑定 | 字符串 `cs:0x5773`、`0x374DA` 比较/拷贝 0x52 words |
| Outer VM 在 `0x31CA:0x7CBC`，表在 `cs:0` | VM 在 **`0x38CD:0x0810`**，表在 **`0x38CD0`** | 入口字节、256 表项、trampoline `lcall 0x38CD:0x0810` |
| AI 由任务 ID `0x08/0x0C–0x11` 触发 | **未证明**；记录描述符是事件记录，不是已确认 AI 触发 | 描述符结构 + 缺少对 ctx/gate 的静态边 |
| 六路分派在 `0x24A67` | 六路表在 **`0x23117`**（`cs:0x2997`，CS=0x2078） | 表字节 + `jmp word ptr cs:[bx+0x2997]` |
| `31CA:xxxx` 六种行动是原生函数 | **内联 VM 脚本 trampoline** | 每个目标以 `call 0x38692/0x3820E/0x37D92` 开始 |
| AI 段“零自引用”是因为 `0x220D` 不存在 | 真实段 `0x2078`；`0x220D` 扫描为零是预期 | 表 A/B + 段映射 |

> V15P 的“三层次映射”“DGROUP 表”“0x374DA 图形驱动绑定”本身仍有效；被修正的是把图形驱动表/AI 段/VM 段混为一谈的部分。

---

## 8. 验证

```bash
cd /home/ou/Projects/Genpei-Gassen-RE/Y-源平合战
python3 tools/v16_ai_chain_vm.py
```

当前结果：

```
155/155 checks passed
RESULT: PASS
```

校验内容：

- 运行时快照 640 KB；
- AI 模块入口 `call 0x28C33`；
- M1 thunk/wrapper；
- 表 A 69 项、表 B 15 项、段 `0x2078`、序号/序言；
- 六路表字节、六个处理器、六条 `lcall 0x31CA:xxxx`；
- 分发器序言/check/六路 `jmp`；
- act0/act1/act3/act5 的 `call 0x2303C`；act3→act2；act2/act4→check；
- action 4/5 包装 `0x234FA`/`0x23598`、表 A 55/56、六个分发器调用点；
- `0x238A8` 内部调用 act5 `0x2348E`（`0x2392C`）和 action 4 包装 `0x234FA`（`0x239D4`）；
- 分发器无 `0x2078:0x28BC` 远指针/远调用；
- `0x281AF` 的 5 个 AI 偏移/第二字；AI 段内无 `0x281AF/0x7A2F` 引用（表/字节码解释仍为 C 级）；
- 六个执行器 stub 的 `call` 目标；
- 三个 trampoline 的 `lcall 0x38CD:0x0810`、header 指针、resume `lcall`；
- 三个脚本头的 `mov ax,header; mov dx,cs; retf` 与 +6 主脚本指针；
- VM 入口字节、`[bx]`/`[bx+6]` 取指、`jmp cs:[di]`、`lcall es:[bx+0xA]`；
- VM 256 项表全部在 `0x0200–0x1E22`，操作码 0x00 = `0x045D`，0xE7 = `0x0200`，最大 `0x1E22`；
- ctx/sel/exec/gate/check 序言、ctx 写 0x920A、gate 读 0x9220；
- **负结论**：ctx/gate 无静态近调用、无 `0x2078:0x1F60/0x27E0` 远指针。

---

## 9. 未决问题（动态跟踪前）

1. **共享主脚本语义（抽样完成，原生调用未解）**：`0x6D8E` / `0x692E` / `0x6476` 的VM字节码已按操作码表逐条反汇编（§4.3.2），已确认脚本间环形调用（0/3/4→5→1/2→0/3/4）和条件分支结构；但**原生战斗函数（`43B3:xxxx`、`28FD:xxxx`等）如何从VM字节码中被调用**尚未解析：字节码中无直接的外部`lcall` opcode，可能通过脚本头描述符表`[+0xA]`的框架表或数据段函数指针间接实现。标 Grade C。
2. **ctx/gate 调用者**：运行时回调、对象虚表，还是死代码。
3. **表 A/表 B 的索引代码**：谁按什么 ID 取表项；表 A 的 49–68 与表 B 的 0–14 如何被选择。
4. **act4 的外部入口**：act3 已确认内联调用 act2；act4 `0x2337E` **静态侧零引用**（零近调用、零 `call far 0x2078:0x2BFE`、零远指针数据，不在表 A/B 中），外部入口尚未定位（action 4/5 包装 `0x234FA`/`0x23598` 已解决，不要与 act4/act5 混淆）。
5. **`0x281AF` 的 5 项**：是运行时/脚本构造的 AI 偏移表，还是字节码/数据？静态侧无引用，索引者未定（Grade C）。
6. **VM 返回协议**：`0x09A3` 已确认为 **0x8A opcode 的位操作handler**（非返回入口）。脚本返回给 trampoline/原调用者的路径有两个候选，均需动态验证：
   - `0x1055:0x11f0`（栈清理返回，带 `DX:CX` 返回值）；
   - `0x3994A: lcall 0x1055:0x13a4; retf`。
   `0x0810` vs `0x0820` 的帧类型差异（`0x1E` vs `0x1C`）亦需动态确认。
7. **动态确认**：在 ctx `0x226E0`、gate `0x22F60`、act4 `0x2337E`、VM 入口 `0x394E0`、六路表 `0x23117` 下断点，记录真实 `CS:IP` 与栈。

---

## 附录 A：关键地址速查

```
AI 段基址            0x20780
AI 模块首指令        0x20780  call 0x28C33
M1 thunk             0x226C6
ctx                  0x226E0  (AI off 0x1F60)
check                0x227BA  (0x203A)
sel                  0x22856  (0x20D6)
exec                 0x22D82  (0x2602)
gate                 0x22F60  (0x27E0)
dispatcher           0x2303C  (0x28BC)
six-way table        0x23117  (0x2997)
handlers             0x23123 / 0x23133 / 0x23140 / 0x2314D / 0x2315E / 0x2316F
act0..act5           0x23196 / 0x231EA / 0x23250 / 0x23304 / 0x2337E / 0x2348E
action 4 wrapper     0x234FA  (0x2D7A, table A 55)
action 5 wrapper     0x23598  (0x2E18, table A 56)
0x281AF 5 offsets    0x2BFE / 0x27E0 / 0x24CA / 0x2A6A / 0x2D7A
table A (DS:0x427A)  0x65C6A  (69 far ptrs)
table B (DS:0x7B0E)  0x694FE  (15 far ptrs)
M1 wrapper           0x24B5E
VM segment           0x38CD0
VM entry             0x394E0  (0x38CD:0x0810)
VM variant           0x394F0  (0x38CD:0x0820)
VM resume            0x39A73  (0x38CD:0x09A3)
VM opcode table      0x38CD0  (256 words)
VM native-call       0x39579  lcall es:[bx+0xA]
order exec 0          0x320EA  (0x31CA:0x044A) -> 0x38692
order exec 1          0x320F8  (0x31CA:0x0458) -> 0x3820E
order exec 2          0x32114  (0x31CA:0x0474) -> 0x3820E
order exec 3          0x32C9C  (0x31CA:0x0FFC) -> 0x38692
order exec 4          0x32CAA  (0x31CA:0x100A) -> 0x38692
order exec 5          0x330FA  (0x31CA:0x145A) -> 0x37D92
trampoline 0x69FE     0x38692
trampoline 0x657A     0x3820E
trampoline 0x60FE     0x37D92
header 0x69FE         0x3869E  +6 = 0x6D8E
header 0x657A         0x3821A  +6 = 0x692E
header 0x60FE         0x37D9E  +6 = 0x6476
```

## 附录 B：六路处理器字节

```
H0 0x23123: ff 76 08 ff 76 06 9a 4a 04 ca 31 83 c4 04 eb 4e
H1 0x23133: ff 76 08 ff 76 06 9a 58 04 ca 31 eb ee
H2 0x23140: ff 76 08 ff 76 06 9a 74 04 ca 31 eb e1
H3 0x2314D: 8d 46 fa 50 ff 76 08 ff 76 06 9a fc 0f ca 31 eb 20
H4 0x2315E: 8d 46 fa 50 ff 76 08 ff 76 06 9a 0a 10 ca 31 eb 0f
H5 0x2316F: 8d 46 fa 50 ff 76 08 ff 76 06 9a 5a 14 ca 31 83 c4 06
```

公共尾 `0x23181`：

```
ff 76 08 ff 76 06 90 0e e8 2e f6 83 c4 04 5e 5f 8b e5 5d cb
```

## 附录 C：复现脚本与产物

| 路径 | 用途 |
|---|---|
| `tools/v16_ai_chain_vm.py` | 本文件所有结构断言的校验器（155 项） |
| `tools/v16_trace_analyzer.py` | 动态跟踪分析器（保留，动态阶段使用） |
| `tools/v16_gdb_script.gdb` | 动态跟踪 gdb 脚本（保留） |
| `docs/v16/V16_DYNAMIC_TRACING_PLAN.md` | 动态跟踪计划（保留） |
| `re_out/runtime_0000_a0000.bin` | 640 KB 运行时快照（只读输入） |

---

**结论**：AI 调度链的**原生决策层**（action 0–5 包装 + dispatcher/check）、**六路分派层**（0x23117 + 六处理器）、**执行器层**（六个 0x31CA trampoline + 三个共享主脚本）和**脚本 VM 层**（0x38CD + 256 项表）已完成静态闭合。剩余缺口是**主脚本原生调用机制**、**ctx/gate 调用者**、**act4 的外部入口（静态零引用，留待动态确认是否为死代码）**、**`0x281AF` 表/字节码性质**和**表 A/B 索引代码**；这些属于动态/后续静态阶段。（主脚本字节码已按操作码表抽样反汇编，见 §4.3.2；脚本导入层 `cs:0x1ECA` 描述符表已解析，见 §4.3.1/§5.4，留待动态确认是否被实际调用。）

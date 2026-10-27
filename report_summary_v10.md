# V10 报告：战略回合距离矩阵与邻接目标选择

日期：2026-10-28
仓库：`Y-源平合战`
基线 HEAD：`f2b9573a247ca046d7fe6a4dae2b4417594e115b`
工作树：基线时干净；本报告对应的修改尚未提交。

## 1. 目标与范围

V10 的目标是用原版 `Main.exe` 的静态证据，先把**战略回合/非玩家势力 AI**
所依赖的两块底层能力做成可验证的 C 接口：

1. 城池距离 BFS、39×39 距离矩阵、最近友城/敌城；
2. 独立的 `0x22856` 邻接目标选择器。

同时修正旧文档中关于邻接槽 `+0x10/+0x12` 的错误结论，并保留现有进攻 AI
不变。按照既定约束：

- **没有**把新选择器接入 `ai_run_turn()`；
- **没有**重写月流程、内政六路分派或 `22D82` 移动执行；
- `7350/71B8` 评分模型隔离在回调后，未声称完整复现原版评分；
- 没有 DOSBox-X，因此没有任何“原版动态验证”结论。

## 2. 证据与地址映射

运行时坐标为 `43B3:IP`，静态等价为 `4000:(IP + 0x2F44)`：

| 能力 | 运行时 | 静态 | 证据等级 |
|---|---|---|---|
| 距离 BFS | `43B3:301C` | `4000:5F60` | B |
| 距离矩阵 | `43B3:30C2` | `4000:6006` | B |
| 最近友城 | `43B3:3114` | `4000:6058` | B |
| 最近敌城 | `43B3:3158` | `4000:609C` | B |
| 邻接槽 | `43B3:20C6` | `4000:500A` | B |
| 城势力 | `43B3:21A8` | `4000:50EC` | B |
| 无主判定 | `43B3:221E` | — | B |
| 武将计数 | `43B3:2ED6` | `4000:5E1A` | B |
| 矩阵读取 | `43B3:30FE` | `4000:6042` | B |
| 邻接目标选择 | 线性 `0x22856` | 覆盖层 | B（控制流）/ C（评分） |
| 决策上下文 | 线性 `0x226E0` | 覆盖层 | B |
| 执行门 | 线性 `0x22F60` | 覆盖层 | B |
| 六路分派 | 线性 `0x2303C` | 覆盖层 | B |

`0x226xx/0x228xx` 页面在 640 KB 转储里没有记录真实 CS。V9 输出的
`2285:0006` 是 `linear/16` 算术标签，**不是**确认的 CS:IP；V10 一律使用
线性地址，避免制造第二套错误坐标。

产物哈希：

- `re_out/runtime_0000_a0000.bin` MD5 `649725c7263606abbc93fef0923f96be`
- `re_out/bin/Main.exe` MD5 `7e7e1b875aa3b07eeb28c2eb5950aac1`
- `games/yphz/Sndata.gp` MD5 `1b5ae43e9c8d13aabe7cb1cae1c05555`（165064 B）
- `games/yphz/Savedata.gp` MD5 `52934d1ed10450e238e329d21eb0892a`

## 3. 静态逆向结论

### 3.1 距离与最近城（B）

`43B3:301C`：

- 起点=终点返回 0；
- `visited[39]` 清零，队列按层推进，`depth` 从 1 开始；
- 展开 7 个邻接槽，命中目标立即返回当前 `depth`；
- 深度上限 200，超限/图耗尽返回 `0xFF`。

`43B3:30C2` 对全部 `(row,col)` 调用 `301C`，写入 39×39 字节矩阵，
索引为 `matrix[from*39 + to]`。

`43B3:3114` / `3158` 的栈参数都是 **`(faction, source)`**：

```text
6058: bestd=0xFF; for c in 0..38:
        if owner(c) != faction: continue
        d = matrix[c*39 + source]
        if d < bestd: best=c, bestd=d
      return best or -1

609C: if faction == 0xFFFF: return -1
      for c in 0..38:
        if owner(c) == 0xFFFF or owner(c) == faction: continue
        d = matrix[c*39 + source]
        if d < bestd: best=c, bestd=d
      return best or -1
```

两者平局都保留 castle id 最小者；`6058` 会把 source 城自己算进去（距离 0），
这是原版行为。

### 3.2 `0x22856` 控制流（B）

完整守卫与比较顺序已确认：

```text
if 5E1A(actor)==0 or 0x920C==-1 or 0x920E==-1 or 0x9210==-1
   or actor==0x920C or 0x9212==-1: return -1

d1 = matrix[actor][0x920C]
d2 = matrix[actor][0x920E]
best_ref  = (d2 <= d1) ? 0x920E : 0x920C     // 平局取 B
best_dist = min(d1, d2)
best      = (5E1A(actor), best_dist, 0x9216)

for slot 0..6:
    cand = 20C6(actor, slot); if cand == -1: break
    owner = 21A8(cand)
    if owner != 0x9210 and owner != -1: continue
    officers = 5E1A(cand); if officers >= 15: continue
    dist  = matrix[cand][best_ref]
    value = 71B8(cand)
    cost  = 7350(owner, 0x9212, cand)
    ratio = 5BC8(value, cost) & 0xFF
    if (dist, officers, ratio) < (best_dist, best_officers, best_ratio):
        best = cand
return best or -1
```

关键指令证据（完整反汇编在 `re_out/v10/trace.json`）：

- `0x228E4`：`best = 0xFFFF`；`0x228DE/0x228E1`：初值 ratio 取字 `0x9216`；
- `0x2295C`：`cmp di, 0xF; jae skip`（15 武将门）；
- `0x22964..0x2297A`：先比距离，再比武将数，最后比率，严格更优才替换；
- `0x22957`：`sub ah, ah`，候选 ratio 只保留低 8 位。

`5BC8(a,b)` 为 `(a+b)*100/a`，`a==0 && b==0` 时返回 50。`0x226E0` 以
`5BC8(value, cost)` 调用；候选处先压成本、再压价值，栈上 `[bp+6]` 仍是
value。因此数值越小表示“单位价值所需成本越低”，选择器取最小值。

### 3.3 邻接槽修正（B）

旧文档称“`+0x12` 第一槽异常”，实际异常值在 `+0x10`：

- `20C6` 读 `castle + 0x12 + 2*slot, slot < 7`，即 `+0x12..+0x1e`；
- `Sndata.gp` 的 `+0x10`：38 条为 0，castle 19 为 179；
- 原版从不读 `+0x10`；
- `src/scenario.c` 从 `c[16 + k*2], k=1..7` 解析，正好是 `+0x12..+0x1e`；
- 39 城共 72 条无向边，图连通。

因此 `+0x10` 是保留/旗标字段，`+0x12` 就是第一邻城。旧版“BFS/边境可能受
第一槽影响”的保留意见撤销。

### 3.4 战略链与未决语义

`0x226E0` 写入共享字：

- `0x920A=castle`，`0x9210=21A8(castle)`；
- `0x920C=28FD:752C(castle,value_array)`；
- `0x920E=43B3:3158(owner,castle)`；
- `0x9214=28FD:71B8(castle)`；
- `0x9216=3B1A:5BC8(value, cost)`，其中 `cost=28FD:7350(owner,0x9212,castle)`；
- `0x9218..0x9220` 来自 `18A6:3D86` 与 `43B3:1C18/1CA2`。

`0x9212` 只读、为 -1 时选择器直接放弃，并作为 `7350` 的路径起点；它很可能
是某个城 ID，但写入者与业务含义未确认。

仍未解码：

- `31CA:044A/0458/0474/0FFC/100A/145A` 六种行动效果；
- `22D82` 状态变化；
- `18A6:3D86/3D54/2DBA/3856`、`43B3:1C18/1CA2` 返回语义；
- 月度 AI 顶层调用者（可能在未驻留覆盖层）；
- `30280` 的 from/to/owner 命名；
- `7350` 完整路径成本与 `71B8` 城价值模型。

## 4. 实现

### 4.1 `include/scenario.h` / `src/scenario.c`

新增：

```c
#define CASTLE_DIST_UNREACHABLE 0xFF

int  scenario_castle_distance(const Scenario *sc, int from, int to);
bool scenario_castle_distance_matrix(const Scenario *sc, uint8_t *out,
                                     int out_rows, int out_cols);
int  scenario_nearest_friendly_castle(const Scenario *sc, int faction, int source);
int  scenario_nearest_enemy_castle(const Scenario *sc, int faction, int source);
```

实现位置：`src/scenario.c:432/471/487/513`。

- BFS 保留原版层序、200 层上限与 `0xFF` 语义；
- 矩阵按 `row*out_cols+col` 写字节；
- 最近城保留 `(faction, source)` 参数顺序、严格 `<`、平局取最小 id；
- 非法 id 在 C 侧统一返回 `0xFF`/`-1`；原版各访问器没有统一的越界检查
  （例如 `30FE` 直接做 `matrix[row*39+col]`），正常 39 城数据上二者一致。

### 4.2 `include/ai.h` / `src/ai.c`

新增独立选择器：

```c
typedef int (*AiTargetScoreFn)(const Scenario *sc, int castle_id, void *user);

typedef struct {
    int castle;     // -1 = 原地不动
    int reference;  // 实际使用的参考城（A 或 B）
    int distance;   // matrix[candidate][reference]
    int officers;   // 候选城武将数
    int ratio;      // 调用方评分，按原版截断为 8 位
} AiAdjacentTarget;

int ai_choose_adjacent_target_22856(const Scenario *sc, int castle_id,
                                    int own_faction, int ref_castle_a,
                                    int ref_castle_b, int route_source,
                                    int current_ratio,
                                    AiTargetScoreFn score_fn, void *user,
                                    AiAdjacentTarget *out);
```

实现位置：`src/ai.c:195`。

- 守卫、`best_ref` 平局、15 武将门、字典序比较全部按反汇编；
- `score_fn == NULL` 时候选沿用当前 ratio，评分不参与平局；
- 未接入 `ai_run_turn()`，现有 C 级进攻启发式不变。

### 4.3 `src/main.c`

新增 `--v10-selftest`（`src/main.c:1913`，分派在 `:2500`）：

- 合成 39 城图覆盖：链、孤立点、平局、无主城、敌城、15 武将、0 武将、
  评分平局、原地不动；
- 额外加载真实 `games/yphz/Sndata.gp` 打印：
  `real d30_29=1 d30_0=13 friend30=30 enemy30=27`；
- 真实值另由独立 Python BFS 复核，结果一致。

### 4.4 工具与回归

- 新增 `tools/trace_v10_strategic.py`：反汇编 26 个函数/辅助，抽取
  `0x920A..0x9220` 读写、调用关系、helper 公式、邻接槽证据，输出
  `re_out/v10/trace.json` 与 `trace.md`；
- `tools/run_regression.sh` 新增 11 项 V10 断言（`:249` 起）；
- 回归总数 93 → **104**，全部 PASS。

## 5. 验证结果

### 5.1 构建

```text
cmake -S . -B build && cmake --build build -j   -> 0 warnings / 0 errors
make -j$(nproc)                                 -> 0 warnings / 0 errors
```

### 5.2 自测

```text
[V10] dist same=0 adj=1 chain=4 unreachable=255 rev=4
[V10] matrix ok=1 0_2=2 5_0=255 4_3=1
[V10] matrix sym=1 diag=1 max=5 unreachable_pairs=1282
[V10] friend tie=0 f1=1 unreachable=-1 badsrc=-1
[V10] enemy from0=1 from6=8 unowned_src=8 badfac=-1
[V10] select basic=11 ref=11 dist=0 officers=2
[V10] select score_tie=17 ratio=5
[V10] select unowned=22 ref=25 dist=1
[V10] select stay=-1
[V10] select no_officers=-1 guards=-1,-1,-1,-1,-1,-1,-1
[V10] real d30_29=1 d30_0=13 friend30=30 enemy30=27
```

### 5.3 回归

```text
TOTAL 104 checks: 104 passed, 0 failed
```

基线 93 项全部保留，未删除或弱化任何旧断言。

### 5.4 `--ai-step` 前后对比

| 项目 | 基线 | V10 后 | 差异 |
|---|---|---|---|
| 月度结果 | `date=1180/10 domestic=62 attacks=5 captures=4 pending=1` | 同左 | 无 |
| 存档大小 | 63655 B | 63655 B | 无 |
| 存档 hash | `0x1a8de859` | `0x1a8de859` | 无 |
| 存档 MD5 | `caecbe152e28c4ec712e29b8e296e147` | 同左 | 无 |

原因：V10 只新增独立 API/选择器与自测，未修改 `ai_run_turn()`、月结算或
存档布局，因此行为和字节完全不变。

### 5.5 存档兼容

- 写入：63655 B，`hash=0x1a8de859`；
- 读回：`[LOAD] read ... (63655 bytes) hash=0x1a8de859`；
- `--dump-state`：`date=1180/9 faction=0 hash=0x1a8de859`；
- 没有新增/移动/改变任何存档字段。

## 6. 置信度与限制

| 类别 | 内容 | 等级 |
|---|---|---|
| 原版静态确认 | BFS/矩阵/最近城/`0x22856` 控制流、15 武将门、字典序、`5BC8`、邻接槽 | B |
| 仅静态、未动态 | 上述全部（无 DOSBox-X） | B |
| C 侧精确移植 | 距离/矩阵/最近城/选择器控制流 | B |
| C 侧近似/隔离 | `7350/71B8` 评分模型；`score_fn=NULL` 时为 0 | C |
| 未知 | `0x9212` 语义、六路行动效果、`22D82`、顶层调用者、`30280` 命名 | 未知 |

**明确缺失：** 本工作区没有 DOSBox-X，未执行原版动态跟踪；没有把静态
反汇编或 C 自测冒充为原版动态验证。A 级结论数量为 0。

## 7. 产物与复现

```bash
cd Y-源平合战
python3 tools/trace_v10_strategic.py
./bin/yphz_engine --v10-selftest
bash tools/run_regression.sh
./bin/yphz_engine --ai-step --frames 1
```

产物：

- `re_out/v10/trace.json` / `trace.md`（反汇编与证据索引）
- `re_out/v10/baseline.md`（基线 HEAD/状态）
- `re_out/v10/regression_baseline.txt`（93/93 基线）
- `re_out/v10/regression_after.txt`（104/104）
- `report_summary_v10.md`（本文件）
- `docs/MAIN_EXE_ARCHITECTURE.md` 第 11 节
- `README.md` V10 小节

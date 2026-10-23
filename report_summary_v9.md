# V9 总结：城池访问器、战略覆盖层与合战评估

日期：2026-10-23  
基线：`e74e4f0`（V8 运行时布局与城字段公式）  
运行时镜像：`re_out/runtime_0000_a0000.bin`  
运行时 MD5：`649725c7263606abbc93fef0923f96be`  
静态 `Main.exe` MD5：`7e7e1b875aa3b07eeb28c2eb5950aac1`

## 1. 本阶段完成内容

1. 新增离线追踪工具 `tools/trace_v9_logic.py`：
   - 在 Ghidra 索引的 `4000` 函数真实指令边界上扫描 `0x0BF8`、
     `0x5358` 两个表基址的立即数/位移操作数；
   - 聚类 `4E8E`（`43B3:1F4A`）的 46 个近调用点；
   - 聚类 `5E1A`（`43B3:2ED6`）的 5 个远调用点；
   - 反汇编 `8C70`、`8EBA`、`6140`、`C07E`；
   - 抽取 `5F81:0000` 的 81×22 字节模板表。
2. 重新核对 `53C4` 的栈顺序与 `3B1A:596E/597E` 两个 helper，确认
   V8 的条件公式成立，并纠正“用户给的 `max(gold, ...)`”版本。
3. 在 `include/scenario.h` / `src/scenario.c` 实现城字段访问器与边界规则。
4. 在 `src/turn.c` 的内政、征税、施舍、朝廷、流言、月结算中启用这些
   helper，保持现有 CLI/存档格式不变。
5. 在 `include/kassen.h` / `src/kassen.c` 实现 `8EBA` 低兵力士气衰减、
   `8C70` 目标选择门、`C07E` 侧统计，以及 `5F81` 81×22 模板加载 API。
6. 新增 `--v9-selftest` 与 13 项回归断言；回归总数从 80 增至 93。

## 2. `619F:0BF8` / `619F:5358` 表

两张表都是 12 项 `seg:off` 远指针：

- `619F:0BF8`：属性 getter 表；
- `619F:5358`：属性 setter 表。

表项指向 `43B3` 的字段访问器，例如：

| 表 | 索引 | 目标 |
|---|---:|---|
| getter | 3 | `43B3:2ED6`（静态 `4000:5E1A`） |
| getter | 5 | `43B3:26D4`（静态 `4000:5618`） |
| getter | 7 | `43B3:2814`（静态 `4000:5758`） |
| setter | 4 | `43B3:2670`（静态 `4000:55B4`） |
| setter | 5 | `43B3:251C`（静态 `4000:5460`） |
| setter | 6 | `43B3:25BC`（静态 `4000:5500`） |

**关键结论：在 640 KiB 运行时镜像中没有任何真实指令把 `0x0BF8` 或
`0x5358` 作为立即数/位移直接引用。** 全镜像 `F8 0B` / `58 53` 字节命中
都不能在真实指令边界上解码成这两个操作数；表也没有对应的线性/远指针
引用。因此这两张表不是可由静态 C 调用图直接到达的 dispatch table，
而更像脚本 VM / 属性系统使用的间接属性表。V9 没有伪造“调用者”，
而是把表项本身和该否证结果写入 `re_out/v9/trace.json`。

## 3. `4E8E` 与高层战略调用者

46 个 `4E8E` 近调用者全部是“取城记录指针”的单字段 wrapper，例如
`5336`（`+0x2a`）、`534c`（`+0x2c`）、`555e`（`+0x32`）、
`5574`（`+0x34`）。它们的上一层调用者仍是复合访问器：

- `53C4`：米/金换算；
- `55B4`：士气增加（城防 cap）；
- `5460`、`5500`、`5530`、`55EA`：其他字段 setter；
- `57F8`：同时调用 4 个 wrapper 的复合型函数。

这些复合访问器在 `4000` 索引中没有静态调用者，进一步支持“表/VM 间接
分派”的判断。

真正的高层战略代码位于覆盖层 `2285:0006`（线性 `0x22856`）。它：

- 调用 `43B3:2ED6`（`5E1A`）遍历候选链；
- 读取 `0x920C/0x920E/0x9210/0x9212` 等战略全局；
- 调用 `43B3:30FE/20C6/21A8/221E/7350/71B8/5BC8` 做目标评估；
- 失败时返回 `-1`，成功时返回候选索引。

这是 V9 找到的最有说服力的“战略 AI 目标选择”高层函数；完整反汇编已
写入 `re_out/v9/trace.md` 的 `5e1a far-call sites` 段。

## 4. `53C4` 公式复核

原始反汇编（`43B3:2480`，静态 `4000:53C4`）顺序：

1. `di = rice / 5`；
2. `ax = gold`；
3. 若 `di > ax`，直接返回（**分支守卫**）；
4. 否则计算 `v = (gold * param) / 100`；
5. `v = max(v, rice/5)`，因为 `3B1A:597E` 是 **max**；
6. `set_gold(v)`，而 setter 是 `min(rice, v)`。

所以确认公式为：

```c
if (rice / 5 <= gold)
    gold = min(rice, max(rice / 5, (gold * param) / 100));
```

不是：

```c
gold = min(rice, max(gold, param * (rice / 5) / 100));
```

`castle_calc_53c4()` 按前者实现。`596E` 是 min、`597E` 是 max 的差异是
V9 纠正 V8 结论的关键证据。

## 5. 已移植的城字段规则

`include/scenario.h` / `src/scenario.c`：

| API | 原版依据 | 规则 |
|---|---|---|
| `castle_set_gold_bounded` | `4000:5362` | `gold = clamp(value, 0, rice)` |
| `castle_sub_gold` | `4000:5396` | `gold = min(rice, max(0, gold - amount))` |
| `castle_calc_53c4` | `4000:53C4` | 上述条件公式 |
| `castle_set_troops` / `add` / `sub` | `4000:5436/5460/5492` | `0..999` |
| `castle_set_training` / `add` / `sub` | `4000:54D6/5500/5530` | `0..100` |
| `castle_set_morale` | `4000:558A` | `0..999` |
| `castle_add_morale_capped_by_def` | `4000:55B4` | `min(morale + delta, min(defense, 999))`，下限 0 |
| `castle_sub_morale` | `4000:55EA` | 下限 0 |

原版 `4F56` 只做“上限”裁剪，不显式做下限；因为参数是 `uint16`，负数
不会从原调用路径出现。移植层仍加了下限 0，以防 C 侧负值破坏显示。

内政接入点：

- 开发：`castle_sub_gold`；
- 征兵：`castle_sub_gold` + `castle_add_troops`；
- 征税：`castle_sub_morale`；
- 施舍：`castle_sub_gold` + `castle_add_morale_capped_by_def`；
- 朝廷：每城 `castle_add_morale_capped_by_def(2)`；
- 流言：`castle_sub_morale` + `castle_sub_troops`；
- 月结算：税收后 `castle_set_gold_bounded`。

## 6. 武将原始字段 helper

`4000:1887` 对两个原始 `u16` 字段做交叉比较：

```c
b_1a == a_1c || a_1a == b_1c
```

`+0x1a/+0x1c` 不在 `Sndata.gp` 的 71 字节存档记录中，因此 API 显式接收
四个原始字，而不是往 `ScenarioOfficer` 里塞未验证字段：

```c
bool officer_same_location_1887(uint16_t a_1a, uint16_t a_1c,
                                uint16_t b_1a, uint16_t b_1c);
```

`4000:1957/1977/1999` 是 `+0x21` 状态位：

```c
bits |= mask;
bits &= ~mask;
return (bits & mask) != 0;
```

同样以纯 `uint16_t` API 暴露，不改存档布局（当前快照仍为 63655 字节）。

`5E1A` 的实际行为是沿 `3B1A:750A` 链遍历并计数，不是简单的
“同城判断”。引擎侧以 `officer_count_at_castle_5e1a()` 做语义移植。
`6006` 是 39×39 地图初始化/BFS，`704E` 是长战略评估函数；它们不是
简单的“同势力/邻接”helper，V9 只提供引擎层
`castle_adjacent_to_faction()`，没有把这两个复杂函数伪装成一行规则。

## 7. 合战逻辑

### 7.1 `8EBA` 低兵力士气衰减

单位记录 `+0x2f` 是兵力（`C07E` 对它求平均）。衰减：

- 兵力 `0`：`morale -= morale / 10`；
- 兵力 `1..9`：`morale -= morale / 50`；
- 兵力 `>=10`：不减。

移植为 `kassen_original_low_troop_decay()`，在 `kassen_command()` 每次
交锋前对攻守双方调用。当前单栈合战只有总兵力，因此这是把单位级规则
应用到总兵力；单位级结构 `KassenOriginalUnit` 已留给后续 10 单位战场。

### 7.2 `8C70` 目标选择门

确认结构：

1. 读取双方侧强度；
2. `3 * actor.hp > target.hp`、侧强度比、`target.action == 5` 等条件决定
   `threshold = 60` 或 `0`；
3. 计算 `2*+0x31 + +0x42` 类型分；
4. 比较侧领袖类型与 actor 类型；
5. 分 `+0x42 > 70` / `> 50` 两条概率分支；
6. 掷 `0..199`，与基础值比较，返回 0/1。

移植为 `kassen_original_target_choice()`，使用确定性 xorshift PRNG，
参数 `KassenOriginalUnit` 显式携带 `side/action/type/status/hp/troops/
stat31/stat42/leader_type`。`--v9-selftest` 用固定 seed 验证返回 1。

### 7.3 `C07E` 侧统计

确认：

- 单位步长 `0x37`；
- 每侧最多 10 个单位，侧块 `0x226` 字节；
- 遍历有效（`+0x2a != 0`）单位，对 `+0x2f` 求平均；
- 从第 5 槽开始累计 `+0x2c`（预备队 HP 和）；
- 结果通过 `43B3:3A64/3856` 写回侧统计。

移植为 `kassen_original_side_average_troops()` 与
`kassen_original_side_reserve_hp()`，并在 selftest 中验证平均 20、预备
HP 和 33。

## 8. `5F81:0000` 81×22 模板表

- 基址：`5F81:0000`，线性 `0x5F810`；
- 81 项 × 22 字节，共 1782 字节；
- SHA-256：`52312a10a1477af3a2e1192654de5020d3ac85a7e8d7a05b095332e53308b82c`；
- **本快照中 81 项完全相同**：
  `ff ff 00 00 00 00 ff ff ff ff 00 00 00 00 00 00 00 00 00 00 00 00`。

因此这是“空/未初始化模板表”，而不是可直接提取的 81 种兵种数值。
V9 把精确 1782 字节固化到 `include/kassen_original_templates.h`，并提供
`kassen_original_template()`；没有编造字段语义。若后续拿到初始化后的
运行时镜像，只需替换该头文件数据即可。

## 9. 验收

- `make clean && make -j$(nproc)`：0 warnings / 0 errors；
- `cmake --build build -j`：0 warnings / 0 errors；
- `bash tools/run_regression.sh`：**93 passed, 0 failed**；
- 新增 13 项 V9 断言覆盖金/米、53C4、兵力、训练、士气、1887、状态位、
  模板、8EBA、C07E、8C70；
- 存档快照仍为 63655 字节，未新增未验证的运行时字段。

## 10. 仍未确认

1. `619F:0BF8/5358` 的 VM/属性分派入口（本镜像无直接指令引用）；
2. `+0x1a/+0x1c` 的真实业务语义；
3. `5F81` 初始化数据来源与 22 字节字段布局；
4. `6006`/`704E` 的完整战略语义；
5. `8C70` 中 `+0x2c` 到底命名 morale 还是 HP（当前按 HP/强度混合建模）；
6. `C07E` 第一轮遍历是否已经包含预备队（当前做一次归一去重）；
7. 3000 段/覆盖层的完整调用图。

以上未确认项不影响已实现的城字段边界规则和可重复回归；报告中明确
区分“二进制证据”“语义移植”和“引擎近似”。

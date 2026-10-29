# V12 静态评分逻辑追踪 V2（详细报告）

## 0. 生成命令与产物哈希

- 生成命令：`python3 tools/trace_v12_scoring_v2.py`
- 本地 JSON：`re_out/v12_v2/trace.json`，163854 bytes
- 本地 JSON SHA-256：`698d27a4ad47e03f343e3e1f99f463c6651322b1410af3fec8d704ce9845f869`
- JSON 是否受跟踪：`False`
- 说明：JSON 留在被忽略的 re_out/ 下，不进入仓库；受跟踪的 Markdown 副本应与本次生成结果逐字节一致。

## 1. 输入与哈希

| 项目 | 值 |
|---|---|
| Runtime 路径 | `re_out/runtime_0000_a0000.bin` |
| Runtime 大小 | 655360 bytes (`0xA0000`) |
| Runtime MD5 | `649725c7263606abbc93fef0923f96be` |
| Runtime 基线匹配 | `True` |
| Main.exe 路径 | `re_out/bin/Main.exe` |
| Main.exe 大小 | 384322 bytes (`0x5DD42`) |
| Main.exe MD5 | `7e7e1b875aa3b07eeb28c2eb5950aac1` |
| Main.exe 基线匹配 | `True` |

## 2. 扫描方法与范围

- 入口数量：22，逐个从入口做 16 位线性反汇编。
- 单入口上限：`0x400` 字节；遇首个 `RETF` 停止。
- 覆盖区间（入口起点最小值到解码终点最大值）：`0x22856`..`0x47031`。区间不连续，未扫描的空洞不参与结论。
- 每个入口记录：起始、结束（不含）、指令数、停止原因、RET-like 列表、解码空隙、`.byte` 不可解码填充、覆盖是否完整。

### 2.1 各入口边界与覆盖

| 函数 | 起始 | 结束（不含） | 指令 | 停止原因 | 覆盖完整 | 空隙 | 不可解码 | RET-like |
|---|---:|---:|---:|---|---|---|---:|---|
| `enum_castles_2f530` | `0x2F530` | `0x2F5D2` | 68 | first_retf_linear_sweep | True | — | — | `0x2F5D1` retf |
| `filter_2f5d2` | `0x2F5D2` | `0x2F6AD` | 85 | first_retf_linear_sweep | True | — | — | `0x2F6AC` retf |
| `route_enum_2f6ae` | `0x2F6AE` | `0x2F6F2` | 35 | first_retf_linear_sweep | True | — | — | `0x2F6F1` retf |
| `helper_300c0` | `0x300C0` | `0x30120` | 43 | first_retf_linear_sweep | True | — | — | `0x3011F` retf |
| `helper_30120` | `0x30120` | `0x30188` | 51 | first_retf_linear_sweep | True | — | — | `0x30187` retf |
| `value_71b8` | `0x30188` | `0x30280` | 104 | first_retf_linear_sweep | True | — | — | `0x3027F` retf |
| `edge_cost_30280` | `0x30280` | `0x302C3` | 29 | first_retf_linear_sweep | True | — | — | `0x302C2` retf |
| `path_cost_a_302c4` | `0x302C4` | `0x302F2` | 27 | first_retf_linear_sweep | True | — | — | `0x302F1` retf |
| `path_cost_b_302f2` | `0x302F2` | `0x30320` | 27 | first_retf_linear_sweep | True | — | — | `0x3031F` retf |
| `route_cost_7350` | `0x30320` | `0x3037E` | 45 | first_retf_linear_sweep | True | — | — | `0x3037D` retf |
| `strategic_guard_22856` | `0x22856` | `0x229AE` | 131 | first_retf_linear_sweep | True | — | — | `0x229AD` retf |
| `sort_key_43b3_15c8` | `0x450F8` | `0x45160` | 48 | first_retf_linear_sweep | True | — | — | `0x4515F` retf |
| `helper_43b3_2814` | `0x46344` | `0x4635A` | 12 | first_retf_linear_sweep | True | — | — | `0x46359` retf |
| `helper_43b3_2920` | `0x46450` | `0x46478` | 24 | first_retf_linear_sweep | True | — | — | `0x46477` retf |
| `helper_43b3_2b2c` | `0x4665C` | `0x46672` | 12 | first_retf_linear_sweep | True | — | — | `0x46671` retf |
| `helper_43b3_34ec` | `0x4701C` | `0x47031` | 12 | first_retf_linear_sweep | True | — | — | `0x47030` retf |
| `helper_43b3_1ca2` | `0x457D2` | `0x457E0` | 9 | first_retf_linear_sweep | True | — | — | `0x457DF` retf |
| `helper_3b1a_596e` | `0x40B0E` | `0x40B1E` | 8 | first_retf_linear_sweep | True | — | — | `0x40B1D` retf |
| `helper_3b1a_5af8` | `0x40C98` | `0x40CAC` | 11 | first_retf_linear_sweep | True | — | — | `0x40CAB` retf |
| `helper_3b1a_5a0e` | `0x40BAE` | `0x40BD8` | 20 | first_retf_linear_sweep | True | — | — | `0x40BD7` retf |
| `helper_3b1a_5a38` | `0x40BD8` | `0x40BEA` | 10 | first_retf_linear_sweep | True | — | — | `0x40BE9` retf |
| `helper_3b1a_5bc8` | `0x40D68` | `0x40DA2` | 28 | first_retf_linear_sweep | True | — | — | `0x40DA1` retf |

## 3. 强制核验项

### known_read_0x920C_at_22874

- 地址：`0x22874`
- 预期：`8b360c92`
- 实际：`8b360c92`
- 结果：**PASS**
- 说明：expected raw bytes for 0x920C read

### known_read_0x920E_at_2287D

- 地址：`0x2287D`
- 预期：`a10e92`
- 实际：`a10e92`
- 结果：**PASS**
- 说明：expected raw bytes for 0x920E read

### known_read_0x9210_at_22888

- 地址：`0x22888`
- 预期：`a11092`
- 实际：`a11092`
- 结果：**PASS**
- 说明：expected raw bytes for 0x9210 read

### known_read_0x9212_at_22898

- 地址：`0x22898`
- 预期：`a11292`
- 实际：`a11292`
- 结果：**PASS**
- 说明：expected raw bytes for 0x9212 read

### known_read_0x9216_at_228DE

- 地址：`0x228DE`
- 预期：`a11692`
- 实际：`a11692`
- 结果：**PASS**
- 说明：expected raw bytes for 0x9216 read

### entry_22856_prologue

- 地址：`0x22856`
- 预期：`558bec`
- 实际：`558bec`
- 结果：**PASS**
- 说明：PUSH BP; MOV BP,SP at the claimed entry

### return_path_22856

- 地址：`0x229A8`
- 预期：`5e5f8be55dcb`
- 实际：`5e5f8be55dcb`
- 结果：**PASS**
- 说明：POP SI; POP DI; MOV SP,BP; POP BP; RETF

### boundary_22856_linear_sweep

- 地址：`0x22856`
- 预期：`22856..229AE`
- 实际：`22856..229AE`
- 结果：**PASS**
- 说明：contiguous linear decode, one RETF at 0x229AD, no gaps; not a full CFG proof

### 7350_call_target_at_3035b

- 地址：`0x3035B`
- 预期：`e894ff`
- 实际：`e894ff`
- 解码目标：`302F2`；独立重算：`302F2`
- 结果：**PASS**
- 说明：near relative target = 0x3035B + 3 + signed16(0xFF94) = 0x302F2

### json_records_9212_reference

- 地址：`0x22898`
- 预期：`a11292`
- 实际：`22898`
- 结果：**PASS**
- 说明：the global-ref list must contain 0x22898 as a read of 0x9212

### scanner_self_test_all_pass

- 地址：`0xsynthetic`
- 预期：`12 synthetic cases`
- 实际：`12/12 pass`
- 结果：**PASS**
- 说明：segment override, base/index exclusion and access classification behave as specified

## 4. 扫描器自检（合成指令）

以下用例只用合成机器码验证过滤规则本身，不读取快照，不代表游戏镜像内容。

| 用例 | 字节 | 指令 | 预期 | 实际 | 结果 |
|---|---|---|---|---|---|
| ds_absolute_read | `a11292` | `mov ax, word ptr [0x9212]` | `{'segment': 'DS(default)', 'is_ds': True, 'access': 'read', 'candidate': True}` | `{'segment': 'DS(default)', 'is_ds': True, 'access': 'read', 'candidate': True}` | PASS |
| es_override_rejected | `26a11292` | `mov ax, word ptr es:[0x9212]` | `{'segment': 'ES', 'is_ds': False, 'access': 'read', 'candidate': False}` | `{'segment': 'ES', 'is_ds': False, 'access': 'read', 'candidate': False}` | PASS |
| cs_override_rejected | `2ea11292` | `mov ax, word ptr cs:[0x9212]` | `{'segment': 'CS', 'is_ds': False, 'access': 'read', 'candidate': False}` | `{'segment': 'CS', 'is_ds': False, 'access': 'read', 'candidate': False}` | PASS |
| ss_override_rejected | `36a11292` | `mov ax, word ptr ss:[0x9212]` | `{'segment': 'SS', 'is_ds': False, 'access': 'read', 'candidate': False}` | `{'segment': 'SS', 'is_ds': False, 'access': 'read', 'candidate': False}` | PASS |
| explicit_ds_override_kept | `3ea11292` | `mov ax, word ptr ds:[0x9212]` | `{'segment': 'DS', 'is_ds': True, 'access': 'read', 'candidate': True}` | `{'segment': 'DS', 'is_ds': True, 'access': 'read', 'candidate': True}` | PASS |
| cmp_is_read_not_write | `39061292` | `cmp word ptr [0x9212], ax` | `{'access': 'read', 'candidate': True}` | `{'access': 'read', 'candidate': True}` | PASS |
| test_is_unknown | `85061292` | `test word ptr [0x9212], ax` | `{'access': 'unknown', 'candidate': True}` | `{'access': 'unknown', 'candidate': True}` | PASS |
| mov_store_is_write | `89061292` | `mov word ptr [0x9212], ax` | `{'access': 'write', 'candidate': True}` | `{'access': 'write', 'candidate': True}` | PASS |
| add_is_read_write | `8306129201` | `add word ptr [0x9212], 1` | `{'access': 'read/write', 'candidate': True}` | `{'access': 'read/write', 'candidate': True}` | PASS |
| inc_is_read_write | `ff061292` | `inc word ptr [0x9212]` | `{'access': 'read/write', 'candidate': True}` | `{'access': 'read/write', 'candidate': True}` | PASS |
| bp_relative_rejected | `8b861292` | `mov ax, word ptr [bp - 0x6dee]` | `{'base_nonzero': True, 'candidate': False}` | `{'base_nonzero': True, 'candidate': False}` | PASS |
| si_indexed_rejected | `8b841292` | `mov ax, word ptr [si - 0x6dee]` | `{'base_nonzero': True, 'candidate': False}` | `{'base_nonzero': True, 'candidate': False}` | PASS |

## 5. DS 绝对寻址候选

| 地址 | 原始字节 | 指令 | 访问 | 全局偏移 | 函数 | 段 | 前缀覆盖 |
|---:|---|---|---|---|---|---|---|
| `0x22874` | `8b360c92` | `mov si, word ptr [0x920c]` | read | `0x920C` | `strategic_guard_22856` | DS(default) | False |
| `0x2287D` | `a10e92` | `mov ax, word ptr [0x920e]` | read | `0x920E` | `strategic_guard_22856` | DS(default) | False |
| `0x22888` | `a11092` | `mov ax, word ptr [0x9210]` | read | `0x9210` | `strategic_guard_22856` | DS(default) | False |
| `0x22898` | `a11292` | `mov ax, word ptr [0x9212]` | read | `0x9212` | `strategic_guard_22856` | DS(default) | False |
| `0x228DE` | `a11692` | `mov ax, word ptr [0x9216]` | read | `0x9216` | `strategic_guard_22856` | DS(default) | False |

## 6. 被排除的段覆盖命中

本次扫描范围内没有出现带非 DS 段覆盖的绝对命中。

## 7. 调用线索

### `enum_castles_2f530` (`0x2F530`)
- `0x2F544` `9a3822b343` far → `43B3:2238` （`43B3*16+2238`）
- `0x2F558` `9a8a23b343` far → `43B3:238A` （`43B3*16+238A`）
- `0x2F568` `9acc23b343` far → `43B3:23CC` （`43B3*16+23CC`）
- `0x2F57E` `9a0a751a3b` far → `3B1A:750A` （`3B1A*16+750A`）
- `0x2F588` `9af400b343` far → `43B3:00F4` （`43B3*16+00F4`）
- `0x2F5A1` `e894fe` near → `linear 0x2F438` （`0x2F5A1+3+(-364)`）

### `filter_2f5d2` (`0x2F5D2`)
- `0x2F5DE` `9a9221b343` far → `43B3:2192` （`43B3*16+2192`）
- `0x2F5E7` `9aa821b343` far → `43B3:21A8` （`43B3*16+21A8`）
- `0x2F5F9` `9a9221b343` far → `43B3:2192` （`43B3*16+2192`）
- `0x2F614` `9aa821b343` far → `43B3:21A8` （`43B3*16+21A8`）
- `0x2F636` `9aaa2cb343` far → `43B3:2CAA` （`43B3*16+2CAA`）
- `0x2F64C` `9af020b343` far → `43B3:20F0` （`43B3*16+20F0`）
- `0x2F661` `9a1e22b343` far → `43B3:221E` （`43B3*16+221E`）
- `0x2F680` `9aa821b343` far → `43B3:21A8` （`43B3*16+21A8`）
- `0x2F697` `9a1e22b343` far → `43B3:221E` （`43B3*16+221E`）

### `route_enum_2f6ae` (`0x2F6AE`)
- `0x2F6C1` `e80eff` near → `linear 0x2F5D2` （`0x2F6C1+3+(-242)`）

### `helper_300c0` (`0x300C0`)
- `0x300DF` `9ad807b343` far → `43B3:07D8` （`43B3*16+07D8`）
- `0x300E9` `9a7a06b343` far → `43B3:067A` （`43B3*16+067A`）
- `0x300F4` `9aaa05b343` far → `43B3:05AA` （`43B3*16+05AA`）
- `0x300FB` `9a0e5a1a3b` far → `3B1A:5A0E` （`3B1A*16+5A0E`）
- `0x30108` `9af85a1a3b` far → `3B1A:5AF8` （`3B1A*16+5AF8`）

### `helper_30120` (`0x30120`)
- `0x30132` `e8f3f7` near → `linear 0x2F928` （`0x30132+3+(-2061)`）
- `0x30147` `e876ff` near → `linear 0x300C0` （`0x30147+3+(-138)`）
- `0x3014F` `9a4038b343` far → `43B3:3840` （`43B3*16+3840`）
- `0x30160` `9a7e39b343` far → `43B3:397E` （`43B3*16+397E`）
- `0x30168` `9a0e5a1a3b` far → `3B1A:5A0E` （`3B1A*16+5A0E`）
- `0x30177` `9af85a1a3b` far → `3B1A:5AF8` （`3B1A*16+5AF8`）

### `value_71b8` (`0x30188`)
- `0x301A3` `e88af3` near → `linear 0x2F530` （`0x301A3+3+(-3190)`）
- `0x301C2` `9ac815b343` far → `43B3:15C8` （`43B3*16+15C8`）
- `0x301CF` `9a6e591a3b` far → `3B1A:596E` （`3B1A*16+596E`）
- `0x301F8` `9a6e591a3b` far → `3B1A:596E` （`3B1A*16+596E`）
- `0x30209` `e8b4fe` near → `linear 0x300C0` （`0x30209+3+(-332)`）
- `0x30217` `9a1428b343` far → `43B3:2814` （`43B3*16+2814`）
- `0x3021E` `9a6e591a3b` far → `3B1A:596E` （`3B1A*16+596E`）
- `0x30230` `9a2c2bb343` far → `43B3:2B2C` （`43B3*16+2B2C`）
- `0x3023B` `9af85a1a3b` far → `3B1A:5AF8` （`3B1A*16+5AF8`）
- `0x3024A` `9a2029b343` far → `43B3:2920` （`43B3*16+2920`）
- `0x30254` `9a0e5a1a3b` far → `3B1A:5A0E` （`3B1A*16+5A0E`）
- `0x30272` `9af85a1a3b` far → `3B1A:5AF8` （`3B1A*16+5AF8`）

### `edge_cost_30280` (`0x30280`)
- `0x3029D` `9aa21cb343` far → `43B3:1CA2` （`43B3*16+1CA2`）
- `0x302AE` `9a385a1a3b` far → `3B1A:5A38` （`3B1A*16+5A38`）

### `path_cost_a_302c4` (`0x302C4`)
- `0x302CF` `e84efe` near → `linear 0x30120` （`0x302CF+3+(-434)`）
- `0x302D7` `9aec34b343` far → `43B3:34EC` （`43B3*16+34EC`）
- `0x302E6` `e897ff` near → `linear 0x30280` （`0x302E6+3+(-105)`）

### `path_cost_b_302f2` (`0x302F2`)
- `0x302FD` `e888fe` near → `linear 0x30188` （`0x302FD+3+(-376)`）
- `0x30305` `9aa821b343` far → `43B3:21A8` （`43B3*16+21A8`）
- `0x30314` `e869ff` near → `linear 0x30280` （`0x30314+3+(-151)`）

### `route_cost_7350` (`0x30320`)
- `0x3033A` `e871f3` near → `linear 0x2F6AE` （`0x3033A+3+(-3215)`）
- `0x3035B` `e894ff` near → `linear 0x302F2` （`0x3035B+3+(-108)`）
- `0x30366` `9af85a1a3b` far → `3B1A:5AF8` （`3B1A*16+5AF8`）

### `strategic_guard_22856` (`0x22856`)
- `0x22861` `9ad62eb343` far → `43B3:2ED6` （`43B3*16+2ED6`）
- `0x228A2` `9afe30b343` far → `43B3:30FE` （`43B3*16+30FE`）
- `0x228B4` `9afe30b343` far → `43B3:30FE` （`43B3*16+30FE`）
- `0x228F4` `9ac620b343` far → `43B3:20C6` （`43B3*16+20C6`）
- `0x22907` `9aa821b343` far → `43B3:21A8` （`43B3*16+21A8`）
- `0x22913` `9a1e22b343` far → `43B3:221E` （`43B3*16+221E`）
- `0x2291E` `9ad62eb343` far → `43B3:2ED6` （`43B3*16+2ED6`）
- `0x2292A` `9afe30b343` far → `43B3:30FE` （`43B3*16+30FE`）
- `0x2293E` `9a5073fd28` far → `28FD:7350` （`28FD*16+7350`）
- `0x22948` `9ab871fd28` far → `28FD:71B8` （`28FD*16+71B8`）
- `0x2294F` `9ac85b1a3b` far → `3B1A:5BC8` （`3B1A*16+5BC8`）

### `sort_key_43b3_15c8` (`0x450F8`)
- `0x45146` `e84bfe` near → `linear 0x44F94` （`0x45146+3+(-437)`）
- `0x45156` `e8edfe` near → `linear 0x45046` （`0x45156+3+(-275)`）

### `helper_43b3_2814` (`0x46344`)
- `0x4634C` `e82bf7` near → `linear 0x45A7A` （`0x4634C+3+(-2261)`）

### `helper_43b3_2920` (`0x46450`)
- `0x4645A` `e8e7fe` near → `linear 0x46344` （`0x4645A+3+(-281)`）
- `0x46465` `e812f6` near → `linear 0x45A7A` （`0x46465+3+(-2542)`）

### `helper_43b3_2b2c` (`0x4665C`)
- `0x46664` `e813f4` near → `linear 0x45A7A` （`0x46664+3+(-3053)`）

### `helper_43b3_34ec` (`0x4701C`)
- `0x47024` `e805fd` near → `linear 0x46D2C` （`0x47024+3+(-763)`）

### `helper_43b3_1ca2` (`0x457D2`)
- `0x457DA` `e8c7ff` near → `linear 0x457A4` （`0x457DA+3+(-57)`）

### `helper_3b1a_5af8` (`0x40C98`)
- `0x40CA7` `e89aff` near → `linear 0x40C44` （`0x40CA7+3+(-102)`）

### `helper_3b1a_5a0e` (`0x40BAE`)
- `0x40BCA` `9a8c0f5510` far → `1055:0F8C` （`1055*16+0F8C`）

### `helper_3b1a_5a38` (`0x40BD8`)
- `0x40BE5` `e8c6ff` near → `linear 0x40BAE` （`0x40BE5+3+(-58)`）

### `helper_3b1a_5bc8` (`0x40D68`)
- `0x40D8B` `e80aff` near → `linear 0x40C98` （`0x40D8B+3+(-246)`）
- `0x40D99` `e812fe` near → `linear 0x40BAE` （`0x40D99+3+(-494)`）

## 8. 与 `0x9212` 相关的限定

- 本次扫描在 `0x22898` 记录到 `A1 12 92` → `mov ax, word ptr [0x9212]`，访问类型 `read`。
- 这证明该地址在被扫描的函数片段中被读取；不能据此判定业务语义、写入来源、运行时值或全程序使用范围。
- 本次扫描范围内未发现对 `0x9212` 的绝对写入；这不等于整个程序没有写入者。

## 9. 限制

- 无 DOSBox-X 动态验证。
- 线性解码不是完整 CFG；首个 RETF 只是启发式边界。
- 快照偏移按等于线性地址处理，DS 基址未核实。
- 间接寻址、寄存器构造地址、未列入口与未覆盖基本块可能含额外引用。
- `0x9212` 业务语义和运行时值未确定。


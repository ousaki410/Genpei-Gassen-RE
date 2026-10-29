# V12 V2 勘误与实现变更

本文件记录 `tools/trace_v12_scoring_v2.py` 相对原版 `tools/trace_v12_scoring.py` 的修正，并列出本次独立核验结果。原版脚本与原版输出未被修改。

- 生成命令：`python3 tools/trace_v12_scoring_v2.py`
- 本地 JSON：`re_out/v12_v2/trace.json`，163854 bytes，SHA-256 `698d27a4ad47e03f343e3e1f99f463c6651322b1410af3fec8d704ce9845f869`（不提交）

## 1. 相对原版扫描器的修正

1. **新增入口 `0x22856`**：V1 的 `FUNCTIONS` 列表没有该入口，因此漏掉 `0x22898: A1 12 92`。V2 将其纳入独立扫描。
2. **全局候选只接受绝对寻址**：要求 `mem.base == 0` 且 `mem.index == 0`。`[BP+disp]`、`[SI+disp]` 等即使位移数值落在 `0x920A..0x9220` 也不计入。
3. **检查显式段覆盖**：`operand.mem.segment` 非零时按实际段（ES/CS/SS/DS）记录；非 DS 的绝对命中放入 `excluded_non_ds_refs`，不并入 DS 全局表。
4. **修正读写判定**：使用 Capstone `operand.access`（`CS_AC_READ` / `CS_AC_WRITE`）。`CMP` 记为 `read`，`TEST` 记为 `unknown`，不再把“内存操作数位于第一位置”当成写入。
5. **记录指令边界证据**：每个入口输出 `gaps`、`undecodable`、`ret_like`、`coverage_complete`，避免把线性扫描当成完整控制流验证。
6. **哈希与大小如实上报**：写入 JSON 和报告；基线不匹配时只告警，不掩盖实测值。
7. **输出隔离**：只写 `re_out/v12_v2/`，不覆盖 `re_out/v12/`。

## 2. 本次核验结果

- **PASS** — `known_read_0x920C_at_22874`：实际 `8b360c92`
- **PASS** — `known_read_0x920E_at_2287D`：实际 `a10e92`
- **PASS** — `known_read_0x9210_at_22888`：实际 `a11092`
- **PASS** — `known_read_0x9212_at_22898`：实际 `a11292`
- **PASS** — `known_read_0x9216_at_228DE`：实际 `a11692`
- **PASS** — `entry_22856_prologue`：实际 `558bec`
- **PASS** — `return_path_22856`：实际 `5e5f8be55dcb`
- **PASS** — `boundary_22856_linear_sweep`：实际 `22856..229AE`
- **PASS** — `7350_call_target_at_3035b`：实际 `e894ff`
- **PASS** — `json_records_9212_reference`：实际 `22898`
- **PASS** — `scanner_self_test_all_pass`：实际 `12/12 pass`

### 2.1 关键结论

- `0x22898` 的实际字节为 `A1 12 92`，反汇编为 `mov ax, word ptr [0x9212]`，访问类型 `read`。
- 新 JSON 的 `global_9212_refs` 包含该条记录（地址 `22898`，函数 `strategic_guard_22856`）。
- `0x3035B` 的实际字节为 `E8 94 FF`；`0x3035B + 3 + (-108) = 0x302F2`，目标是 `0x302F2`，不是 `0x30280`。
- `0x22856` 线性解码覆盖 `0x22856..0x229AE`，唯一 `RETF` 位于 `0x229AD`，无解码空隙。

### 2.2 对 V1 报告的纠正

- `re_out/v12/V12_SCORING_TRACE.md` 把 `0x3035B` 的调用目标写成 `0x30280`，与实际字节不符。
- `re_out/v12/V12_SCORING_ERRATA.md` 进一步把该错误固化为“7350 调用 30280、不调用 302F2”，方向正好相反。
- 正确关系：`7350` 在 `0x3035B` 直接调用 `0x302F2`；`0x302F2` 在 `0x30314` 再调用 `0x30280`。因此可以说“7350 经由 302F2 间接到达 30280”，但不能说“7350 直接调用 30280”。
- V1 的这两处表述不因 V2 而修改：原文件保持原样，本文件仅在 V2 目录中记录正确结论。

## 3. 仍未解决

- 线性扫描不是完整 CFG；函数边界（尤其超过 0x400 字节或含内嵌数据的函数）仍可能判断错误。
- 未覆盖的入口、间接寻址和寄存器构造地址可能包含额外引用。
- 没有动态运行验证。
- `0x9212` 的业务语义、写入者和实际运行时值未确定。


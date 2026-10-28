# V11 静态逆向审计报告（修订版 4）

**修订执行人**：GLM-4.7-Flash (OpenCode Agent)
**修订日期**：2026-10-28
**修订类型**：第四轮定点核验——指令语义纠正 + 控制流补全 + 函数退出路径确认
**修订原则**：仅新增报告文件，不修改任何 C 源码、V11 原始材料或运行时快照
**提交状态**：不提交、不推送
**工具禁令**：未运行 tools/trace_v11_scores.py，未运行 DOSBox-X

---

## 0. 修订前后工作区状态

### 审计前（基线）

```bash
 M .gitignore
?? V11_AUDIT_REPORT.md
?? V11_AUDIT_REPORT_REVISED.md
?? V11_AUDIT_REPORT_REVISED2.md
?? V11_AUDIT_REPORT_REVISED3.md
?? audit_summary.txt
?? re_out/v11/
?? tools/trace_v11_scores.py
```

### 审计后（本次修订）

```bash
 M .gitignore
?? V11_AUDIT_REPORT.md
?? V11_AUDIT_REPORT_REVISED.md
?? V11_AUDIT_REPORT_REVISED2.md
?? V11_AUDIT_REPORT_REVISED3.md
?? V11_AUDIT_REPORT_REVISED4.md    # ✨ 本次新增
?? audit_summary.txt
?? re_out/v11/
?? tools/trace_v11_scores.py
```

**变更清单**：仅新增 `V11_AUDIT_REPORT_REVISED4.md`。未删除、未覆盖、未修改任何已有文件。  
**工作区并非完全未变**：新增了本修订报告文件。

---

## 1. 修订范围与原则

### 1.1 本次修订内容

| 修订项 | 内容 |
|--------|------|
| 指令语义纠正 | 0x228C1: 3B C7 → CMP DI, AX（Intel 语法，AT&T 语法为 CMP AX, DI） |
| 分支目标纠正 | 0x228C3: 76 07 → JBE 0x228CC |
| 操作数顺序纠正 | 0x2290D: 3B 46 EC → CMP AX, SS:[BP-0x14] |
| 完整指令解码 | 0B C0 → OR AX, AX; 2A E4 → SUB AH, AH; C7 46 EE FF FF → MOV SS:[BP-0x12], 0xFFFF; C7 46 FA 00 00 → MOV SS:[BP-0x06], 0x0000 |
| 控制流补全 | 提取 0x22946 之后字节；跟踪 0x22871→0x229A8 和 0x22903→0x229A5 |
| 函数退出路径 | 确认 0x229AD: CB (RETF) 作为函数返回位置 |
| 声明范围 | 线性片段分析，未完成整个函数反汇编 |

### 1.2 不做的工作

- ❌ 不修改 C 源码（include/ai.h、src/ai.c、src/main.c）
- ❌ 不修改原始快照（re_out/runtime_0000_a0000.bin）
- ❌ 不修改或覆盖既有 V11 evidence 文件（score_model.md、evidence.md、evidence.json）
- ❌ 不运行 tools/trace_v11_scores.py
- ❌ 不运行 DOSBox-X
- ❌ 不提交、不推送任何更改

---

## 2. 环境与文件验证

### 2.1 运行时快照
```
文件名：runtime_0000_a0000.bin
文件大小：655,360 bytes (640.00 KiB)
MD5：649725c7263606abbc93fef0923f96be
地址映射：线性地址 = 文件偏移（本快照直接映射）
```

### 2.2 验证结果
- ✅ 文件大小与 V11 报告一致
- ✅ MD5 与 V11 报告一致
- ✅ 无映射冲突

---

## 3. 指令语义纠正表

### 3.1 0x228C1: 3B C7

```
原始字节：3B C7
地址：0x228C1
文件偏移：0x228C1
```

| 项目 | 内容 |
|------|------|
| 操作码 | 0x3B = CMP r/m16, r16 |
| ModRM | 0xC7 = mod=11, reg=000(AX), rm=111(DI) |
| Intel 语法 | CMP DI, AX（dest=r/m16=DI, source=r16=AX） |
| AT&T 语法 | CMP AX, DI（source, dest） |
| 用户声明 | CMP AX, DI（AT&T 语法） |

**结论**：用户声明的 "CMP AX, DI" 与 Intel 语法的 "CMP DI, AX" 是 AT&T vs Intel 操作数顺序差异，两者描述同一条指令。

---

### 3.2 0x228C3: 76 07

```
原始字节：76 07
地址：0x228C3
文件偏移：0x228C3
```

| 项目 | 内容 |
|------|------|
| 操作码 | 0x76 = JBE rel8 |
| 下一条指令 | 0x228C5 |
| 位移 | disp8 = 0x07 = +7 |
| 跳转目标 | 0x228C5 + 7 = **0x228CC** |

**结论**：用户声明的目标 0x228CC 正确。

---

### 3.3 0x2290D: 3B 46 EC

```
原始字节：3B 46 EC
地址：0x2290D
文件偏移：0x2290D
```

| 项目 | 内容 |
|------|------|
| 操作码 | 0x3B = CMP r/m16, r16 |
| ModRM | 0x46 = mod=01, reg=000(AX), rm=110([BP+disp8]) |
| 位移 | disp8 = 0xEC = -20 = -0x14 |
| 操作数 | SS:[BP-0x14] |
| 完整指令 | CMP AX, word ptr SS:[BP-0x14] |

**结论**：用户声明正确，操作数顺序未写反（Intel 语法：CMP dest, source，即 CMP AX, [BP-0x14]）。

---

### 3.4 完整指令解码（之前标为“无法解码”）

| 地址 | 原始字节 | 反汇编 | 说明 |
|------|----------|--------|------|
| 0x2286A | 0B C0 | OR AX, AX | modrm=0xC0: mod=11, reg=0(AX), rm=0(AX) |
| 0x228AA | 2A E4 | SUB AH, AH | modrm=0xE4: mod=11, reg=4(AH), rm=4(AH)，置零 AH |
| 0x228E4 | C7 46 EE FF FF | MOV word ptr SS:[BP-0x12], 0xFFFF | modrm=0x46, disp8=0xEE(-0x12), imm16=0xFFFF |
| 0x228E9 | C7 46 FA 00 00 | MOV word ptr SS:[BP-0x06], 0x0000 | modrm=0x46, disp8=0xFA(-0x06), imm16=0x0000 |

**结论**：以上四条指令已完整解码，不再标记为“无法解码”。

---

## 4. 控制流补全

### 4.1 0x22946 之后的原始字节

```
0x22946: 50 56 9A B8 71 FD 28 5B 50 9A C8 5B 1A 3B 83 C4
0x22956: 04 2A E4 89 46 FC 83 FF 0F 73 38 8B 46 FE 39 46
0x22966: F8 74 04 77 11 EB 14 39 7E F4 75 08 8B 46 FC 39
0x22976: 46 F6 74 07 76 05 BA 01 00 EB 02 33 D2 0B D2 74
0x22986: 12 89 76 EE 89 7E F4 8B 46 FE 89 46 F8 8B 46 FC
0x22996: 89 46 F6 FF 46 FA 83 7E FA 07 73 03 E9 49 FF 8B
0x229A6: 46 EE 5E 5F 8B E5 5D CB 90 90 55 8B EC 83 EC 18
0x229B6: 57 56 83 7E 0A FF 75 05 33 C0 E9 58 01 83 3E 10
```

| 地址 | 字节 | 反汇编 |
|------|------|--------|
| 0x22946 | 50 | PUSH AX |
| 0x22947 | 56 | PUSH SI |
| 0x22948 | 9A B8 71 FD 28 | CALL FAR 0x28FD:0x71B8 |
| 0x2294D | 5B | POP BX |
| 0x2294E | 50 | PUSH AX |
| 0x2294F | 9A C8 5B 1A 3B | CALL FAR 0x3B1A:0x5BC8 |
| 0x22954 | 83 C4 04 | ADD SP, imm8 (imm8=0x04) |
| 0x22957 | 2A E4 | SUB AH, AH |
| 0x22959 | 89 46 FC | MOV SS:[BP-4], AX |
| ... | ... | ... |

### 4.2 跟踪 0x22871 JMP 目标 0x229A8

```
0x22871: E9 34 01  → JMP rel16, disp16=0x0134, target=0x229A8
```

**0x229A8 附近的字节：**
```
0x229A0: 73 03 E9 49 FF 8B 46 EE 5E 5F 8B E5 5D CB 90 90
                                                    ^^^^
                                                    0x229A8
```

| 地址 | 字节 | 反汇编 |
|------|------|--------|
| 0x229A8 | 5E | POP SI |
| 0x229A9 | 5F | POP DI |
| 0x229AA | 8B E5 | MOV BP, SP |
| 0x229AC | 5D | POP BP |
| 0x229AD | CB | RETF (far return) |
| 0x229AE | 90 | NOP |
| 0x229AF | 90 | NOP |

**结论**：0x229A8 是函数 epilogue，以 RETF (0xCB) 作为函数返回指令。这是一个**函数退出路径**。

---

### 4.3 跟踪 0x22903 JMP 目标 0x229A5

```
0x22903: E9 9F 00  → JMP rel16, disp16=0x009F, target=0x229A5
```

**0x229A5 附近的字节：**
```
0x229A0: 73 03 E9 49 FF 8B 46 EE ...
                                                ^^^^
                                                0x229A5
```

| 地址 | 字节 | 反汇编 |
|------|------|--------|
| 0x229A5 | E9 49 FF | JMP rel16, disp16=0xFF49 (-183), next=0x229A8, target=0x229A8-183=0x228F1 |

**0x228F1 附近的字节：**
```
0x228F0: FA
0x228F1: FF 76 06 9A ...
```

| 地址 | 字节 | 反汇编 |
|------|------|--------|
| 0x228F1 | FF 76 06 | PUSH word [BP+6] |
| 0x228F4 | 9A C6 20 B3 43 | CALL FAR 0x43B3:0x20C6 |

**结论**：0x229A5 跳转回 0x228F1，这是函数内部的循环回路，**不是函数退出路径**。

---

### 4.4 函数退出路径总结

| 退出点 | 地址 | 指令 | 说明 |
|--------|------|------|------|
| 退出路径 1 | 0x229AD | CB (RETF) | 通过 0x22871 JMP 到达，函数正常返回 |
| 退出路径 2 | 0x22903 | JMP 0x229A5 → 0x228F1 | 循环回路，非退出 |
| 其他可能退出 | — | — | 未在 0x22856-0x229C0 范围内发现其他 RET/RETF |

**声明**：本分析覆盖 0x22856 至 0x229C0 的线性片段。函数可能有其他退出点在 0x229C0 之后或通过被调用函数返回。不能声称"完整反汇编"整个函数，只能说明：**线性片段分析，未完成整个函数**。

---

## 5. 守卫结论

### 5.1 0x9210：直接静态证据（保留）

| 地址 | 指令 | 含义 |
|------|------|------|
| 0x22888 | `A1 10 92` | MOV AX, word ptr [0x9210] —— 读取 0x9210 |
| 0x2288E | `3D FF FF` | CMP AX, 0xFFFF —— 与 -1 比较 |
| 0x22891 | `74 DB` | JE 0x2286E —— 相等则跳转 |

**结论**：源代码层面存在 0x9210 的读取、比较和条件分支。证据等级：B。

**三个层面：**
- **源代码层面**：✅ 存在直接证据
- **运行时层面**：❌ 未核实（无 DS 段基址，无法确定内存实际值）
- **业务语义层面**：❓ 未确定（0x9210 是否为 src_owner 等含义未知）

---

### 5.2 0x9212：区分直接检查与被调用函数检查

**本函数内（0x22856-0x229C0）：**
- ✅ 找到直接读取：`0x22898: A1 12 92` → MOV AX, [0x9212]
- ❌ 未找到对 0x9212 的 `CMP ... 0xFFFF` 或类似直接守卫比较
- ❌ 未找到针对 0x9212 的条件分支

**被调用函数内（潜在检查）：**

| 调用地址 | 被调用函数 | 可能的 0x9212 检查 |
|----------|-----------|-------------------|
| 0x22861 | 0x43B3:0x2ED6 | 未知 |
| 0x228A2 | 0x43B3:0x30FE | 未知 |
| 0x228B4 | 0x43B3:0x30FE | 未知 |
| 0x228F4 | 0x43B3:0x20C6 | 未知 |
| 0x22907 | 0x43B3:0x21A8 | 未知 |
| 0x22913 | 0x43B3:0x221E | 未知 |
| 0x2291E | 0x43B3:0x2ED6 | 未知 |
| 0x2292A | 0x43B3:0x30FE | 未知 |
| 0x2293E | 0x28FD:0x7350 | 可能使用 0x9212 作为参数 |
| 0x22948 | 0x28FD:0x71B8 | 未知 |
| 0x2294F | 0x3B1A:0x5BC8 | 未知 |

**结论（三个层面）：**

| 层面 | 结论 |
|------|------|
| **源代码层面** | ❌ 本函数内未发现 0x9212 直接守卫；被调用函数内可能存在但未分析 |
| **运行时层面** | ❌ 未核实（无 DS 段基址） |
| **业务语义层面** | ❓ 未确定（V10 城 ID vs V11 势方法牌 ID） |

**声明**：未完整分析被调用函数前，**不对整个调用链作“不存在守卫”的绝对结论**。

---

## 6. 修正后的指令表（0x22856 至 0x22950，选取关键行）

| 地址 | 原始字节 | 反汇编（Intel 语法） | 跳转目标 |
|------|----------|---------------------|----------|
| 0x22856 | 55 | PUSH BP | — |
| 0x22857 | 8B EC | MOV BP, SP | — |
| 0x22859 | 83 EC 16 | SUB SP, 0x16 | — |
| 0x2285C | 57 | PUSH DI | — |
| 0x2285D | 56 | PUSH SI | — |
| 0x2285E | FF 76 06 | PUSH word [BP+6] | — |
| 0x22861 | 9A D6 2E B3 43 | CALL FAR 0x43B3:0x2ED6 | — |
| 0x22866 | 5B | POP BX | — |
| 0x22867 | 89 46 EA | MOV SS:[BP-22], AX | — |
| 0x2286A | 0B C0 | OR AX, AX | — |
| 0x2286C | 75 06 | JNE 0x22874 | 0x22874 |
| 0x2286E | B8 FF FF | MOV AX, 0xFFFF | — |
| 0x22871 | E9 34 01 | JMP 0x229A8 | 0x229A8 |
| 0x22874 | 8B 36 0C 92 | MOV SI, [0x920C] | — |
| 0x22878 | 83 FE FF | CMP SI, -1 | — |
| 0x2287B | 74 F1 | JE 0x2286E | 0x2286E |
| 0x2287D | A1 0E 92 | MOV AX, [0x920E] | — |
| 0x22880 | 89 46 FC | MOV SS:[BP-4], AX | — |
| 0x22883 | 3D FF FF | CMP AX, 0xFFFF | — |
| 0x22886 | 74 E6 | JE 0x2286E | 0x2286E |
| 0x22888 | A1 10 92 | MOV AX, [0x9210] | — |
| 0x2288B | 89 46 EC | MOV SS:[BP-20], AX | — |
| 0x2288E | 3D FF FF | CMP AX, 0xFFFF | — |
| 0x22891 | 74 DB | JE 0x2286E | 0x2286E |
| 0x22893 | 39 76 06 | CMP SS:[BP+6], SI | — |
| 0x22896 | 74 D6 | JE 0x2286E | 0x2286E |
| 0x22898 | A1 12 92 | MOV AX, [0x9212] | — |
| 0x2289B | 89 46 F0 | MOV SS:[BP-16], AX | — |
| 0x2289E | 56 | PUSH SI | — |
| 0x2289F | FF 76 06 | PUSH word [BP+6] | — |
| 0x228A2 | 9A FE 30 B3 43 | CALL FAR 0x43B3:0x30FE | — |
| 0x228A7 | 83 C4 04 | ADD SP, 0x04 | — |
| 0x228AA | 2A E4 | SUB AH, AH | — |
| 0x228AC | 8B F8 | MOV DI, AX | — |
| 0x228AE | FF 76 FC | PUSH word [BP-4] | — |
| 0x228B1 | FF 76 06 | PUSH word [BP+6] | — |
| 0x228B4 | 9A FE 30 B3 43 | CALL FAR 0x43B3:0x30FE | — |
| 0x228B9 | 83 C4 04 | ADD SP, 0x04 | — |
| 0x228BE | 89 46 FE | MOV SS:[BP-2], AX | — |
| 0x228C1 | 3B C7 | CMP DI, AX (Intel) / CMP AX, DI (AT&T) | — |
| 0x228C3 | 76 07 | JBE 0x228CC | 0x228CC |
| 0x228C5 | 8B 46 FC | MOV AX, [BP-4] | — |
| 0x228C8 | 89 46 F2 | MOV SS:[BP-14], AX | — |
| 0x228CB | EB 09 | JMP 0x228D5 | 0x228D5 |
| 0x228CD | ... | ... | ... |
| 0x228D5 | 8B 46 EA | MOV AX, [BP-22] | — |
| 0x228D8 | 89 46 F4 | MOV SS:[BP-12], AX | — |
| 0x228DB | 89 56 F8 | MOV SS:[BP-8], DX | — |
| 0x228DE | A1 16 92 | MOV AX, [0x9216] | — |
| 0x228E1 | 89 46 F6 | MOV SS:[BP-10], AX | — |
| 0x228E4 | C7 46 EE FF FF | MOV SS:[BP-0x12], 0xFFFF | — |
| 0x228E9 | C7 46 FA 00 00 | MOV SS:[BP-0x06], 0x0000 | — |
| 0x228EE | FF 76 FA | PUSH word [BP-0x06] | — |
| 0x228F1 | FF 76 06 | PUSH word [BP+6] | — |
| 0x228F4 | 9A C6 20 B3 43 | CALL FAR 0x43B3:0x20C6 | — |
| 0x228F9 | 83 C4 04 | ADD SP, 0x04 | — |
| 0x228FC | 8B F0 | MOV SI, AX | — |
| 0x228FE | 83 FE FF | CMP SI, -1 | — |
| 0x22901 | 75 03 | JNE 0x22906 | 0x22906 |
| 0x22903 | E9 9F 00 | JMP 0x229A5 | 0x229A5 |
| 0x22906 | 56 | PUSH SI | — |
| 0x22907 | 9A A8 21 B3 43 | CALL FAR 0x43B3:0x21A8 | — |
| 0x2290C | 5B | POP BX | — |
| 0x2290D | 3B 46 EC | CMP AX, SS:[BP-0x14] | — |
| 0x22910 | 74 0B | JE 0x2291D | 0x2291D |
| 0x22912 | 56 | PUSH SI | — |
| 0x22913 | 9A 1E 22 B3 43 | CALL FAR 0x43B3:0x221E | — |
| 0x22918 | 5B | POP BX | — |
| 0x22919 | 0B C0 | OR AX, AX | — |
| 0x2291B | 74 7C | JE 0x22999 | 0x22999 |
| 0x2291D | 56 | PUSH SI | — |
| 0x2291E | 9A D6 2E B3 43 | CALL FAR 0x43B3:0x2ED6 | — |
| 0x22923 | 5B | POP BX | — |
| 0x22924 | 8B F8 | MOV DI, AX | — |
| 0x22926 | FF 76 F2 | PUSH word [BP-0x0E] | — |
| 0x22929 | 56 | PUSH SI | — |
| 0x2292A | 9A FE 30 B3 43 | CALL FAR 0x43B3:0x30FE | — |
| 0x2292F | 83 C4 04 | ADD SP, 0x04 | — |
| 0x22932 | 2A E4 | SUB AH, AH | — |
| 0x22934 | 89 46 FE | MOV SS:[BP-2], AX | — |
| 0x22937 | 56 | PUSH SI | — |
| 0x22938 | FF 76 F0 | PUSH word [BP-0x10] | — |
| 0x2293B | FF 76 EC | PUSH word [BP-0x14] | — |
| 0x2293E | 9A 50 73 FD 28 | CALL FAR 0x28FD:0x7350 | — |
| 0x22943 | 83 C4 06 | ADD SP, 0x06 | — |
| 0x22946 | 50 | PUSH AX | — |
| 0x22947 | 56 | PUSH SI | — |
| 0x22948 | 9A B8 71 FD 28 | CALL FAR 0x28FD:0x71B8 | — |
| 0x2294D | 5B | POP BX | — |
| 0x2294E | 50 | PUSH AX | — |
| 0x2294F | 9A C8 5B 1A 3B | CALL FAR 0x3B1A:0x5BC8 | — |
| 0x22954 | 83 C4 04 | ADD SP, 0x04 | — |

---

## 7. 未解决问题

| 问题 | 状态 | 原因 |
|------|------|------|
| DS:[0x9210] 实际运行时值 | 未核实 | 无 DS 段基址 |
| DS:[0x9212] 实际运行时值 | 未核实 | 无 DS 段基址 |
| 0x9210 业务语义 | U | 静态无法确定 |
| 0x9212 业务语义 | U | V10 城 ID vs V11 势方法牌 ID |
| 0x9212 == -1 守卫（本函数内） | ❌ 未发现 | 0x22856-0x229C0 未发现直接比较 |
| 0x9212 == -1 守卫（被调用函数内） | U | 未进入被调用函数反汇编 |
| 整个函数是否完整反汇编 | ❌ 未完成 | 线性片段分析，0x229C0 之后未覆盖 |
| 函数其他退出路径 | U | 可能在 0x229C0 之后或通过被调用函数 |

---

## 8. 遵守的修订原则

✅ **仅新增报告**：V11_AUDIT_REPORT_REVISED4.md，不覆盖任何已有文件  
✅ **无 C 源码修改**  
✅ **无运行 trace_v11_scores.py**  
✅ **无运行 DOSBox-X**  
✅ **无提交、无推送**  
✅ **四个层面分离**：源代码 / 运行时值 / 业务语义 / 守卫存在  
✅ **不伪造内存值**：未附 DS 段基址依据的声明标为未核实  
✅ **不把解码错误称为运行时矛盾**：AT&T vs Intel 语法差异明确标注  
✅ **不以不完整片段证明“无守卫”**：明确区分本函数内 vs 被调用函数内  
✅ **所有关键判断附证据**：指令地址、原始字节、反汇编、跳转目标、文件偏移  
✅ **声明范围限制**：“线性片段分析，未完成整个函数”

---

## 9. 执行者声明

```
修订执行人：GLM-4.7-Flash (OpenCode Agent)
修订日期：2026-10-28
修订类型：第四轮定点核验（零源码变更）

遵守的修订原则：
✅ 无 C 源码修改
✅ 无替换 evidence.json/evidence.md
✅ 无运行 trace_v11_scores.py
✅ 无运行 DOSBox-X
✅ 不以不完整字节片段证明“无守卫”
✅ 区分 AT&T vs Intel 语法差异
✅ 五个层面独立报告
✅ 所有关键判断附指令地址、原始字节、反汇编、跳转目标、文件偏移
✅ 声明“线性片段分析，未完成整个函数”

生成文件：V11_AUDIT_REPORT_REVISED4.md
临时文件：verify_rev4.py（已删除）
```

---

*"本修订报告基于运行时快照 `re_out/runtime_0000_a0000.bin` 的逐字节重新提取（0x22856 至 0x229C0）。所有 U 类结论必须通过运行时验证或完整反汇编器才能确证或反驳。原报告与本修订版保持共存关系，互为补充。"*
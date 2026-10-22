# 《源平合战》V7 运行时 VM 转储与 256 Opcode 表报告

> 日期：2026-10-22  
> 对象：`games/yphz/Main.exe`（MD5 `7e7e1b875aa3b07eeb28c2eb5950aac1`）  
> 运行环境：`/home/ou/build/dosbox-x`，`--enable-debug=heavy --enable-sdl2`  
> 调试器状态：V6 的断点 **`38CD:1B79`** 仍处于命中/暂停状态；V7 只做内存转储和
> 离线反汇编，没有继续执行，也没有再发送鼠标输入。

---

## 0. 一页结论

| 项目 | 结果 |
|---|---|
| 运行时镜像 | ✅ 已保存 `1055:0000` 起 `0x60000` 字节；另存完整 640 KB 低端内存 |
| 主表 | ✅ `38CD:0000` 的 256 个 `uint16` 入口已提取 |
| 唯一 Handler | ✅ **138 / 256** |
| 主表直接原生调用 | ✅ **0 个**；主表 Handler 通过 `jmp`/`ret` 结束，不含 `call` |
| 0xE6 扩展表 | ✅ `38CD:022C`，256 项，117 个唯一 Handler |
| 0xDD 返回分派 | ✅ `38CD:08BA` → `[BP-4]=0x1C` → `38CD:097E` → `retf` |
| 命中点语义 | ✅ `38CD:1B79` 是 VM 分派尾，不是 0xDD Handler 的起点 |
| `DS:SI` | ✅ `1055:2A26`；`1055:2A25` 的字节是 `0xDD` |
| 原生调用桥 | ✅ `E6 4C/50/54` → `lcall [BP-0x10]`；`E6 05/3B/3C/40/9D` → `1055:xxxx` helper |
| 原生返回点 | ✅ VM 的 `retf` 返回到 `3B1A:2195` |
| 4000 段 Getter/Setter | ⚠️ 通用间接调用机制已确认；**具体到 4000 段的函数映射仍未确认** |

**一句话：V7 把 `38CD` 从“一个命中地址”变成了可复现的 256 项线程化字节码
分派表；`0xDD` 在命中处是 VM 的返回/退出路径，`0xE6` 才是通向原生 helper
和通用 `lcall` 的扩展前缀。**

---

## 1. 运行时镜像归档

### 1.1 文件

| 文件 | 大小 | MD5 | 覆盖范围 |
|---|---:|---|---|
| `re_out/runtime_1055_60000.bin` | `393216` | `c7728266e406e98e6f24e4bf8f6388a7` | `1055:0000` 起 `0x60000` 字节 |
| `re_out/runtime_0000_a0000.bin` | `655360` | `649725c7263606abbc93fef0923f96be` | 物理/线性 `0000:0000` 起 640 KB |

两个文件都在 `.gitignore` 的 `re_out/` 规则下，不进入提交；报告只记录路径、
大小和 MD5，便于本地复现。

生成命令（在仍暂停于 `38CD:1B79` 的 DOSBox-X 调试器中）：

```text
MEMDUMPBIN 1055:0000 60000 runtime_1055_60000.bin
MEMDUMPBIN 0000:0000 A0000 runtime_0000_a0000.bin
```

随后把 DOSBox-X 在当前目录生成的 `RUNTIME_*.BIN` 移到 `re_out/` 并改为
小写文件名。`a0000` 是长度，不是段地址；它表示从 `0000:0000` 起转储
`0xA0000 = 655360` 字节。

### 1.2 表偏移复核

`1055:0000` 的转储中，`38CD:0000` 的相对偏移是：

```text
(0x38CD - 0x1055) << 4
= 0x2878 << 4
= 0x28780
```

完整 640 KB 转储中则是 `0x38CD0`。两份转储在这 512 字节上逐字节一致；
用 `--base-seg 0 --vm-seg 0x38CD` 对完整 640 KB 镜像重跑工具，得到的
256 项表和 138 个唯一 Handler 也完全一致。前 16 个入口都是：

```text
045D 055A 055A 055A 055A 055A 055A 0F48
0F48 0F3A 0F3A 0F3A 0F3A 0F3A 0F3A 0F3A
```

`MEMDUMPBIN` 的 `0x60000` 长度覆盖 `1055:0000` 到 `1055:FFFF` 以外的
后续段；本报告只使用其中可验证的 `38CD` 表、`38CD` Handler 和 `1055`
原生 helper。没有把 dump 中所有字节都当成代码。

---

## 2. 离线分析工具

新增：

```text
tools/dump_vm_opcodes.py
```

依赖 `capstone`（本机 `5.0.7`，16 位模式），只读：

```bash
python3 tools/dump_vm_opcodes.py \
  --check-md5 c7728266e406e98e6f24e4bf8f6388a7
```

默认读取 `re_out/runtime_1055_60000.bin`，输出到 `re_out/vm/`：

```text
re_out/vm/vm_opcodes.json   # 完整表、Handler、指令、二级表
re_out/vm/vm_opcodes.csv    # 256 行扁平表
re_out/vm/vm_opcodes.md     # 人类可读报告（含全部 256 行）
```

工具行为：

1. 用 `vm_seg*16 - base_seg*16` 定位主表，不硬编码文件偏移；
2. 读取 256 个小端 `uint16`，按 Handler 地址去重；
3. 从每个唯一入口反汇编到 `ret/retf/iret/jmp/int3`，或到下一个 VM 分派尾
   （`lodsb; mov di,ax; shl di,1; jmp [cs:di]` 等）为止；
4. 标记 ALU / STACK / CTRL / MEM / STR / TRAP / CALL；
5. 额外解析已由 Handler 字节确认的二级表：
   - `022C`：`E6` 的 256 项扩展表；
   - `04B0`：`DD` 的返回分派子表；本次按 `0x00..0x1F` 读取，命中索引
     `0x1C`，完整上界未证明；
   - `0470` / `0490`：各 16 项对象/操作分派表（由 `and bx,0x1e` 限定）；
   - `04FA`：原生调用返回后的 16 项工作切片，完整上界未证明。

**分类是结构分类，不是业务语义。** 例如 `mov` 被标成 `MEM` 只表示操作数
含内存，不代表它一定是“城池字段读取”。

---

## 3. 命中点 `38CD:1B79` 的真实语义

### 3.1 命中快照中与本节有关的寄存器

```text
CS=38CD  EIP=00001B79
DS=1055  ES=619F  SS=619F
EAX=000000DD  EBX=00002A28  ESI=00002A26
EDI=000001BA  EBP=0000A486  ESP=0000A46E
```

### 3.2 `38CD:1B79` 是分派尾，不是 0xDD Handler

`38CD:1B70` 附近的字节：

```text
38CD:1B70  62 01 03 F7 AC 8B F8 D1 E7 2E FF 25 81 EF 69 01
38CD:1B80  03 F7 33 C0 AC 8B F8 D1 E7 2E FF 25 AC 2B F0 AC
```

从 `1B74` 看是：

```asm
1B74: AC              lodsb al, byte ptr [si]
1B75: 8B F8           mov di, ax
1B77: D1 E7           shl di, 1
1B79: 2E FF 25        jmp word ptr cs:[di]
```

命中时 `DI=0x1BA = 0xDD * 2`，`[38CD:01BA] = 08BA`。所以
`1B79` 只是**读完字节码 `0xDD` 后执行主表分派的那条 `jmp`**；真正的
0xDD Handler 是 `38CD:08BA`。

`1055:2A20` 的字节：

```text
1055:2A20  56 61 C3 7E 0D DD AA 08 08 00 E8 0B 53 42 72 2A
```

`1055:2A25 = DD`，`lodsb` 后 `SI=2A26`，与命中快照一致。

### 3.3 0xDD Handler 与返回分派

`38CD:08BA` 的原始字节和反汇编：

```text
8A 07             mov al, byte ptr [bx]
33 DB             xor bx, bx
8A 5E FC          mov bl, byte ptr [bp - 4]
2E FF A7 B0 04    jmp word ptr cs:[bx + 0x4B0]
```

命中时：

| 项目 | 值 |
|---|---:|
| `BX`（进入 Handler 时） | `2A28` |
| `[DS:BX] = [1055:2A28]` | `0x08` |
| `[SS:BP-4] = [619F:A482]` | `0x1C` |
| `38CD:04B0 + 0x1C*2 = 38CD:04E8` | `097E` |

因此这条路径是：

```text
38CD:1B79  jmp [cs:di]            ; DI=0x1BA -> 08BA
38CD:08BA  mov al,[bx] ...        ; 读一个字节
38CD:08BA  jmp [cs:bx+0x4B0]      ; BX=0x1C -> 04E8 -> 097E
38CD:097E  ... retf
```

`38CD:097E` 的关键尾部：

```asm
097E: 8B C8              mov cx, ax
0980: 83 E9 04           sub cx, 4
0983: 8B F1              mov si, cx
0985: 8B 46 04           mov ax, word ptr [bp + 4]
0988: 89 42 04           mov word ptr [bp + si + 4], ax
098B: 8B 46 02           mov ax, word ptr [bp + 2]
098E: 89 42 02           mov word ptr [bp + si + 2], ax
0991: 8B 76 EE           mov si, word ptr [bp - 0x12]
0994: 8B 7E EC           mov di, word ptr [bp - 0x14]
0997: 8E 5E FE           mov ds, word ptr [bp - 2]
099A: 58                 pop ax
099B: 5A                 pop dx
099C: 8B E5              mov sp, bp
099E: 5D                 pop bp
099F: 4D                 dec bp
09A0: 03 E1              add sp, cx
09A2: CB                 retf
```

命中帧里：

```text
[SS:BP+2] = [619F:A488] = 2195
[SS:BP+4] = [619F:A48A] = 3B1A
```

`retf` 因此返回到 **`3B1A:2195`**。该处是有效的原生代码：

```text
3B1A:2195  89 76 FE       mov word ptr [bp - 2], si
3B1A:2198  0B F6          or si, si
...
```

**结论：结构上 `0xDD` 仍是二级分派器（`08BA` → `04B0`），但在本次命中的
路径上，它选中的是返回/退出项；它不是
“调用 4000 段 Getter/Setter”的调用指令。** `0xDD` 的其它子选择器
（`04B0` 表中的 `08C6/08DA/08F0/0908/092C/0952`）会先恢复 VM 帧再继续
主分派；`0B66` 是 `int3` 陷阱项。V6 把 `38CD:1B79` 当成目标位置是对的，
但把它直接当成 0xDD Handler 的起点是不准确的。

---

## 4. 256 项主表

### 4.1 结构

`38CD:0000` 是 256 个小端字，`entry[op]` 是 `38CD` 段内 Handler 偏移。
主分派序列（多个入口共享）是：

```asm
lodsb al, byte ptr [si]
mov di, ax
shl di, 1
jmp word ptr cs:[di]
```

`opcode 0x00` 的 Handler 就是这条分派序列本身（`38CD:045D`）；很多
opcode 共享同一个 Handler。统计：

| 指标 | 值 |
|---|---:|
| 表项 | 256 |
| 唯一 Handler | 138 |
| 主表 Handler 中的 `call`/`lcall` | 0（按每个唯一入口的线性反汇编范围） |
| 主表 Handler 的停止方式 | 分派尾 / `ret` / `retf` / `jmp` / `int3` |

在这个线性反汇编范围内，主表没有直接原生调用；说明主 opcode 主要是 VM
内部算术、栈、内存、分支和分派，真正的原生桥在扩展前缀里（见 §5）。

### 4.2 分类统计

| 类别 | Handler 数 |
|---|---:|
| ALU | 113 |
| CTRL | 44 |
| MEM | 102 |
| OTHER | 45 |
| STACK | 107 |
| STR | 66 |
| TRAP | 2 |

| 停止方式 | Handler 数 |
|---|---:|
| 下一字节是 VM 分派尾（未计入 Handler） | 121 |
| ret/retf/iret/jmp/int3 等终止指令 | 17 |

主表含 `lcall`/`call` 的唯一 Handler（线性反汇编范围）：`0` 个。

### 4.3 16×16 主表矩阵

行是低半字节，列是高半字节；每个单元是该 opcode 的 Handler 偏移。

| low\high | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0_ | `045D` | `0F3A` | `1072` | `0F88` | `1284` | `139C` | `0CA0` | `1782` | `16D6` | `05BE` | `133A` | `1B60` | `1B16` | `1BE0` | `0D02` | `19C6` |
| 1_ | `055A` | `0F3A` | `1072` | `0F88` | `1284` | `139C` | `0CA0` | `1782` | `1600` | `0578` | `133A` | `1B5E` | `1B16` | `1BCA` | `1AD4` | `0F98` |
| 2_ | `055A` | `0F3A` | `1072` | `0F88` | `1284` | `139C` | `0CA0` | `1782` | `16AC` | `05BA` | `0CCC` | `1B6E` | `1B26` | `17AA` | `1A96` | `0FF5` |
| 3_ | `055A` | `0F3A` | `0F73` | `0F88` | `1284` | `139C` | `0CA0` | `1782` | `1640` | `0F26` | `0EC6` | `1B6E` | `1B36` | `18E0` | `1A06` | `124A` |
| 4_ | `055A` | `0F3A` | `0F5E` | `10BC` | `1284` | `139C` | `0CA0` | `1782` | `1702` | `0F22` | `1654` | `1B6E` | `1B36` | `1900` | `1A18` | `1C18` |
| 5_ | `055A` | `0F3A` | `0F6E` | `10D6` | `1284` | `15E8` | `0CA0` | `1796` | `1696` | `10EA` | `166E` | `1B7C` | `1B4A` | `1940` | `19D4` | `1C18` |
| 6_ | `055A` | `0F3A` | `0F88` | `10D6` | `105E` | `15EA` | `0CAE` | `1796` | `1696` | `176C` | `166E` | `1B7C` | `1B4A` | `1950` | `0466` | `1C18` |
| 7_ | `0F48` | `0F3A` | `0F88` | `10D6` | `1DD0` | `15EA` | `0CAE` | `0D65` | `1696` | `10F2` | `1658` | `1B7C` | `1B4A` | `197C` | `0200` | `0FBA` |
| 8_ | `0F48` | `105B` | `0F88` | `10D6` | `1C4C` | `15EA` | `0CAE` | `0CE2` | `1712` | `13F8` | `1658` | `1AEA` | `1BF2` | `17DA` | `11E4` | `102F` |
| 9_ | `0F3A` | `1072` | `0F88` | `10D6` | `0C8A` | `15EA` | `0CAE` | `1B8C` | `1C2C` | `105F` | `0E86` | `1AFA` | `1C04` | `17EA` | `129A` | `11CA` |
| A_ | `0F3A` | `1072` | `0F88` | `10D6` | `0ECF` | `15EA` | `0CAE` | `17CC` | `0DA2` | `0FF0` | `14DC` | `1B06` | `1C04` | `0CD8` | `0E9E` | `11AC` |
| B_ | `0F3A` | `1072` | `0F88` | `10D6` | `0EC9` | `11D2` | `0CAE` | `17C8` | `05BE` | `12CE` | `14F6` | `1B06` | `06B8` | `18A4` | `17B8` | `1300` |
| C_ | `0F3A` | `1072` | `0F88` | `10D6` | `0ECC` | `11D2` | `1770` | `0D0E` | `05BE` | `133A` | `1684` | `1B06` | `0F13` | `1DE8` | `1DFC` | `11BC` |
| D_ | `0F3A` | `1072` | `0F88` | `10D6` | `1026` | `11D2` | `1782` | `0D1E` | `05BE` | `133A` | `1580` | `1B06` | `0FEA` | `08BA` | `1DD3` | `11BC` |
| E_ | `0F3A` | `1072` | `0F88` | `1218` | `0FE4` | `0C96` | `1782` | `1C3E` | `05BE` | `133A` | `15A0` | `1B16` | `1B9E` | `0DE2` | `1E22` | `11BC` |
| F_ | `0F3A` | `1072` | `0F88` | `1284` | `136E` | `11D2` | `1782` | `1C5C` | `05BE` | `133A` | `15C2` | `1B16` | `1BB4` | `1874` | `0EAC` | `11BC` |

### 4.4 全部 256 个 Opcode

| op | handler | 独占 | 停止于 | 类别 | 调用 | 首指令 |
|---:|---:|:---:|---|---|---|---|
| `00` | `045D` | 是 | `dispatch main [cs:di] @0462` | ALU,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `01` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `02` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `03` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `04` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `05` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `06` | `055A` | 否(6) | `dispatch group [cs:bx+0x470] @0572` | ALU,MEM,OTHER | - | `xlatb ` |
| `07` | `0F48` | 否(2) | `dispatch main [cs:di] @0F56` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `08` | `0F48` | 否(2) | `dispatch main [cs:di] @0F56` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `09` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0A` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0B` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0C` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0D` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0E` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `0F` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `10` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `11` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `12` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `13` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `14` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `15` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `16` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `17` | `0F3A` | 否(15) | `dispatch main [cs:di] @0F3F` | ALU,MEM,STACK | - | `sub di, 0x44` |
| `18` | `105B` | 是 | `jmp 0x1062` | CTRL,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `19` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1A` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1B` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1C` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1D` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1E` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `1F` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `20` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `21` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `22` | `1072` | 否(10) | `dispatch main [cs:di] @1078` | ALU,STACK | - | `sub di, 0x5a` |
| `23` | `0F73` | 是 | `dispatch main [cs:di] @0F7F` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `24` | `0F5E` | 是 | `jmp 0xf76` | CTRL,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `25` | `0F6E` | 是 | `dispatch main [cs:di] @0F7F` | ALU,MEM,OTHER,STACK,STR | - | `mov di, sp` |
| `26` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `27` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `28` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `29` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2A` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2B` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2C` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2D` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2E` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `2F` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `30` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `31` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `32` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `33` | `0F88` | 否(14) | `dispatch main [cs:di] @0F90` | ALU,MEM,STACK | - | `sub di, 0x7c` |
| `34` | `10BC` | 是 | `dispatch main [cs:di] @10CD` | ALU,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `35` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `36` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `37` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `38` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `39` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `3A` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `3B` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `3C` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `3D` | `10D6` | 否(9) | `dispatch main [cs:di] @10E2` | ALU,MEM | - | `sub di, 0x90` |
| `3E` | `1218` | 是 | `dispatch main [cs:di] @122D` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `3F` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `40` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `41` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `42` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `43` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `44` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `45` | `1284` | 否(7) | `dispatch main [cs:di] @1292` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `46` | `105E` | 是 | `dispatch main [cs:di] @1069` | ALU,MEM,STACK,STR | - | `push ss` |
| `47` | `1DD0` | 是 | `dispatch main [cs:di] @1DDF` | ALU,MEM,STACK | - | `xor al, al` |
| `48` | `1C4C` | 是 | `dispatch main [cs:di] @1C54` | ALU,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `49` | `0C8A` | 是 | `dispatch main [cs:di] @0C8E` | ALU,MEM,STACK,STR | - | `lodsw ax, word ptr [si]` |
| `4A` | `0ECF` | 是 | `dispatch main [cs:di] @0EF3` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `xor cx, cx` |
| `4B` | `0EC9` | 是 | `dispatch main [cs:di] @0EF3` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `add ah, 1` |
| `4C` | `0ECC` | 是 | `dispatch main [cs:di] @0EF3` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `add ah, 1` |
| `4D` | `1026` | 是 | `dispatch main [cs:di] @103D` | ALU,MEM,OTHER,STACK,STR | - | `mov di, sp` |
| `4E` | `0FE4` | 是 | `jmp 0xff5` | ALU,CTRL,STACK | - | `xor cx, cx` |
| `4F` | `136E` | 是 | `dispatch main [cs:di] @1380` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `50` | `139C` | 否(5) | `dispatch main [cs:di] @13A8` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `51` | `139C` | 否(5) | `dispatch main [cs:di] @13A8` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `52` | `139C` | 否(5) | `dispatch main [cs:di] @13A8` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `53` | `139C` | 否(5) | `dispatch main [cs:di] @13A8` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `54` | `139C` | 否(5) | `dispatch main [cs:di] @13A8` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `55` | `15E8` | 是 | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `56` | `15EA` | 否(5) | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK | - | `sub al, 0x56` |
| `57` | `15EA` | 否(5) | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK | - | `sub al, 0x56` |
| `58` | `15EA` | 否(5) | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK | - | `sub al, 0x56` |
| `59` | `15EA` | 否(5) | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK | - | `sub al, 0x56` |
| `5A` | `15EA` | 否(5) | `dispatch main [cs:di] @15F7` | ALU,MEM,OTHER,STACK | - | `sub al, 0x56` |
| `5B` | `11D2` | 否(4) | `dispatch main [cs:di] @11DB` | ALU,MEM,STACK | - | `sub di, 0xb0` |
| `5C` | `11D2` | 否(4) | `dispatch main [cs:di] @11DB` | ALU,MEM,STACK | - | `sub di, 0xb0` |
| `5D` | `11D2` | 否(4) | `dispatch main [cs:di] @11DB` | ALU,MEM,STACK | - | `sub di, 0xb0` |
| `5E` | `0C96` | 是 | `dispatch main [cs:di] @0C98` | MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `5F` | `11D2` | 否(4) | `dispatch main [cs:di] @11DB` | ALU,MEM,STACK | - | `sub di, 0xb0` |
| `60` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `61` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `62` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `63` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `64` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `65` | `0CA0` | 否(6) | `dispatch main [cs:di] @0CA5` | ALU,STACK | - | `sub di, 0xc0` |
| `66` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `67` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `68` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `69` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `6A` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `6B` | `0CAE` | 否(6) | `dispatch main [cs:di] @0CB3` | ALU,STACK | - | `sub di, 0xcd` |
| `6C` | `1770` | 是 | `dispatch main [cs:di] @177A` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `6D` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `6E` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `6F` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `70` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `71` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `72` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `73` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `74` | `1782` | 否(8) | `dispatch main [cs:di] @178D` | ALU,MEM,STACK | - | `sub di, 0xd8` |
| `75` | `1796` | 否(2) | `dispatch main [cs:di] @17A1` | ALU,MEM,STACK | - | `sub di, 0xe9` |
| `76` | `1796` | 否(2) | `dispatch main [cs:di] @17A1` | ALU,MEM,STACK | - | `sub di, 0xe9` |
| `77` | `0D65` | 是 | `dispatch main [cs:di] @0D96` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `xor ch, ch` |
| `78` | `0CE2` | 是 | `dispatch main [cs:di] @0CE7` | MEM,OTHER,STACK | - | `mov di, sp` |
| `79` | `1B8C` | 是 | `dispatch main [cs:di] @1B8F` | ALU,MEM,STR | - | `lodsb al, byte ptr [si]` |
| `7A` | `17CC` | 是 | `dispatch main [cs:di] @17D1` | ALU,STACK | - | `pop cx` |
| `7B` | `17C8` | 是 | `dispatch main [cs:di] @17D1` | ALU,STACK | - | `pop cx` |
| `7C` | `0D0E` | 是 | `dispatch main [cs:di] @0D16` | MEM,STACK | - | `pop di` |
| `7D` | `0D1E` | 是 | `dispatch main [cs:di] @0D26` | MEM,STACK | - | `pop di` |
| `7E` | `1C3E` | 是 | `dispatch main [cs:di] @1C44` | ALU,MEM,STR | - | `lodsb al, byte ptr [si]` |
| `7F` | `1C5C` | 是 | `dispatch main [cs:di] @1C65` | ALU,CTRL,MEM,OTHER | - | `xor cx, cx` |
| `80` | `16D6` | 是 | `dispatch main [cs:di] @16E1` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `81` | `1600` | 是 | `dispatch main [cs:di] @1609` | ALU,MEM,STACK | - | `pop cx` |
| `82` | `16AC` | 是 | `dispatch main [cs:di] @16B6` | ALU,MEM,STACK | - | `pop cx` |
| `83` | `1640` | 是 | `dispatch main [cs:di] @164C` | ALU,MEM,STACK | - | `pop ax` |
| `84` | `1702` | 是 | `dispatch main [cs:di] @170A` | ALU,MEM,STACK | - | `pop cx` |
| `85` | `1696` | 否(3) | `dispatch main [cs:di] @16A3` | ALU,MEM,STACK | - | `sub al, 0x85` |
| `86` | `1696` | 否(3) | `dispatch main [cs:di] @16A3` | ALU,MEM,STACK | - | `sub al, 0x85` |
| `87` | `1696` | 否(3) | `dispatch main [cs:di] @16A3` | ALU,MEM,STACK | - | `sub al, 0x85` |
| `88` | `1712` | 是 | `dispatch main [cs:di] @1717` | MEM,STACK | - | `pop di` |
| `89` | `1C2C` | 是 | `dispatch main [cs:di] @1C36` | ALU,MEM,OTHER,STR | - | `mov ah, 1` |
| `8A` | `0DA2` | 是 | `dispatch main [cs:di] @0DD2` | ALU,MEM,OTHER,STACK,STR | - | `mov di, ss` |
| `8B` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `8C` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `8D` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `8E` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `8F` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `90` | `05BE` | 否(6) | `dispatch group [cs:bx+0x470] @05F8` | ALU,CTRL,MEM,OTHER | - | `sub ax, 0x8a` |
| `91` | `0578` | 是 | `dispatch group [cs:bx+0x470] @0590` | ALU,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `92` | `05BA` | 是 | `jmp 0x5c2` | CTRL,MEM,STR | - | `lodsb al, byte ptr [si]` |
| `93` | `0F26` | 是 | `dispatch main [cs:di] @0F31` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `94` | `0F22` | 是 | `jmp 0xf29` | CTRL,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `95` | `10EA` | 是 | `jmp 0x1108` | CTRL,MEM,OTHER,STR | - | `lodsw ax, word ptr [si]` |
| `96` | `176C` | 是 | `jmp 0x1771` | CTRL,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `97` | `10F2` | 是 | `jmp 0x1108` | ALU,CTRL,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `98` | `13F8` | 是 | `jmp 0x1421` | ALU,CTRL,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `99` | `105F` | 是 | `dispatch main [cs:di] @1069` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `9A` | `0FF0` | 是 | `dispatch main [cs:di] @0FFF` | ALU,MEM,OTHER,STACK,STR | - | `pop cx` |
| `9B` | `12CE` | 是 | `dispatch main [cs:di] @12E3` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `9C` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `9D` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `9E` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `9F` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `A0` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `A1` | `133A` | 否(6) | `dispatch main [cs:di] @1349` | ALU,MEM,OTHER,STACK | - | `mov di, ax` |
| `A2` | `0CCC` | 是 | `dispatch main [cs:di] @0CD0` | ALU,STACK | - | `xor ax, ax` |
| `A3` | `0EC6` | 是 | `dispatch main [cs:di] @0EF3` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `add ah, 1` |
| `A4` | `1654` | 是 | `jmp 0x165e` | CTRL,MEM,STR | - | `lodsb al, byte ptr [si]` |
| `A5` | `166E` | 否(2) | `dispatch main [cs:di] @167B` | ALU,MEM,OTHER,STACK | - | `sub di, 0x149` |
| `A6` | `166E` | 否(2) | `dispatch main [cs:di] @167B` | ALU,MEM,OTHER,STACK | - | `sub di, 0x149` |
| `A7` | `1658` | 否(2) | `dispatch main [cs:di] @1665` | ALU,MEM,OTHER,STACK | - | `sub di, 0x14e` |
| `A8` | `1658` | 否(2) | `dispatch main [cs:di] @1665` | ALU,MEM,OTHER,STACK | - | `sub di, 0x14e` |
| `A9` | `0E86` | 是 | `dispatch main [cs:di] @0E87` | STACK | - | `push ss` |
| `AA` | `14DC` | 是 | `dispatch main [cs:di] @14DF` | MEM,STACK | - | `push word ptr [bp - 0xc]` |
| `AB` | `14F6` | 是 | `dispatch main [cs:di] @14F9` | MEM,STACK | - | `pop word ptr [bp - 0xc]` |
| `AC` | `1684` | 是 | `dispatch main [cs:di] @168D` | MEM,STACK | - | `pop di` |
| `AD` | `1580` | 是 | `dispatch main [cs:di] @1586` | MEM,STACK | - | `mov di, word ptr [bp - 0xc]` |
| `AE` | `15A0` | 是 | `dispatch main [cs:di] @15A7` | MEM,STACK | - | `mov di, word ptr [bp - 0xc]` |
| `AF` | `15C2` | 是 | `dispatch main [cs:di] @15CB` | ALU,MEM,STACK | - | `mov di, word ptr [bp - 0xc]` |
| `B0` | `1B60` | 是 | `dispatch main [cs:di] @1B65` | ALU,MEM,STR | - | `lodsb al, byte ptr [si]` |
| `B1` | `1B5E` | 是 | `dispatch main [cs:di] @1B65` | ALU,MEM,STR | - | `lodsw ax, word ptr [si]` |
| `B2` | `1B6E` | 否(3) | `dispatch main [cs:di] @1B74` | ALU | - | `sub di, 0x162` |
| `B3` | `1B6E` | 否(3) | `dispatch main [cs:di] @1B74` | ALU | - | `sub di, 0x162` |
| `B4` | `1B6E` | 否(3) | `dispatch main [cs:di] @1B74` | ALU | - | `sub di, 0x162` |
| `B5` | `1B7C` | 否(3) | `dispatch main [cs:di] @1B84` | ALU | - | `sub di, 0x169` |
| `B6` | `1B7C` | 否(3) | `dispatch main [cs:di] @1B84` | ALU | - | `sub di, 0x169` |
| `B7` | `1B7C` | 否(3) | `dispatch main [cs:di] @1B84` | ALU | - | `sub di, 0x169` |
| `B8` | `1AEA` | 是 | `dispatch main [cs:di] @1AF1` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `B9` | `1AFA` | 是 | `dispatch main [cs:di] @1AFE` | CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `BA` | `1B06` | 否(4) | `dispatch main [cs:di] @1B0D` | ALU,CTRL,STACK | - | `sub di, 0x172` |
| `BB` | `1B06` | 否(4) | `dispatch main [cs:di] @1B0D` | ALU,CTRL,STACK | - | `sub di, 0x172` |
| `BC` | `1B06` | 否(4) | `dispatch main [cs:di] @1B0D` | ALU,CTRL,STACK | - | `sub di, 0x172` |
| `BD` | `1B06` | 否(4) | `dispatch main [cs:di] @1B0D` | ALU,CTRL,STACK | - | `sub di, 0x172` |
| `BE` | `1B16` | 否(4) | `dispatch main [cs:di] @1B1D` | ALU,CTRL,STACK | - | `sub di, 0x17b` |
| `BF` | `1B16` | 否(4) | `dispatch main [cs:di] @1B1D` | ALU,CTRL,STACK | - | `sub di, 0x17b` |
| `C0` | `1B16` | 否(4) | `dispatch main [cs:di] @1B1D` | ALU,CTRL,STACK | - | `sub di, 0x17b` |
| `C1` | `1B16` | 否(4) | `dispatch main [cs:di] @1B1D` | ALU,CTRL,STACK | - | `sub di, 0x17b` |
| `C2` | `1B26` | 是 | `dispatch main [cs:di] @1B2D` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `C3` | `1B36` | 否(2) | `dispatch main [cs:di] @1B42` | ALU,CTRL,STACK | - | `sub di, 0x184` |
| `C4` | `1B36` | 否(2) | `dispatch main [cs:di] @1B42` | ALU,CTRL,STACK | - | `sub di, 0x184` |
| `C5` | `1B4A` | 否(3) | `dispatch main [cs:di] @1B56` | ALU,CTRL,STACK | - | `sub di, 0x189` |
| `C6` | `1B4A` | 否(3) | `dispatch main [cs:di] @1B56` | ALU,CTRL,STACK | - | `sub di, 0x189` |
| `C7` | `1B4A` | 否(3) | `dispatch main [cs:di] @1B56` | ALU,CTRL,STACK | - | `sub di, 0x189` |
| `C8` | `1BF2` | 是 | `dispatch main [cs:di] @1BF8` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `C9` | `1C04` | 否(2) | `dispatch main [cs:di] @1C0D` | ALU,CTRL,STACK | - | `sub di, 0x190` |
| `CA` | `1C04` | 否(2) | `dispatch main [cs:di] @1C0D` | ALU,CTRL,STACK | - | `sub di, 0x190` |
| `CB` | `06B8` | 是 | `jmp 0x6f8` | ALU,CTRL,MEM,OTHER,STR | - | `xor cx, cx` |
| `CC` | `0F13` | 是 | `int3` | TRAP | - | `int3 ` |
| `CD` | `0FEA` | 是 | `jmp 0xff5` | CTRL,OTHER,STACK | - | `mov cx, 1` |
| `CE` | `1B9E` | 是 | `dispatch main [cs:di] @1BA5` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `CF` | `1BB4` | 是 | `dispatch main [cs:di] @1BBB` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `D0` | `1BE0` | 是 | `dispatch main [cs:di] @1BE7` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `D1` | `1BCA` | 是 | `dispatch main [cs:di] @1BD1` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `D2` | `17AA` | 是 | `dispatch main [cs:di] @17B0` | ALU,MEM,STACK | - | `pop cx` |
| `D3` | `18E0` | 是 | `dispatch main [cs:di] @18E5` | ALU,STACK | - | `pop di` |
| `D4` | `1900` | 是 | `dispatch main [cs:di] @1905` | ALU,STACK | - | `pop di` |
| `D5` | `1940` | 是 | `dispatch main [cs:di] @1947` | ALU,STACK | - | `pop cx` |
| `D6` | `1950` | 是 | `dispatch main [cs:di] @1956` | ALU,STACK | - | `pop ax` |
| `D7` | `197C` | 是 | `dispatch main [cs:di] @1981` | ALU,STACK | - | `pop cx` |
| `D8` | `17DA` | 是 | `dispatch main [cs:di] @17E1` | ALU,STACK | - | `pop cx` |
| `D9` | `17EA` | 是 | `dispatch main [cs:di] @17F5` | ALU,MEM,OTHER,STACK | - | `pop cx` |
| `DA` | `0CD8` | 是 | `dispatch main [cs:di] @0CDA` | ALU | - | `inc sp` |
| `DB` | `18A4` | 是 | `dispatch main [cs:di] @18A8` | ALU,STACK | - | `pop cx` |
| `DC` | `1DE8` | 是 | `dispatch main [cs:di] @1DF4` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `DD` | `08BA` | 是 | `dispatch dd-return [cs:bx+0x4B0] @08C1` | ALU,MEM | - | `mov al, byte ptr [bx]` |
| `DE` | `0DE2` | 是 | `jmp 0xe22` | ALU,CTRL,MEM,OTHER,STACK,STR | - | `xor ax, ax` |
| `DF` | `1874` | 是 | `dispatch main [cs:di] @1883` | ALU,CTRL,OTHER,STACK | - | `pop dx` |
| `E0` | `0D02` | 是 | `dispatch main [cs:di] @0D06` | STACK | - | `pop di` |
| `E1` | `1AD4` | 是 | `dispatch main [cs:di] @1ADB` | ALU,STACK | - | `pop cx` |
| `E2` | `1A96` | 是 | `jmp 0x1ab3` | ALU,CTRL,STACK | - | `pop di` |
| `E3` | `1A06` | 是 | `dispatch main [cs:di] @1A10` | ALU,STACK | - | `pop di` |
| `E4` | `1A18` | 是 | `dispatch main [cs:di] @1A22` | ALU,STACK | - | `pop di` |
| `E5` | `19D4` | 是 | `dispatch main [cs:di] @19DA` | ALU,OTHER,STACK | - | `pop ax` |
| `E6` | `0466` | 是 | `dispatch ext [cs:di+0x22C] @046B` | ALU,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `E7` | `0200` | 是 | `int3` | TRAP | - | `int3 ` |
| `E8` | `11E4` | 是 | `dispatch main [cs:di] @11FA` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `E9` | `129A` | 是 | `dispatch main [cs:di] @12B0` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `EA` | `0E9E` | 是 | `dispatch main [cs:di] @0EA4` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `EB` | `17B8` | 是 | `dispatch main [cs:di] @17C0` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `EC` | `1DFC` | 是 | `dispatch main [cs:di] @1E05` | MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `ED` | `1DD3` | 是 | `dispatch main [cs:di] @1DDF` | ALU,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `EE` | `1E22` | 是 | `dispatch main [cs:di] @1E34` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `EF` | `0EAC` | 是 | `dispatch main [cs:di] @0EAD` | STACK | - | `pop si` |
| `F0` | `19C6` | 是 | `dispatch main [cs:di] @19CB` | ALU,OTHER,STACK | - | `pop ax` |
| `F1` | `0F98` | 是 | `dispatch main [cs:di] @0FA2` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `F2` | `0FF5` | 是 | `dispatch main [cs:di] @0FFF` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `F3` | `124A` | 是 | `dispatch main [cs:di] @1263` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `F4` | `1C18` | 否(3) | `dispatch main [cs:di] @1C24` | ALU,CTRL,STACK | - | `sub di, 0x1e7` |
| `F5` | `1C18` | 否(3) | `dispatch main [cs:di] @1C24` | ALU,CTRL,STACK | - | `sub di, 0x1e7` |
| `F6` | `1C18` | 否(3) | `dispatch main [cs:di] @1C24` | ALU,CTRL,STACK | - | `sub di, 0x1e7` |
| `F7` | `0FBA` | 是 | `dispatch main [cs:di] @0FC8` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `F8` | `102F` | 是 | `dispatch main [cs:di] @103D` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `F9` | `11CA` | 是 | `jmp 0x11d6` | ALU,CTRL,MEM,OTHER,STR | - | `lodsb al, byte ptr [si]` |
| `FA` | `11AC` | 是 | `dispatch main [cs:di] @11B3` | ALU,MEM,OTHER,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `FB` | `1300` | 是 | `dispatch main [cs:di] @1319` | ALU,CTRL,MEM,STACK,STR | - | `lodsb al, byte ptr [si]` |
| `FC` | `11BC` | 否(4) | `dispatch main [cs:di] @11C2` | ALU,MEM,STACK | - | `sub di, 0x1f2` |
| `FD` | `11BC` | 否(4) | `dispatch main [cs:di] @11C2` | ALU,MEM,STACK | - | `sub di, 0x1f2` |
| `FE` | `11BC` | 否(4) | `dispatch main [cs:di] @11C2` | ALU,MEM,STACK | - | `sub di, 0x1f2` |
| `FF` | `11BC` | 否(4) | `dispatch main [cs:di] @11C2` | ALU,MEM,STACK | - | `sub di, 0x1f2` |

---

## 5. `0xE6` 扩展表与原生调用桥

### 5.1 前缀 Handler

`38CD:0466`：

```asm
0466: AC              lodsb al, byte ptr [si]
0467: 8B F8           mov di, ax
0469: D1 E7           shl di, 1
046B: 2E FF A5 2C 02  jmp word ptr cs:[di + 0x22C]
```

`0xE6` 读取下一个字节作为子 opcode，再从 `38CD:022C` 的 256 项表分派。
该表有 117 个唯一 Handler。它包含本次找到的全部固定原生调用和通用间接
原生调用。

### 5.2 `E6` 子 opcode 的原生调用映射

| 主 opcode | 子 opcode | Handler | 调用 | 说明 |
|---:|---:|---:|---|---|
| `E6` | `05` | `1A56` | `lcall 1055:0F8C` | 含 `div` 的算术 helper |
| `E6` | `3B` | `1854` | `lcall 1055:1456` | 含 `div` 的算术 helper |
| `E6` | `3C` | `1A76` | `lcall 1055:13F6` | 含 `div` 的算术 helper |
| `E6` | `40` | `1834` | `lcall 1055:1058` | 含 `div` 的算术 helper |
| `E6` | `9D` | `1742` | `lcall 1055:11F0` | 栈分配/栈上限检查 helper |
| `E6` | `4C` | `0659` | `lcall [BP-0x10]` | 通用间接原生调用 |
| `E6` | `50` | `0656` | `lcall [BP-0x10]` | 通用间接原生调用 |
| `E6` | `54` | `0653` | `lcall [BP-0x10]` | 通用间接原生调用 |

`1055:11F0` 的形态：

```asm
11F0: 59              pop cx
11F1: 5A              pop dx
11F2: 8B DC           mov bx, sp
11F4: 2B D8           sub bx, ax
11F6: 72 0B           jb 0x1203
11F8: 3B 1E 82 5B     cmp bx, word ptr [0x5B82]
11FC: 72 05           jb 0x1203
11FE: 8B E3           mov sp, bx
1200: 52              push dx
1201: 51              push cx
1202: CB              retf
```

这符合“按 `AX` 申请栈空间并检查栈上限”的 helper 形态，具体业务名未命名。

`1055:1058/1456/0F8C/13F6` 都包含 `div`、符号修正和栈上传参/返回值，
本报告只把它们归为“原生算术/除法 helper”，不把商/余数语义写成已确认。

### 5.3 通用间接调用

`E6 54`（`38CD:0653`）的核心：

```asm
0653: 80 C4 04          add ah, 4
0656: 80 C4 0C          add ah, 0xC
0659: 80 C4 0C          add ah, 0xC
065C: 33 DB             xor bx, bx
065E: 8F 46 F0          pop word ptr [bp - 0x10]
0661: 8A FC             mov bh, ah
0663: 8F 46 F2          pop word ptr [bp - 0xE]
0666: 8C D0             mov ax, ss
0668: 89 76 F4          mov word ptr [bp - 0xC], si
066B: 8B FB             mov di, bx
066D: 8E D8             mov ds, ax
066F: FF 5E F0          lcall [bp - 0x10]
0672: 8B DF             mov bx, di
0674: 32 DB             xor bl, bl
0676: 33 FB             xor di, bx
0678: 86 FB             xchg bl, bh
067A: 2E FF A7 FA 04    jmp word ptr cs:[bx + 0x4FA]
```

它从 VM 栈弹出 far 指针到 `[BP-0x10]`，切到 `SS` 后 `lcall`，再按返回值
通过 `04FA` 表分派。`E6 4C/50/54` 是同一通用桥的三个入口变体。

这条路径可以调用任意 far 指针，因此它是“VM 字节码 → 原生函数”的通用
门，而不是某个特定 Getter/Setter 的固定 opcode。

### 5.4 原生 → VM → 原生 的闭环

命中返回点 `3B1A:2195` 的原生调用者：

```asm
3B1A:218B  FF 76 08       push word ptr [bp + 8]
3B1A:218E  57             push di
3B1A:218F  8B 5E E8       mov bx, word ptr [bp - 0x18]
3B1A:2192  FF 5F 50       lcall [bx + 0x50]
3B1A:2195  89 76 FE       mov word ptr [bp - 2], si
```

即原生游戏代码通过对象/函数表 `[bx+0x50]` 调用一个 far 入口；VM 字节码
执行到 `0xDD` 后 `retf` 的返回点正是 `2195`。这与命中帧中的
`[BP+2]/[BP+4]` 完全一致，强证据表明该次调用是 **native → VM → native**
闭环。

同一原生段还有对象虚调用候选：

```asm
3B1A:1F25  FF 5F 18       lcall [bx + 0x18]
3B1A:1FA5  FF 5F 1C       lcall [bx + 0x1C]
```

这两个点更像“对象方法/字段访问”的 native 分派，但**没有**在本 V7 快照中
解析出它们指向的具体对象和 4000 段函数。因此不能把它们写成已确认的
城池/武将 Getter/Setter。

---

## 6. 关于“VM 调用 4000 段 Getter/Setter”的结论

### 6.1 已确认

1. 主表 256 项和 `E6` 扩展表 256 项都已从运行时 dump 提取。
2. 主表 Handler 没有 `call`/`lcall`；原生桥集中在 `E6` 扩展。
3. `E6 4C/50/54` 是通用 `lcall [BP-0x10]`，可调用任意 far 指针。
4. `E6 05/3B/3C/40/9D` 是固定的 `1055:xxxx` 原生 helper 调用。
5. 命中处 `0xDD` 是返回路径，返回到原生 `3B1A:2195`。
6. 原生侧通过 `lcall [bx+0x50]` 进入 VM，通过 `lcall [bx+0x18]` /
   `[bx+0x1c]` 进行对象方法分派。

### 6.2 未确认

1. `E6 4C/50/54` 的通用桥在具体字节码里实际传入了哪些 far 指针；
   本次 dump 没有把该字节码序列的指针参数解码出来。
2. `3B1A:1F25/1FA5` 的对象表项是否最终指向静态分析里的 `4000` 段
   Getter/Setter；需要再在 `3B1A` 的对象/虚表上设断点，或解析
   `Main.exe` 的运行时覆盖段映射。
3. `2ac5` / `335e` / `18a8` 等运行时段的身份仍未定位。
4. `38CD:0000` 与静态 Ghidra `3000:8780` 的覆盖/重定位关系仍未确认；
   本报告只使用运行时 dump 中的实际字节。
5. 256 个 opcode 的**业务名称**没有逐个命名；本报告给出的是结构行为
   （算术/栈/内存/分支/原生调用），不是完整指令集语义。

---

## 7. 构建与回归

```text
make clean && make -j$(nproc)
=> BUILD_EXIT=0, NO_WARNINGS

bash tools/run_regression.sh
=> TOTAL 80 checks: 80 passed, 0 failed
```

V7 只新增 Python 工具和文档，没有改动 C 源；构建与回归仍应保持 V5/V6 的
基线结果。

---

## 附录 A：自动生成的 256 项表、关键 Handler 与二级表

以下内容由 `tools/dump_vm_opcodes.py` 从运行时 dump 生成，表格中的地址
均为 `38CD:xxxx` 段内偏移；二级表另行标注基址。

**A.1 关键 Handler 反汇编**

### `045D` — opcode `00`

```asm
045D: ac                       lodsb al, byte ptr [si]
045E: 8b f8                    mov di, ax
0460: d1 e7                    shl di, 1
```

- 停止于：`dispatch main [cs:di] @0462`
- 类别：`ALU, MEM, OTHER, STR`

### `055A` — opcode `01`, `02`, `03`, `04`, `05`, `06`

```asm
055A: d7                       xlatb 
055B: 89 76 f4                 mov word ptr [bp - 0xc], si
055E: d1 e0                    shl ax, 1
0560: d1 e0                    shl ax, 1
0562: 8b 76 fa                 mov si, word ptr [bp - 6]
0565: 8b 7c 02                 mov di, word ptr [si + 2]
0568: 03 f8                    add di, ax
056A: 8b 5d 0a                 mov bx, word ptr [di + 0xa]
056D: 8b cb                    mov cx, bx
056F: 83 e3 1e                 and bx, 0x1e
```

- 停止于：`dispatch group [cs:bx+0x470] @0572`
- 类别：`ALU, MEM, OTHER`

### `08BA` — opcode `DD`

```asm
08BA: 8a 07                    mov al, byte ptr [bx]
08BC: 33 db                    xor bx, bx
08BE: 8a 5e fc                 mov bl, byte ptr [bp - 4]
```

- 停止于：`dispatch dd-return [cs:bx+0x4B0] @08C1`
- 类别：`ALU, MEM`

### `0466` — opcode `E6`

```asm
0466: ac                       lodsb al, byte ptr [si]
0467: 8b f8                    mov di, ax
0469: d1 e7                    shl di, 1
```

- 停止于：`dispatch ext [cs:di+0x22C] @046B`
- 类别：`ALU, MEM, OTHER, STR`

### 二级关键 Handler（0xDD 返回 / 0xE6 原生调用）

#### `dd_return:097E`

0xDD selector 0x1C -> retf (observed at hit)

```asm
097E: 8b c8                    mov cx, ax
0980: 83 e9 04                 sub cx, 4
0983: 8b f1                    mov si, cx
0985: 8b 46 04                 mov ax, word ptr [bp + 4]
0988: 89 42 04                 mov word ptr [bp + si + 4], ax
098B: 8b 46 02                 mov ax, word ptr [bp + 2]
098E: 89 42 02                 mov word ptr [bp + si + 2], ax
0991: 8b 76 ee                 mov si, word ptr [bp - 0x12]
0994: 8b 7e ec                 mov di, word ptr [bp - 0x14]
0997: 8e 5e fe                 mov ds, word ptr [bp - 2]
099A: 58                       pop ax
099B: 5a                       pop dx
099C: 8b e5                    mov sp, bp
099E: 5d                       pop bp
099F: 4d                       dec bp
09A0: 03 e1                    add sp, cx
09A2: cb                       retf 
```

- 停止于：`retf`

#### `ext_e6:0653`

E6 54 / E6 50 / E6 4C: indirect lcall [BP-0x10]

```asm
0653: 80 c4 04                 add ah, 4
0656: 80 c4 0c                 add ah, 0xc
0659: 80 c4 0c                 add ah, 0xc
065C: 33 db                    xor bx, bx
065E: 8f 46 f0                 pop word ptr [bp - 0x10]
0661: 8a fc                    mov bh, ah
0663: 8f 46 f2                 pop word ptr [bp - 0xe]
0666: 8c d0                    mov ax, ss
0668: 89 76 f4                 mov word ptr [bp - 0xc], si
066B: 8b fb                    mov di, bx
066D: 8e d8                    mov ds, ax
066F: ff 5e f0                 lcall [bp - 0x10]
0672: 8b df                    mov bx, di
0674: 32 db                    xor bl, bl
0676: 33 fb                    xor di, bx
0678: 86 fb                    xchg bl, bh
```

- 停止于：`dispatch native-return [cs:bx+0x4FA] @067A`
- 调用：`066F: lcall [bp - 0x10]` (native_indirect)

#### `ext_e6:1742`

E6 9D: lcall 1055:11F0 (stack helper)

```asm
1742: ad                       lodsw ax, word ptr [si]
1743: 8c d1                    mov cx, ss
1745: 8e d9                    mov ds, cx
1747: 9a f0 11 55 10           lcall 0x1055, 0x11f0
174C: 33 db                    xor bx, bx
174E: 8a 5e fd                 mov bl, byte ptr [bp - 3]
1751: d1 e3                    shl bx, 1
1753: 8b fe                    mov di, si
1755: d1 e3                    shl bx, 1
1757: 2e 8e 9f cc 1e           mov ds, word ptr cs:[bx + 0x1ecc]
175C: 8b f7                    mov si, di
175E: 8b 5e f8                 mov bx, word ptr [bp - 8]
1761: 32 e4                    xor ah, ah
```

- 停止于：`dispatch main [cs:di] @1763`
- 调用：`1747: lcall 0x1055, 0x11f0` (native_far)

#### `ext_e6:1834`

E6 40: lcall 1055:1058 (native arithmetic)

```asm
1834: 8b fc                    mov di, sp
1836: 36 ff 75 06              push word ptr ss:[di + 6]
183A: 36 ff 75 04              push word ptr ss:[di + 4]
183E: 9a 58 10 55 10           lcall 0x1055, 0x1058
1843: 59                       pop cx
1844: 59                       pop cx
1845: 52                       push dx
1846: 50                       push ax
1847: 8b 5e f8                 mov bx, word ptr [bp - 8]
184A: 32 e4                    xor ah, ah
```

- 停止于：`dispatch main [cs:di] @184C`
- 调用：`183E: lcall 0x1055, 0x1058` (native_far)

#### `ext_e6:1854`

E6 3B: lcall 1055:1456 (native arithmetic)

```asm
1854: 8b fc                    mov di, sp
1856: 36 ff 75 06              push word ptr ss:[di + 6]
185A: 36 ff 75 04              push word ptr ss:[di + 4]
185E: 9a 56 14 55 10           lcall 0x1055, 0x1456
1863: 59                       pop cx
1864: 59                       pop cx
1865: 52                       push dx
1866: 50                       push ax
1867: 8b 5e f8                 mov bx, word ptr [bp - 8]
186A: 32 e4                    xor ah, ah
```

- 停止于：`dispatch main [cs:di] @186C`
- 调用：`185E: lcall 0x1055, 0x1456` (native_far)

#### `ext_e6:1A56`

E6 05: lcall 1055:0F8C (native arithmetic)

```asm
1A56: 8b fc                    mov di, sp
1A58: 36 ff 75 06              push word ptr ss:[di + 6]
1A5C: 36 ff 75 04              push word ptr ss:[di + 4]
1A60: 9a 8c 0f 55 10           lcall 0x1055, 0xf8c
1A65: 59                       pop cx
1A66: 59                       pop cx
1A67: 52                       push dx
1A68: 50                       push ax
1A69: 8b 5e f8                 mov bx, word ptr [bp - 8]
1A6C: 32 e4                    xor ah, ah
```

- 停止于：`dispatch main [cs:di] @1A6E`
- 调用：`1A60: lcall 0x1055, 0xf8c` (native_far)

#### `ext_e6:1A76`

E6 3C: lcall 1055:13F6 (native arithmetic)

```asm
1A76: 8b fc                    mov di, sp
1A78: 36 ff 75 06              push word ptr ss:[di + 6]
1A7C: 36 ff 75 04              push word ptr ss:[di + 4]
1A80: 9a f6 13 55 10           lcall 0x1055, 0x13f6
1A85: 59                       pop cx
1A86: 59                       pop cx
1A87: 52                       push dx
1A88: 50                       push ax
1A89: 8b 5e f8                 mov bx, word ptr [bp - 8]
1A8C: 32 e4                    xor ah, ah
```

- 停止于：`dispatch main [cs:di] @1A8E`
- 调用：`1A80: lcall 0x1055, 0x13f6` (native_far)

**A.2 二级分派表与原生调用**

| 名称 | 基址 | 项数 | 唯一 | 入口约定 | 调用 |
|---|---:|---:|---:|---|---|
| `ext_e6` | `022C` | 256 | 117 | main opcode 0xE6 -> 38CD:0466  jmp word ptr cs:[di+0x22C]<br>DI = next bytecode byte * 2 (0x00..0xFF) | `0653: lcall [bp - 0x10]`<br>`0656: lcall [bp - 0x10]`<br>`0659: lcall [bp - 0x10]`<br>`1742: lcall 0x1055, 0x11f0`<br>`1834: lcall 0x1055, 0x1058`<br>`1854: lcall 0x1055, 0x1456`<br>`1A56: lcall 0x1055, 0xf8c`<br>`1A76: lcall 0x1055, 0x13f6` |
| `dd_return` | `04B0` | 32 | 9 | main opcode 0xDD -> 38CD:08BA  jmp word ptr cs:[bx+0x4B0]<br>BX = word [BP-4] (observed 0x1C at 38CD:1B79); 0x00..0x1F working slice, exact upper bound not proven | - |
| `group_470` | `0470` | 16 | 4 | 38CD:0572  jmp word ptr cs:[bx+0x470] after 'and bx,0x1e'<br>BX = (object word [DI+0xA] & 0x1E) | - |
| `group_490` | `0490` | 16 | 4 | 38CD:05F8  jmp word ptr cs:[bx+0x470] after 'add bx,0x20'<br>BX = (object word [DI+0xA] & 0x1E) + 0x20 | - |
| `native_call_return` | `04FA` | 16 | 13 | 38CD:067A  jmp word ptr cs:[bx+0x4FA] after lcall [BP-0x10]<br>BX derived from AX/return value of the native call; 16-entry working slice, exact upper bound not proven | - |

### 0xE6 扩展 Opcode 的原生调用映射

| 主 opcode | 扩展子 opcode | handler | 调用 |
|---:|---:|---:|---|
| `E6` | `54` | `0653` | `066F: lcall [bp - 0x10]` (native_indirect) |
| `E6` | `50` | `0656` | `066F: lcall [bp - 0x10]` (native_indirect) |
| `E6` | `4C` | `0659` | `066F: lcall [bp - 0x10]` (native_indirect) |
| `E6` | `9D` | `1742` | `1747: lcall 0x1055, 0x11f0` (native_far) |
| `E6` | `40` | `1834` | `183E: lcall 0x1055, 0x1058` (native_far) |
| `E6` | `3B` | `1854` | `185E: lcall 0x1055, 0x1456` (native_far) |
| `E6` | `05` | `1A56` | `1A60: lcall 0x1055, 0xf8c` (native_far) |
| `E6` | `3C` | `1A76` | `1A80: lcall 0x1055, 0x13f6` (native_far) |

### 所有绝对/间接原生调用

| 来源 Handler | 指令 | 类型 |
|---|---|---|
| `ext_e6:0653` | `066F: lcall [bp - 0x10]` | `native_indirect` |
| `ext_e6:0656` | `066F: lcall [bp - 0x10]` | `native_indirect` |
| `ext_e6:0659` | `066F: lcall [bp - 0x10]` | `native_indirect` |
| `ext_e6:1742` | `1747: lcall 0x1055, 0x11f0` | `native_far` |
| `ext_e6:1834` | `183E: lcall 0x1055, 0x1058` | `native_far` |
| `ext_e6:1854` | `185E: lcall 0x1055, 0x1456` | `native_far` |
| `ext_e6:1A56` | `1A60: lcall 0x1055, 0xf8c` | `native_far` |
| `ext_e6:1A76` | `1A80: lcall 0x1055, 0x13f6` | `native_far` |

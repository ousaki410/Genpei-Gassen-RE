# 执行报告 V2

> 更新：第 6 节已完成，见 `docs/MAIN_EXE_ARCHITECTURE.md`；本文件仍保留第 2~5 节的历史记录。

> 本报告只记录第 2~5 节的结果。第 6 节（写 docs/MAIN_EXE_ARCHITECTURE.md、更新 README、跑完整 regression）按新要求暂缓，等第 2~5 节确认后再开始。
>
> 基线核对：`Main.exe` MD5 = `7e7e1b875aa3b07eeb28c2eb5950aac1`。本次没有重新做“是否加壳”的验证。

## 1. FID 过滤结果

已标记为库函数: **0 个** / 未标记(候选游戏逻辑): **1714 个**

FID 分析器已经启用，但 Ghidra 12.1.4 自带的 10 个 `*.fidbf` 全部是 Visual Studio 2012/2015/2017/2019 的 x86/x64 库；`Main.exe` 的语言是 `x86:LE:16:Real Mode`，`FidService.canProcess()` 返回 `false`。因此没有函数被标记为库函数，也不能据此排除 CRT 函数。

- 安装的 FID 文件：10 个，全部 `active=true`
  - `/opt/ghidra/Ghidra/Features/FunctionID/data/vs2012_x86.fidbf` 等
- `index.json` 每条函数已包含：
  - `is_fid_match: false`
  - `fid_name: ""`
- `index.json` 顶层：
  - `fid_available: false`
  - `fid_match_count: 0`
  - `fid_status: "FID libraries cannot process language x86:LE:16:Real Mode (installed Visual Studio FID databases are 32/64-bit only)"`

结论：**FID 过滤这一层没有实际过滤掉任何函数**；不能把“未标记”误写成“已确认不是库函数”。

## 2. Open.exe / End.exe 脱壳结果

| 文件 | 原始大小 | 脱壳后大小 | 是否成功 |
|---|---:|---:|---|
| `Open.exe` | 79,605 字节 | 未生成 | **否，blocked** |
| `End.exe` | 92,121 字节 | 未生成 | **否，blocked** |

原因：`unp.exe` 不存在。已搜索：

- 仓库目录
- `/home/ou`
- `/tmp`
- `/opt`

均没有找到 `unp.exe` / `unp*.exe`。已向你确认，你选择“继续 FID/grep/xref，等 UNP”。因此没有运行 DOSBox，也没有伪造“脱壳成功”的体积。

`Open.exe` / `End.exe` 仍按 Stage 1 的结果保存为 `Open_packed.exe` / `End_packed.exe`；没有对它们跑 Stage 2。

## 3. 71/76 全量 grep 结果

在 `re_out/decompiled/Main.exe/` 上执行：

```bash
grep -rnE '(0x47|0x4c|\b71\b|\b76\b)' .
```

- 原始命中：**95 条**
- FID 过滤后：**95 条**（因为 FID 对 16-bit 不可用，过滤数为 0）
- 最终候选函数：**15 个**，已写入 `re_out/candidate_functions.txt`

说明：

- 95 条中有不少是函数头注释里的“函数大小 71/76 字节”，不是数组步长。
- 真正含有 `* 0x47` / `* 0x4c` 的代码行只有 6 条，集中在 `4000:15fb`、`4000:1697`、`4000:4e8e`、`4000:4eea` 及其调用者。
- 没有发现“同一函数内同时出现 71 和 76 步长”的函数。

候选清单（不超过 15 个）：

```text
 1. 4000:15fb  FUN_4000_15fb  -- index*0x47, clamps index>=400 to 3; 71-byte record scale (meaning not confirmed)
 2. 4000:1697  FUN_4000_1697  -- param_3*0x47 and param_4*0x47 with base 0x451a
 3. 4000:16b7  FUN_4000_16b7  -- calls 71-byte scaler; writes a 7-byte rotating buffer
 4. 4000:1709  FUN_4000_1709  -- copies strings from +0 and +0xe of 71-byte record into a 13-byte buffer
 5. 4000:185b  FUN_4000_185b  -- reads 16-bit field +0x1a from 71-byte record
 6. 4000:1871  FUN_4000_1871  -- reads 16-bit field +0x1c from 71-byte record
 7. 4000:1887  FUN_4000_1887  -- compares +0x1a/+0x1c of two 71-byte records (same-location test)
 8. 4000:1957  FUN_4000_1957  -- sets flag bits at +0x21 of 71-byte record, index<400
 9. 4000:1999  FUN_4000_1999  -- tests flag bits at +0x21 of 71-byte record, index<400
10. 4000:4e8e  FUN_4000_4e8e  -- index*0x4c, clamps index>0x26 to 0; 76-byte record scale (meaning not confirmed)
11. 4000:4eea  FUN_4000_4eea  -- param_3*0x4c and param_4*0x4c with base 0x4c1d
12. 4000:50d6  FUN_4000_50d6  -- reads 16-bit field +0x26 from 76-byte record
13. 4000:50ec  FUN_4000_50ec  -- reads 16-bit field +0x28 from 76-byte record
14. 4000:5150  FUN_4000_5150  -- returns pointer to +0x20 in 76-byte record
15. 4000:5bce  FUN_4000_5bce  -- tests flag bits at +0x46 in 76-byte record
```

“71 字节记录”和“76 字节记录”只是根据缩放函数和字段偏移做的描述；本次没有把它们直接写成“武将记录/城池记录”，因为还没有做第 6 节的玩法语义确认。

## 4. asset_xref 字符串校验结果

### 前后对比

| 项目 | 旧 index（仅按 0x1000–0x1e00 标量猜测） | 新 index（严格字符串校验） |
|---|---:|---:|
| 非空 asset 数量 | 5 | 6 |
| xref 条目数 | 6 | 10 |
| 原始候选目标数 | — | 505 |
| 因字符串不匹配而丢弃 | — | 495 |

严格规则：从目标地址读取 NUL 结尾的纯 ASCII 字符串；允许可选的 DOS 盘符 `C:`；去掉盘符后必须与 `re_out/seeds.json` 中的 34 个文件名之一逐字节匹配（大小写不敏感），否则丢弃。`C:MESSAGE.GPC:MUSIC.GP` 这种拼接串不会被当成单个资产命中。

### 校验后保留的 10 条 xref

```text
3000:215a -> MAINOBJ.GP   (string C:MAINOBJ.GP @ 6000:170f)
3000:44c2 -> MAINOBJ.GP   (string C:MAINOBJ.GP @ 6000:170f)
3000:7a04 -> MAINOBJ.GP   (string MAINOBJ.GP   @ 6000:1711)
1000:6005 -> MAINEVT.GP   (string MAINEVT.GP    @ 6000:1775)
3000:08cc -> MAINEVT.GP   (string C:MAINEVT.GP  @ 6000:1773)
3000:a241 -> MAINEVT.GP   (string MAINEVT.GP    @ 6000:1775)
3000:8018 -> HKEI.GP      (string HKEI.GP       @ 6000:180b)
2000:c770 -> KAODATA.GP   (string C:KAODATA.GP  @ 6000:183d)
1000:176e -> LOGO.GP      (string LOGO.GP       @ 6000:185b)
3000:a241 -> DISK-3.GP    (string C:DISK-3.GP   @ 6000:1889)
```

**重要限定**：字符串校验只能证明“该函数里有一个立即数/操作数指向了正确的文件名”，不能证明该函数就是资产加载器。上面多条 xref 所在的 `3000:215a`、`3000:44c2`、`3000:7a04`、`3000:08cc`、`3000:a241`、`3000:8018`、`2000:c770` 反编译结果带有 bad-instruction / 栈帧异常警告，仍然很可能是数据区被误当代码。`1000:176e` 是 DOS 文件句柄/访问包装函数，也不是“资产加载器”的直接证据。这些都必须标注为“不确定”，不能当作已验证的资产引用。

另外，`index.json` 中实际从 seeds 读到的是 **34 个资产名**，不是 36 个；这是 Stage 1 自己生成的口径，没有手敲补充。

## 5. 候选函数反编译原文

挑选 5 个，理由分别是一句话：

1. `FUN_4000_15fb`：直接展示了 71 字节缩放，并且有 `index >= 400` 的钳制。
2. `FUN_4000_1697`：同一函数里出现两次 `* 0x47`，且带一个基址常量 `0x451a`。
3. `FUN_4000_4e8e`：直接展示了 76 字节缩放，并有 `index > 0x26` 的钳制。
4. `FUN_4000_4eea`：同一函数里出现两次 `* 0x4c`，且带基址常量 `0x4c1d`。
5. `FUN_4000_1887`：通过两个 71 字节记录的 `+0x1a`/`+0x1c` 字段做同位置判断，是字段访问层的直接证据。

### 5.1 `4000:15fb`

```c
/* FUN_4000_15fb @ 4000:15fb (26 bytes) */

int __cdecl16far FUN_4000_15fb(uint param_1)

{
  if (399 < param_1) {
    param_1 = 3;
  }
  return param_1 * 0x47;
}
```

### 5.2 `4000:1697`

```c
/* FUN_4000_1697 @ 4000:1697 (32 bytes) */

void __cdecl16far FUN_4000_1697(undefined2 param_1,undefined2 param_2,int param_3,int param_4)

{
  FUN_4000_4230(param_1,param_2,param_3 * 0x47,0x451a,param_4 * 0x47);
  return;
}
```

### 5.3 `4000:4e8e`

```c
/* FUN_4000_4e8e @ 4000:4e8e (24 bytes) */

int __cdecl16far FUN_4000_4e8e(uint param_1)

{
  if (0x26 < param_1) {
    param_1 = 0;
  }
  return param_1 * 0x4c;
}
```

### 5.4 `4000:4eea`

```c
/* FUN_4000_4eea @ 4000:4eea (32 bytes) */

void __cdecl16far FUN_4000_4eea(undefined2 param_1,undefined2 param_2,int param_3,int param_4)

{
  FUN_4000_4230(param_1,param_2,param_3 * 0x4c,0x4c1d,param_4 * 0x4c);
  return;
}
```

### 5.5 `4000:1887`

```c
/* FUN_4000_1887 @ 4000:1887 (69 bytes) */

undefined2 __cdecl16far FUN_4000_1887(undefined2 param_1,undefined2 param_2)

{
  int iVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  undefined2 uVar5;
  
  iVar1 = FUN_4000_1871(param_1);
  iVar2 = FUN_4000_1871(param_2);
  iVar3 = FUN_4000_185b(param_1);
  iVar4 = FUN_4000_185b(param_2);
  if ((iVar4 == iVar1) || (iVar3 == iVar2)) {
    uVar5 = 1;
  }
  else {
    uVar5 = 0;
  }
  return uVar5;
}
```

## 6. 遇到的报错 / 异常

1. **UNP 缺失（Step 3 blocked）**
   - 搜索 `unp.exe` / `unp*.exe` 无结果。
   - 未运行 `unp Open.exe` / `unp End.exe`，因此没有脱壳后体积。
2. **FID 架构不匹配**
   - `FidService.canProcess()` 对 `x86:LE:16:Real Mode` 返回 `false`。
   - Ghidra 自带 10 个 active FID 库均为 Visual Studio 32/64-bit。
   - 结果：0 个函数被 FID 标记，不能执行真正的 FID 过滤。
3. **两个函数反编译失败**
   - `2000:bfd0 FUN_2000_bfd0`：
     `Exception while decompiling 2000:bfd0: process: timeout`
   - `2000:c86e FUN_2000_c86e`：
     `Low-level Error: Cannot properly adjust input varnodes`
   - 其余 1712/1714 成功。
4. **asset_xref 仍有假阳性风险**
   - 严格字符串校验后仍保留 10 条，但多条所在函数有 bad instruction 警告。
   - 已在第 4 节明确标注“不确定”。

## 7. git 状态

```text
297a0aa feat(re): add DOS RE pipeline, FID probe and strict asset xrefs
3c004da fix(testbench): hide the overlay for headless modal actions
57a7f2e polish(hud): use the full toolbar labels from the spec
713fbc2 feat(council): palace chamber, diplomacy/espionage commands and intro/ending movies (subtask 15)
6c7a955 feat(hud): in-game toolbar, beginner guide and F2 testbench (subtask 14)

?? report_summary_v2.md

(报告文件本身在上面的 status 中是未跟踪；本次提交会把报告一并纳入。提交后工作区应为 clean。)
```

**尚未 push，等待确认。**

## 8. 本阶段未完成 / 被 block 的事项

- **Open.exe / End.exe 脱壳未完成**：缺少 `unp.exe`；没有伪造体积或入口反编译结果。
- **FID 过滤未真正生效**：Ghidra 自带 FID 库不支持 16-bit Real Mode；没有自行创建/伪造 16-bit FID 库。
- **docs/MAIN_EXE_ARCHITECTURE.md、README 更新、regression、第 6 节玩法定位未开始**：按新要求等第 2~5 节确认后再做。

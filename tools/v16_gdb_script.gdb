# GDB Script for PCem/86Box + GDB stub (V16 corrected)
# 适用于连接到模拟器 GDB stub (通常端口 1234)
# target remote localhost:1234
#
# 线性地址 = segment*16 + offset (实模式)
# AI 段基址 0x20780, DGROUP 基址 0x619F0, VM 基址 0x38CD0, Order 基址 0x31CA0

# ============================================================
# 定义关键地址 (线性/物理)
# ============================================================
set $AI_SEG   = 0x20780
set $ORDER_SEG = 0x31CA0
set $VM_SEG   = 0x38CD0
set $DGROUP   = 0x619F0

# ============================================================
# 每次停止时打印寄存器
# ============================================================
define hook-stop
  printf "\n=== STOP at CS:IP=0x%x:0x%x (linear 0x%x) ===\n", $cs, $pc, ($cs << 4) + $pc
  info registers cs ds es ss si di bx ax dx bp sp
end

# ============================================================
# 断点定义 (V16 corrected)
# ============================================================

# BP #1: 六路表读监视 (GDB watch on memory)
watch *(short*)0x23117
commands
  silent
  printf "\n>>> BP#1 SIXWAY_TABLE_READ: addr=0x23117 value=0x%x\n", *(short*)0x23117
  continue
end

# BP #2: 分发器入口
break *0x2303C
commands
  silent
  printf "\n>>> BP#2 DISPATCHER: unit=[bp+6]=0x%x targets=[bp+8]=0x%x action=[bp+0xA]=0x%x\n",
         *(short*)($bp+6), *(short*)($bp+8), *(short*)($bp+0xA)
  continue
end

# BP #3: 六路 jmp cs:[bx+0x2997]
break *0x23112
commands
  silent
  printf ">>> BP#3 SIXWAY_JMP: BX=0x%x AX=0x%x SI=0x%x\n", $bx, $ax, $si
  continue
end

# BP #4: 六路处理器入口 (6 个)
break *0x23123
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 0: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x23133
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 1: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x23140
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 2: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x2314D
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 3: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x2315E
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 4: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x2316F
commands
  silent
  printf ">>> BP#4 HANDLER ACTION 5: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end

# BP #5: 六个执行器 trampoline (6 个)
break *0x320EA
commands
  silent
  printf ">>> BP#5 EXECUTOR 0: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x320F8
commands
  silent
  printf ">>> BP#5 EXECUTOR 1: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x32114
commands
  silent
  printf ">>> BP#5 EXECUTOR 2: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x32C9C
commands
  silent
  printf ">>> BP#5 EXECUTOR 3: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x32CAA
commands
  silent
  printf ">>> BP#5 EXECUTOR 4: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end
break *0x330FA
commands
  silent
  printf ">>> BP#5 EXECUTOR 5: CS:IP=0x%x:0x%x\n", $cs, $pc
  continue
end

# BP #6: VM 主入口
break *0x394E0
commands
  silent
  printf "\n>>> BP#6 VM_ENTRY: BX=0x%x (header ptr), DX=0x%x (caller CS), CX=0x%x (inline block)\n",
         $bx, $dx, $cx
  continue
end

# BP #7: VM mov bx,[bx]
break *0x3950B
commands
  silent
  printf ">>> BP#7 VM_HEADER_LOAD: [BX]=0x%x (header offset)\n", *(short*)($bx)
  continue
end

# BP #8: VM 第一个 opcode 分派 jmp cs:[di]
break *0x39543
commands
  silent
  printf ">>> BP#8 VM_OP_DISPATCH: AL=0x%x (opcode), DI=0x%x (opcode*2)\n", $al & 0xFF, $di
  continue
end

# BP #9: VM 原生导入调用 lcall es:[bx+0xA]
break *0x39579
commands
  silent
  printf "\n>>> BP#9 VM_IMPORT_CALL: ES=0x%x BX=0x%x DI=0x%x\n", $es, $bx, $di
  printf "    6-byte record at ES:BX+0xA: "
  set $off = $es * 16 + $bx + 0xA
  printf "off=0x%x seg=0x%x\n", *(short*)$off, *(short*)($off + 2)
  continue
end

# BP #10: ctx (次级链)
break *0x226E0
commands
  silent
  printf "\n>>> BP#10 CTX_ENTRY: full stack recorded\n"
  info registers
  continue
end

# BP #11: gate (次级链)
break *0x22F60
commands
  silent
  printf "\n>>> BP#11 GATE_ENTRY: full stack recorded\n"
  info registers
  continue
end

# BP #12: check
break *0x227BA
commands
  silent
  printf ">>> BP#12 CHECK_ENTRY\n"
  continue
end

# ============================================================
# 内存观测点 (Read watch)
# ============================================================

# BP #13: 表 A 读监视 DS:0x427A = 线性 0x65C6A
watch *(short*)0x65C6A
commands
  silent
  printf ">>> BP#13 TABLE_A_READ: idx record at 0x65C6A\n"
  continue
end

# BP #14: 表 B 读监视 DS:0x7B0E = 线性 0x694FE
watch *(short*)0x694FE
commands
  silent
  printf ">>> BP#14 TABLE_B_READ: idx record at 0x694FE\n"
  continue
end

# BP #15: 0x281AF 5 个 4 字节记录
watch *(short*)0x281AF
commands
  silent
  printf ">>> BP#15 REGION_281AF_READ: value=0x%x\n", *(short*)0x281AF
  continue
end

# ============================================================
# 运行控制
# ============================================================
# 启动后先跑到游戏主循环
# 然后手动触发 AI 回合 (或设置条件断点在战斗开始时)

# 方便命令
define ai_trace_on
  set logging file v16_gdb_trace.log
  set logging on
  printf "Tracing ON\n"
end

define ai_trace_off
  set logging off
  printf "Tracing OFF\n"
end

document ai_trace_on
Enable GDB trace logging to v16_gdb_trace.log
end

document ai_trace_off
Disable trace logging
end

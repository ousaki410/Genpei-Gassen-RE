# GDB Script for PCem/86Box + GDB stub
# 适用于连接到模拟器 GDB stub (通常端口 1234)
# target remote localhost:1234

# ============================================================
# 定义关键地址 (物理/线性 = 实模式下 segment*16 + offset)
# ============================================================
define hook-stop
  printf "\n=== STOP at %p ===\n", $pc
  info registers cs ds si di bx ax dx
end

# 断点定义
# 外层 VM 取指循环
break *0x3995C
commands
  silent
  printf "\n>>> VM_FETCH: opcode=0x%x (AL)\n", $al & 0xFF
  if $al == 0x08
    printf "    >>> TASK 0x08 DETECTED!\n"
  end
  continue
end

# 真正分发
break *0x39963
commands
  silent
  printf ">>> VM_DISPATCH: DI=0x%x (opcode*2), CS:[DI]=0x%x\n", $di, *(short*)($cs*16+$di)
  continue
end

# Task 0x08 描述符读取 (物理地址 0x6ACF8)
watch *(char*)0x6ACF8
commands
  silent
  printf ">>> TASK_08_DESC READ: value=0x%x\n", *(char*)0x6ACF8
  continue
end

# 惰性解析器入口
break *0x398E1
commands
  silent
  printf "\n>>> RESOLVER_ENTRY: BX=0x%x (module_idx), SI=0x%x (ret_addr)\n", $bx, $si
  continue
end

# 解析器 miss 路径
break *0x398F3
commands
  silent
  printf ">>> RESOLVER_MISS: calling stub at cs:[bx*2+0x1ECA]\n"
  continue
end

# 解析器返回前 (jmp si 前)
break *0x398F1
commands
  silent
  printf ">>> RESOLVER_RET: DX=0x%x (seg), AX=0x%x (off), DI=0x%x\n", $dx, $ax, $di
  continue
end

# DS:0x5B92 缓存写入 (物理 0x67582)
watch *(short*)0x67582
commands
  silent
  printf ">>> SEG_CACHE_WRITE: module_idx=0x%x, value=0x%x\n", ($bx/2), *(short*)0x67582
  continue
end

# AI VM 操作码表访问
watch *(char*)0x220D0
commands
  silent
  printf ">>> AI_VM_TABLE_READ\n"
  continue
end

# AI VM opcode 0x08 入口
break *0x220E0
commands
  silent
  printf ">>> AI_OP_08_ENTRY: CS=0x%x, IP=0x%x\n", $cs, $ip
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
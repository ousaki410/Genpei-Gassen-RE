#!/usr/bin/env python3
"""
V16 Trace Analyzer — parse DOSBox-X/GDB logs, verify the corrected AI chain.
Matches log lines emitted by tools/v16_trace_setup.txt and tools/v16_gdb_script.gdb.
"""
import re
import sys

# 成功标准中要求记录的断点编号
REQUIRED_BPS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15}

patterns = {
    # BP#1 six-way table read
    'bp1_sixway_read': re.compile(r'>>> BP#1 SIXWAY_TABLE_READ: addr=0x23117 value=0x([0-9a-fA-F]+)'),
    # BP#2 dispatcher entry
    'bp2_dispatcher': re.compile(
        r'>>> BP#2 DISPATCHER: unit=0x([0-9a-fA-F]+) targets=0x([0-9a-fA-F]+) action=0x([0-9a-fA-F]+)'),
    # BP#3 six-way jmp
    'bp3_sixway_jmp': re.compile(r'>>> BP#3 SIXWAY_JMP: BX=0x([0-9a-fA-F]+) AX=0x([0-9a-fA-F]+) SI=0x([0-9a-fA-F]+)'),
    # BP#4 handler entry (action 0–5)
    'bp4_handler': re.compile(r'>>> BP#4 HANDLER ACTION (\d): CS:IP=0x([0-9a-fA-F]+):0x([0-9a-fA-F]+)'),
    # BP#5 executor trampoline
    'bp5_executor': re.compile(r'>>> BP#5 EXECUTOR (\d): CS:IP=0x([0-9a-fA-F]+):0x([0-9a-fA-F]+)'),
    # BP#6 VM entry
    'bp6_vm_entry': re.compile(
        r'>>> BP#6 VM_ENTRY: BX=0x([0-9a-fA-F]+) \(header ptr\), DX=0x([0-9a-fA-F]+) \(caller CS\), CX=0x([0-9a-fA-F]+) \(inline block\)'),
    # BP#7 VM header load
    'bp7_vm_header': re.compile(r'>>> BP#7 VM_HEADER_LOAD: \[BX\]=0x([0-9a-fA-F]+) \(header offset\)'),
    # BP#8 VM opcode dispatch
    'bp8_vm_op': re.compile(r'>>> BP#8 VM_OP_DISPATCH: AL=0x([0-9a-fA-F]+) \(opcode\), DI=0x([0-9a-fA-F]+) \(opcode\*2\)'),
    # BP#9 VM native import call
    'bp9_vm_import': re.compile(
        r'>>> BP#9 VM_IMPORT_CALL: ES=0x([0-9a-fA-F]+) BX=0x([0-9a-fA-F]+) DI=0x([0-9a-fA-F]+)\n'
        r'    6-byte record at ES:BX\+0xA: off=0x([0-9a-fA-F]+) seg=0x([0-9a-fA-F]+)'),
    # BP#10 ctx entry
    'bp10_ctx': re.compile(r'>>> BP#10 CTX_ENTRY'),
    # BP#11 gate entry
    'bp11_gate': re.compile(r'>>> BP#11 GATE_ENTRY'),
    # BP#12 check entry
    'bp12_check': re.compile(r'>>> BP#12 CHECK_ENTRY'),
    # BP#13 table A read
    'bp13_table_a': re.compile(r'>>> BP#13 TABLE_A_READ'),
    # BP#14 table B read
    'bp14_table_b': re.compile(r'>>> BP#14 TABLE_B_READ'),
    # BP#15 0x281AF region read
    'bp15_region': re.compile(r'>>> BP#15 REGION_281AF_READ: value=0x([0-9a-fA-F]+)'),
}

# 表项 -> action ID 映射 (六路表)
SIXWAY_TABLE = {
    0x29A3: 0, 0x29B3: 1, 0x29C0: 2,
    0x29CD: 3, 0x29DE: 4, 0x29EF: 5,
}

# 执行器线性地址 -> 编号
EXECUTOR_LIN = {
    0x320EA: 0, 0x320F8: 1, 0x32114: 2,
    0x32C9C: 3, 0x32CAA: 4, 0x330FA: 5,
}

# 脚本头偏移 -> 主脚本
HEADER_TO_MAIN = {
    0x69FE: 0x6D8E,
    0x657A: 0x692E,
    0x60FE: 0x6476,
}


def parse_log(log_file):
    events = []
    with open(log_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.rstrip('\n')
            for etype, pat in patterns.items():
                m = pat.search(line)
                if m:
                    events.append({
                        'line': line_num,
                        'type': etype,
                        'groups': m.groups(),
                        'raw': line,
                    })
                    break  # 一行只匹配一个事件
    return events


def verify_chain(events):
    print("=== V16 TRACE VERIFICATION ===\n")

    hit_bps = set()
    sequence = []
    details = {}

    for e in events:
        t = e['type']
        g = e['groups']
        seq_line = f"#{e['line']} "

        if t == 'bp1_sixway_read':
            hit_bps.add(1)
            table_word = int(g[0], 16)
            action = SIXWAY_TABLE.get(table_word & 0xFFFF, None)
            seq_line += f"BP#1 SIXWAY_TABLE_READ value=0x{table_word:04X}"
            if action is not None:
                seq_line += f" -> action {action}"
            details['sixway_value'] = table_word
            details['sixway_action'] = action

        elif t == 'bp2_dispatcher':
            hit_bps.add(2)
            unit = int(g[0], 16)
            targets = int(g[1], 16)
            action = int(g[2], 16)
            seq_line += f"BP#2 DISPATCHER unit=0x{unit:04X} targets=0x{targets:04X} action={action}"
            details['dispatcher_action'] = action
            details['dispatcher_unit'] = unit

        elif t == 'bp3_sixway_jmp':
            hit_bps.add(3)
            bx = int(g[0], 16)
            ax = int(g[1], 16)
            seq_line += f"BP#3 SIXWAY_JMP BX=0x{bx:04X} AX=0x{ax:04X}"

        elif t == 'bp4_handler':
            hit_bps.add(4)
            action = int(g[0], 16)
            cs = int(g[1], 16)
            ip = int(g[2], 16)
            seq_line += f"BP#4 HANDLER action={action} CS:IP=0x{cs:04X}:0x{ip:04X}"
            details['handler_action'] = action

        elif t == 'bp5_executor':
            hit_bps.add(5)
            exec_num = int(g[0], 16)
            cs = int(g[1], 16)
            ip = int(g[2], 16)
            seq_line += f"BP#5 EXECUTOR {exec_num} CS:IP=0x{cs:04X}:0x{ip:04X}"
            details['executor_num'] = exec_num

        elif t == 'bp6_vm_entry':
            hit_bps.add(6)
            bx = int(g[0], 16)
            dx = int(g[1], 16)
            cx = int(g[2], 16)
            seq_line += f"BP#6 VM_ENTRY BX=0x{bx:04X} DX=0x{dx:04X} CX=0x{cx:04X}"
            details['vm_bx'] = bx
            details['vm_dx'] = dx
            details['vm_cx'] = cx

        elif t == 'bp7_vm_header':
            hit_bps.add(7)
            hdr_off = int(g[0], 16)
            seq_line += f"BP#7 VM_HEADER_LOAD header_offset=0x{hdr_off:04X}"
            details['vm_header_offset'] = hdr_off
            details['vm_main_script'] = HEADER_TO_MAIN.get(hdr_off)

        elif t == 'bp8_vm_op':
            hit_bps.add(8)
            opcode = int(g[0], 16)
            di = int(g[1], 16)
            seq_line += f"BP#8 VM_OP_DISPATCH opcode=0x{opcode:02X} DI=0x{di:04X}"
            details['vm_opcode'] = opcode

        elif t == 'bp9_vm_import':
            hit_bps.add(9)
            es = int(g[0], 16)
            bx = int(g[1], 16)
            di = int(g[2], 16)
            rec_off = int(g[3], 16)
            rec_seg = int(g[4], 16)
            seq_line += (f"BP#9 VM_IMPORT_CALL ES=0x{es:04X} BX=0x{bx:04X} DI=0x{di:04X} "
                         f"record=0x{rec_seg:04X}:0x{rec_off:04X}")
            details['import_es'] = es
            details['import_bx'] = bx
            details['import_di'] = di
            details['import_target'] = (rec_seg, rec_off)

        elif t == 'bp10_ctx':
            hit_bps.add(10)
            seq_line += "BP#10 CTX_ENTRY"

        elif t == 'bp11_gate':
            hit_bps.add(11)
            seq_line += "BP#11 GATE_ENTRY"

        elif t == 'bp12_check':
            hit_bps.add(12)
            seq_line += "BP#12 CHECK_ENTRY"

        elif t == 'bp13_table_a':
            hit_bps.add(13)
            seq_line += "BP#13 TABLE_A_READ"

        elif t == 'bp14_table_b':
            hit_bps.add(14)
            seq_line += "BP#14 TABLE_B_READ"

        elif t == 'bp15_region':
            hit_bps.add(15)
            val = int(g[0], 16)
            seq_line += f"BP#15 REGION_281AF_READ value=0x{val:04X}"
            details['region_281af_first_word'] = val

        sequence.append(seq_line)

    print("EVENT SEQUENCE:")
    for i, s in enumerate(sequence):
        print(f"  {i+1:2d}. {s}")

    print("\n=== VERIFICATION RESULT ===")
    checks = [
        ('BP#1 six-way table read', 1 in hit_bps),
        ('BP#2 dispatcher entry', 2 in hit_bps),
        ('BP#3 six-way jmp', 3 in hit_bps),
        ('BP#4 handler entry', 4 in hit_bps),
        ('BP#5 executor trampoline', 5 in hit_bps),
        ('BP#6 VM entry', 6 in hit_bps),
        ('BP#7 VM header load', 7 in hit_bps),
        ('BP#8 VM opcode dispatch', 8 in hit_bps),
        ('BP#9 VM native import call', 9 in hit_bps),
        ('BP#12 check entry', 12 in hit_bps),
        ('BP#13 table A read', 13 in hit_bps),
        ('BP#14 table B read', 14 in hit_bps),
        ('BP#15 region 0x281AF read', 15 in hit_bps),
    ]
    # ctx/gate is optional (secondary chain)
    checks.append(('BP#10 ctx entry (optional)', 10 in hit_bps))
    checks.append(('BP#11 gate entry (optional)', 11 in hit_bps))

    all_pass = True
    for desc, result in checks:
        status = "PASS" if result else "FAIL"
        if not result and desc not in ('BP#10 ctx entry (optional)', 'BP#11 gate entry (optional)'):
            all_pass = False
        print(f"  [{status}]: {desc}")

    # Extra diagnostics
    if 'dispatcher_action' in details:
        da = details['dispatcher_action']
        print(f"\n  Dispatcher action ID: {da} (valid 0–5: {0 <= da <= 5})")
    if 'sixway_action' in details:
        sa = details['sixway_action']
        print(f"  Six-way table resolved action: {sa}")
    if 'executor_num' in details:
        print(f"  Executor number: {details['executor_num']}")
    if 'vm_main_script' in details:
        print(f"  VM main script from header: 0x{details['vm_main_script']:04X}")
    if 'import_target' in details:
        print(f"  VM import target: 0x{details['import_target'][0]:04X}:0x{details['import_target'][1]:04X}")
    if 10 in hit_bps or 11 in hit_bps:
        print("  Secondary chain (ctx/gate) was reached — major finding!")
    else:
        print("  Secondary chain (ctx/gate) was NOT reached (consistent with static negative finding).")

    print(f"\nOVERALL: {'ALL CRITICAL CHECKS PASSED' if all_pass else 'SOME CRITICAL CHECKS FAILED'}")
    return all_pass


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 v16_trace_analyzer.py <trace_log_file>")
        sys.exit(1)

    events = parse_log(sys.argv[1])
    print(f"Parsed {len(events)} relevant events from {sys.argv[1]}\n")
    verify_chain(events)


if __name__ == '__main__':
    main()

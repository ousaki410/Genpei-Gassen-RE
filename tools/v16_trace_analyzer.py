#!/usr/bin/env python3
"""
V16 Trace Analyzer — parse DOSBox-X/GDB logs, verify task 0x08 chain
"""
import re
import sys

def parse_log(log_file):
    """Parse trace log, extract key events"""
    events = []
    
    patterns = {
        'task08_read': re.compile(r'TASK_08_DESC READ.*?value=0x([0-9a-fA-F]+)'),
        'vm_fetch': re.compile(r'VM_FETCH.*?opcode=0x([0-9a-fA-F]+)'),
        'vm_dispatch': re.compile(r'VM_DISPATCH.*?DI=0x([0-9a-fA-F]+)'),
        'resolver_entry': re.compile(r'RESOLVER_ENTRY.*?BX=0x([0-9a-fA-F]+).*SI=0x([0-9a-fA-F]+)'),
        'resolver_miss': re.compile(r'RESOLVER_MISS'),
        'resolver_ret': re.compile(r'RESOLVER_RET.*DX=0x([0-9a-fA-F]+).*AX=0x([0-9a-fA-F]+).*DI=0x([0-9a-fA-F]+)'),
        'seg_cache': re.compile(r'SEG_CACHE_WRITE.*module_idx=0x([0-9a-fA-F]+).*value=0x([0-9a-fA-F]+)'),
        'ai_table': re.compile(r'AI_VM_TABLE_READ'),
        'ai_op08': re.compile(r'AI_OP_08_ENTRY'),
    }
    
    with open(log_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            for event_type, pattern in patterns.items():
                m = pattern.search(line)
                if m:
                    events.append({
                        'line': line_num,
                        'type': event_type,
                        'groups': m.groups(),
                        'raw': line.strip()
                    })
    return events

def verify_chain(events):
    """Verify the expected sequence occurred"""
    print("=== V16 TRACE VERIFICATION ===\n")
    
    # Track state
    state = {
        'task08_read': False,
        'vm_fetch_08': False,
        'vm_dispatch': False,
        'resolver_entry_bx': None,
        'resolver_miss': False,
        'resolver_ret_dx': None,
        'seg_cache_val': None,
        'ai_table_read': False,
        'ai_op08_entry': False,
    }
    
    sequence = []
    
    for e in events:
        t = e['type']
        g = e['groups']
        
        if t == 'task08_read':
            state['task08_read'] = True
            sequence.append(f"#{e['line']} Task 0x08 descriptor READ")
            
        elif t == 'vm_fetch':
            opcode = int(g[0], 16)
            if opcode == 0x08:
                state['vm_fetch_08'] = True
                sequence.append(f"#{e['line']} VM_FETCH opcode=0x08 ✅")
            else:
                sequence.append(f"#{e['line']} VM_FETCH opcode=0x{opcode:02X}")
                
        elif t == 'vm_dispatch':
            di = int(g[0], 16)
            if di == 0x10:
                state['vm_dispatch'] = True
                sequence.append(f"#{e['line']} VM_DISPATCH DI=0x10 (0x08*2) ✅")
            else:
                sequence.append(f"#{e['line']} VM_DISPATCH DI=0x{di:04X}")
                
        elif t == 'resolver_entry':
            bx = int(g[0], 16)
            si = int(g[1], 16)
            state['resolver_entry_bx'] = bx
            sequence.append(f"#{e['line']} RESOLVER_ENTRY BX=0x{bx:04X} (module_idx), SI=0x{si:04X}")
            
        elif t == 'resolver_miss':
            state['resolver_miss'] = True
            sequence.append(f"#{e['line']} RESOLVER_MISS → loading module")
            
        elif t == 'resolver_ret':
            dx = int(g[0], 16)
            ax = int(g[1], 16)
            di = int(g[2], 16)
            state['resolver_ret_dx'] = dx
            sequence.append(f"#{e['line']} RESOLVER_RET DX=0x{dx:04X} (seg), AX=0x{ax:04X}, DI=0x{di:04X}")
            
        elif t == 'seg_cache':
            idx = int(g[0], 16)
            val = int(g[1], 16)
            state['seg_cache_val'] = val
            sequence.append(f"#{e['line']} SEG_CACHE_WRITE idx=0x{idx:04X} value=0x{val:04X}")
            
        elif t == 'ai_table':
            state['ai_table_read'] = True
            sequence.append(f"#{e['line']} AI_VM_TABLE_READ ✅")
            
        elif t == 'ai_op08':
            state['ai_op08_entry'] = True
            sequence.append(f"#{e['line']} AI_OP_08_ENTRY ✅")
    
    # Print sequence
    print("EVENT SEQUENCE:")
    for i, s in enumerate(sequence):
        print(f"  {i+1:2d}. {s}")
    
    print("\n=== VERIFICATION RESULT ===")
    checks = [
        ('Task 0x08 descriptor read', state['task08_read']),
        ('VM fetched opcode 0x08', state['vm_fetch_08']),
        ('VM dispatched to DI=0x10', state['vm_dispatch']),
        ('Resolver called with module index', state['resolver_entry_bx'] is not None),
        ('Resolver miss path taken', state['resolver_miss']),
        ('Resolver returned AI segment (0x220D)', state['resolver_ret_dx'] == 0x220D),
        ('Segment cache updated with 0x220D|1', state['seg_cache_val'] in (0x220D, 0x220D|1)),
        ('AI VM opcode table accessed', state['ai_table_read']),
        ('AI VM opcode 0x08 entered', state['ai_op08_entry']),
    ]
    
    all_pass = True
    for desc, result in checks:
        status = "✅ PASS" if result else "❌ FAIL"
        if not result:
            all_pass = False
        print(f"  {status}: {desc}")
    
    print(f"\nOVERALL: {'ALL CHECKS PASSED ✅' if all_pass else 'SOME CHECKS FAILED ❌'}")
    return all_pass

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 v16_trace_analyzer.py <trace_log_file>")
        sys.exit(1)
    
    events = parse_log(sys.argv[1])
    print(f"Parsed {len(events)} relevant events from {sys.argv[1]}")
    verify_chain(events)

if __name__ == '__main__':
    main()
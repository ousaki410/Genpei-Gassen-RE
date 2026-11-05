#!/usr/bin/env python3
"""
V15+ Lazy Resolver Simulator — demonstrates module loading flow
"""
import struct

RT = '/home/ou/Projects/Genpei-Gassen-RE/Y-源平合战/re_out/runtime_0000_a0000.bin'
rt = open(RT, 'rb').read()

S31 = 0x31CA0
DSB = 0x619F0

class ResolverState:
    def __init__(self):
        self.module_count = struct.unpack_from('<H', rt, DSB + 0x5B90)[0]
        self.seg_cache = {}
        for i in range(self.module_count):
            val = struct.unpack_from('<H', rt, DSB + 0x5B92 + 2*i)[0]
            self.seg_cache[i] = val
        
        # Secondary table (cs:0x22C6)
        self.sec_table = [struct.unpack_from('<H', rt, S31 + 0x22C6 + 2*i)[0] for i in range(256)]
        
        # Import table (cs:0x1ECA) - far resolver stubs
        self.imp_table = []
        for i in range(64):
            o, s = struct.unpack_from('<HH', rt, S31 + 0x1ECA + 4*i)
            self.imp_table.append((o, s))
        
        # Command table (cs:0x5785)
        self.cmd_table = []
        for i in range(41):
            o, s = struct.unpack_from('<HH', rt, S31 + 0x5785 + 4*i)
            self.cmd_table.append((o, s))
        
        # Dispatch table (cs:0x5789) - 41 far ptrs
        self.disp_table = []
        for i in range(41):
            o, s = struct.unpack_from('<HH', rt, S31 + 0x5789 + 4*i)
            self.disp_table.append((o, s))

    def resolve_module(self, module_idx: int):
        """Simulate resolver at 0x31CA:0x7C41"""
        print(f"\n=== RESOLVE MODULE {module_idx} ===")
        
        # Check cache
        cached = self.seg_cache.get(module_idx, 0)
        if cached & 1:
            seg = cached & ~1
            near_off = self.sec_table[module_idx] if module_idx < 256 else 0
            print(f"  CACHED: seg={seg:#04x}, near_off={near_off:#04x}")
            print(f"  -> MOV DS, {seg:#04x}; MOV DI, {near_off:#04x}; JMP SI")
            return seg, near_off
        
        print(f"  NOT CACHED: calling resolver stub")
        # Call resolver stub at cs:0x1ECA + module_idx*4
        if module_idx < len(self.imp_table):
            stub_off, stub_seg = self.imp_table[module_idx]
            print(f"  Stub at {stub_seg:#04x}:{stub_off:#04x} (lin {stub_seg*16+stub_off:#06x})")
        
        # In reality, stub would load module and return (DX=seg, AX=off)
        # For AI module, we know seg=0x220D (or its data segment)
        print(f"  -> Stub returns DX=0x220D (AI seg), AX=<entry_off>")
        print(f"  -> MOV DS, DX; MOV DI, AX; cache at DS:0x5B92[{module_idx}]")
        print(f"  -> JMP SI (return to caller)")
        return 0x220D, 0  # AI segment

    def register_module(self, module_idx: int, signature: bytes, seg: int, off: int, entry_array: bytes):
        """Simulate module registration at 0x374DA"""
        print(f"\n=== REGISTER MODULE {module_idx} ===")
        print(f"  Signature match at cs:[0x5773] (9 words)")
        print(f"  Write cs:[0x5785] = {seg:#04x}:{off:#04x}")
        print(f"  REP MOVSW 0x52 words from module entry array to cs:[0x5789]")
        print(f"  -> Dispatch table entries {module_idx*41}..{(module_idx+1)*41-1} populated")

def main():
    print("V15+ Lazy Resolver Simulator")
    print("=" * 60)
    
    state = ResolverState()
    
    print(f"Module count (DS:0x5B90): {state.module_count}")
    print(f"Seg cache (DS:0x5B92): {state.seg_cache}")
    print(f"Secondary table (cs:0x22C6) clean entries: {sum(1 for o in state.sec_table if o)}")
    print(f"Command table (cs:0x5785): all {state.cmd_table[0]}")
    print(f"Dispatch table (cs:0x5789): {len(state.disp_table)} entries")
    
    # Simulate AI module resolution
    # AI module index unknown - but let's assume it's one of the modules
    print("\n" + "="*60)
    print("SIMULATION: Outer VM executes AI task (NN=0x08)")
    print("="*60)
    
    print("\n1. Outer VM fetches opcode 0x08 from record descriptor")
    print("   -> Handler for task 0x08 calls resolver with BX = AI_MODULE_IDX")
    
    # The resolver would be called with the AI module's index
    # We don't know the exact index, but we can simulate the flow
    ai_idx = 0x08  # Hypothetical - task ID might map to module index
    state.resolve_module(ai_idx)
    
    print("\n2. Resolver returns: DS=0x220D, DI=AI_VM_ENTRY_OFF")
    print("3. Caller does NEAR CALL to AI VM entry (or JMP)")
    print("4. AI VM starts with CS=0x220D, fetches its own opcodes from cs:0")
    
    print("\n" + "="*60)
    print("SIMULATION: Module Registration (e.g., AI module load)")
    print("="*60)
    state.register_module(
        module_idx=ai_idx,
        signature=b'\x00'*18,  # 9 words
        seg=0x220D,
        off=0x0000,
        entry_array=b'\x00'*0xA4  # 41 far ptrs
    )

if __name__ == '__main__':
    main()
// @category YPHZ.RE
// Create functions at compiler prologues that Ghidra's auto-analysis skipped.
//
// This is a supplementary analysis pass.  It deliberately scans only the
// conventional code segments used by Main.exe and only recognises the two
// prologues emitted by the Watcom/Microsoft 16-bit compilers used here:
//
//   55 8B EC        push bp; mov bp,sp
//   C8 xx xx 00     enter frame,0
//
// Some tables in the DOS image contain byte sequences that look like one of
// those prologues.  A failed disassembly/createFunction is therefore skipped
// rather than treated as a fatal error.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;

public class GhidraGapCreate extends GhidraScript {
    private static final int[][] SEGMENTS = {
        { 0x3000, 0x10000 },
        { 0x4000, 0x10000 },
        { 0x5000, 0x10000 }
    };

    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressSpace space = currentProgram.getAddressFactory()
                .getDefaultAddressSpace();
        int created = 0;
        int skipped = 0;

        for (int[] seg : SEGMENTS) {
            int segment = seg[0];
            int size = seg[1];
            Address base = toAddr(String.format("%04x:0000", segment));
            if (memory.getBlock(base) == null) {
                println("[gap] no memory block for " + Integer.toHexString(segment));
                continue;
            }
            for (int off = 0; off + 4 < size; off++) {
                Address a = base.add(off);
                byte[] b = new byte[4];
                try {
                    memory.getBytes(a, b);
                }
                catch (MemoryAccessException exc) {
                    break;
                }
                boolean enter = (b[0] & 0xff) == 0xc8
                        && (b[3] & 0xff) == 0x00;
                boolean pushbp = (b[0] & 0xff) == 0x55
                        && (b[1] & 0xff) == 0x8b
                        && (b[2] & 0xff) == 0xec;
                if (!enter && !pushbp) {
                    continue;
                }
                if (fm.getFunctionContaining(a) != null) {
                    continue;
                }
                try {
                    disassemble(a);
                    if (fm.getFunctionAt(a) == null) {
                        createFunction(a, String.format(
                                "FUN_gap_%04x_%04x", segment, off));
                    }
                    if (fm.getFunctionAt(a) != null) {
                        created++;
                        if ((created % 25) == 0) {
                            println("[gap] created " + created + " at " + a);
                        }
                    }
                    else {
                        skipped++;
                    }
                }
                catch (Exception exc) {
                    skipped++;
                }
            }
        }
        println("[gap] done: created=" + created + " skipped=" + skipped);
    }
}

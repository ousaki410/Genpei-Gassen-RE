# V15 Memory Map Reference — Genpei Gassen Main.exe

## Three-Layer Addressing

| Layer | Description | Base / Delta | Example |
|-------|-------------|--------------|---------|
| **App** | Original EXE (file offsets = linear) | — | AI code at 0x11B80 |
| **Dec** | QL stub decompressed output | `dec = app + 0x10000` | AI code at 0x21B80 |
| **Runtime** | 640KB snapshot (file offset = linear) | `rt = dec + 0x550 = app + 0x10550` | AI code at 0x220D0 |

**Segment value mapping** (paragraphs):
- `rt_seg = app_seg + 0x1055`
- `dec_seg = app_seg` (QL patches segment values by +0x1055 at load)

## Segment Map (App / Runtime values)

| Name | App Seg | Runtime Seg | Runtime Base | Description |
|------|---------|-------------|--------------|-------------|
| Core | 0x0000 | 0x1055 | 0x10550 | Main executable, C runtime, core APIs |
| DGROUP | 0x514A | 0x619F | 0x619F0 | Data segment (DS), BSS, heap, globals |
| AI | 0x11B8 | 0x220D | 0x220D0 | AI module + inner VM |
| Lib | 0x2AC5 | 0x3B1A | 0x3B1A0 | Common library (record, object, math) |
| Sys | 0x2175 | 0x31CA | 0x31CA0 | Task/VM overlay (outer VM) |
| GFX | 0x43FA | 0x544F | 0x544F0 | Graphics service (stub-resident) |
| INT66 Drv | — | 0x09F6 | 0x09F60 | Resident device driver (INT 66h) |
| CMD Drv | — | 0x0B1C | 0x0B1C0 | Low driver (cs:0x57xx table target) |

## Key Runtime Data Structures (DS = 0x619F)

| DS Offset | Size | Description |
|-----------|------|-------------|
| 0x3D80 | 32B | "GENPEI" string (verified probe) |
| 0x5B02 | — | Heap header (heap end at +6) |
| 0x5B7A | 4B | Heap hook 1 (far ptr, currently 0) |
| 0x5B84 | 4B | OOM handler (far ptr, = 0x544F:0x0C46) |
| 0x5B88 | 4B | Heap hook 2 (far ptr, currently 0) |
| 0x5B8C | 2W | Reg-table cursor (init 0x957C, bound 0x95FC) |
| 0x5B90 | 2B | Import module count (0 at snapshot) |
| 0x5B92 | — | Import seg cache (word per module) |
| 0x5B94 | — | Another module array (cluster at 0x5F0E1+) |
| 0x5DA0 | 2W | Installer state (zeroed at 0x11DFE) |
| 0x5DB8 | 4B | Install+invoke slot (fn at 0x11DFE) |
| 0x5EC0 | 1862×4B | Export jump table (seg-patched at load) |
| 0x77EA | 4B | Callback slot 0 (default 0x3B1A:0x1A42) |
| 0x78B6 | 4B | Callback slot 1 (default 0x3B1A:0x1A42) |
| 0x796E | 4B | Callback slot 2 (default 0x3B1A:0x1A42) |
| 0x7A26 | 4B | Callback slot 3 (default 0x3B1A:0x1A42) |
| 0x7BEA | 4B | Signal handler 0 (0 at snapshot) |
| 0x7D4C | — | BSS start (record table base) |
| 0x8E46 | — | Main record table (sigA units) |
| 0x9212 | 2B | AI context field (written by 0x226E0) |
| 0x9224 | — | Record restart zone (sigA units) |
| 0x92C0 | — | Event descriptors (bytecode stream) |
| 0x957C | — | Reg-table initial cursor |
| 0x95FC | — | BSS end / reg-table bound |

## AI Segment Internals (CS = 0x220D, base 0x220D0)

| Offset | Linear | Description |
|--------|--------|-------------|
| 0x0000 | 0x220D0 | **VM opcode table** (256 × 2-byte near offsets) |
| 0x05F6 | 0x226C6 | M1 thunk (`lcall 0x3B1A:0x1A` with sigA args) |
| 0x0610 | 0x226E0 | Ctx-setup (writes DS:0x920A–0x9220) |
| 0x074F | 0x2281F | AI VM op 0x21 handler |
| 0x0F7E | 0x2304E | AI VM op 0x29 handler |
| 0x1715 | 0x237E5 | AI VM op 0x12 handler |
| 0x1D04 | 0x23DD4 | AI VM op 0x76 handler |
| 0x245D0 | 0x245D0 | AI VM internal near target (`push cs; call`) |
| 0x24682 | 0x24682 | AI VM op 0x39 handler (traverses list at 0x8DD4) |
| 0x252E | 0x245FE | AI VM op 0x37 handler |
| 0x25B8 | 0x24688 | AI VM op 0x39 alt entry |
| 0x2A2B | 0x24AFB | AI VM op 0x4E handler |
| 0xD467 | 0x2F537 | AI VM op 0x58 handler |
| 0x8DD4 | 0x2ADA4 | Global list head (used by op 0x39) |

## Outer VM Segment (CS = 0x31CA, base 0x31CA0)

| Offset | Linear | Description |
|--------|--------|-------------|
| 0x0000 | 0x31CA0 | Primary opcode table (256 words, garbage at snapshot) |
| 0x1ECA | 0x33B6A | Import resolver table (4-byte far ptrs per module) |
| 0x22C6 | 0x33F66 | Secondary opcode table (256 words, 6/32 clean) |
| 0x7C41 | 0x398E1 | Resolver: cache check → `lcall cs:[bx+0x1ECA]` |
| 0x7CC3 | 0x39963 | Fetch loop: `lodsb; mov di,ax; shl di,1; jmp cs:[di]` |
| 0x7CAA | 0x3994A | Boot: `lcall 0x1055:0x13A4` (core init) |

## Conversion Quick Reference

```python
# App file offset -> Runtime linear
rt = app_off + 0x10550

# Runtime linear -> App file offset  
app_off = rt - 0x10550

# App segment -> Runtime segment
rt_seg = app_seg + 0x1055

# Runtime seg:off -> Linear
linear = (rt_seg << 4) + off

# Linear -> Runtime seg:off (with known segment bases)
# Use SEG dict in v15_mapping.py
```

## Verified Probe Set (Δ = 0x550 bytes)

| Label | Runtime | Dec | Delta |
|-------|---------|-----|-------|
| M1 thunk | 0x226C6 | 0x22176 | 0x550 ✓ |
| M2 thunk | 0x2F09A | 0x2EB4A | 0x550 ✓ |
| Helper | 0x3B1BA | 0x3AC6A | 0x550 ✓ |
| Zone A | 0x1F060 | 0x1EB10 | 0x550 ✓ |
| Zone B | 0x126A0 | 0x12150 | 0x550 ✓ |
| DGROUP str | 0x65770 | 0x65220 | 0x550 ✓ (32/32 bytes) |
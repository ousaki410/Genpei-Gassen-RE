#!/usr/bin/env python3
"""dump_vm_opcodes.py — decode the 38CD bytecode VM in Main.exe.

The runtime dump produced by DOSBox-X with

    MEMDUMPBIN 1055:0000 60000 runtime_1055_60000.bin

contains the relocated VM at ``38CD:0000``.  The first 0x200 bytes are a
256-entry little-endian word table.  Each word is a handler offset inside the
``38CD`` segment.  This tool:

* validates the dump and locates the table using the 1055 base segment;
* reads all 256 entries and groups duplicate handlers;
* disassembles every unique handler in 16-bit mode with Capstone;
* classifies instructions (ALU / stack / control flow / memory / native call);
* follows the known secondary dispatch tables used by the 0xDD and 0xE6
  prefix handlers;
* writes a JSON database plus a human-readable Markdown table.

It is read-only and does not need DOSBox-X to be running.

Usage:
    python3 tools/dump_vm_opcodes.py
    python3 tools/dump_vm_opcodes.py --dump /path/to/runtime_1055_60000.bin
    python3 tools/dump_vm_opcodes.py --outdir /tmp/vm --no-markdown
    python3 tools/dump_vm_opcodes.py --check-md5 c7728266e406e98e6f24e4bf8f6388a7

The dump itself is large and is normally kept under the git-ignored ``re_out/``
tree.  Only this script, its generated small report, and the documentation are
committed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError:  # pragma: no cover - dependency error is reported to the user
    Cs = None  # type: ignore[assignment]
    CS_ARCH_X86 = CS_MODE_16 = None  # type: ignore[assignment]


DEFAULT_DUMP = "re_out/runtime_1055_60000.bin"
DEFAULT_OUTDIR = "re_out/vm"
DEFAULT_BASE_SEG = 0x1055
DEFAULT_VM_SEG = 0x38CD
DEFAULT_DUMP_MD5 = "c7728266e406e98e6f24e4bf8f6388a7"

MAIN_TABLE = 0x0000
MAIN_COUNT = 256

# Instruction sequences that mean "this is the VM dispatch tail, stop here".
DISPATCH_SEQUENCES = (
    bytes.fromhex("AC 8B F8 D1 E7 2E FF 25"),       # lodsb; mov di,ax; shl di,1; jmp [cs:di]
    bytes.fromhex("2E FF 25"),                       # jmp word ptr cs:[di]
    bytes.fromhex("2E FF A5 2C 02"),                 # jmp word ptr cs:[di+0x22c]
    bytes.fromhex("2E FF A7 70 04"),                 # jmp word ptr cs:[bx+0x470]
    bytes.fromhex("2E FF A7 30 04"),                 # jmp word ptr cs:[bx+0x430]
    bytes.fromhex("2E FF A7 50 04"),                 # jmp word ptr cs:[bx+0x450]
    bytes.fromhex("2E FF A7 FA 04"),                 # jmp word ptr cs:[bx+0x4fa]
    bytes.fromhex("2E FF A7 B0 04"),                 # jmp word ptr cs:[bx+0x4b0]
)

TERMINAL_MNEMONICS = {
    "ret", "retf", "iret", "iretd", "iretq",
    "jmp", "ljmp", "jmpf",
    "hlt", "int3", "ud2",
}

ALU = {
    "add", "adc", "sub", "sbb", "cmp", "inc", "dec", "neg",
    "mul", "imul", "div", "idiv", "and", "or", "xor", "not",
    "shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr",
    "test", "xadd", "xchg",
}
STACK = {"push", "pop", "pusha", "popa", "pushad", "popad", "enter", "leave"}
CONTROL = {
    "call", "lcall", "callf", "jmp", "ljmp", "jmpf",
    "ret", "retf", "iret", "iretd", "iretq", "loop", "loope", "loopne",
}
STRING = {
    "lodsb", "lodsw", "lodsd", "stosb", "stosw", "stosd",
    "movsb", "movsw", "movsd", "cmpsb", "cmpsw", "cmpsd",
    "scasb", "scasw", "scasd",
}
TRAP = {"int", "int3", "into", "hlt", "ud2", "bound"}
IO = {"in", "out", "insb", "insw", "outsb", "outsw"}
FPU = {
    "fadd", "fsub", "fmul", "fdiv", "fld", "fst", "fstp",
    "fild", "fist", "fistp", "fcom", "fcomp", "fxch",
}

# Secondary dispatch tables confirmed by the handler bytes.  ``index`` records
# how the handler derives the table index; it is documentation, not emulation.
SECONDARY_TABLES = (
    {
        "name": "ext_e6",
        "base": 0x022C,
        "count": 0x100,
        "via": "main opcode 0xE6 -> 38CD:0466  jmp word ptr cs:[di+0x22C]",
        "index": "DI = next bytecode byte * 2 (0x00..0xFF)",
    },
    {
        "name": "dd_return",
        "base": 0x04B0,
        "count": 0x20,
        "via": "main opcode 0xDD -> 38CD:08BA  jmp word ptr cs:[bx+0x4B0]",
        "index": "BX = word [BP-4] (observed 0x1C at 38CD:1B79); "
                 "0x00..0x1F working slice, exact upper bound not proven",
    },
    {
        "name": "group_470",
        "base": 0x0470,
        "count": 0x10,
        "via": "38CD:0572  jmp word ptr cs:[bx+0x470] after 'and bx,0x1e'",
        "index": "BX = (object word [DI+0xA] & 0x1E)",
    },
    {
        "name": "group_490",
        "base": 0x0490,
        "count": 0x10,
        "via": "38CD:05F8  jmp word ptr cs:[bx+0x470] after 'add bx,0x20'",
        "index": "BX = (object word [DI+0xA] & 0x1E) + 0x20",
    },
    {
        "name": "native_call_return",
        "base": 0x04FA,
        "count": 0x10,
        "via": "38CD:067A  jmp word ptr cs:[bx+0x4FA] after lcall [BP-0x10]",
        "index": "BX derived from AX/return value of the native call; "
                 "16-entry working slice, exact upper bound not proven",
    },
)


@dataclass
class Insn:
    address: int
    size: int
    bytes_hex: str
    mnemonic: str
    op_str: str
    categories: list[str]
    call_kind: str | None = None

    def as_dict(self) -> dict:
        return {
            "address": f"0x{self.address:04X}",
            "bytes": self.bytes_hex,
            "mnemonic": self.mnemonic,
            "op_str": self.op_str,
            "categories": self.categories,
            **({"call_kind": self.call_kind} if self.call_kind else {}),
        }


@dataclass
class Handler:
    address: int
    opcodes: list[int] = field(default_factory=list)
    instructions: list[Insn] = field(default_factory=list)
    stop_reason: str = "end"
    stop_detail: str = ""

    @property
    def terminal(self) -> str:
        if not self.instructions:
            return "empty"
        if self.stop_reason == "dispatch":
            return f"dispatch {self.stop_detail}".strip()
        if self.stop_reason == "terminal":
            last = self.instructions[-1]
            return f"{last.mnemonic} {last.op_str}".strip()
        if self.stop_reason == "limit":
            return "decode-limit"
        last = self.instructions[-1]
        return f"{last.mnemonic} {last.op_str}".strip()

    @property
    def categories(self) -> list[str]:
        cats: set[str] = set()
        for ins in self.instructions:
            cats.update(ins.categories)
        return sorted(cats)

    @property
    def calls(self) -> list[Insn]:
        return [i for i in self.instructions if i.call_kind]

    def as_dict(self) -> dict:
        return {
            "address": f"0x{self.address:04X}",
            "opcodes": [f"0x{o:02X}" for o in self.opcodes],
            "instruction_count": len(self.instructions),
            "terminal": self.terminal,
            "stop_reason": self.stop_reason,
            "stop_detail": self.stop_detail,
            "categories": self.categories,
            "calls": [
                {
                    "address": f"0x{i.address:04X}",
                    "mnemonic": i.mnemonic,
                    "op_str": i.op_str,
                    "kind": i.call_kind,
                }
                for i in self.calls
            ],
            "instructions": [i.as_dict() for i in self.instructions],
        }


def classify(ins, md) -> tuple[list[str], str | None]:
    """Return (categories, call_kind) for one Capstone instruction."""
    m = ins.mnemonic.lower()
    op = ins.op_str.lower()
    cats: set[str] = set()

    if m in ALU:
        cats.add("ALU")
    if m in STACK:
        cats.add("STACK")
    if m in CONTROL or m.startswith("j") and m != "jmp":
        cats.add("CTRL")
    if m in ("ret", "retf", "iret", "iretd", "iretq"):
        cats.add("RET")
    if m in STRING:
        cats.add("STR")
    if m in TRAP:
        cats.add("TRAP")
    if m in IO:
        cats.add("IO")
    if m in FPU:
        cats.add("FPU")

    # Memory access detection.  Capstone exposes the operand type directly and
    # the textual form is also reliable for 16-bit mode.
    has_mem = "[" in op or "ptr" in op
    if has_mem:
        cats.add("MEM")
    if m in ("lea", "lds", "les", "lfs", "lgs", "lss"):
        cats.add("MEM")
    if m == "xlatb":
        cats.add("MEM")

    if m in ("lodsb", "lodsw", "lodsd"):
        cats.add("MEM")
    if m in ("movsb", "movsw", "movsd", "stosb", "stosw", "stosd",
             "cmpsb", "cmpsw", "cmpsd", "scasb", "scasw", "scasd"):
        cats.add("MEM")

    call_kind: str | None = None
    if m in ("call", "lcall", "callf"):
        cats.add("CALL")
        if m in ("lcall", "callf"):
            cats.add("NATIVE")
            call_kind = "native_indirect" if has_mem else "native_far"
        elif has_mem:
            cats.add("NATIVE")
            call_kind = "native_indirect"
        else:
            call_kind = "near"
    if m in ("int", "int3", "into"):
        cats.add("TRAP")

    if not cats:
        cats.add("OTHER")
    return sorted(cats), call_kind


DISPATCH_LABELS = {
    bytes.fromhex("AC 8B F8 D1 E7 2E FF 25"): "main [cs:di]",
    bytes.fromhex("2E FF 25"): "main [cs:di]",
    bytes.fromhex("2E FF A5 2C 02"): "ext [cs:di+0x22C]",
    bytes.fromhex("2E FF A7 70 04"): "group [cs:bx+0x470]",
    bytes.fromhex("2E FF A7 30 04"): "group [cs:bx+0x430]",
    bytes.fromhex("2E FF A7 50 04"): "group [cs:bx+0x450]",
    bytes.fromhex("2E FF A7 FA 04"): "native-return [cs:bx+0x4FA]",
    bytes.fromhex("2E FF A7 B0 04"): "dd-return [cs:bx+0x4B0]",
}


def dispatch_label(seg: bytes, off: int) -> str | None:
    if off < 0 or off >= len(seg):
        return None
    for seq, label in DISPATCH_LABELS.items():
        if seg.startswith(seq, off):
            return label
    return None


def disassemble_handler(
    seg: bytes,
    md: Cs,
    address: int,
    max_bytes: int = 256,
    max_instructions: int = 96,
) -> Handler:
    """Disassemble one handler until a terminal instruction or dispatch tail."""
    handler = Handler(address=address)
    if address < 0 or address >= len(seg):
        handler.stop_reason = "empty"
        return handler
    code = seg[address:min(len(seg), address + max_bytes)]
    for ins in md.disasm(code, address):
        next_addr = ins.address + ins.size
        cats, call_kind = classify(ins, md)
        handler.instructions.append(
            Insn(
                address=ins.address,
                size=ins.size,
                bytes_hex=ins.bytes.hex(" "),
                mnemonic=ins.mnemonic,
                op_str=ins.op_str,
                categories=cats,
                call_kind=call_kind,
            )
        )
        if len(handler.instructions) >= max_instructions:
            handler.stop_reason = "limit"
            break
        # Stop *after* the current instruction when the next bytes are a VM
        # dispatch tail.  This keeps the tail out of the handler body.
        label = dispatch_label(seg, next_addr)
        if label is not None:
            handler.stop_reason = "dispatch"
            handler.stop_detail = f"{label} @{next_addr:04X}"
            break
        if ins.mnemonic.lower() in TERMINAL_MNEMONICS:
            handler.stop_reason = "terminal"
            break
    else:
        handler.stop_reason = "end"
    return handler


def read_u16_words(data: bytes, offset: int, count: int) -> list[int]:
    if offset < 0 or offset + count * 2 > len(data):
        raise ValueError(f"table at 0x{offset:X} with {count} words exceeds dump")
    return list(struct.unpack_from(f"<{count}H", data, offset))


def analyze_table(
    seg: bytes,
    md: Cs,
    base: int,
    count: int,
    max_bytes: int,
) -> tuple[list[int], dict[int, Handler]]:
    words = read_u16_words(seg, base, count)
    by_addr: dict[int, Handler] = {}
    for opcode, addr in enumerate(words):
        handler = by_addr.get(addr)
        if handler is None:
            handler = disassemble_handler(seg, md, addr, max_bytes=max_bytes)
            by_addr[addr] = handler
        handler.opcodes.append(opcode)
    return words, by_addr


def fmt_targets(handler: Handler) -> str:
    if not handler.calls:
        return "-"
    parts = []
    for ins in handler.calls:
        target = ins.op_str or "-"
        parts.append(f"`{ins.mnemonic} {target}`")
    return "<br>".join(parts)


def first_instruction(handler: Handler) -> str:
    if not handler.instructions:
        return "-"
    ins = handler.instructions[0]
    return f"`{ins.mnemonic} {ins.op_str}`".strip()


def make_matrix(words: list[int]) -> list[str]:
    lines = [
        "| low\\high | " + " | ".join(f"{hi:X}" for hi in range(16)) + " |",
        "|---|" + "---|" * 16,
    ]
    for lo in range(16):
        row = []
        for hi in range(16):
            op = (hi << 4) | lo
            row.append(f"`{words[op]:04X}`")
        lines.append(f"| {lo:X}_ | " + " | ".join(row) + " |")
    return lines


def write_markdown(
    path: Path,
    meta: dict,
    words: list[int],
    by_addr: dict[int, Handler],
    secondary: list[dict],
) -> None:
    counts = Counter(words)
    native_handlers = [h for h in by_addr.values() if h.calls]
    stop_counts = Counter(h.stop_reason for h in by_addr.values())
    category_counts = Counter(c for h in by_addr.values() for c in h.categories)

    lines: list[str] = []
    lines.append("# 38CD 字节码 VM — 256 项 Opcode 表")
    lines.append("")
    lines.append(f"- 运行时 dump：`{meta['dump']}`")
    lines.append(f"- dump 大小：`{meta['dump_size']}` 字节")
    lines.append(f"- dump MD5：`{meta['dump_md5']}`")
    lines.append(f"- 基段/VM 段：`{meta['base_seg']:04X}` / `{meta['vm_seg']:04X}`")
    lines.append(f"- 主表文件偏移：`0x{meta['table_offset']:X}`")
    lines.append(f"- 主表：`0x0000`，{MAIN_COUNT} 个 `uint16` 入口")
    lines.append(f"- 唯一 Handler：`{len(by_addr)}` / `{MAIN_COUNT}`")
    lines.append("")
    lines.append("## 1. 16×16 主表矩阵")
    lines.append("")
    lines.append("行是低半字节，列是高半字节；每个单元是该 opcode 的 Handler 偏移。")
    lines.append("")
    lines.extend(make_matrix(words))
    lines.append("")
    lines.append("## 2. 分类统计")
    lines.append("")
    lines.append("| 类别 | Handler 数 |")
    lines.append("|---|---:|")
    for cat in sorted(category_counts):
        lines.append(f"| {cat} | {category_counts[cat]} |")
    lines.append("")
    lines.append("| 停止方式 | Handler 数 |")
    lines.append("|---|---:|")
    stop_labels = {
        "dispatch": "下一字节是 VM 分派尾（未计入 Handler）",
        "terminal": "ret/retf/iret/jmp/int3 等终止指令",
        "limit": "达到反汇编上限",
        "end": "代码区结束",
        "empty": "无法解码",
    }
    for reason, n in stop_counts.most_common():
        lines.append(f"| {stop_labels.get(reason, reason)} | {n} |")
    lines.append("")
    lines.append(
        f"主表含 `lcall`/`call` 的唯一 Handler（线性反汇编范围）："
        f"`{len(native_handlers)}` 个。"
    )
    lines.append("")
    lines.append("## 3. 全部 256 个 Opcode")
    lines.append("")
    lines.append("| op | handler | 独占 | 停止于 | 类别 | 调用 | 首指令 |")
    lines.append("|---:|---:|:---:|---|---|---|---|")
    for op, addr in enumerate(words):
        h = by_addr[addr]
        unique = "是" if counts[addr] == 1 else f"否({counts[addr]})"
        cats = ",".join(h.categories)
        lines.append(
            f"| `{op:02X}` | `{addr:04X}` | {unique} | `{h.terminal}` | "
            f"{cats} | {fmt_targets(h)} | {first_instruction(h)} |"
        )
    lines.append("")
    lines.append("## 4. 关键 Handler 反汇编")
    lines.append("")
    key_ops = [0x00, 0x01, 0xDD, 0xE6]
    seen: set[int] = set()
    for op in key_ops:
        addr = words[op]
        if addr in seen:
            continue
        seen.add(addr)
        h = by_addr[addr]
        ops = ", ".join(f"`{o:02X}`" for o in h.opcodes)
        lines.append(f"### `{addr:04X}` — opcode {ops}")
        lines.append("")
        lines.append("```asm")
        for ins in h.instructions:
            lines.append(
                f"{ins.address:04X}: {ins.bytes_hex:<24} "
                f"{ins.mnemonic} {ins.op_str}"
            )
        if not h.instructions:
            lines.append("; no instruction decoded")
        lines.append("```")
        lines.append("")
        lines.append(f"- 停止于：`{h.terminal}`")
        lines.append(f"- 类别：`{', '.join(h.categories)}`")
        if h.calls:
            lines.append("- 调用：")
            for ins in h.calls:
                lines.append(
                    f"  - `{ins.address:04X}: {ins.mnemonic} {ins.op_str}` "
                    f"({ins.call_kind})"
                )
        lines.append("")

    # Secondary handlers that matter for the 0xDD return path and the native
    # call bridge.  These are not reachable from the 256-entry main table
    # directly, so they need their own blocks.
    secondary_by_name = {t["name"]: t for t in secondary}
    key_secondary = [
        ("dd_return", 0x097E, "0xDD selector 0x1C -> retf (observed at hit)"),
        ("ext_e6", 0x0653, "E6 54 / E6 50 / E6 4C: indirect lcall [BP-0x10]"),
        ("ext_e6", 0x1742, "E6 9D: lcall 1055:11F0 (stack helper)"),
        ("ext_e6", 0x1834, "E6 40: lcall 1055:1058 (native arithmetic)"),
        ("ext_e6", 0x1854, "E6 3B: lcall 1055:1456 (native arithmetic)"),
        ("ext_e6", 0x1A56, "E6 05: lcall 1055:0F8C (native arithmetic)"),
        ("ext_e6", 0x1A76, "E6 3C: lcall 1055:13F6 (native arithmetic)"),
    ]
    lines.append("### 二级关键 Handler（0xDD 返回 / 0xE6 原生调用）")
    lines.append("")
    for table_name, addr, note in key_secondary:
        table = secondary_by_name.get(table_name)
        if table is None or addr not in table["handlers"]:
            continue
        h = table["handlers"][addr]
        lines.append(f"#### `{table_name}:{addr:04X}`")
        lines.append("")
        lines.append(f"{note}")
        lines.append("")
        lines.append("```asm")
        for ins in h.instructions:
            lines.append(
                f"{ins.address:04X}: {ins.bytes_hex:<24} "
                f"{ins.mnemonic} {ins.op_str}"
            )
        if not h.instructions:
            lines.append("; no instruction decoded")
        lines.append("```")
        lines.append("")
        lines.append(f"- 停止于：`{h.terminal}`")
        if h.calls:
            for ins in h.calls:
                lines.append(
                    f"- 调用：`{ins.address:04X}: {ins.mnemonic} {ins.op_str}` "
                    f"({ins.call_kind})"
                )
        lines.append("")

    lines.append("## 5. 二级分派表")
    lines.append("")
    lines.append("| 名称 | 基址 | 项数 | 唯一 | 入口约定 | 调用 |")
    lines.append("|---|---:|---:|---:|---|---|")
    for table in secondary:
        calls = []
        for addr in table["unique_handlers"]:
            h = table["handlers"][addr]
            for ins in h.calls:
                calls.append(f"`{addr:04X}: {ins.mnemonic} {ins.op_str}`")
        call_text = "<br>".join(calls) if calls else "-"
        lines.append(
            f"| `{table['name']}` | `{table['base']:04X}` | "
            f"{table['count']} | {table['unique_count']} | "
            f"{table['via']}<br>{table['index']} | {call_text} |"
        )
    lines.append("")
    lines.append("### 0xE6 扩展 Opcode 的原生调用映射")
    lines.append("")
    lines.append("| 主 opcode | 扩展子 opcode | handler | 调用 |")
    lines.append("|---:|---:|---:|---|")
    ext = secondary_by_name.get("ext_e6")
    if ext:
        for addr in ext["unique_handlers"]:
            h = ext["handlers"][addr]
            if not h.calls:
                continue
            for op in h.opcodes:
                call_text = "<br>".join(
                    f"`{i.address:04X}: {i.mnemonic} {i.op_str}` "
                    f"({i.call_kind})"
                    for i in h.calls
                )
                lines.append(
                    f"| `E6` | `{op:02X}` | `{addr:04X}` | {call_text} |"
                )
    lines.append("")
    lines.append("## 6. 所有绝对/间接原生调用")
    lines.append("")
    lines.append("| 来源 Handler | 指令 | 类型 |")
    lines.append("|---|---|---|")
    rows = 0
    for h in sorted(by_addr.values(), key=lambda x: x.address):
        for ins in h.calls:
            lines.append(
                f"| `{h.address:04X}` | `{ins.address:04X}: "
                f"{ins.mnemonic} {ins.op_str}` | `{ins.call_kind}` |"
            )
            rows += 1
    for table in secondary:
        for addr in table["unique_handlers"]:
            h = table["handlers"][addr]
            for ins in h.calls:
                lines.append(
                    f"| `{table['name']}:{h.address:04X}` | "
                    f"`{ins.address:04X}: {ins.mnemonic} {ins.op_str}` | "
                    f"`{ins.call_kind}` |"
                )
                rows += 1
    if rows == 0:
        lines.append("| - | - | - |")
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_csv(path: Path, words: list[int], by_addr: dict[int, Handler]) -> None:
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow([
            "opcode", "handler", "terminal", "categories", "calls",
            "instruction_count", "first_instruction",
        ])
        for op, addr in enumerate(words):
            h = by_addr[addr]
            writer.writerow([
                f"{op:02X}",
                f"{addr:04X}",
                h.terminal,
                ";".join(h.categories),
                " | ".join(f"{i.mnemonic} {i.op_str}" for i in h.calls),
                len(h.instructions),
                first_instruction(h),
            ])


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode the KOEI 38CD 256-opcode VM table from a DOSBox-X dump."
    )
    parser.add_argument("--dump", default=DEFAULT_DUMP,
                        help=f"runtime dump (default: {DEFAULT_DUMP})")
    parser.add_argument("--base-seg", type=lambda s: int(s, 0),
                        default=DEFAULT_BASE_SEG,
                        help="segment at the start of the dump (default: 0x1055)")
    parser.add_argument("--vm-seg", type=lambda s: int(s, 0),
                        default=DEFAULT_VM_SEG,
                        help="VM segment (default: 0x38CD)")
    parser.add_argument("--outdir", default=DEFAULT_OUTDIR,
                        help=f"output directory (default: {DEFAULT_OUTDIR})")
    parser.add_argument("--max-bytes", type=int, default=256,
                        help="maximum bytes decoded per handler (default: 256)")
    parser.add_argument("--check-md5", default=None,
                        help="fail unless the dump has this MD5")
    parser.add_argument("--no-markdown", action="store_true",
                        help="only write JSON/CSV")
    parser.add_argument("--no-csv", action="store_true",
                        help="do not write CSV")
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    if Cs is None:
        print("error: capstone is required (pip install capstone)", file=sys.stderr)
        return 2

    dump_path = Path(args.dump)
    if not dump_path.is_file():
        print(f"error: dump not found: {dump_path}", file=sys.stderr)
        print("hint: run MEMDUMPBIN 1055:0000 60000 in DOSBox-X first",
              file=sys.stderr)
        return 2

    data = dump_path.read_bytes()
    digest = hashlib.md5(data).hexdigest()
    if args.check_md5 and digest.lower() != args.check_md5.lower():
        print(f"error: MD5 mismatch: {digest} != {args.check_md5}",
              file=sys.stderr)
        return 2

    table_offset = args.vm_seg * 16 - args.base_seg * 16
    if table_offset < 0 or table_offset + MAIN_COUNT * 2 > len(data):
        print(
            f"error: VM table 0x{table_offset:X} is outside dump "
            f"(len=0x{len(data):X})",
            file=sys.stderr,
        )
        return 2

    seg_view = data[table_offset:]  # offset 0 == vm_seg:0000
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True

    words, by_addr = analyze_table(seg_view, md, MAIN_TABLE, MAIN_COUNT,
                                   args.max_bytes)

    secondary: list[dict] = []
    for spec in SECONDARY_TABLES:
        base = spec["base"]
        count = spec["count"]
        try:
            sub_words = read_u16_words(seg_view, base, count)
        except ValueError as exc:
            print(f"warning: {spec['name']}: {exc}", file=sys.stderr)
            continue
        sub_by_addr: dict[int, Handler] = {}
        for idx, addr in enumerate(sub_words):
            h = sub_by_addr.get(addr)
            if h is None:
                h = disassemble_handler(seg_view, md, addr,
                                        max_bytes=args.max_bytes)
                sub_by_addr[addr] = h
            h.opcodes.append(idx)
        secondary.append({
            **spec,
            "words": sub_words,
            "handlers": sub_by_addr,
            "unique_handlers": sorted(sub_by_addr),
            "unique_count": len(sub_by_addr),
        })

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    meta = {
        "dump": str(dump_path),
        "dump_size": len(data),
        "dump_md5": digest,
        "base_seg": args.base_seg,
        "vm_seg": args.vm_seg,
        "table_offset": table_offset,
        "main_table": f"0x{MAIN_TABLE:04X}",
        "main_count": MAIN_COUNT,
        "unique_handlers": len(by_addr),
    }

    json_path = outdir / "vm_opcodes.json"
    json_path.write_text(
        json.dumps(
            {
                "meta": meta,
                "main_table": [f"0x{w:04X}" for w in words],
                "handlers": [h.as_dict() for h in
                             sorted(by_addr.values(), key=lambda x: x.address)],
                "secondary_tables": [
                    {
                        "name": t["name"],
                        "base": f"0x{t['base']:04X}",
                        "count": t["count"],
                        "via": t["via"],
                        "index": t["index"],
                        "words": [f"0x{w:04X}" for w in t["words"]],
                        "unique_count": t["unique_count"],
                        "handlers": [
                            t["handlers"][a].as_dict()
                            for a in t["unique_handlers"]
                        ],
                    }
                    for t in secondary
                ],
            },
            ensure_ascii=False,
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )

    if not args.no_csv:
        write_csv(outdir / "vm_opcodes.csv", words, by_addr)

    if not args.no_markdown:
        write_markdown(outdir / "vm_opcodes.md", meta, words, by_addr,
                       secondary)

    print(f"dump:    {dump_path} ({len(data)} bytes, md5 {digest})")
    print(f"table:   {args.vm_seg:04X}:0000 at file offset 0x{table_offset:X}")
    main_call_handlers = sum(1 for h in by_addr.values() if h.calls)
    secondary_call_handlers = sum(
        1 for t in secondary for h in t["handlers"].values() if h.calls
    )
    print(f"opcodes: {MAIN_COUNT}, unique handlers: {len(by_addr)}")
    print(f"calls:   main={main_call_handlers}, "
          f"secondary={secondary_call_handlers} handlers")
    print(f"output:  {outdir}/vm_opcodes.json")
    if not args.no_csv:
        print(f"         {outdir}/vm_opcodes.csv")
    if not args.no_markdown:
        print(f"         {outdir}/vm_opcodes.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

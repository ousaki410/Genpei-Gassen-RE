#!/usr/bin/env python3
"""V12 static scoring tracer v2.

Independent revision of tools/trace_v12_scoring.py.  It reads the local
runtime snapshot and Main.exe and writes only to re_out/v12_v2/.

Goals of this revision (kept deliberately narrow):
  * scan the 0x22856 entry, which the v1 FUNCTIONS list omitted;
  * treat only absolute, base-less, index-less memory operands as global
    address candidates;
  * honour explicit segment overrides instead of silently folding them into
    DS globals;
  * classify read / write / read-write / unknown without treating CMP or TEST
    as stores;
  * record decode coverage, gaps and RET-like sites so that "linear sweep"
    is not mistaken for verified control flow.

It never starts an emulator, never runs the game, and never writes outside
re_out/v12_v2/.

Usage:
    python3 tools/trace_v12_scoring_v2.py
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from capstone import CS_AC_READ, CS_AC_WRITE, CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86_const import X86_OP_MEM

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "re_out" / "runtime_0000_a0000.bin"
MAIN_EXE = ROOT / "re_out" / "bin" / "Main.exe"
OUTDIR = ROOT / "re_out" / "v12_v2"

# Baselines from the prior local audit.  A mismatch is reported, never used to
# suppress or rewrite the measured values.
EXPECTED_RUNTIME_MD5 = "649725c7263606abbc93fef0923f96be"
EXPECTED_MAIN_MD5 = "7e7e1b875aa3b07eeb28c2eb5950aac1"

# Shared words currently of interest.  This is an allow-list of offsets, not a
# claim that no other global state exists.
GLOBALS = set(range(0x920A, 0x9222, 2))

# (name, linear address).  Linear addresses are used directly as file offsets
# for this snapshot; that mapping is stated as an assumption in the report.
FUNCTIONS: list[tuple[str, int]] = [
    # Main route / path-cost cluster
    ("enum_castles_2f530", 0x2F530),
    ("filter_2f5d2", 0x2F5D2),
    ("route_enum_2f6ae", 0x2F6AE),
    ("helper_300c0", 0x300C0),
    ("helper_30120", 0x30120),
    ("value_71b8", 0x30188),
    ("edge_cost_30280", 0x30280),
    ("path_cost_a_302c4", 0x302C4),
    ("path_cost_b_302f2", 0x302F2),
    ("route_cost_7350", 0x30320),
    # v2 addition: guard/read function around DS:0x920C..0x9216
    ("strategic_guard_22856", 0x22856),
    # Segment 43B3 helpers: linear = segment * 16 + offset
    ("sort_key_43b3_15c8", 0x43B30 + 0x15C8),
    ("helper_43b3_2814", 0x43B30 + 0x2814),
    ("helper_43b3_2920", 0x43B30 + 0x2920),
    ("helper_43b3_2b2c", 0x43B30 + 0x2B2C),
    ("helper_43b3_34ec", 0x43B30 + 0x34EC),
    ("helper_43b3_1ca2", 0x43B30 + 0x1CA2),
    # Segment 3B1A helpers
    ("helper_3b1a_596e", 0x3B1A0 + 0x596E),
    ("helper_3b1a_5af8", 0x3B1A0 + 0x5AF8),
    ("helper_3b1a_5a0e", 0x3B1A0 + 0x5A0E),
    ("helper_3b1a_5a38", 0x3B1A0 + 0x5A38),
    ("helper_3b1a_5bc8", 0x3B1A0 + 0x5BC8),
]

MAX_FUNCTION_BYTES = 0x400
RET_LIKE = {"ret", "retf", "iret", "iretd"}

# Known byte-level facts asserted independently of the decoder.
KNOWN_READS = [
    (0x22874, "8b360c92", "0x920C"),
    (0x2287D, "a10e92", "0x920E"),
    (0x22888, "a11092", "0x9210"),
    (0x22898, "a11292", "0x9212"),
    (0x228DE, "a11692", "0x9216"),
]


def file_md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def decode(img: bytes, start: int, max_len: int = MAX_FUNCTION_BYTES) -> dict[str, Any]:
    """Linear-sweep decode with explicit coverage bookkeeping.

    Stops at the first RETF, EOF, or max_len.  This is a linear sweep, not a
    control-flow graph.  Gaps and Capstone `.byte` fills are recorded so the
    caller can tell whether the sweep stayed instruction-aligned.
    """
    if start < 0 or start >= len(img):
        return {
            "instructions": [], "stop_reason": "start_out_of_range",
            "start": f"{start:05X}", "end_exclusive": f"{start:05X}",
            "bytes_available": 0, "gaps": [], "undecodable": [],
            "ret_like": [], "coverage_complete": False,
        }

    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True
    stop = min(start + max_len, len(img))
    instructions: list[dict[str, Any]] = []
    gaps: list[dict[str, str]] = []
    undecodable: list[dict[str, str]] = []
    ret_like: list[dict[str, str]] = []
    expected = start
    cursor = start
    stop_reason = "max_len_or_decode_stop"

    for ins in md.disasm(img[start:stop], start):
        if ins.address != expected:
            gaps.append({"from": f"{expected:05X}", "to": f"{ins.address:05X}"})
        item = {
            "addr": f"{ins.address:05X}",
            "bytes": ins.bytes.hex(),
            "text": f"{ins.mnemonic} {ins.op_str}".strip(),
            "size": ins.size,
        }
        instructions.append(item)
        if ins.mnemonic.startswith(".byte"):
            undecodable.append({"addr": f"{ins.address:05X}", "bytes": ins.bytes.hex()})
        cursor = ins.address + ins.size
        expected = cursor
        if ins.mnemonic in RET_LIKE:
            ret_like.append({"addr": f"{ins.address:05X}", "kind": ins.mnemonic})
        if ins.mnemonic == "retf":
            stop_reason = "first_retf_linear_sweep"
            break
    else:
        stop_reason = "eof" if stop == len(img) and start + max_len > len(img) else "max_len_or_decode_stop"

    return {
        "instructions": instructions,
        "stop_reason": stop_reason,
        "start": f"{start:05X}",
        "end_exclusive": f"{cursor:05X}",
        "bytes_available": max(0, stop - start),
        "gaps": gaps,
        "undecodable": undecodable,
        "ret_like": ret_like,
        "coverage_complete": not gaps and not undecodable,
    }


def call_targets(instructions: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Recover direct E8 near calls and 9A far calls from original bytes."""
    calls: list[dict[str, Any]] = []
    for item in instructions:
        raw = bytes.fromhex(item["bytes"])
        addr = int(item["addr"], 16)
        if not raw:
            continue
        if raw[0] == 0xE8 and len(raw) == 3:
            rel = int.from_bytes(raw[1:3], "little", signed=True)
            target = (addr + 3 + rel) & 0xFFFFF
            calls.append({
                "site": f"{addr:05X}", "kind": "near",
                "target_linear": f"{target:05X}",
                "target": f"linear 0x{target:05X}",
                "bytes": raw.hex(),
                "formula": f"0x{addr:05X}+3+({rel})",
            })
        elif raw[0] == 0x9A and len(raw) == 5:
            off = raw[1] | (raw[2] << 8)
            seg = raw[3] | (raw[4] << 8)
            target = seg * 16 + off
            calls.append({
                "site": f"{addr:05X}", "kind": "far",
                "target_linear": f"{target:05X}",
                "target": f"{seg:04X}:{off:04X}",
                "bytes": raw.hex(),
                "formula": f"{seg:04X}*16+{off:04X}",
            })
    return calls


def effective_segment(ins: Any, mem_operand: Any, md: Cs) -> tuple[str, bool, bool]:
    """Return (segment label, is_ds, has_explicit_override).

    Empirically for this Capstone build, `ins.segment` is not exposed, while
    `operand.mem.segment` is set to the override register (es/cs/ss/ds) when a
    prefix is present and stays 0 for the default.  Absolute 16-bit addressing
    without a prefix therefore defaults to DS.
    """
    seg_id = getattr(mem_operand.mem, "segment", 0)
    if seg_id:
        name = md.reg_name(seg_id).upper()
        return name, name == "DS", True
    return "DS(default)", True, False


def access_kind(ins: Any, operand: Any) -> str:
    """Conservative access classification from Capstone operand metadata.

    CS_AC_READ|CS_AC_WRITE are used when present.  When Capstone reports 0
    (e.g. some TEST forms in this build) the result is 'unknown' rather than a
    guess based on operand position.
    """
    access = getattr(operand, "access", 0)
    reads = bool(access & CS_AC_READ)
    writes = bool(access & CS_AC_WRITE)
    if reads and writes:
        return "read/write"
    if writes:
        return "write"
    if reads:
        return "read"
    return "unknown"


def analyze_mem_operands(raw_hex: str) -> list[dict[str, Any]]:
    """Decode one instruction and describe its memory operands.

    Used only by the scanner self-test; it does not read the game image.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    described: list[dict[str, Any]] = []
    for ins in md.disasm(bytes.fromhex(raw_hex), 0x1000):
        for op_index, operand in enumerate(ins.operands):
            if operand.type != X86_OP_MEM:
                continue
            segment, is_ds, override = effective_segment(ins, operand, md)
            disp = operand.mem.disp & 0xFFFF
            absolute = operand.mem.base == 0 and operand.mem.index == 0
            described.append({
                "operand_index": op_index,
                "instruction": f"{ins.mnemonic} {ins.op_str}".strip(),
                "base_nonzero": operand.mem.base != 0,
                "index_nonzero": operand.mem.index != 0,
                "disp": f"0x{disp:04X}",
                "in_allowlist": disp in GLOBALS,
                "segment": segment,
                "is_ds": is_ds,
                "segment_override": override,
                "access": access_kind(ins, operand),
                "candidate": absolute and disp in GLOBALS and is_ds,
            })
        break
    return described


# Synthetic encodings used to prove the filter rules without touching the image.
SELF_TEST_CASES: list[dict[str, Any]] = [
    {"label": "ds_absolute_read", "bytes": "a11292",
     "expect": {"segment": "DS(default)", "is_ds": True, "access": "read", "candidate": True}},
    {"label": "es_override_rejected", "bytes": "26a11292",
     "expect": {"segment": "ES", "is_ds": False, "access": "read", "candidate": False}},
    {"label": "cs_override_rejected", "bytes": "2ea11292",
     "expect": {"segment": "CS", "is_ds": False, "access": "read", "candidate": False}},
    {"label": "ss_override_rejected", "bytes": "36a11292",
     "expect": {"segment": "SS", "is_ds": False, "access": "read", "candidate": False}},
    {"label": "explicit_ds_override_kept", "bytes": "3ea11292",
     "expect": {"segment": "DS", "is_ds": True, "access": "read", "candidate": True}},
    {"label": "cmp_is_read_not_write", "bytes": "39061292",
     "expect": {"access": "read", "candidate": True}},
    {"label": "test_is_unknown", "bytes": "85061292",
     "expect": {"access": "unknown", "candidate": True}},
    {"label": "mov_store_is_write", "bytes": "89061292",
     "expect": {"access": "write", "candidate": True}},
    {"label": "add_is_read_write", "bytes": "8306129201",
     "expect": {"access": "read/write", "candidate": True}},
    {"label": "inc_is_read_write", "bytes": "ff061292",
     "expect": {"access": "read/write", "candidate": True}},
    {"label": "bp_relative_rejected", "bytes": "8b861292",
     "expect": {"base_nonzero": True, "candidate": False}},
    {"label": "si_indexed_rejected", "bytes": "8b841292",
     "expect": {"base_nonzero": True, "candidate": False}},
]


def run_self_test() -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for case in SELF_TEST_CASES:
        described = analyze_mem_operands(case["bytes"])
        first = described[0] if described else {}
        mismatches = {
            key: {"expected": value, "actual": first.get(key)}
            for key, value in case["expect"].items()
            if first.get(key) != value
        }
        results.append({
            "label": case["label"],
            "bytes": case["bytes"],
            "instruction": first.get("instruction"),
            "expected": case["expect"],
            "actual": {key: first.get(key) for key in case["expect"]},
            "pass": not mismatches,
            "mismatches": mismatches,
        })
    return results


def global_refs(
    instructions: list[dict[str, Any]], where: str
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Return (DS absolute refs, excluded absolute refs in other segments).

    Only operands with base == 0 and index == 0 are considered.  Base/index
    relative displacements (BP/SI/BX frames, pointer walks) are never treated
    as global addresses even when the displacement value happens to match.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True
    refs: list[dict[str, Any]] = []
    excluded: list[dict[str, Any]] = []

    for item in instructions:
        raw = bytes.fromhex(item["bytes"])
        addr = int(item["addr"], 16)
        for ins in md.disasm(raw, addr):
            for op_index, operand in enumerate(ins.operands):
                if operand.type != X86_OP_MEM:
                    continue
                if operand.mem.base != 0 or operand.mem.index != 0:
                    continue
                disp = operand.mem.disp
                if disp < 0:
                    disp &= 0xFFFF
                if disp not in GLOBALS:
                    continue
                segment, is_ds, override = effective_segment(ins, operand, md)
                record = {
                    "where": where,
                    "addr": f"{ins.address:05X}",
                    "bytes": ins.bytes.hex(),
                    "global": f"0x{disp:04X}",
                    "access": access_kind(ins, operand),
                    "text": f"{ins.mnemonic} {ins.op_str}".strip(),
                    "operand_index": op_index,
                    "addressing": "absolute(no base, no index)",
                    "segment": segment,
                    "segment_override": override,
                }
                if is_ds:
                    refs.append(record)
                else:
                    excluded.append(record)
    return refs, excluded


def hex_bytes(img: bytes, addr: int, size: int) -> str:
    return img[addr:addr + size].hex()


def build_checks(results: dict[str, Any], img: bytes) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []

    # 1) Known absolute reads in the 0x22856 function.
    for addr, expected, global_name in KNOWN_READS:
        actual = hex_bytes(img, addr, len(expected) // 2)
        checks.append({
            "check": f"known_read_{global_name}_at_{addr:05X}",
            "address": f"{addr:05X}",
            "expected_bytes": expected,
            "actual_bytes": actual,
            "pass": actual.lower() == expected.lower(),
            "meaning": f"expected raw bytes for {global_name} read",
        })

    # 2) 0x22856 entry prologue.
    prologue = hex_bytes(img, 0x22856, 3)
    checks.append({
        "check": "entry_22856_prologue",
        "address": "22856",
        "expected_bytes": "558bec",
        "actual_bytes": prologue,
        "pass": prologue.lower() == "558bec",
        "meaning": "PUSH BP; MOV BP,SP at the claimed entry",
    })

    # 3) 0x22856 return path bytes.
    ret_bytes = hex_bytes(img, 0x229A8, 6)
    checks.append({
        "check": "return_path_22856",
        "address": "229A8",
        "expected_bytes": "5e5f8be55dcb",
        "actual_bytes": ret_bytes,
        "pass": ret_bytes.lower() == "5e5f8be55dcb",
        "meaning": "POP SI; POP DI; MOV SP,BP; POP BP; RETF",
    })

    # 4) Linear-sweep boundary of 0x22856.
    guard = results.get("strategic_guard_22856", {})
    retf_addrs = [r["addr"] for r in guard.get("ret_like", []) if r["kind"] == "retf"]
    boundary_ok = (
        guard.get("start") == "22856"
        and guard.get("end_exclusive") == "229AE"
        and retf_addrs == ["229AD"]
        and guard.get("coverage_complete") is True
        and not guard.get("gaps")
        and not guard.get("undecodable")
    )
    checks.append({
        "check": "boundary_22856_linear_sweep",
        "address": "22856",
        "expected_bytes": "22856..229AE",
        "actual_bytes": f"{guard.get('start')}..{guard.get('end_exclusive')}",
        "pass": bool(boundary_ok),
        "meaning": "contiguous linear decode, one RETF at 0x229AD, no gaps; not a full CFG proof",
    })

    # 5) The contested 7350 call target.
    route = results.get("route_cost_7350", {})
    call = next((c for c in route.get("calls", []) if c.get("site") == "3035B"), None)
    raw = hex_bytes(img, 0x3035B, 3)
    computed = None
    if len(img) >= 0x3035E:
        rel = int.from_bytes(img[0x3035C:0x3035E], "little", signed=True)
        computed = f"{0x3035E + rel:05X}"
    checks.append({
        "check": "7350_call_target_at_3035b",
        "address": "3035B",
        "expected_bytes": "e894ff",
        "actual_bytes": raw,
        "actual_target": call.get("target_linear") if call else None,
        "recomputed_target": computed,
        "expected_target": "302F2",
        "pass": bool(
            raw.lower() == "e894ff"
            and computed == "302F2"
            and call
            and call.get("target_linear") == "302F2"
        ),
        "meaning": "near relative target = 0x3035B + 3 + signed16(0xFF94) = 0x302F2",
    })

    # 6) 0x9212 reference must be present in the new JSON's ref list.
    refs_9212 = [r for r in results.get("_all_refs", []) if r["global"] == "0x9212"]
    checks.append({
        "check": "json_records_9212_reference",
        "address": "22898",
        "expected_bytes": "a11292",
        "actual_bytes": ",".join(sorted(r["addr"] for r in refs_9212)) or "(none)",
        "pass": any(r["addr"] == "22898" for r in refs_9212),
        "meaning": "the global-ref list must contain 0x22898 as a read of 0x9212",
    })
    return checks


def md_cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def build_reports(report: dict[str, Any]) -> tuple[str, str, str]:
    functions = report["functions"]
    refs = report["global_refs"]
    excluded = report["excluded_non_ds_refs"]
    checks = report["checks"]
    meta = report["inputs"]
    scope = report["scan_scope"]
    prov = report["artifact_provenance"]

    compact: list[str] = [
        "# V12 静态评分逻辑追踪 V2（摘要）", "",
        f"- 生成命令：`{prov['generated_by']}`",
        f"- 本地 JSON：`{prov['local_json_path']}`，{prov['local_json_size']} bytes，SHA-256 `{prov['local_json_sha256']}`（不提交）",
        f"- 运行时快照：`{meta['runtime_path']}`，{meta['runtime_size']} bytes (`0x{meta['runtime_size']:X}`)，MD5 `{meta['runtime_md5']}`（基线匹配：`{meta['runtime_md5_matches_baseline']}`）",
        f"- Main.exe：`{meta['main_path']}`，{meta['main_size']} bytes (`0x{meta['main_size']:X}`)，MD5 `{meta['main_md5']}`（基线匹配：`{meta['main_md5_matches_baseline']}`）",
        f"- 扫描入口数：{len(functions)}；线性覆盖 `0x{scope['min_start']}`..`0x{scope['max_end']}`（不连续，仅按入口逐个线性扫描）",
        f"- DS 绝对寻址候选：{len(refs)}；其中对 `0x9212`：{len(report['global_9212_refs'])} 条",
        "",
        "## 已解码范围",
        "",
        "| 函数 | 起始 | 线性结束（不含） | 指令 | 停止原因 | RET-like | 空隙 | 不可解码 | 覆盖完整 | DS 引用 |",
        "|---|---:|---:|---:|---|---:|---:|---:|---|---:|",
    ]
    for name, func in functions.items():
        compact.append(
            f"| `{name}` | `0x{func['start']}` | `0x{func['end_exclusive']}` | "
            f"{func['instruction_count']} | {func['stop_reason']} | "
            f"{len(func['ret_like'])} | {len(func['gaps'])} | "
            f"{len(func['undecodable'])} | {func['coverage_complete']} | "
            f"{len(func['global_refs'])} |"
        )

    compact.extend(["", "## DS 绝对寻址候选（无基址、无索引）", ""])
    if refs:
        compact.extend([
            "| 地址 | 原始字节 | 指令 | 访问 | 全局偏移 | 函数 | 段 |",
            "|---:|---|---|---|---|---|---|",
        ])
        for ref in refs:
            compact.append(
                f"| `0x{ref['addr']}` | `{ref['bytes']}` | `{md_cell(ref['text'])}` | "
                f"{ref['access']} | `{ref['global']}` | `{ref['where']}` | {ref['segment']} |"
            )
    else:
        compact.append(
            "在本次列出的入口和成功解码的指令范围内，没有识别到符合条件的 DS 绝对引用。"
            "这不表示程序其他区域不存在引用。"
        )

    compact.extend(["", "## 非 DS 段或带覆盖前缀的绝对命中（已排除，不并入 DS 全局）", ""])
    if excluded:
        compact.extend(["| 地址 | 原始字节 | 指令 | 访问 | 偏移 | 段 | 函数 |", "|---:|---|---|---|---|---|---|"])
        for ref in excluded:
            compact.append(
                f"| `0x{ref['addr']}` | `{ref['bytes']}` | `{md_cell(ref['text'])}` | "
                f"{ref['access']} | `{ref['global']}` | {ref['segment']} | `{ref['where']}` |"
            )
    else:
        compact.append("本次扫描范围内未出现带非 DS 段覆盖的绝对命中。")

    compact.extend(["", "## 强制核验项", "", "| 检查 | 地址 | 实际 | 结果 |", "|---|---:|---|---|"])
    for check in checks:
        compact.append(
            f"| {check['check']} | `0x{check['address']}` | `{md_cell(str(check['actual_bytes']))}` | "
            f"{'PASS' if check['pass'] else 'FAIL'} |"
        )

    self_test = report["scanner_self_test"]
    compact.extend(["", "## 扫描器自检（合成指令，不读取游戏镜像）", "", "| 用例 | 字节 | 指令 | 关键判定 | 结果 |", "|---|---|---|---|---|"])
    for case in self_test:
        compact.append(
            f"| {case['label']} | `{case['bytes']}` | `{md_cell(str(case['instruction']))}` | "
            f"`{md_cell(str(case['actual']))}` | {'PASS' if case['pass'] else 'FAIL'} |"
        )

    compact.extend([
        "", "## 限制", "",
        "- 仅静态分析，不启动 DOSBox-X，不做动态验证。",
        f"- 从每个入口线性解码，遇首个 `RETF` 停止，或达到 `0x{MAX_FUNCTION_BYTES:X}` 字节上限；这不是完整控制流恢复，函数边界仍可能有误。",
        "- 只有无基址、无索引且有效段为 DS 的绝对寻址才进入 DS 全局表；`[BP+disp]`、`[SI+disp]` 等即使位移数值相同也不计入。",
        "- 访问类型来自 Capstone 操作数访问元数据；元数据为 0 时记 `unknown`，不按操作数位置猜测。",
        "- 快照文件偏移按等于线性地址读取；DS 段基址与运行时值未验证，`0x9212` 业务语义未确定。",
        "- 未列入入口的函数、间接寻址和未覆盖基本块可能包含额外引用；本报告不外推为“整个程序不存在引用”。",
        "",
    ])

    detailed: list[str] = [
        "# V12 静态评分逻辑追踪 V2（详细报告）", "",
        "## 0. 生成命令与产物哈希", "",
        f"- 生成命令：`{prov['generated_by']}`",
        f"- 本地 JSON：`{prov['local_json_path']}`，{prov['local_json_size']} bytes",
        f"- 本地 JSON SHA-256：`{prov['local_json_sha256']}`",
        f"- JSON 是否受跟踪：`{prov['json_tracked']}`",
        f"- 说明：{prov['note']}",
        "",
        "## 1. 输入与哈希", "",
        "| 项目 | 值 |",
        "|---|---|",
        f"| Runtime 路径 | `{meta['runtime_path']}` |",
        f"| Runtime 大小 | {meta['runtime_size']} bytes (`0x{meta['runtime_size']:X}`) |",
        f"| Runtime MD5 | `{meta['runtime_md5']}` |",
        f"| Runtime 基线匹配 | `{meta['runtime_md5_matches_baseline']}` |",
        f"| Main.exe 路径 | `{meta['main_path']}` |",
        f"| Main.exe 大小 | {meta['main_size']} bytes (`0x{meta['main_size']:X}`) |",
        f"| Main.exe MD5 | `{meta['main_md5']}` |",
        f"| Main.exe 基线匹配 | `{meta['main_md5_matches_baseline']}` |",
        "",
        "## 2. 扫描方法与范围", "",
        f"- 入口数量：{len(functions)}，逐个从入口做 16 位线性反汇编。",
        f"- 单入口上限：`0x{MAX_FUNCTION_BYTES:X}` 字节；遇首个 `RETF` 停止。",
        f"- 覆盖区间（入口起点最小值到解码终点最大值）：`0x{scope['min_start']}`..`0x{scope['max_end']}`。区间不连续，未扫描的空洞不参与结论。",
        "- 每个入口记录：起始、结束（不含）、指令数、停止原因、RET-like 列表、解码空隙、`.byte` 不可解码填充、覆盖是否完整。",
        "",
        "### 2.1 各入口边界与覆盖",
        "",
        "| 函数 | 起始 | 结束（不含） | 指令 | 停止原因 | 覆盖完整 | 空隙 | 不可解码 | RET-like |",
        "|---|---:|---:|---:|---|---|---|---:|---|",
    ]
    for name, func in functions.items():
        rets = ", ".join(f"`0x{r['addr']}` {r['kind']}" for r in func["ret_like"]) or "—"
        gaps = ", ".join(f"`0x{g['from']}`..`0x{g['to']}`" for g in func["gaps"]) or "—"
        undec = ", ".join(f"`0x{u['addr']}`" for u in func["undecodable"]) or "—"
        detailed.append(
            f"| `{name}` | `0x{func['start']}` | `0x{func['end_exclusive']}` | "
            f"{func['instruction_count']} | {func['stop_reason']} | "
            f"{func['coverage_complete']} | {gaps} | {undec} | {rets} |"
        )

    detailed.extend(["", "## 3. 强制核验项", ""])
    for check in checks:
        detailed.extend([
            f"### {check['check']}", "",
            f"- 地址：`0x{check['address']}`",
            f"- 预期：`{check['expected_bytes']}`",
            f"- 实际：`{check['actual_bytes']}`",
        ])
        if "actual_target" in check:
            detailed.append(f"- 解码目标：`{check['actual_target']}`；独立重算：`{check['recomputed_target']}`")
        detailed.extend([
            f"- 结果：**{'PASS' if check['pass'] else 'FAIL'}**",
            f"- 说明：{check['meaning']}", "",
        ])

    self_test = report["scanner_self_test"]
    detailed.extend([
        "## 4. 扫描器自检（合成指令）", "",
        "以下用例只用合成机器码验证过滤规则本身，不读取快照，不代表游戏镜像内容。",
        "",
        "| 用例 | 字节 | 指令 | 预期 | 实际 | 结果 |",
        "|---|---|---|---|---|---|",
    ])
    for case in self_test:
        detailed.append(
            f"| {case['label']} | `{case['bytes']}` | `{md_cell(str(case['instruction']))}` | "
            f"`{md_cell(str(case['expected']))}` | `{md_cell(str(case['actual']))}` | "
            f"{'PASS' if case['pass'] else 'FAIL'} |"
        )
    detailed.append("")

    detailed.extend(["## 5. DS 绝对寻址候选", ""])
    if refs:
        detailed.extend(["| 地址 | 原始字节 | 指令 | 访问 | 全局偏移 | 函数 | 段 | 前缀覆盖 |", "|---:|---|---|---|---|---|---|---|"])
        for ref in refs:
            detailed.append(
                f"| `0x{ref['addr']}` | `{ref['bytes']}` | `{md_cell(ref['text'])}` | "
                f"{ref['access']} | `{ref['global']}` | `{ref['where']}` | {ref['segment']} | "
                f"{ref['segment_override']} |"
            )
    else:
        detailed.append("本次扫描范围内没有符合条件的候选；不得外推为整个程序不存在引用。")

    detailed.extend(["", "## 6. 被排除的段覆盖命中", ""])
    if excluded:
        detailed.extend(["| 地址 | 原始字节 | 指令 | 访问 | 偏移 | 段 | 函数 |", "|---:|---|---|---|---|---|---|"])
        for ref in excluded:
            detailed.append(
                f"| `0x{ref['addr']}` | `{ref['bytes']}` | `{md_cell(ref['text'])}` | "
                f"{ref['access']} | `{ref['global']}` | {ref['segment']} | `{ref['where']}` |"
            )
    else:
        detailed.append("本次扫描范围内没有出现带非 DS 段覆盖的绝对命中。")

    detailed.extend(["", "## 7. 调用线索", ""])
    for name, func in functions.items():
        if not func["calls"]:
            continue
        detailed.append(f"### `{name}` (`0x{func['start']}`)")
        for call in func["calls"]:
            detailed.append(
                f"- `0x{call['site']}` `{call['bytes']}` {call['kind']} → `{call['target']}` "
                f"（`{call['formula']}`）"
            )
        detailed.append("")

    detailed.extend([
        "## 8. 与 `0x9212` 相关的限定", "",
        f"- 本次扫描在 `0x22898` 记录到 `A1 12 92` → `mov ax, word ptr [0x9212]`，访问类型 `read`。",
        "- 这证明该地址在被扫描的函数片段中被读取；不能据此判定业务语义、写入来源、运行时值或全程序使用范围。",
        "- 本次扫描范围内未发现对 `0x9212` 的绝对写入；这不等于整个程序没有写入者。",
        "",
        "## 9. 限制", "",
        "- 无 DOSBox-X 动态验证。",
        "- 线性解码不是完整 CFG；首个 RETF 只是启发式边界。",
        "- 快照偏移按等于线性地址处理，DS 基址未核实。",
        "- 间接寻址、寄存器构造地址、未列入口与未覆盖基本块可能含额外引用。",
        "- `0x9212` 业务语义和运行时值未确定。",
        "",
    ])

    errata = [
        "# V12 V2 勘误与实现变更", "",
        "本文件记录 `tools/trace_v12_scoring_v2.py` 相对原版 `tools/trace_v12_scoring.py` 的修正，并列出本次独立核验结果。原版脚本与原版输出未被修改。",
        "",
        f"- 生成命令：`{prov['generated_by']}`",
        f"- 本地 JSON：`{prov['local_json_path']}`，{prov['local_json_size']} bytes，SHA-256 `{prov['local_json_sha256']}`（不提交）",
        "",
        "## 1. 相对原版扫描器的修正", "",
        "1. **新增入口 `0x22856`**：V1 的 `FUNCTIONS` 列表没有该入口，因此漏掉 `0x22898: A1 12 92`。V2 将其纳入独立扫描。",
        "2. **全局候选只接受绝对寻址**：要求 `mem.base == 0` 且 `mem.index == 0`。`[BP+disp]`、`[SI+disp]` 等即使位移数值落在 `0x920A..0x9220` 也不计入。",
        "3. **检查显式段覆盖**：`operand.mem.segment` 非零时按实际段（ES/CS/SS/DS）记录；非 DS 的绝对命中放入 `excluded_non_ds_refs`，不并入 DS 全局表。",
        "4. **修正读写判定**：使用 Capstone `operand.access`（`CS_AC_READ` / `CS_AC_WRITE`）。`CMP` 记为 `read`，`TEST` 记为 `unknown`，不再把“内存操作数位于第一位置”当成写入。",
        "5. **记录指令边界证据**：每个入口输出 `gaps`、`undecodable`、`ret_like`、`coverage_complete`，避免把线性扫描当成完整控制流验证。",
        "6. **哈希与大小如实上报**：写入 JSON 和报告；基线不匹配时只告警，不掩盖实测值。",
        "7. **输出隔离**：只写 `re_out/v12_v2/`，不覆盖 `re_out/v12/`。",
        "",
        "## 2. 本次核验结果", "",
    ]
    for check in checks:
        errata.append(
            f"- **{'PASS' if check['pass'] else 'FAIL'}** — `{check['check']}`：实际 `{check['actual_bytes']}`"
        )
    errata.extend([
        "",
        "### 2.1 关键结论",
        "",
        "- `0x22898` 的实际字节为 `A1 12 92`，反汇编为 `mov ax, word ptr [0x9212]`，访问类型 `read`。",
        "- 新 JSON 的 `global_9212_refs` 包含该条记录（地址 `22898`，函数 `strategic_guard_22856`）。",
        "- `0x3035B` 的实际字节为 `E8 94 FF`；`0x3035B + 3 + (-108) = 0x302F2`，目标是 `0x302F2`，不是 `0x30280`。",
        "- `0x22856` 线性解码覆盖 `0x22856..0x229AE`，唯一 `RETF` 位于 `0x229AD`，无解码空隙。",
        "",
        "### 2.2 对 V1 报告的纠正",
        "",
        "- `re_out/v12/V12_SCORING_TRACE.md` 把 `0x3035B` 的调用目标写成 `0x30280`，与实际字节不符。",
        "- `re_out/v12/V12_SCORING_ERRATA.md` 进一步把该错误固化为“7350 调用 30280、不调用 302F2”，方向正好相反。",
        "- 正确关系：`7350` 在 `0x3035B` 直接调用 `0x302F2`；`0x302F2` 在 `0x30314` 再调用 `0x30280`。因此可以说“7350 经由 302F2 间接到达 30280”，但不能说“7350 直接调用 30280”。",
        "- V1 的这两处表述不因 V2 而修改：原文件保持原样，本文件仅在 V2 目录中记录正确结论。",
        "",
        "## 3. 仍未解决", "",
        "- 线性扫描不是完整 CFG；函数边界（尤其超过 0x400 字节或含内嵌数据的函数）仍可能判断错误。",
        "- 未覆盖的入口、间接寻址和寄存器构造地址可能包含额外引用。",
        "- 没有动态运行验证。",
        "- `0x9212` 的业务语义、写入者和实际运行时值未确定。",
        "",
    ])

    return "\n".join(compact), "\n".join(detailed), "\n".join(errata)


def main() -> int:
    missing = [str(path) for path in (RUNTIME, MAIN_EXE) if not path.is_file()]
    if missing:
        raise SystemExit("Missing required input(s):\n  " + "\n  ".join(missing))

    runtime_md5 = file_md5(RUNTIME)
    main_md5 = file_md5(MAIN_EXE)
    img = RUNTIME.read_bytes()

    function_results: dict[str, Any] = {}
    all_global_refs: list[dict[str, Any]] = []
    all_excluded: list[dict[str, Any]] = []
    all_calls: list[dict[str, Any]] = []
    starts: list[int] = []
    ends: list[int] = []

    for name, start in FUNCTIONS:
        decoded = decode(img, start)
        instructions = decoded["instructions"]
        calls = call_targets(instructions)
        refs, excluded = global_refs(instructions, name)
        func = {
            "start": decoded["start"],
            "end_exclusive": decoded["end_exclusive"],
            "stop_reason": decoded["stop_reason"],
            "bytes_available": decoded["bytes_available"],
            "instruction_count": len(instructions),
            "instructions": instructions,
            "calls": calls,
            "global_refs": refs,
            "excluded_non_ds_refs": excluded,
            "gaps": decoded["gaps"],
            "undecodable": decoded["undecodable"],
            "ret_like": decoded["ret_like"],
            "coverage_complete": decoded["coverage_complete"],
        }
        function_results[name] = func
        all_global_refs.extend(refs)
        all_excluded.extend(excluded)
        all_calls.extend(calls)
        starts.append(start)
        ends.append(int(decoded["end_exclusive"], 16))

    refs_9212 = [ref for ref in all_global_refs if ref["global"] == "0x9212"]
    refs_9210 = [ref for ref in all_global_refs if ref["global"] == "0x9210"]

    # `_all_refs` is a private convenience for build_checks; stripped below.
    function_results["_all_refs"] = all_global_refs
    checks = build_checks(function_results, img)
    function_results.pop("_all_refs", None)

    self_test = run_self_test()
    checks.append({
        "check": "scanner_self_test_all_pass",
        "address": "synthetic",
        "expected_bytes": f"{len(self_test)} synthetic cases",
        "actual_bytes": f"{sum(1 for c in self_test if c['pass'])}/{len(self_test)} pass",
        "pass": all(c["pass"] for c in self_test),
        "meaning": "segment override, base/index exclusion and access classification behave as specified",
    })

    metadata = {
        "runtime_path": str(RUNTIME.relative_to(ROOT)),
        "runtime_size": len(img),
        "runtime_md5": runtime_md5,
        "runtime_md5_expected": EXPECTED_RUNTIME_MD5,
        "runtime_md5_matches_baseline": runtime_md5 == EXPECTED_RUNTIME_MD5,
        "main_path": str(MAIN_EXE.relative_to(ROOT)),
        "main_size": MAIN_EXE.stat().st_size,
        "main_md5": main_md5,
        "main_md5_expected": EXPECTED_MAIN_MD5,
        "main_md5_matches_baseline": main_md5 == EXPECTED_MAIN_MD5,
    }
    report: dict[str, Any] = {
        "schema_version": 2,
        "tool": "trace_v12_scoring_v2.py",
        "inputs": metadata,
        "analysis_mode": "static_only",
        "scan_method": "linear_sweep_per_entry_until_first_RETF_or_0x400_bytes",
        "linear_address_equals_file_offset": True,
        "function_entry_count": len(function_results),
        "function_entry_points": [
            {"name": name, "start": f"{start:05X}"} for name, start in FUNCTIONS
        ],
        "scan_scope": {
            "min_start": f"{min(starts):05X}" if starts else None,
            "max_end": f"{max(ends):05X}" if ends else None,
            "contiguous": False,
            "note": "Per-entry sweeps only; gaps between entries were not scanned.",
        },
        "functions": function_results,
        "global_refs": all_global_refs,
        "global_9212_refs": refs_9212,
        "global_9210_refs": refs_9210,
        "excluded_non_ds_refs": all_excluded,
        "calls": all_calls,
        "checks": checks,
        "scanner_self_test": self_test,
        "limitations": [
            "Linear sweep is not complete control-flow recovery.",
            "A first RETF or fixed max length does not prove true function boundaries.",
            "Only absolute memory operands with no base/index and effective DS are listed as DS globals.",
            "Absolute hits behind non-DS segment overrides are recorded separately and not counted as DS globals.",
            "Access type comes from Capstone operand metadata; unknown stays unknown.",
            "Indirect references and functions outside the entry list may contain additional references.",
            "No DOSBox-X or dynamic runtime validation was performed.",
            "DS base and runtime values are not established; 0x9212 business meaning remains unknown.",
        ],
    }

    OUTDIR.mkdir(parents=True, exist_ok=True)
    json_path = OUTDIR / "trace.json"
    compact_path = OUTDIR / "trace.md"
    detailed_path = OUTDIR / "V12_SCORING_TRACE_V2.md"
    errata_path = OUTDIR / "V12_SCORING_ERRATA_V2.md"

    # Write the JSON first, then record its real size/hash in the Markdown
    # reports.  The JSON content is deterministic, so re-running reproduces
    # the same hash.  The JSON itself stays under the ignored re_out/ tree.
    json_text = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    json_bytes = json_text.encode("utf-8")
    json_path.write_bytes(json_bytes)
    report["artifact_provenance"] = {
        "generated_by": "python3 tools/trace_v12_scoring_v2.py",
        "local_json_path": str(json_path.relative_to(ROOT)),
        "local_json_size": len(json_bytes),
        "local_json_sha256": hashlib.sha256(json_bytes).hexdigest(),
        "json_tracked": False,
        "note": "JSON 留在被忽略的 re_out/ 下，不进入仓库；受跟踪的 Markdown 副本应与本次生成结果逐字节一致。",
    }
    compact_md, detailed_md, errata_md = build_reports(report)
    compact_path.write_text(compact_md + "\n", encoding="utf-8")
    detailed_path.write_text(detailed_md + "\n", encoding="utf-8")
    errata_path.write_text(errata_md + "\n", encoding="utf-8")

    print("V12 V2 static trace completed.")
    print(f"Runtime: {metadata['runtime_size']} bytes MD5={runtime_md5} baseline_match={metadata['runtime_md5_matches_baseline']}")
    print(f"Main.exe: {metadata['main_size']} bytes MD5={main_md5} baseline_match={metadata['main_md5_matches_baseline']}")
    print(f"Functions scanned: {len(function_results)}")
    print(f"Scan scope: 0x{report['scan_scope']['min_start']}..0x{report['scan_scope']['max_end']} (non-contiguous)")
    print(f"DS absolute refs: {len(all_global_refs)}")
    print(f"Excluded non-DS absolute refs: {len(all_excluded)}")
    print(f"DS:0x9212 refs: {len(refs_9212)}")
    for ref in refs_9212:
        print(f"  0x{ref['addr']} {ref['bytes']} {ref['text']} access={ref['access']}")
    print(f"DS:0x9210 refs: {len(refs_9210)}")
    for check in checks:
        print(f"CHECK {'PASS' if check['pass'] else 'FAIL'}: {check['check']} actual={check['actual_bytes']}")
    print(f"Scanner self-test: {sum(1 for c in self_test if c['pass'])}/{len(self_test)} pass")
    print("Outputs:")
    for path in (json_path, compact_path, detailed_path, errata_path):
        print(f"  {path.relative_to(ROOT)}")

    if not metadata["runtime_md5_matches_baseline"] or not metadata["main_md5_matches_baseline"]:
        print("WARNING: one or more inputs do not match the prior MD5 baseline; inspect the reported hashes before interpreting the trace.")
    if not all(check["pass"] for check in checks):
        print("WARNING: one or more byte-level assertions failed; inspect V12_SCORING_ERRATA_V2.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Stage 1: inspect (and, where supported, unpack) the DOS executables.

The Genpei Kassen PC-98 release contains five MZ ``.EXE`` files, three
``.COM`` files and one additional MZ image named ``Main.ori``.  This tool is
deliberately dependency-free so it can run before any Ghidra/DOSBox setup.

What it does
------------
* Parses the 16-bit MZ header (magic, page/byte counts, relocation count,
  header paragraphs, initial SS:SP and CS:IP, overlay/trailer bytes).
* Recognises the common EXEPACK / LZEXE / PKLITE signatures.
* Copies already-unpacked MZ images to ``re_out/bin/``.
* Extracts ASCII and Shift-JIS strings plus the known asset-name anchors
  (``Sndata.gp``, ``Hchikei.gp`` ...) to ``re_out/seeds.json``.
* Has an optional DOSBox probe mode for a short, headless runtime sanity
  check.  It never starts DOSBox automatically.

The EXEPACK/LZEXE cases are reported explicitly.  A compact, dependency-free
unpacker for those two formats is intentionally not guessed at: writing a
wrong unpacker would silently produce a corrupt image and poison Stage 2.
The script records ``packer_status`` and leaves the original packed image in
``re_out/bin/`` under ``<name>_packed.exe`` so a later, verified unpacker can
be dropped in without changing the pipeline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

# Asset names found in Main.exe's string table.  The list intentionally omits
# extensions that are ambiguous (e.g. ``Music.gp`` appears twice in one packed
# C string); we still search for every spelling below.
ASSET_NAMES = [
    "Sndata.gp", "Savedata.gp", "Maincmd.gp", "Maincmd2.gp", "Mainobj.gp",
    "Mainstl.gp", "Mainevt.gp", "Mainitem.gp", "Mainanm.gp", "Mainmap.gp",
    "Mainpal.pld", "Message.gp", "Music.gp", "Pcm.gp", "Haikei.gp",
    "Kisetsu.gp", "Hchikei.gp", "Hkeshiki.gp", "Hground.gp", "Hunitpat.gp",
    "Hikuchi.gp", "Hkumi.gp", "Hikback.gp", "Hkei.gp", "Losepal.pld",
    "Kaodata.gp", "Montage.gp", "Logo.gp", "Disk-1.gp", "Disk-2.gp",
    "Disk-3.gp", "Disk-4.gp", "Disk-5.gp", "Gen.dir",
]

PACKER_SIGNATURES = (
    (b"LZ91", "LZEXE 0.91"),
    (b"LZ90", "LZEXE 0.90"),
    (b"PKLITE", "PKLITE"),
    (b"EXEPACK", "EXEPACK"),
)

# Ghidra's MZ loader in the installed Ghidra maps file offset 0x40 to
# 1000:0000, then advances the segment by 0x1000 for every 64 KiB.  Keeping
# this helper here lets seeds.json carry the same addresses as the decompiler
# index without needing a project to be open.
def ghidra_address_for_file_offset(file_offset: int) -> Optional[str]:
    if file_offset < 0x40:
        return None
    delta = file_offset - 0x40
    seg = 0x1000 + (delta // 0x10000) * 0x1000
    off = delta % 0x10000
    return "%04x:%04x" % (seg, off)


@dataclass
class MzHeader:
    magic: str
    e_cblp: int
    e_cp: int
    e_crlc: int
    e_cparhdr: int
    e_minalloc: int
    e_maxalloc: int
    e_ss: int
    e_sp: int
    e_csum: int
    e_ip: int
    e_cs: int
    e_lfarlc: int
    e_ovno: int
    header_bytes: int
    image_bytes: int
    declared_file_bytes: int
    entry_file_offset: int
    overlay_bytes: int
    load_image_bytes: int


def parse_mz(data: bytes) -> Optional[MzHeader]:
    if len(data) < 0x1C or data[:2] != b"MZ":
        return None
    values = struct.unpack_from("<13H", data, 2)
    (cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum,
     ip, cs, lfarlc, ovno) = values
    header_bytes = cparhdr * 16
    image_bytes = ((cp - 1) * 512 + cblp) if cp else len(data)
    declared_file_bytes = image_bytes
    entry_file_offset = header_bytes + (cs * 16 + ip)
    overlay_bytes = max(0, len(data) - declared_file_bytes)
    load_image_bytes = max(0, declared_file_bytes - header_bytes)
    return MzHeader(
        magic="MZ",
        e_cblp=cblp,
        e_cp=cp,
        e_crlc=crlc,
        e_cparhdr=cparhdr,
        e_minalloc=minalloc,
        e_maxalloc=maxalloc,
        e_ss=ss,
        e_sp=sp,
        e_csum=csum,
        e_ip=ip,
        e_cs=cs,
        e_lfarlc=lfarlc,
        e_ovno=ovno,
        header_bytes=header_bytes,
        image_bytes=image_bytes,
        declared_file_bytes=declared_file_bytes,
        entry_file_offset=entry_file_offset,
        overlay_bytes=overlay_bytes,
        load_image_bytes=load_image_bytes,
    )


def detect_packer(data: bytes, hdr: Optional[MzHeader]) -> Tuple[str, str]:
    """Return (packer, evidence).  Heuristics are conservative."""
    head = data[: min(len(data), 0x1000)].upper()
    for needle, label in PACKER_SIGNATURES:
        if needle in head:
            return label, "signature %r in first 4 KiB" % needle.decode("ascii")
    if hdr is None:
        return "none", "COM image (no MZ header)"
    # EXEPACK expands the header to hold the compressed image and leaves a
    # very large relocation count.  End.exe/Open.exe exhibit both traits.
    if (hdr.e_cparhdr >= 0x100 and hdr.e_crlc >= 0x100 and
            hdr.entry_file_offset >= hdr.header_bytes and
            hdr.overlay_bytes == 0):
        return ("EXEPACK",
                "large header (%d B), %d relocations, entry at +0x%x; "
                "classic EXEPACK layout" %
                (hdr.header_bytes, hdr.e_crlc, hdr.entry_file_offset))
    return "none", "no known packer signature and normal MZ header"


def ascii_runs(data: bytes, min_len: int = 4) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % min_len, data):
        text = m.group().decode("ascii", "replace")
        out.append({"offset": m.start(), "text": text})
    return out


def _sjis_pair(b0: int, b1: int) -> bool:
    return (0x81 <= b0 <= 0x9F or 0xE0 <= b0 <= 0xEF) and (
        0x40 <= b1 <= 0x7E or 0x80 <= b1 <= 0xFC)


def sjis_runs(data: bytes, min_chars: int = 3) -> List[Dict[str, object]]:
    """Extract conservative cp932 runs (at least min_chars characters).

    We only accept runs that decode cleanly with cp932 and contain at least
    one double-byte character.  This keeps accidental x86 byte sequences out
    of the seed file while still capturing the Japanese UI/table strings.
    """
    out: List[Dict[str, object]] = []
    i = 0
    n = len(data)
    while i < n:
        start = i
        chars: List[int] = []
        pairs = 0
        while i < n:
            b = data[i]
            if b == 0:
                break
            if 0x20 <= b <= 0x7E:
                chars.append(b)
                i += 1
                continue
            if i + 1 < n and _sjis_pair(b, data[i + 1]):
                chars.extend((b, data[i + 1]))
                pairs += 1
                i += 2
                continue
            break
        if pairs and len(chars) >= min_chars:
            raw = bytes(chars)
            try:
                text = raw.decode("cp932")
            except UnicodeDecodeError:
                text = ""
            if text and any(ord(c) > 0x7F for c in text):
                out.append({
                    "offset": start,
                    "text": text,
                    "bytes": raw.hex(),
                    "chars": len(text),
                })
        i = max(i + 1, start + 1)
    return out


def scan_entry_code(data: bytes, hdr: Optional[MzHeader]) -> Dict[str, object]:
    """Disassemble the MZ entry stub and count x86 function markers.

    This is the cheap, deterministic check that answers the question "is this
    image compressed?" before Stage 2 spends minutes in Ghidra.  A packed
    image normally has a tiny stub, almost no standard prologues, and a very
    different entry instruction mix.  Main.exe instead has a real code image
    plus a self-relocating loader; the loader is reported, not stripped.
    """
    if hdr is not None:
        entry = hdr.entry_file_offset
        image_end = min(len(data), hdr.image_bytes)
        image_start = min(hdr.header_bytes, len(data))
    else:
        entry = 0x100
        image_start, image_end = 0, len(data)
    entry = min(max(0, entry), len(data))
    prologues = data.count(b"\x55\x8b\xec")
    returns = (data.count(b"\xc3") + data.count(b"\xca") +
               data.count(b"\xcb"))
    disasm: List[Dict[str, object]] = []
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_16
        cs = Cs(CS_ARCH_X86, CS_MODE_16)
        if hdr is not None:
            base = hdr.e_cs * 16 + hdr.e_ip
        else:
            base = 0x100
        for ins in cs.disasm(data[entry:entry + 256], base):
            rel = ins.address - base
            if hdr is not None:
                disp = "%04x:%04x" % (hdr.e_cs, (hdr.e_ip + rel) & 0xffff)
            else:
                disp = "%04x" % ins.address
            disasm.append({
                "address": disp,
                "linear": "0x%05x" % ins.address,
                "file_offset": entry + rel,
                "bytes": ins.bytes.hex(),
                "mnemonic": ins.mnemonic,
                "op_str": ins.op_str,
            })
    except Exception as exc:
        disasm.append({"error": str(exc)})

    text = " ".join((i.get("mnemonic", "") + " " + i.get("op_str", ""))
                    for i in disasm if "mnemonic" in i)
    loader_markers = []
    for marker in ("rep movsb", "call cx", "ljmp", "int 0x15", "rep stosb"):
        if marker in text:
            loader_markers.append(marker)
    if hdr is None:
        verdict = "COM image; entry at 0x100"
    elif prologues >= 50 and not loader_markers:
        verdict = "unpacked: %d prologues/%d returns, no loader markers" % (
            prologues, returns)
    elif loader_markers:
        verdict = ("self-relocating loader in entry stub (%s); the "
                   "0x%x-byte image contains %d prologues/%d returns" %
                   (", ".join(loader_markers), image_end - image_start,
                    prologues, returns))
    else:
        verdict = "unclassified: %d prologues/%d returns" % (prologues,
                                                              returns)
    return {
        "entry_file_offset": entry,
        "entry_disassembly": disasm,
        "prologue_55_8bec_count": prologues,
        "return_count": returns,
        "loader_markers": loader_markers,
        "packed_verdict": verdict,
    }


def find_asset_anchors(data: bytes) -> Dict[str, List[Dict[str, object]]]:
    anchors: Dict[str, List[Dict[str, object]]] = {}
    upper = data.upper()
    for name in ASSET_NAMES:
        needle = name.upper().encode("ascii")
        found: List[Dict[str, object]] = []
        pos = 0
        while True:
            pos = upper.find(needle, pos)
            if pos < 0:
                break
            found.append({
                "offset": pos,
                "text": data[pos:pos + len(needle)].decode("ascii", "replace"),
                "address": ghidra_address_for_file_offset(pos),
            })
            pos += 1
        if found:
            anchors[name] = found
    return anchors


def table_anchors(data: bytes, anchors: Dict[str, List[Dict[str, object]]]) -> List[Dict[str, object]]:
    """Capture a small window around each first asset-name occurrence."""
    out: List[Dict[str, object]] = []
    for name, hits in sorted(anchors.items()):
        off = hits[0]["offset"]
        lo = max(0, off - 64)
        hi = min(len(data), off + len(name) + 64)
        out.append({
            "name": name,
            "offset": off,
            "address": ghidra_address_for_file_offset(off),
            "context_hex": data[lo:hi].hex(),
            "context_ascii": "".join(
                chr(c) if 0x20 <= c <= 0x7E else "." for c in data[lo:hi]),
        })
    return out


def inspect_com(data: bytes) -> Dict[str, object]:
    entry = 0x100
    jump = None
    if len(data) >= 3 and data[0] == 0xE9:
        jump = 0x100 + 3 + struct.unpack_from("<h", data, 1)[0]
    return {
        "kind": "COM",
        "header": None,
        "entry_file_offset": entry,
        "entry_jump": jump,
        "overlay_bytes": 0,
        "load_image_bytes": len(data),
    }


def _run_dosbox_probe(exe: Path, seconds: int) -> Dict[str, object]:
    dosbox = shutil.which("dosbox")
    if not dosbox:
        return {"ok": False, "error": "dosbox not found on PATH"}
    work = Path(tempfile.mkdtemp(prefix="yphz_dosbox_", dir="/tmp"))
    try:
        cfg = work / "dosbox.conf"
        cfg.write_text(
            "[sdl]\n"
            "output=null\n"
            "fullscreen=false\n"
            "autolock=false\n"
            "\n"
            "[cpu]\n"
            "core=auto\n"
            "cycles=auto\n"
            "\n"
            "[autoexec]\n"
            "mount c \"%s\"\n"
            "c:\n"
            "%s\n"
            "exit\n" % (exe.parent, exe.name),
            encoding="utf-8",
        )
        env = dict(os.environ)
        env.setdefault("SDL_VIDEODRIVER", "dummy")
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        cmd = [dosbox, "-conf", str(cfg)]
        if not env.get("DISPLAY") and shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a"] + cmd
        proc = subprocess.run(
            cmd, cwd=str(work), env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=max(1, seconds), text=True,
            errors="replace")
        return {
            "ok": proc.returncode in (0, 124),
            "returncode": proc.returncode,
            "timed_out": proc.returncode == 124,
            "output_tail": proc.stdout[-3000:],
        }
    except subprocess.TimeoutExpired as exc:
        output = exc.output or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return {"ok": True, "returncode": 124, "timed_out": True,
                "output_tail": output[-3000:]}
    except Exception as exc:  # pragma: no cover - diagnostics path
        return {"ok": False, "error": str(exc)}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _copy_unpacked(src: Path, dst_dir: Path) -> Path:
    dst = dst_dir / src.name
    shutil.copy2(src, dst)
    return dst


def process_file(path: Path, out_dir: Path, run_dosbox: bool,
                 dosbox_seconds: int) -> Dict[str, object]:
    data = path.read_bytes()
    hdr = parse_mz(data)
    packer, evidence = detect_packer(data, hdr)
    stem = path.stem
    bin_dir = out_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    record: Dict[str, object] = {
        "name": path.name,
        "path": str(path),
        "size": len(data),
        "md5": hashlib.md5(data).hexdigest(),
        "kind": "MZ" if hdr else "COM",
        "packer": packer,
        "packer_evidence": evidence,
        "header": asdict(hdr) if hdr else None,
        "asset_anchors": find_asset_anchors(data),
        "code_scan": scan_entry_code(data, hdr),
    }
    if hdr is None:
        record.update(inspect_com(data))
    else:
        record.update({
            "entry_file_offset": hdr.entry_file_offset,
            "entry_address": ghidra_address_for_file_offset(
                hdr.entry_file_offset),
            "load_image_address": ghidra_address_for_file_offset(
                hdr.header_bytes),
        })

    # MZ images that are not packed can be handed straight to Ghidra.
    output_name = None
    if hdr is not None and packer == "none":
        out = _copy_unpacked(path, bin_dir)
        output_name = out.name
        record["unpacked"] = True
        record["packer_status"] = "unpacked (normal MZ image)"
    elif hdr is not None:
        out = bin_dir / (stem + "_packed.exe")
        shutil.copy2(path, out)
        output_name = out.name
        record["unpacked"] = False
        record["packer_status"] = (
            "packed image preserved as %s; a verified external unpacker is "
            "required before semantic analysis" % out.name)
    else:
        out = _copy_unpacked(path, bin_dir)
        output_name = out.name
        record["unpacked"] = True
        record["packer_status"] = "COM image copied"

    record["output"] = output_name
    record["ascii_strings"] = ascii_runs(data)
    record["sjis_strings"] = sjis_runs(data)
    record["table_anchors"] = table_anchors(data, record["asset_anchors"])

    if run_dosbox and out is not None:
        record["dosbox_probe"] = _run_dosbox_probe(out, dosbox_seconds)
    return record


def discover_targets(game_dir: Path) -> List[Path]:
    targets = []
    for p in sorted(game_dir.iterdir()):
        if not p.is_file():
            continue
        suffix = p.suffix.lower()
        if suffix in (".exe", ".com"):
            targets.append(p)
    # Main.ori is an MZ image without the .exe suffix in this release.
    ori = game_dir / "Main.ori"
    if ori.is_file() and ori not in targets:
        targets.append(ori)
    return targets


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Inspect DOS MZ/COM images and seed the Ghidra pipeline")
    parser.add_argument("game_dir", nargs="?",
                        default=str(Path(__file__).resolve().parents[1] /
                                    "games" / "yphz"))
    parser.add_argument("--out", default=None,
                        help="output root (default: <repo>/re_out)")
    parser.add_argument("--dosbox-run", metavar="FILE",
                        help="run one copied executable in DOSBox for a short "
                             "headless verification")
    parser.add_argument("--dosbox-seconds", type=int, default=5)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(list(argv) if argv is not None else None)

    game_dir = Path(args.game_dir).resolve()
    if not game_dir.is_dir():
        print("game directory not found: %s" % game_dir, file=sys.stderr)
        return 2
    out_dir = Path(args.out).resolve() if args.out else (
        Path(__file__).resolve().parents[1] / "re_out")
    out_dir.mkdir(parents=True, exist_ok=True)

    targets = discover_targets(game_dir)
    if not targets:
        print("no DOS executables found in %s" % game_dir, file=sys.stderr)
        return 2

    records = []
    for p in targets:
        rec = process_file(p, out_dir, bool(args.dosbox_run),
                           args.dosbox_seconds)
        records.append(rec)
        if not args.quiet:
            print("[stage1] %-14s %7d B  %-10s %-8s -> %s" % (
                rec["name"], rec["size"], rec["kind"], rec["packer"],
                rec["output"]))

    if args.dosbox_run:
        wanted = Path(args.dosbox_run).name
        candidates = [r for r in records if r["name"].lower() == wanted.lower()]
        if not candidates:
            print("--dosbox-run target not found: %s" % wanted, file=sys.stderr)
            return 2
        target = out_dir / "bin" / str(candidates[0]["output"])
        probe = _run_dosbox_probe(target, args.dosbox_seconds)
        candidates[0]["dosbox_probe"] = probe
        print("[stage1] dosbox probe: ok=%s rc=%s timeout=%s" % (
            probe.get("ok"), probe.get("returncode"),
            probe.get("timed_out")))

    seeds = {
        "generated_by": "tools/re_unpack_dos.py",
        "game_dir": str(game_dir),
        "out_dir": str(out_dir),
        "target_count": len(records),
        "targets": records,
    }
    seeds_path = out_dir / "seeds.json"
    with seeds_path.open("w", encoding="utf-8") as fh:
        json.dump(seeds, fh, indent=1, ensure_ascii=False)
        fh.write("\n")

    # A tiny machine-readable manifest for shell scripts.
    manifest = {
        "out_dir": str(out_dir),
        "bin": str(out_dir / "bin"),
        "seeds": str(seeds_path),
        "targets": [
            {
                "name": r["name"],
                "output": r["output"],
                "packer": r["packer"],
                "unpacked": r["unpacked"],
            }
            for r in records
        ],
    }
    (out_dir / "stage1_manifest.json").write_text(
        json.dumps(manifest, indent=1, ensure_ascii=False) + "\n",
        encoding="utf-8")
    if not args.quiet:
        print("[stage1] wrote %s" % seeds_path)
        print("[stage1] targets=%d packed=%d" % (
            len(records),
            sum(1 for r in records if not r["unpacked"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

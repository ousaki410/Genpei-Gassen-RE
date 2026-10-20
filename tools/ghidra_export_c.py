# @runtime Jython
# @category YPHZ.RE
# Ghidra headless postScript: export one C file per function plus index.json.
#
# Usage (from tools/run_re_pipeline.sh):
#   YPHZ_RE_OUT=/abs/re_out analyzeHeadless ... \
#       -postScript ghidra_export_c.py -scriptPath tools
#
# The script is written for Ghidra's Jython 2.x GhidraScript engine.  It also
# works with the Python/Jython object model used by Ghidra 10/11; the installed
# Ghidra 12 requires the Jython extension and the "# @runtime Jython" tag.

import binascii
import json
import os
import re

from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

OUT_ROOT = os.environ.get("YPHZ_RE_OUT", "/tmp/yphz_re_out")
BIN_NAME = currentProgram.getName()
OUT_DIR = os.path.join(OUT_ROOT, "decompiled", BIN_NAME)
if not os.path.isdir(OUT_DIR):
    os.makedirs(OUT_DIR)

MONITOR = ConsoleTaskMonitor()
FM = currentProgram.getFunctionManager()
MEM = currentProgram.getMemory()
LISTING = currentProgram.getListing()
REFMGR = currentProgram.getReferenceManager()

_DEFAULT_ASSETS = [
    "SNDATA.GP", "SAVEDATA.GP", "MAINCMD.GP", "MAINCMD2.GP", "MAINOBJ.GP",
    "MAIN.EXE", "MAINEVT.GP", "MAINITEM.GP", "MAINANM.GP", "MAINMAP.GP",
    "MAINPAL.PLD", "MESSAGE.GP", "MUSIC.GP", "PCM.GP", "HAIKEI.GP",
    "KISETSU.GP", "HCHIKEI.GP", "HKESHIKI.GP", "HGROUND.GP", "HUNITPAT.GP",
    "HIKUCHI.GP", "HKUMI.GP", "HIKBACK.GP", "HKEI.GP", "LOSEPAL.PLD",
    "KAODATA.GP", "MONTAGE.GP", "LOGO.GP", "DISK-1.GP", "DISK-2.GP",
    "DISK-3.GP", "DISK-4.GP", "DISK-5.GP", "GEN.DIR",
]


def load_seed_asset_names():
    """Read the canonical asset list from Stage 1's seeds.json.

    The keys in asset_anchors are title-cased (Sndata.gp); the bytes in the
    executable are upper-case (SNDATA.GP).  Normalise to upper case so the
    byte search and strict string comparison use one spelling.
    """
    path = os.path.join(OUT_ROOT, "seeds.json")
    try:
        text = open(path, "rb").read().decode("utf-8", "replace")
        names = re.findall(
            r'"([A-Za-z0-9_.-]+\.(?:gp|pld))"\s*:\s*\[', text,
            re.IGNORECASE)
        out = []
        seen = set()
        for name in names:
            name = name.upper()
            if name not in seen:
                out.append(name)
                seen.add(name)
        if out:
            return out
    except Exception as exc:
        print("[ghidra-export] cannot read %s: %s" % (path, exc))
    return list(_DEFAULT_ASSETS)


ASSET_NAMES = load_seed_asset_names()


def astr(addr):
    if addr is None:
        return None
    return str(addr)


def file_offset(addr):
    try:
        info = MEM.getAddressSourceInfo(addr)
        if info is not None:
            return int(info.getFileOffset())
    except Exception:
        pass
    return None


def sanitize(name):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def filename_for(func):
    ep = func.getEntryPoint()
    text = str(ep)
    if ":" in text:
        seg, off = text.split(":", 1)
    else:
        seg, off = "flat", text
    return "%s_%s_%s.c" % (sanitize(seg), sanitize(off),
                           sanitize(func.getName()))


def printable_string_at(addr, max_len=96):
    """Read a NUL-terminated ASCII/SJIS string and return (text, hex)."""
    if addr is None:
        return None
    raw = []
    try:
        for i in range(max_len):
            b = MEM.getByte(addr.add(i)) & 0xff
            if b == 0:
                break
            raw.append(b)
    except Exception:
        return None
    if len(raw) < 2:
        return None
    try:
        text = "".join(chr(b) for b in raw if 0x20 <= b <= 0x7e)
        # Count printable ASCII and valid SJIS pairs; reject obvious code.
        ascii_count = sum(1 for b in raw if 0x20 <= b <= 0x7e)
        sjis_count = 0
        j = 0
        while j + 1 < len(raw):
            b0, b1 = raw[j], raw[j + 1]
            if ((0x81 <= b0 <= 0x9f or 0xe0 <= b0 <= 0xef) and
                    (0x40 <= b1 <= 0x7e or 0x80 <= b1 <= 0xfc)):
                sjis_count += 2
                j += 2
            else:
                j += 1
        if ascii_count + sjis_count < max(2, len(raw) * 0.75):
            return None
        rawstr = str(bytearray(raw))
        try:
            text = rawstr.decode("cp932", "replace")
        except Exception:
            text = "".join(chr(b) if 0x20 <= b <= 0x7e else "." for b in raw)
        return {"text": text, "hex": binascii.hexlify(rawstr)}
    except Exception:
        return None


def ascii_string_at(addr, max_len=64):
    """Read an ASCII NUL-terminated string; reject any non-printable byte."""
    if addr is None:
        return None
    raw = []
    try:
        for i in range(max_len):
            b = MEM.getByte(addr.add(i)) & 0xff
            if b == 0:
                break
            if b < 0x20 or b > 0x7e:
                return None
            raw.append(b)
    except Exception:
        return None
    if len(raw) < 3:
        return None
    return "".join(chr(b) for b in raw)


def strict_asset_hit(addr):
    """Return (asset_name, matched_text) only for an exact seed filename.

    A leading DOS drive prefix C: is permitted; anything else (including a
    longer packed string such as C:MESSAGE.GPC:MUSIC.GP) is rejected.
    """
    text = ascii_string_at(addr)
    if text is None:
        return None
    candidate = text
    if len(candidate) > 2 and candidate[0] in ("C", "c") and candidate[1] == ":":
        candidate = candidate[2:]
    for name in ASSET_NAMES:
        if candidate.upper() == name.upper():
            return (name, text)
    return None


def find_asset_addresses():
    found = {}
    for name in ASSET_NAMES:
        needle = name.encode("ascii")
        start = MEM.getMinAddress()
        addrs = []
        while start is not None:
            try:
                hit = MEM.findBytes(start, needle, None, True, MONITOR)
            except Exception:
                hit = None
            if hit is None:
                break
            addrs.append(hit)
            start = hit.add(1)
            if MONITOR.isCancelled():
                break
        if addrs:
            found[name] = addrs
    return found


def asset_offset_map(asset_addrs):
    result = {}
    for name, addrs in asset_addrs.items():
        for a in addrs:
            result[a.getOffset() & 0xffff] = name
    return result


def instruction_refs(func):
    """Return (data_refs, scalar_values) for every instruction in func."""
    refs = []
    scalars = []
    body = func.getBody()
    ins = LISTING.getInstructionAt(body.getMinAddress())
    while ins is not None and body.contains(ins.getAddress()):
        # Direct memory references (segment:offset and flat).
        try:
            rr = ins.getReferencesFrom()
            while rr.hasNext():
                r = rr.next()
                to = r.getToAddress()
                if to is not None:
                    refs.append({"address": astr(to),
                                 "type": str(r.getReferenceType()),
                                 "from": astr(r.getFromAddress())})
        except Exception:
            pass
        for oi in range(ins.getNumOperands()):
            try:
                objs = ins.getOpObjects(oi)
            except Exception:
                continue
            for o in objs:
                v = None
                try:
                    v = o.getValue()
                except Exception:
                    try:
                        v = o.getOffset()
                    except Exception:
                        v = None
                if v is not None:
                    try:
                        scalars.append(int(v) & 0xffffffff)
                    except Exception:
                        pass
        ins = LISTING.getInstructionAfter(ins.getAddress())
    return refs, scalars


FID_MATCHES = {}
FID_AVAILABLE = False
global_candidate_count = [0]
global_valid_count = [0]
FID_STATUS = "not run"


def setup_fid():
    """Best-effort FID match.  Ghidra's shipped Visual Studio FID databases
    are 32/64-bit, so a 16-bit Real Mode DOS program normally reports
    canProcessLanguage=false; that is recorded, not hidden."""
    global FID_MATCHES, FID_AVAILABLE, FID_STATUS
    try:
        from ghidra.feature.fid.db import FidFileManager
        from ghidra.feature.fid.service import FidService
        manager = FidFileManager.getInstance()
        manager.load()
        service = FidService()
        if not service.canProcess(currentProgram.getLanguage()):
            FID_STATUS = ("FID libraries cannot process %s; installed "
                          "Visual Studio FID databases are 32/64-bit only" %
                          currentProgram.getLanguageID())
            return
        query = service.openFidQueryService(
            currentProgram.getLanguage(), False)
        try:
            results = service.processProgram(
                currentProgram, query, service.getDefaultScoreThreshold(),
                MONITOR)
            for result in results:
                if not result.matches:
                    continue
                best = None
                for match in result.matches:
                    if best is None or match.getOverallScore() > best.getOverallScore():
                        best = match
                if best is not None:
                    FID_MATCHES[astr(result.function.getEntryPoint())] = \
                        best.getFunctionRecord().getName()
            FID_AVAILABLE = True
            FID_STATUS = "FID matched %d functions" % len(FID_MATCHES)
        finally:
            query.close()
    except Exception as exc:
        FID_AVAILABLE = False
        FID_STATUS = "FID setup failed: %s" % exc


def main():
    setup_fid()
    di = DecompInterface()
    di.setOptions(DecompileOptions())
    di.openProgram(currentProgram)

    asset_addrs = find_asset_addresses()
    asset_offsets = asset_offset_map(asset_addrs)
    asset_xrefs = {}
    for name in ASSET_NAMES:
        asset_xrefs[name] = []

    functions = []
    count = 0
    ok_count = 0
    fail_count = 0
    used_names = set()

    for func in FM.getFunctions(True):
        if MONITOR.isCancelled():
            break
        count += 1
        ep = func.getEntryPoint()
        fname = filename_for(func)
        if fname in used_names:
            root, ext = os.path.splitext(fname)
            n = 2
            while ("%s_%d%s" % (root, n, ext)) in used_names:
                n += 1
            fname = "%s_%d%s" % (root, n, ext)
        used_names.add(fname)

        refs, scalars = instruction_refs(func)
        ref_addrs = set()
        data_refs = []
        seen_refs = set()
        for r in refs:
            key = (r["address"], r["type"])
            if key in seen_refs:
                continue
            seen_refs.add(key)
            try:
                a = currentProgram.getAddressFactory().getAddress(r["address"])
            except Exception:
                a = None
            if a is not None:
                ref_addrs.add(a)
                txt = printable_string_at(a)
                if txt is not None:
                    r["text"] = txt["text"]
                    r["hex"] = txt["hex"]
            data_refs.append(r)

        # The game addresses its 0x6000 data segment with 16-bit offsets.
        # Treat in-range scalar operands as possible DS:offset references and
        # resolve them against the 0x6000 segment when possible.
        for v in scalars:
            if 0x1000 <= v < 0x8000:
                try:
                    a = currentProgram.getAddressFactory().getAddress(
                        "6000:%04x" % (v & 0xffff))
                except Exception:
                    a = None
                if a is not None:
                    txt = printable_string_at(a)
                    if txt is not None:
                        data_refs.append({"address": astr(a),
                                          "type": "scalar-offset",
                                          "from": None,
                                          "value": "0x%x" % v,
                                          "text": txt["text"],
                                          "hex": txt["hex"]})
                        ref_addrs.add(a)

        # Strict asset xrefs: the candidate target must resolve to an exact
        # filename from seeds.json (optional C: prefix).  The old heuristic
        # accepted any scalar in 0x1000-0x1e00 and produced many false hits.
        candidate_targets = []
        for r in data_refs:
            if r.get("address") and r.get("type") != "scalar-offset":
                try:
                    candidate_targets.append(
                        currentProgram.getAddressFactory().getAddress(
                            r["address"]))
                except Exception:
                    pass
        for v in scalars:
            if 0x1000 <= v < 0x2000:
                try:
                    candidate_targets.append(
                        currentProgram.getAddressFactory().getAddress(
                            "6000:%04x" % (v & 0xffff)))
                except Exception:
                    pass
        for a in candidate_targets:
            if a is None:
                continue
            global_candidate_count[0] += 1
            hit = strict_asset_hit(a)
            if hit is None:
                continue
            name, matched = hit
            global_valid_count[0] += 1
            seed = asset_addrs.get(name, [a])[0]
            item = {
                "function": astr(ep),
                "function_name": func.getName(),
                "asset_address": astr(seed),
                "string_address": astr(a),
                "matched_text": matched,
                "kind": "strict-string",
            }
            if item not in asset_xrefs[name]:
                asset_xrefs[name].append(item)

        res = di.decompileFunction(func, 30, MONITOR)
        ctext = None
        err = ""
        try:
            if res is not None and res.decompileCompleted():
                cf = res.getDecompiledFunction()
                if cf is not None:
                    ctext = cf.getC()
            if res is not None:
                err = res.getErrorMessage() or ""
        except Exception as exc:
            err = str(exc)

        path = os.path.join(OUT_DIR, fname)
        if ctext:
            ok_count += 1
            with open(path, "w") as fh:
                fh.write("/* %s @ %s (%d bytes) */\n" %
                         (func.getName(), astr(ep),
                          func.getBody().getNumAddresses()))
                fh.write(ctext)
                if not ctext.endswith("\n"):
                    fh.write("\n")
        else:
            fail_count += 1
            with open(path, "w") as fh:
                fh.write("/* DECOMPILE FAILED @ %s */\n" % astr(ep))
                fh.write("/* %s */\n" % err.replace("\n", " "))

        callees = []
        try:
            for c in func.getCalledFunctions(MONITOR):
                callees.append(astr(c.getEntryPoint()))
        except Exception:
            pass
        callers = []
        try:
            for c in func.getCallingFunctions(MONITOR):
                callers.append(astr(c.getEntryPoint()))
        except Exception:
            pass

        functions.append({
            "name": func.getName(),
            "entry": astr(ep),
            "size": int(func.getBody().getNumAddresses()),
            "callees": sorted(callees),
            "callers": sorted(callers),
            "data_refs": data_refs,
            "is_fid_match": astr(ep) in FID_MATCHES,
            "fid_name": FID_MATCHES.get(astr(ep), ""),
            "decompiled": bool(ctext),
            "file": fname,
            "error": err,
        })
        if count % 50 == 0:
            println("[ghidra-export] %s: %d functions, %d decompiled, %d failed" %
                    (BIN_NAME, count, ok_count, fail_count))

    # If a name was absent, retain an empty xref list for stable consumers.
    assets_out = {}
    for name in ASSET_NAMES:
        assets_out[name] = {
            "addresses": [astr(a) for a in asset_addrs.get(name, [])],
            "file_offsets": [file_offset(a) for a in asset_addrs.get(name, [])],
            "xrefs": asset_xrefs.get(name, []),
        }

    validated_xref_entries = sum(len(v) for v in asset_xrefs.values())
    index = {
        "bin": BIN_NAME,
        "program": BIN_NAME,
        "language": str(currentProgram.getLanguageID()),
        "compiler_spec": str(currentProgram.getCompilerSpec().getCompilerSpecID()),
        "image_base": astr(currentProgram.getImageBase()),
        "function_count": count,
        "decompiled_count": ok_count,
        "failed_count": fail_count,
        "fid_available": FID_AVAILABLE,
        "fid_status": FID_STATUS,
        "fid_match_count": len(FID_MATCHES),
        "asset_xref_stats": {
            "candidate_count": global_candidate_count[0],
            "validated_count": validated_xref_entries,
            "discarded_count": (global_candidate_count[0] -
                                validated_xref_entries),
            "validated_nonempty_assets": sum(
                1 for v in asset_xrefs.values() if v),
        },
        "assets": assets_out,
        "asset_xrefs": asset_xrefs,
        "functions": functions,
    }
    indexPath = os.path.join(OUT_DIR, "index.json")
    with open(indexPath, "w") as fh:
        # Jython 2.7's json.dump writes to a byte stream; escaping non-ASCII
        # keeps the index readable by every consumer without codec surprises.
        json.dump(index, fh, indent=1, ensure_ascii=True)
        fh.write("\n")
    println("[ghidra-export] wrote %s (%d functions, %d decompiled, %d failed)" %
            (indexPath, count, ok_count, fail_count))
    di.dispose()


main()

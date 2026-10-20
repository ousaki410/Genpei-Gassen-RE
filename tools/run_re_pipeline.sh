#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless DOS RE pipeline (Stage 1 + Stage 2).
#
#   Stage 1  tools/re_unpack_dos.py      -> re_out/bin/, re_out/seeds.json
#   Stage 2  Ghidra analyzeHeadless      -> re_out/decompiled/<bin>/*.c
#                                           + index.json
#
# The script is intentionally serial: DOSBox/Stage 1 finishes before Ghidra
# starts, and Ghidra processes one executable at a time.  This keeps memory
# usage within the 2 GiB headless heap on an 8 GiB machine.
#
# Usage:
#   tools/run_re_pipeline.sh                  # Main.exe only (default)
#   tools/run_re_pipeline.sh --only-main      # Main.exe only
#   tools/run_re_pipeline.sh --binaries Open.exe End.exe
#   tools/run_re_pipeline.sh --skip-stage1
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="${YPHZ_RE_OUT:-$ROOT/re_out}"
GAME_DIR="${YPHZ_GAME_DIR:-$ROOT/games/yphz}"
PROJECT_DIR="${YPHZ_GHIDRA_PROJECT:-/tmp/ghidra_yphz}"
PROJECT_NAME="${YPHZ_GHIDRA_PROJECT_NAME:-yphz_proj}"
ONLY_MAIN=0
SKIP_STAGE1=0
BINARIES=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --only-main) ONLY_MAIN=1 ;;
        --skip-stage1) SKIP_STAGE1=1 ;;
        --binaries)
            shift
            while [ "$#" -gt 0 ] && [ "${1#-}" = "$1" ]; do
                BINARIES+=("$1"); shift
            done
            ;;
        --out) OUT="$2"; shift ;;
        -h|--help)
            sed -n '1,35p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

if [ ! -x /usr/local/bin/analyzeHeadless ] && ! command -v analyzeHeadless >/dev/null 2>&1; then
    echo "[pipeline] analyzeHeadless not found" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "[pipeline] python3 not found" >&2
    exit 2
fi

mkdir -p "$OUT" "$PROJECT_DIR"
export YPHZ_RE_OUT="$OUT"

# Ghidra 12's Jython loader cannot open a script whose *path* contains
# non-ASCII bytes (the repository name is 源平合战).  Stage the script at an
# ASCII path and point -scriptPath there.  The Python data path itself may be
# Unicode; Stage 1/2 outputs stay in the repository.
SCRIPT_STAGE="${YPHZ_SCRIPT_STAGE:-/tmp/yphz_ghidra_scripts}"
mkdir -p "$SCRIPT_STAGE"
cp "$ROOT/tools/GhidraExportC.java" "$SCRIPT_STAGE/GhidraExportC.java"
cp "$ROOT/tools/EnableFid.java" "$SCRIPT_STAGE/EnableFid.java"

if [ "$SKIP_STAGE1" -eq 0 ]; then
    echo "[pipeline] Stage 1: inspect DOS images in $GAME_DIR"
    python3 "$ROOT/tools/re_unpack_dos.py" "$GAME_DIR" --out "$OUT"
else
    echo "[pipeline] Stage 1 skipped"
fi

if [ "${#BINARIES[@]}" -eq 0 ]; then
    BINARIES=("Main.exe")
fi

# Keep the requested order but ignore absent files with a clear message.
FINAL=()
for b in "${BINARIES[@]}"; do
    if [ -f "$OUT/bin/$b" ]; then
        FINAL+=("$b")
    else
        echo "[pipeline] skip missing $OUT/bin/$b" >&2
    fi
done
if [ "${#FINAL[@]}" -eq 0 ]; then
    echo "[pipeline] no importable binaries" >&2
    exit 2
fi

for bin in "${FINAL[@]}"; do
    echo "[pipeline] Stage 2: Ghidra $bin"
    project="$PROJECT_NAME"
    if [ "${#FINAL[@]}" -gt 1 ]; then
        project="${PROJECT_NAME}_${bin%.*}"
        project="$(printf '%s' "$project" | tr -c 'A-Za-z0-9_.-' '_')"
    fi
    rm -rf "$PROJECT_DIR/$project" "$PROJECT_DIR/${project}.rep" "$PROJECT_DIR/${project}.gpr" 2>/dev/null || true
    log="$OUT/ghidra_${bin}.log"
    if ! timeout "${YPHZ_GHIDRA_TIMEOUT:-1800}" \
        analyzeHeadless "$PROJECT_DIR" "$project" \
        -import "$OUT/bin/$bin" \
        -preScript EnableFid.java \
        -postScript GhidraExportC.java \
        -scriptPath "$SCRIPT_STAGE" \
        -deleteProject >"$log" 2>&1; then
        echo "[pipeline] Ghidra failed for $bin (see $log)" >&2
        tail -40 "$log" >&2 || true
        exit 1
    fi
    index="$OUT/decompiled/$bin/index.json"
    if [ ! -s "$index" ]; then
        echo "[pipeline] Ghidra produced no index for $bin (see $log)" >&2
        tail -40 "$log" >&2 || true
        exit 1
    fi
    python3 - "$index" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p, encoding="utf-8"))
print("[pipeline] %s: functions=%d decompiled=%d failed=%d assets_with_xrefs=%d" % (
    d.get("bin"), d.get("function_count", 0), d.get("decompiled_count", 0),
    d.get("failed_count", 0),
    sum(1 for v in d.get("assets", {}).values() if v.get("xrefs"))))
PY
done

echo "[pipeline] done: $OUT"

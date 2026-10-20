#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Headless regression suite for the 源平合戦 Linux engine.
#
# Runs the engine through its CLI-only ("jump") paths with the SDL dummy video
# driver and asserts the key log lines for every implemented sub-task:
#   1  input/map      2  message     3  turn      4  gp probe
#   5  click/gallery  6  kassen      7  ai/event  8  save/opening
#   9  road network + 家宝 (Mainitem.gp)   12  encoding   13  assets
#
# Usage:  tools/run_regression.sh          (expects ./bin/yphz_engine built)
# Exit:   0 = all pass, 1 = at least one failure.
# ---------------------------------------------------------------------------
set -u

cd "$(dirname "$0")/.." || exit 2
BIN="./bin/yphz_engine"
export SDL_VIDEODRIVER=dummy

if [ ! -x "$BIN" ]; then
    echo "build first: cmake -S . -B build && cmake --build build -j" >&2
    exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

check() { # <name> <regex> <args...>
    local name="$1" pat="$2"; shift 2
    local out
    local has_frames=0
    for a in "$@"; do [ "$a" = "--frames" ] && has_frames=1; done
    if [ "$has_frames" -eq 1 ]; then
        out="$("$BIN" "$@" 2>&1)"
    else
        out="$("$BIN" "$@" --frames 1 2>&1)"
    fi
    if printf '%s' "$out" | grep -qE "$pat"; then
        printf 'PASS  %s\n' "$name"; pass=$((pass + 1))
    else
        printf 'FAIL  %s  (want /%s/)\n' "$name" "$pat"; fail=$((fail + 1))
        printf '%s\n' "$out" | tail -4 | sed 's/^/      /'
    fi
}

check_not() { # <name> <regex> <args...>
    local name="$1" pat="$2"; shift 2
    local out
    local has_frames=0
    for a in "$@"; do [ "$a" = "--frames" ] && has_frames=1; done
    if [ "$has_frames" -eq 1 ]; then
        out="$("$BIN" "$@" 2>&1)"
    else
        out="$("$BIN" "$@" --frames 1 2>&1)"
    fi
    if printf '%s' "$out" | grep -qE "$pat"; then
        printf 'FAIL  %s  (unexpected /%s/)\n' "$name" "$pat"; fail=$((fail + 1))
    else
        printf 'PASS  %s\n' "$name"; pass=$((pass + 1))
    fi
}

echo "== sub-task 1-6: map / domestic / battle =="
check "map turn-state"          "Turn state: faction 0" --map -c 30
check "faction override"        "Turn state: faction 1" --map -c 16 --faction 1
check "select-castle"           "Turn state: faction 0" --select-castle 16
check "develop"                 "DEVELOP castle=30 officer=0 ok=1" --do-cmd develop -c 30 --faction 0
check "recruit"                 "RECRUIT castle=30 .* ok=1" --do-cmd recruit -c 30 --faction 0
check "tax"                     "TAX castle=30 .* ok=1" --do-cmd tax -c 30 --faction 0
check "end turn"                "END -> 1180/10" --end-turn 1 -c 30 --faction 0
check "move adjacent"           "MOVE castle=30 -> 31 officer=0 ok=1" --do-cmd move -c 30 --faction 0 --move-to 31
check "move non-adjacent reject" "MOVE castle=30 -> 0 officer=-1 ok=0" --do-cmd move -c 30 --faction 0 --move-to 0
check "officer roster"          "roster castle=30 officers=25" --do-cmd officers -c 30 --faction 0
check "attack capture"          "end to=29 victory=1 .*new_faction=0" --attack 30,29 \
      --battle-plan charge,charge,charge,charge,charge,charge --frames 12
check "attack non-adjacent"     "not adjacent" --attack 30,0
check "defend"                  "Defence: AI castle 29 -> player castle 30" --defend 29,30 --frames 3
check "defend non-adjacent"     "not adjacent" --defend 16,30
check "battle retreat"          "retreated=1" --attack 30,29 --battle-plan retreat --frames 8

echo "== sub-task 7: AI / events =="
check "ai-step"                 "date=1180/10 domestic=62 attacks=5 captures=4 pending=1" --ai-step
check "trigger-event"           "EVENT\] index=0 .*MINAMOTO-UPRISING" --trigger-event 0
check "unified ending"          "forced unification" --unified

echo "== sub-task 5/2: gallery / message =="
check "gallery"                 "Loaded Mainitem.gp|HAL_VIDEO" --gallery 2
check "preview-npk"             "Preview Mainitem.gp block 0/19: 64x64" --preview-npk Mainitem.gp 0
check "test-msg"                "Loaded Message.gp: 5 LS11 entries" --test-msg 0

echo "== sub-task 8: save / load / opening =="
check "save size"               "SAVE\] wrote $TMP/x.sav \(63655 bytes\)" --do-cmd develop -c 30 --faction 0 --save "$TMP/x.sav"
check "opening flow"            "enter map: faction=0 castle=30 officer=0" \
      --click 320,200@2 --click 60,110@4 --click 60,74@6 --frames 10
check_not "opening not leaked"  "enter map" --map -c 30

# save -> load round-trip must reproduce the identical state digest.
"$BIN" --do-cmd develop --end-turn 2 -c 30 --faction 0 --save "$TMP/rt.sav" --frames 1 >/dev/null 2>&1
h1="$("$BIN" --load "$TMP/rt.sav" --dump-state --frames 1 2>&1 | grep -oE 'hash=0x[0-9a-f]+' | head -1)"
h2="$("$BIN" --do-cmd develop --end-turn 2 -c 30 --faction 0 --dump-state --frames 1 2>&1 | grep -oE 'hash=0x[0-9a-f]+' | head -1)"
if [ -n "$h1" ] && [ "$h1" = "$h2" ]; then
    printf 'PASS  save/load round-trip (%s)\n' "$h1"; pass=$((pass + 1))
else
    printf 'FAIL  save/load round-trip (%s vs %s)\n' "$h1" "$h2"; fail=$((fail + 1))
fi

echo "== sub-task 9: road network + 家宝 =="
check "show-roads"              "ROADS\] castles=39 edges=72" --show-roads
check "show-item 0"             "ITEM\] id=0 owner=0 lead\+8 war\+0 intel\+0 pol\+8" --show-item 0
check "show-item 5"             "ITEM\] id=5 owner=5" --show-item 5
check "grant-item"              "ITEM\] grant item=0 officer=0 lead\+8 war\+0 intel\+0 pol\+8" --grant-item 0
check "grant-item foreign fails" "grant failed item=5" --grant-item 5
check "seize heirloom"          "seized item=14 faction 14 -> 0" --attack 30,29 \
      --battle-plan charge,charge,charge,charge,charge,charge --frames 12

# The road graph must be symmetric, connected and free of self/duplicate edges.
if "$BIN" --show-roads --frames 1 2>&1 | grep '^\[ROADS\] [0-9]' > "$TMP/roads.txt" && \
   python3 - "$TMP/roads.txt" <<'PY'
import sys
adj = {}
for line in open(sys.argv[1]):
    parts = line.split()
    cid = int(parts[1]); adj[cid] = [int(x) for x in parts[2:]]
assert len(adj) == 39, "expected 39 castles"
for a, ns in adj.items():
    assert len(ns) == len(set(ns)), f"duplicate edge at {a}"
    for b in ns:
        assert b != a, f"self edge at {a}"
        assert a in adj[b], f"asymmetric edge {a}-{b}"
seen, stack = set(), [0]
while stack:
    v = stack.pop()
    if v in seen: continue
    seen.add(v); stack += adj[v]
assert len(seen) == 39, "graph is not connected"
PY
then
    printf 'PASS  road graph symmetric + connected\n'; pass=$((pass + 1))
else
    printf 'FAIL  road graph validation\n'; fail=$((fail + 1))
fi

echo "== sub-task 10: DOS save slots + raw 4-plane sprites =="
# 原版 Savedata.gp 槽 0：导入 → 导出到槽 4 → 再导入，状态哈希必须一致
if [ -f games/yphz/Savedata.gp ]; then
    cp games/yphz/Savedata.gp "$TMP/dos.gp"
    h0=$("$BIN" --import-dos-save 0 --dos-save-path "$TMP/dos.gp" --dump-state --frames 1 2>&1 | grep -oE 'hash=0x[0-9a-f]+')
    "$BIN" --import-dos-save 0 --export-dos-save 4 --dos-save-path "$TMP/dos.gp" --frames 1 >/dev/null 2>&1
    h1=$("$BIN" --import-dos-save 4 --dos-save-path "$TMP/dos.gp" --dump-state --frames 1 2>&1 | grep -oE 'hash=0x[0-9a-f]+')
    if [ -n "$h0" ] && [ "$h0" = "$h1" ]; then
        printf 'PASS  DOS save slot round-trip (%s)\n' "$h0"; pass=$((pass + 1))
    else
        printf 'FAIL  DOS save slot round-trip (%s vs %s)\n' "$h0" "$h1"; fail=$((fail + 1))
    fi
else
    printf 'SKIP  DOS save slot round-trip (no Savedata.gp)\n'
fi

# 裸 4 位平面精灵：24 px 扫描线（12 B 步长），单位精灵 24x32，36 行间距
check "raw 4-plane unit bank"  "Unit patterns: 12661 rows of 24 px, sprites 24x32" --attack 30,29 --frames 2
check "raw 4-plane preview"   "Raw Hunitpat.gp: 24x32 \(4-plane" --preview-raw Hunitpat.gp 3 --frames 2
check "raw 4-plane sheet"     "Raw Mainobj.gp: 24x2045 \(4-plane" --preview-raw Mainobj.gp --frames 2

# 布局求解器必须复现已解出的几何（24 px 步长 / chunk8 平面序）
if python3 tools/solve_raw_layout.py --files Hunitpat.gp 2>&1 | grep -q "s=12:"; then
    printf 'PASS  raw layout solver finds 12-byte stride\n'; pass=$((pass + 1))
else
    printf 'FAIL  raw layout solver stride\n'; fail=$((fail + 1))
fi
if python3 tools/decode_raw_sprites.py --out "$TMP/sprites" 2>&1 | grep -q "24 px wide, 12661 rows"; then
    printf 'PASS  raw sprite decoder geometry\n'; pass=$((pass + 1))
else
    printf 'FAIL  raw sprite decoder geometry\n'; fail=$((fail + 1))
fi

echo "== sub-task 12: encoding / localization =="
check "SJIS->UTF-8 officer dump"   "name=源頼朝"            --dump-officers
check "zh-Hans officer dump"       "name=源赖朝"            --dump-officers --lang zh
check "zh-Hans castle dump"        "name=镰仓"              --dump-officers --lang zh
check "Message.gp UTF-8 dump"      "武将ｦ選択"             --dump-messages
check "Message.gp zh-Hans dump"    "武将ｦ选择"             --dump-messages --lang zh
check "encoding backend banner"    "Encoding: iconv:.* lang=zh tables: koei=13212" --dump-officers --lang zh
check "KOEI legacy code table"     "name=koei .*entries=13212"  --dump-code-table koei
check "KOEI probe is not cp932"    "probe=0xD19F cp=U.938C"     --dump-code-table koei
check "GBK table (UTF-8 BOM)"      "name=gbk .*source=utf-8 .*entries=24096" --dump-code-table gbk
check "Big5 table"                 "name=big5 .*entries=13912"  --dump-code-table big5

if python3 tools/gen_zh_hans_table.py --check 2>&1 | grep -q "zh-Hans table is current"; then
    printf 'PASS  zh-Hans table matches generator\n'; pass=$((pass + 1))
else
    printf 'FAIL  zh-Hans table regeneration\n'; fail=$((fail + 1))
fi

echo "== sub-task 13: multi-pack NPK, palette banks, disk labels, raw inspector =="
check "Opendat multi-pack"      "file=Opendat.gp .*blocks=52"  --dump-packs Opendat.gp
check "Enddat multi-pack"       "file=Enddat.gp .*blocks=84"   --dump-packs Enddat.gp
check "Opendat block geometry"  "000 off=0x0007b0 size=42163 dims=640x200" --dump-packs Opendat.gp
check "palette main bank 2"     "Palette: source=main bank=2"  --palette main --palette-bank 2 --dump-packs Opendat.gp
check "palette lose bank 3"     "Palette: source=lose bank=3"  --palette lose --palette-bank 3 --dump-packs Enddat.gp
check "Opendat palette preview" "Preview Opendat.gp block 10/52: 640x200 \(palette=lose#0\)" --preview-npk Opendat.gp 10 --palette lose --palette-bank 0 --frames 2
check "disk volume labels"      'label="源平合戦 DISK-1"'      --dump-disk-labels
check "chikei 100x100 grid"     "grid=100x100 formation=60B ids=13" --dump-chikei
check "chikei terrain ids"      "0=4279 1=1886 2=1284"         --dump-chikei
check "raw width inspector"     "Raw Maincmd.gp: 96x1275 \(4-plane, width=96, stride=48\)" --preview-raw Maincmd.gp --raw-width 96 --frames 2
check "raw tile bank inspector" "Raw Montage.gp: 96x96 \(4-plane, width=96" --preview-raw Montage.gp --raw-width 96 --raw-height 96 --frames 2

echo "== sub-task 14: HUD toolbar, onboarding, testbench =="
check "HUD council button"       "\[HUD\] button=council" --map -c 30 --click 40,368@1 --frames 2
check "council chamber opens"    "\[COUNCIL\] open castle=30" --map -c 30 --click 40,368@1 --frames 2
check "onboarding guide"         "\[GUIDE\] open step=0" --guide --map -c 30
check "testbench opens"          "\[TESTBENCH\] open tab=0" --testbench --map -c 30 --frames 2
check "testbench max cheat"      "\[TESTBENCH\] MAX castle=30 gold=99999 rice=99999 troops=2000" --testbench-action max -c 30
check "testbench action reset"   "\[TESTBENCH\] RESET actions=0" --testbench-action reset -c 30
check "testbench historical event" "\[TESTBENCH\] EVENT index=0" --testbench-action event0
check "testbench Opendat player" "\[MOVIE\] start file=Opendat.gp blocks=52 loop=1" --testbench-action movie-open --frames 2
check "testbench scenario switch" "\[TESTBENCH\] SCENARIO 2 castles=39" --testbench-action scenario2
check "testbench faction switch" "\[TESTBENCH\] FACTION 1 castle=0 officer=45" --testbench-action faction1

echo "-------------------------------------------------------------------"
printf 'TOTAL %d checks: %d passed, %d failed\n' "$((pass + fail))" "$pass" "$fail"
[ "$fail" -eq 0 ]

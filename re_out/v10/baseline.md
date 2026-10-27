# V10 baseline

date: 2026-10-27T17:13:11Z

## git

- HEAD: `f2b9573a247ca046d7fe6a4dae2b4417594e115b`
  (`feat(re): port V9 castle accessors and battle evaluation helpers`)
- branch: `main`, tracking `origin/main`
- working tree: clean (`git status --short` empty)

## toolchain

- cmake + gcc + nasm + ghidra + python3 + capstone available
- `dosbox` is 0.74-3; **no DOSBox-X** (no dynamic original verification)

## regression baseline

- `bash tools/run_regression.sh` -> **93 passed / 0 failed**
- raw output: `re_out/v10/regression_baseline.txt`

## AI baseline

```text
[AI] date=1180/10 domestic=62 attacks=5 captures=4 pending=1
```

## save baseline

- 临时存档（未提交），63655 字节
- `[SAVE] hash=0x1a8de859`
- MD5 `caecbe152e28c4ec712e29b8e296e147`

## V10 after

- regression: **104 passed / 0 failed** (`re_out/v10/regression_after.txt`)
- AI: identical baseline line
- save: 63655 bytes, hash `0x1a8de859`, MD5 `caecbe152e28c4ec712e29b8e296e147`
- see `report_summary_v10.md`

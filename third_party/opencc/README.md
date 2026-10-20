# OpenCC dictionaries (vendored)

`JPShinjitaiCharacters.txt` and `TSCharacters.txt` are data files from
[OpenCC](https://github.com/BYVoid/OpenCC), licensed under the Apache License
2.0 (see `LICENSE`).

They are vendored so `tools/gen_zh_hans_table.py` can regenerate
`src/zh_hans_table.c` (the Japanese kanji -> Simplified Chinese table used by
`--lang zh`) without network access.

| file | upstream path | last upstream commit |
|---|---|---|
| `JPShinjitaiCharacters.txt` | `data/dictionary/JPShinjitaiCharacters.txt` | `6aa8c54e100678150985a9af631359f80e31956a` (2026-07-03) |
| `TSCharacters.txt` | `data/dictionary/TSCharacters.txt` | `5c764a5a886f46eb365656ed91a0410d7162fac6` (2026-09-16) |

Upstream format: `<key>\t<value>[ <value>...]` per line, UTF-8, `#` comments.
Only the first value of each entry is used by the generator.

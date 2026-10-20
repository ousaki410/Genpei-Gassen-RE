#ifndef YPHZ_ZH_HANS_TABLE_H
#define YPHZ_ZH_HANS_TABLE_H

#include <stdint.h>

// Generated kanji table (see tools/gen_zh_hans_table.py): Japanese kanji ->
// Simplified Chinese, restricted to the characters cp932 can represent.
// Lookup is a binary search over g_zh_hans_keys, which is sorted ascending.
#define ZH_HANS_TABLE_CAPACITY 2200

extern const int g_zh_hans_count;
extern const uint16_t g_zh_hans_keys[ZH_HANS_TABLE_CAPACITY];
extern const uint16_t g_zh_hans_vals[ZH_HANS_TABLE_CAPACITY];

#endif // YPHZ_ZH_HANS_TABLE_H

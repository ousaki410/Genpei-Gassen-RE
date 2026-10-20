#ifndef YPHZ_ENCODING_H
#define YPHZ_ENCODING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// 子任务 12: text encoding + localization layer.
//
// The DOS/PC-98 build stores every string as Shift-JIS (cp932): the officer
// and castle names in Sndata.gp, the dialogue in Message.gp, and the SJIS
// literals sprinkled through src/. Terminal output has to be UTF-8, and
// --lang zh maps the Japanese kanji onto Simplified Chinese before printing.
//
// Two different code-table families live in Plugins/:
//
//   * GBK.txt / Big5.txt are ordinary Unicode code tables: the key is a CJK
//     byte code and the value is the character. Both files are themselves
//     GBK text (GBK.txt additionally carries a UTF-8 BOM), so the parser
//     auto-detects the container encoding.
//
//   * koei.txt is KOEI's legacy 2-byte font code, *not* cp932: 0xD19F is 鎌,
//     while cp932 spells 鎌 as 0x8A99. It is parsed for KOEI translation
//     patches and is deliberately never applied to this game's strings.
//
// cp932 <-> UTF-8 conversion goes through the C library's iconv (glibc ships
// CP932); the kanji mapping is compiled in (zh_hans_table.c) so the engine
// needs no external Unicode data at runtime.
// ---------------------------------------------------------------------------

typedef enum {
    ENCODING_LANG_JA = 0,   // Japanese: cp932 source, UTF-8 output
    ENCODING_LANG_ZH = 1    // Simplified Chinese localization
} EncodingLang;

#define ENCODING_LANG_COUNT      2
#define ENCODING_MAX_TABLES      3
#define ENCODING_NAME_MAX        16
#define ENCODING_PATH_MAX        512
#define ENCODING_TABLE_CAPACITY  32768

// DOS / PC-98 compact layout convention (see hal_video.h).
#pragma pack(push, 1)

typedef struct {
    uint16_t code;       // byte code as it appears in the table's key column
    uint32_t codepoint;  // Unicode scalar value (BMP in every shipped table)
} EncodingPair;

typedef struct {
    char     name[ENCODING_NAME_MAX];       // "koei" / "gbk" / "big5"
    char     path[ENCODING_PATH_MAX];       // file that was parsed
    char     source_encoding[8];            // "utf-8" (BOM) or "gbk"
    uint32_t lines;                         // physical lines examined
    uint32_t entries;                       // usable <hex>=<char> records
    uint32_t skipped;                       // malformed / duplicate lines
    bool     loaded;
} EncodingTableInfo;

#pragma pack(pop)

// Parse Plugins/koei.txt, GBK.txt and Big5.txt from `plugin_dir`. Missing files
// are reported but are not fatal: cp932 conversion and --lang zh still work.
bool encoding_init(const char *plugin_dir);
void encoding_shutdown(void);
bool encoding_is_ready(void);

int encoding_table_count(void);
const EncodingTableInfo *encoding_table_info(int index);
const EncodingTableInfo *encoding_table_find(const char *name);
// Look up a byte code in a parsed table; returns 0 when absent.
uint32_t encoding_table_lookup(const char *name, uint16_t code);
// "koei=13211 gbk=24096 big5=13912" (entries, 0 for a table that failed).
void encoding_table_summary(char *dst, size_t dst_size);

// Shift-JIS (cp932) -> UTF-8. `dst` is always NUL terminated when dst_size>0.
// Bytes that cp932 cannot decode become '?'. Returns the byte count written.
size_t encoding_sjis_to_utf8(const char *src, char *dst, size_t dst_size);
// UTF-8 -> Shift-JIS (cp932). Returns 0 when a character has no cp932 form.
size_t encoding_utf8_to_sjis(const char *src, char *dst, size_t dst_size);

// Localized UTF-8 for terminals / logs.
//   ja: cp932 -> UTF-8
//   zh: cp932 -> UTF-8 -> Simplified Chinese
size_t encoding_sjis_to_display(const char *src, char *dst, size_t dst_size);

// Localized cp932 for the JIS.FNT renderer. In zh mode each kanji is replaced
// by its Simplified form *only* when JIS X 0208 has a glyph for it; otherwise
// the original character is kept (the PC-98 font has no simplified-only
// glyphs). Text always stays full-width, so layout is unaffected.
size_t encoding_sjis_to_screen(const char *src, char *dst, size_t dst_size);

void encoding_set_lang(EncodingLang lang);
EncodingLang encoding_get_lang(void);
bool encoding_is_zh(void);
bool encoding_parse_lang(const char *text, EncodingLang *out);  // "ja" / "zh"
const char *encoding_lang_code(void);                            // "ja" / "zh"

// Single-character han conversion (identity when the kanji is unchanged).
uint32_t encoding_zh_hans_char(uint32_t codepoint);
int encoding_zh_hans_count(void);

// Minimal UTF-8 helpers shared by the dump commands.
size_t encoding_utf8_encode(uint32_t cp, char *dst, size_t dst_size);
uint32_t encoding_utf8_decode(const char *src, int *advance);

// "iconv:CP932" (or "iconv:SJIS-WIN" / "none").
const char *encoding_backend(void);

#endif // YPHZ_ENCODING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>

#include "encoding.h"
#include "zh_hans_table.h"

// ---------------------------------------------------------------------------
// iconv back-ends
// ---------------------------------------------------------------------------
// The engine never links against ICU: glibc already carries CP932 and GBK.
// All conversions are single-threaded (the HAL and the CLI dumps run on the
// main thread), so one descriptor per direction is enough.

static iconv_t s_dec = (iconv_t)-1;   // cp932 -> UTF-8
static iconv_t s_enc = (iconv_t)-1;   // UTF-8 -> cp932
static char    s_backend[32] = "none";

static EncodingLang s_lang = ENCODING_LANG_JA;

static bool ensure_cp932(void) {
    if (s_dec != (iconv_t)-1 && s_enc != (iconv_t)-1) return true;

    static const char *const names[] = { "CP932", "SJIS-WIN", "SHIFT_JIS", "SJIS" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        iconv_t dec = iconv_open("UTF-8", names[i]);
        if (dec == (iconv_t)-1) continue;
        iconv_t enc = iconv_open(names[i], "UTF-8");
        if (enc == (iconv_t)-1) {
            iconv_close(dec);
            continue;
        }
        s_dec = dec;
        s_enc = enc;
        snprintf(s_backend, sizeof(s_backend), "iconv:%s", names[i]);
        return true;
    }
    snprintf(s_backend, sizeof(s_backend), "none");
    return false;
}

// Convert a complete buffer. Returns false on an invalid/incomplete sequence or
// when the destination is too small; nothing is terminated here.
static bool convert_buf(iconv_t cd, const char *src, size_t src_len,
                        char *dst, size_t dst_size, size_t *out_len) {
    if (cd == (iconv_t)-1 || !dst) return false;
    iconv(cd, NULL, NULL, NULL, NULL); // reset shift state

    char  *in      = (char *)src;
    size_t in_left = src_len;
    char  *out     = dst;
    size_t out_left = dst_size;

    while (in_left > 0) {
        if (iconv(cd, &in, &in_left, &out, &out_left) == (size_t)-1) return false;
    }
    *out_len = (size_t)(out - dst);
    return true;
}

static size_t copy_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

static bool all_ascii(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p >= 0x80) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// UTF-8 primitives
// ---------------------------------------------------------------------------

size_t encoding_utf8_encode(uint32_t cp, char *dst, size_t dst_size) {
    if (!dst) return 0;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;

    size_t len;
    if (cp < 0x80) len = 1;
    else if (cp < 0x800) len = 2;
    else if (cp < 0x10000) len = 3;
    else len = 4;
    if (dst_size < len) return 0;

    if (len == 1) {
        dst[0] = (char)cp;
    } else if (len == 2) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
    } else if (len == 3) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        dst[0] = (char)(0xF0 | (cp >> 18));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
    }
    return len;
}

uint32_t encoding_utf8_decode(const char *src, int *advance) {
    const unsigned char *p = (const unsigned char *)src;
    if (advance) *advance = 1;
    if (!p || !p[0]) return 0;

    if (p[0] < 0x80) {
        return p[0];
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        if (advance) *advance = 2;
        return ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        if (advance) *advance = 3;
        return ((uint32_t)(p[0] & 0x0F) << 12) |
               ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        if (advance) *advance = 4;
        return ((uint32_t)(p[0] & 0x07) << 18) |
               ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    }
    return 0xFFFD;
}

// ---------------------------------------------------------------------------
// cp932 <-> UTF-8
// ---------------------------------------------------------------------------

size_t encoding_sjis_to_utf8(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;
    if (all_ascii(src)) return copy_cstr(dst, dst_size, src);
    if (!ensure_cp932()) return copy_cstr(dst, dst_size, src);

    char  *in      = (char *)src;
    size_t in_left = strlen(src);
    char  *out     = dst;
    size_t out_left = dst_size - 1;

    iconv(s_dec, NULL, NULL, NULL, NULL);
    while (in_left > 0) {
        if (iconv(s_dec, &in, &in_left, &out, &out_left) != (size_t)-1) break;
        if (errno == E2BIG || out_left == 0) break;
        // Undecodable or truncated sequence: emit '?' and resynchronise.
        *out++ = '?';
        out_left--;
        in++;
        in_left--;
        iconv(s_dec, NULL, NULL, NULL, NULL);
    }
    *out = '\0';
    return (size_t)(out - dst);
}

size_t encoding_utf8_to_sjis(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;
    if (all_ascii(src)) return copy_cstr(dst, dst_size, src);
    if (!ensure_cp932()) return 0;

    size_t out_len = 0;
    if (!convert_buf(s_enc, src, strlen(src), dst, dst_size - 1, &out_len)) {
        dst[0] = '\0';
        return 0;
    }
    dst[out_len] = '\0';
    return out_len;
}

// ---------------------------------------------------------------------------
// Simplified Chinese mapping
// ---------------------------------------------------------------------------

uint32_t encoding_zh_hans_char(uint32_t codepoint) {
    if (codepoint > 0xFFFF) return codepoint;
    int lo = 0;
    int hi = g_zh_hans_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t key = g_zh_hans_keys[mid];
        if (key == codepoint) return g_zh_hans_vals[mid];
        if (key < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return codepoint;
}

int encoding_zh_hans_count(void) {
    return g_zh_hans_count;
}

static size_t utf8_to_zh_hans(const char *u8, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    size_t n = 0;
    for (const char *p = u8; *p; ) {
        int adv = 0;
        uint32_t cp = encoding_utf8_decode(p, &adv);
        p += adv;
        cp = encoding_zh_hans_char(cp);
        size_t w = encoding_utf8_encode(cp, dst + n, dst_size - n);
        if (w == 0) break;
        n += w;
    }
    if (n < dst_size) dst[n] = '\0';
    else {
        n = dst_size - 1;
        dst[n] = '\0';
    }
    return n;
}

size_t encoding_sjis_to_display(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;
    if (s_lang != ENCODING_LANG_ZH) return encoding_sjis_to_utf8(src, dst, dst_size);

    size_t cap = strlen(src) * 3 + 8;
    char *u8 = (char *)malloc(cap);
    char *zh = (char *)malloc(cap);
    if (!u8 || !zh) {
        free(u8);
        free(zh);
        return encoding_sjis_to_utf8(src, dst, dst_size);
    }
    encoding_sjis_to_utf8(src, u8, cap);
    utf8_to_zh_hans(u8, zh, cap);
    size_t n = copy_cstr(dst, dst_size, zh);
    free(u8);
    free(zh);
    return n;
}

// Simplified-only glyphs (赖, 仓, 战, ...) are absent from JIS X 0208, and the
// PC-98 font cannot invent them. Resolve once, at start-up, which of the 2097
// mappings the bundled font can actually draw; the rest keep their Japanese
// form on screen while the terminal dumps stay fully localized.
static uint8_t s_screen_ok[ZH_HANS_TABLE_CAPACITY];
static bool    s_screen_ready = false;

static void build_screen_map(void) {
    if (s_screen_ready || !ensure_cp932()) return;
    for (int i = 0; i < g_zh_hans_count; i++) {
        char u8[8] = { 0 };
        char sj[8] = { 0 };
        size_t u8_len = encoding_utf8_encode(g_zh_hans_vals[i], u8, sizeof(u8));
        size_t sj_len = 0;
        s_screen_ok[i] = (u8_len > 0 &&
                          convert_buf(s_enc, u8, u8_len, sj, sizeof(sj), &sj_len)) ? 1 : 0;
    }
    s_screen_ready = true;
}

// Same as encoding_zh_hans_char(), but only returns a mapping whose glyph the
// JIS X 0208 font really contains.
static uint32_t screen_hans_char(uint32_t codepoint) {
    if (codepoint > 0xFFFF) return codepoint;
    int lo = 0;
    int hi = g_zh_hans_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t key = g_zh_hans_keys[mid];
        if (key == codepoint) {
            return s_screen_ok[mid] ? g_zh_hans_vals[mid] : codepoint;
        }
        if (key < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return codepoint;
}

size_t encoding_sjis_to_screen(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!src || !src[0]) return 0;
    if (s_lang != ENCODING_LANG_ZH || !ensure_cp932()) {
        return copy_cstr(dst, dst_size, src);
    }
    build_screen_map();

    size_t cap = strlen(src) * 3 + 8;
    char *u8 = (char *)malloc(cap);
    char *zh = (char *)malloc(cap);
    if (!u8 || !zh) {
        free(u8);
        free(zh);
        return copy_cstr(dst, dst_size, src);
    }
    encoding_sjis_to_utf8(src, u8, cap);

    size_t n = 0;
    for (const char *p = u8; *p; ) {
        int adv = 0;
        uint32_t cp = encoding_utf8_decode(p, &adv);
        p += adv;
        size_t w = encoding_utf8_encode(screen_hans_char(cp), zh + n, cap - 1 - n);
        if (w == 0) break;
        n += w;
    }
    free(u8);

    size_t out_len = 0;
    bool ok = convert_buf(s_enc, zh, n, dst, dst_size - 1, &out_len);
    free(zh);
    if (!ok) return copy_cstr(dst, dst_size, src);
    dst[out_len] = '\0';
    return out_len;
}

// ---------------------------------------------------------------------------
// Language selection
// ---------------------------------------------------------------------------

void encoding_set_lang(EncodingLang lang) {
    s_lang = (lang == ENCODING_LANG_ZH) ? ENCODING_LANG_ZH : ENCODING_LANG_JA;
}

EncodingLang encoding_get_lang(void) {
    return s_lang;
}

bool encoding_is_zh(void) {
    return s_lang == ENCODING_LANG_ZH;
}

bool encoding_parse_lang(const char *text, EncodingLang *out) {
    if (!text || !out) return false;
    if (strcmp(text, "ja") == 0 || strcmp(text, "jp") == 0 ||
        strcmp(text, "jpn") == 0 || strcmp(text, "japanese") == 0) {
        *out = ENCODING_LANG_JA;
        return true;
    }
    if (strcmp(text, "zh") == 0 || strcmp(text, "cn") == 0 ||
        strcmp(text, "chs") == 0 || strcmp(text, "zh-cn") == 0 ||
        strcmp(text, "chinese") == 0) {
        *out = ENCODING_LANG_ZH;
        return true;
    }
    return false;
}

const char *encoding_lang_code(void) {
    return s_lang == ENCODING_LANG_ZH ? "zh" : "ja";
}

const char *encoding_backend(void) {
    return s_backend;
}

// ---------------------------------------------------------------------------
// Plugins/*.txt code tables
// ---------------------------------------------------------------------------

typedef struct {
    EncodingTableInfo info;
    EncodingPair     *pairs;
    int               count;
    int               capacity;
} EncodingTable;

static EncodingTable s_tables[ENCODING_MAX_TABLES];
static int           s_table_count = 0;
static bool          s_ready = false;

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *out_size = got;
    return buf;
}

// EmuCheat tables are GBK text; GBK.txt ships with a UTF-8 BOM instead, so the
// container encoding is detected rather than assumed.
static char *decode_container(const uint8_t *raw, size_t raw_size, size_t *out_size,
                              char *enc_name, size_t enc_name_size) {
    if (raw_size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        snprintf(enc_name, enc_name_size, "utf-8");
        char *text = (char *)malloc(raw_size); // BOM is dropped
        if (!text) return NULL;
        memcpy(text, raw + 3, raw_size - 3);
        text[raw_size - 3] = '\0';
        *out_size = raw_size - 3;
        return text;
    }

    static const char *const gbk_names[] = { "GBK", "CP936", "GB18030" };
    for (size_t i = 0; i < sizeof(gbk_names) / sizeof(gbk_names[0]); i++) {
        iconv_t cd = iconv_open("UTF-8", gbk_names[i]);
        if (cd == (iconv_t)-1) continue;
        size_t cap = raw_size * 2 + 32;
        char *text = (char *)malloc(cap);
        if (!text) {
            iconv_close(cd);
            return NULL;
        }
        size_t out_len = 0;
        bool ok = convert_buf(cd, (const char *)raw, raw_size, text, cap - 1, &out_len);
        iconv_close(cd);
        if (ok) {
            text[out_len] = '\0';
            snprintf(enc_name, enc_name_size, "gbk");
            *out_size = out_len;
            return text;
        }
        free(text);
    }
    return NULL;
}

static void table_push(EncodingTable *t, uint16_t code, uint32_t cp) {
    if (t->count == t->capacity) {
        int cap = t->capacity ? t->capacity * 2 : 1024;
        EncodingPair *grown = (EncodingPair *)realloc(t->pairs, (size_t)cap * sizeof(EncodingPair));
        if (!grown) return;
        t->pairs = grown;
        t->capacity = cap;
    }
    t->pairs[t->count].code = code;
    t->pairs[t->count].codepoint = cp;
    t->count++;
}

static int pair_cmp(const void *a, const void *b) {
    uint16_t ca = ((const EncodingPair *)a)->code;
    uint16_t cb = ((const EncodingPair *)b)->code;
    return (ca > cb) - (ca < cb);
}

static bool parse_code_table(EncodingTable *t, const char *name, const char *path) {
    memset(t, 0, sizeof(*t));
    snprintf(t->info.name, sizeof(t->info.name), "%s", name);
    snprintf(t->info.path, sizeof(t->info.path), "%s", path);

    size_t raw_size = 0;
    uint8_t *raw = read_whole_file(path, &raw_size);
    if (!raw) return false;

    size_t text_size = 0;
    char *text = decode_container(raw, raw_size, &text_size,
                                  t->info.source_encoding,
                                  sizeof(t->info.source_encoding));
    free(raw);
    if (!text) return false;

    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        t->info.lines++;

        size_t i = 0;
        while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r')) i++;
        if (i < len && p[i] != '#') {
            size_t eq = i;
            while (eq < len && p[eq] != '=') eq++;
            if (eq < len && eq > i && eq - i <= 4) {
                uint32_t code = 0;
                bool hex_ok = true;
                for (size_t k = i; k < eq; k++) {
                    char c = p[k];
                    int v;
                    if (c >= '0' && c <= '9') v = c - '0';
                    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                    else { hex_ok = false; break; }
                    code = (code << 4) | (uint32_t)v;
                }
                if (hex_ok) {
                    int adv = 0;
                    uint32_t cp = encoding_utf8_decode(p + eq + 1, &adv);
                    if (cp != 0 && cp != 0xFFFD) {
                        table_push(t, (uint16_t)code, cp);
                    } else {
                        t->info.skipped++;
                    }
                } else {
                    t->info.skipped++;
                }
            } else {
                t->info.skipped++;
            }
        }

        p += len;
        if (*p == '\n') p++;
    }
    free(text);

    if (t->count > 1) qsort(t->pairs, (size_t)t->count, sizeof(EncodingPair), pair_cmp);

    // Drop duplicate keys (the first record wins).
    int unique = 0;
    for (int i = 0; i < t->count; i++) {
        if (unique > 0 && t->pairs[unique - 1].code == t->pairs[i].code) {
            t->info.skipped++;
            continue;
        }
        t->pairs[unique++] = t->pairs[i];
    }
    t->count = unique;

    t->info.entries = (uint32_t)t->count;
    t->info.loaded = true;
    return true;
}

bool encoding_init(const char *plugin_dir) {
    static const struct { const char *name; const char *file; } defs[ENCODING_MAX_TABLES] = {
        { "koei", "koei.txt" },
        { "gbk",  "GBK.txt"  },
        { "big5", "Big5.txt" },
    };
    for (int i = 0; i < s_table_count; i++) {
        free(s_tables[i].pairs);
        s_tables[i].pairs = NULL;
        s_tables[i].count = 0;
        s_tables[i].capacity = 0;
    }
    s_table_count = 0;
    s_ready = true;

    const char *dir = (plugin_dir && plugin_dir[0]) ? plugin_dir : "Plugins";
    for (int i = 0; i < ENCODING_MAX_TABLES; i++) {
        EncodingTable *t = &s_tables[s_table_count++];
        char path[ENCODING_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, defs[i].file);
        if (!parse_code_table(t, defs[i].name, path)) {
            // Keep the descriptor so --dump-code-table can report the miss.
            memset(t, 0, sizeof(*t));
            snprintf(t->info.name, sizeof(t->info.name), "%s", defs[i].name);
            snprintf(t->info.path, sizeof(t->info.path), "%s", path);
            snprintf(t->info.source_encoding, sizeof(t->info.source_encoding), "?");
            t->info.loaded = false;
        }
    }
    ensure_cp932();
    return true;
}

void encoding_shutdown(void) {
    for (int i = 0; i < s_table_count; i++) {
        free(s_tables[i].pairs);
        s_tables[i].pairs = NULL;
        s_tables[i].count = 0;
        s_tables[i].capacity = 0;
    }
    s_table_count = 0;
    s_ready = false;
    if (s_dec != (iconv_t)-1) {
        iconv_close(s_dec);
        s_dec = (iconv_t)-1;
    }
    if (s_enc != (iconv_t)-1) {
        iconv_close(s_enc);
        s_enc = (iconv_t)-1;
    }
    snprintf(s_backend, sizeof(s_backend), "none");
}

bool encoding_is_ready(void) {
    return s_ready;
}

int encoding_table_count(void) {
    return s_table_count;
}

const EncodingTableInfo *encoding_table_info(int index) {
    if (index < 0 || index >= s_table_count) return NULL;
    return &s_tables[index].info;
}

const EncodingTableInfo *encoding_table_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_table_count; i++) {
        if (strcmp(s_tables[i].info.name, name) == 0) return &s_tables[i].info;
    }
    return NULL;
}

uint32_t encoding_table_lookup(const char *name, uint16_t code) {
    for (int i = 0; i < s_table_count; i++) {
        if (!name || strcmp(s_tables[i].info.name, name) != 0) continue;
        int lo = 0;
        int hi = s_tables[i].count - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            uint16_t key = s_tables[i].pairs[mid].code;
            if (key == code) return s_tables[i].pairs[mid].codepoint;
            if (key < code) lo = mid + 1;
            else hi = mid - 1;
        }
        return 0;
    }
    return 0;
}

void encoding_table_summary(char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < s_table_count; i++) {
        int n = snprintf(dst + used, dst_size - used, "%s%s=%u",
                         i ? " " : "", s_tables[i].info.name,
                         s_tables[i].info.loaded ? s_tables[i].info.entries : 0u);
        if (n < 0 || (size_t)n >= dst_size - used) break;
        used += (size_t)n;
    }
}

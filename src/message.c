#include "message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decompress.h"

// ---------------------------------------------------------------------------
// Message.gp (LS11 text container) loader
//
// Reference: tools/extractor/extract_text.py
//   * container header "LS11", dictionary blob[0x10:0x110]
//   * 12-byte big-endian records from 0x110 (clen, dlen, off)
//   * per-entry string index table (uint16 LE offsets)
// ---------------------------------------------------------------------------

#define MESSAGE_DICT_OFFSET   0x10
#define MESSAGE_TABLE_OFFSET  0x110
#define MESSAGE_RECORD_SIZE   12

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool is_strip_byte(uint8_t c) {
    return c == '\0' || c == ' ' || c == '\r';
}

static bool is_ascii_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Trim a display string in place, mirroring the Python tool's
// bytes.strip(b'\x00 \r') followed by str.strip().
static size_t trim_display(char *s, size_t n) {
    size_t start = 0;
    while (start < n && is_strip_byte((uint8_t)s[start])) start++;
    size_t end = n;
    while (end > start && is_strip_byte((uint8_t)s[end - 1])) end--;
    while (start < end && is_ascii_space((uint8_t)s[start])) start++;
    while (end > start && is_ascii_space((uint8_t)s[end - 1])) end--;

    size_t len = end - start;
    if (start > 0 && len > 0) memmove(s, s + start, len);
    s[len] = '\0';
    return len;
}

// Parse the per-entry string index table into out->offsets/out->count.
static void parse_string_table(MessageEntry *out) {
    out->offsets = NULL;
    out->count = 0;
    if (out->raw_size < 4) return;

    uint16_t first_off = le16(out->raw);
    if (first_off == 0 || (size_t)first_off >= out->raw_size) return;

    int count = first_off / 2;
    if (count <= 0) return;
    // The offset table itself must fit inside the entry.
    if ((size_t)count * 2 > out->raw_size) return;

    uint16_t *offsets = (uint16_t*)malloc(((size_t)count + 1) * sizeof(uint16_t));
    if (!offsets) return;

    for (int i = 0; i < count; i++) {
        offsets[i] = le16(out->raw + (size_t)i * 2);
    }
    // Sentinels past 0xFFFF cannot be addressed by a uint16 table anyway, so a
    // huge final entry is clamped exactly like the Python tool's slice end.
    offsets[count] = (out->raw_size > 0xFFFFu) ? 0xFFFFu : (uint16_t)out->raw_size;

    out->offsets = offsets;
    out->count = count;
}

bool message_load(const char *path, MessageFile *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[MESSAGE] Cannot open %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= MESSAGE_TABLE_OFFSET) {
        fprintf(stderr, "[MESSAGE] %s too small for an LS11 container\n", path);
        fclose(f);
        return false;
    }

    uint8_t *blob = (uint8_t*)malloc((size_t)file_size);
    if (!blob) { fclose(f); return false; }
    if (fread(blob, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(blob); fclose(f); return false;
    }
    fclose(f);

    if (memcmp(blob, "LS11", 4) != 0) {
        fprintf(stderr, "[MESSAGE] %s is not a KOEI LS11 container\n", path);
        free(blob);
        return false;
    }

    memcpy(out->dict, blob + MESSAGE_DICT_OFFSET, MESSAGE_DICT_SIZE);

    size_t o = MESSAGE_TABLE_OFFSET;
    int idx = 0;
    while (o + MESSAGE_RECORD_SIZE <= (size_t)file_size && idx < MESSAGE_MAX_ENTRIES) {
        uint32_t clen = be32(blob + o);
        uint32_t dlen = be32(blob + o + 4);
        uint32_t off  = be32(blob + o + 8);

        if (off == 0 || off >= (size_t)file_size || clen == 0) break;
        if ((size_t)off + clen > (size_t)file_size) break;

        uint8_t *data = NULL;
        size_t data_size = 0;
        if (!ls11_decompress_stream(blob + off, clen, out->dict, MESSAGE_DICT_SIZE,
                                    dlen, &data, &data_size)) {
            fprintf(stderr, "[MESSAGE] entry %d failed to decompress\n", idx);
            break;
        }

        MessageEntry *e = &out->entries[idx];
        e->raw = data;
        e->raw_size = data_size;
        parse_string_table(e);

        o += MESSAGE_RECORD_SIZE;
        idx++;
    }

    free(blob);

    out->num_entries = idx;
    out->loaded = (idx > 0);
    return out->loaded;
}

void message_free(MessageFile *msg) {
    if (!msg) return;
    for (int i = 0; i < msg->num_entries; i++) {
        free(msg->entries[i].raw);
        free(msg->entries[i].offsets);
    }
    memset(msg, 0, sizeof(*msg));
}

int message_entry_count(const MessageFile *msg) {
    return msg ? msg->num_entries : 0;
}

int message_string_count(const MessageFile *msg, int entry) {
    if (!msg || entry < 0 || entry >= msg->num_entries) return 0;
    return msg->entries[entry].count;
}

size_t message_get_text(const MessageFile *msg, int entry, int index,
                        char *dst, size_t dst_size) {
    if (!msg || !dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (entry < 0 || entry >= msg->num_entries) return 0;

    const MessageEntry *e = &msg->entries[entry];
    if (!e->raw || !e->offsets || index < 0 || index >= e->count) return 0;

    size_t s_off = e->offsets[index];
    size_t e_off = e->offsets[index + 1];
    if (s_off > e->raw_size) s_off = e->raw_size;
    if (e_off > e->raw_size) e_off = e->raw_size;
    if (e_off < s_off) e_off = s_off;

    static const char wait_marker[] = " [WAIT] ";
    const size_t wait_len = sizeof(wait_marker) - 1;

    size_t out = 0;
    for (size_t i = s_off; i < e_off; i++) {
        uint8_t c = e->raw[i];
        if (c == 0x05) {
            if (out + wait_len + 1 >= dst_size) break;
            memcpy(dst + out, wait_marker, wait_len);
            out += wait_len;
        } else if (c == 0x0A) {
            if (out + 2 > dst_size) break;
            dst[out++] = '\n';
        } else {
            if (out + 2 > dst_size) break;
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';

    return trim_display(dst, out);
}

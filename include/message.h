#ifndef YPHZ_MESSAGE_H
#define YPHZ_MESSAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// KOEI LS11 text container (Message.gp).
//
// Message.gp starts with "LS11", followed by a 256-byte literal dictionary at
// 0x10 and a table of 12-byte big-endian records from 0x110:
//     uint32 compressed_len, uint32 decompressed_len, uint32 data_offset
// Each record's payload is an LS11 bitstream that decodes to an index table:
// a uint16 offset to the first string followed by one uint16 offset per string.
// This mirrors tools/extractor/extract_text.py exactly.

#define MESSAGE_DICT_SIZE   256
#define MESSAGE_MAX_ENTRIES 128

// DOS / PC-98 compact layout convention (see hal_video.h).
#pragma pack(push, 1)

typedef struct {
    uint8_t  *raw;       // exact decompressed bytes (byte-identical to extract_text.py)
    size_t    raw_size;
    uint16_t *offsets;   // string start offsets; [count] entries (last = raw_size)
    int       count;     // number of strings in this entry
} MessageEntry;

typedef struct {
    uint8_t      dict[MESSAGE_DICT_SIZE];
    MessageEntry entries[MESSAGE_MAX_ENTRIES];
    int          num_entries;
    bool         loaded;
} MessageFile;

#pragma pack(pop)

// Load and fully decode a Message.gp file. Returns false (and leaves a zeroed
// MessageFile) on any failure. Call message_free() when finished.
bool message_load(const char *path, MessageFile *out);
void message_free(MessageFile *msg);

int message_entry_count(const MessageFile *msg);
int message_string_count(const MessageFile *msg, int entry);

// Copy string `index` of `entry` into dst as a NUL-terminated display string.
// Control codes are translated the same way as the Python reference:
//   0x05 -> " [WAIT] ", 0x0A -> '\n'
// and trailing/leading NUL / space / CR are trimmed. Returns the byte count
// written (excluding the NUL), or 0 when out of range / on failure.
size_t message_get_text(const MessageFile *msg, int entry, int index,
                        char *dst, size_t dst_size);

#endif // YPHZ_MESSAGE_H

#ifndef YPHZ_NPK_H
#define YPHZ_NPK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "decompress.h"

// ---------------------------------------------------------------------------
// Generic NPK016 container access.
//
// A .gp container is just a byte stream with one or more "NPK016" block
// headers. Each block is independently LZ-compressed (see decompress.c), so we
// only remember the offsets and decode blocks on demand. This keeps large
// packs such as Hkeshiki.gp (4 x 640x400) off the heap until needed.
// ---------------------------------------------------------------------------

// 子任务 13: the opening/ending data files are *multi-pack* containers.
// Opendat.gp holds 52 back-to-back NPK016 images, Enddat.gp holds 84, so the
// table must be larger than the single-scene packs (Hkeshiki.gp = 4).
#define NPK_MAX_BLOCKS 128

typedef struct {
    char     name[64];
    uint8_t *data;
    size_t   size;
    size_t   offsets[NPK_MAX_BLOCKS];
    int      count;
} NpkFile;

// Load `dir/filename` and index every NPK016 header. Returns false on I/O
// failure or when the file contains no NPK016 block.
bool npk_load(NpkFile *npk, const char *dir, const char *filename);

// Same, but with an explicit path (子任务 13: --dump-packs <file>).
bool npk_load_path(NpkFile *npk, const char *path);

// Header-only block description: image dimensions and the number of bytes the
// block occupies (next header offset minus this one). No decompression.
bool npk_block_info(const NpkFile *npk, int index, uint16_t *width,
                    uint16_t *height, size_t *size);

// Decode block `index` into `out`. Returns false when out of range or when the
// decompressor rejects the block.
bool npk_decode(const NpkFile *npk, int index, ImageDecoded *out);

void npk_free(NpkFile *npk);

#endif // YPHZ_NPK_H

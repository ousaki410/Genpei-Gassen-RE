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

#define NPK_MAX_BLOCKS 64

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

// Decode block `index` into `out`. Returns false when out of range or when the
// decompressor rejects the block.
bool npk_decode(const NpkFile *npk, int index, ImageDecoded *out);

void npk_free(NpkFile *npk);

#endif // YPHZ_NPK_H

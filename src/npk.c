#include "npk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

bool npk_load_path(NpkFile *npk, const char *path) {
    if (!npk || !path) return false;
    memset(npk, 0, sizeof(*npk));
    snprintf(npk->name, sizeof(npk->name), "%s", path);

    npk->data = read_whole_file(path, &npk->size);
    if (!npk->data) return false;

    for (size_t off = 0; off + 6 <= npk->size && npk->count < NPK_MAX_BLOCKS; ) {
        if (memcmp(npk->data + off, "NPK016", 6) == 0) {
            npk->offsets[npk->count++] = off;
            off += 6;
        } else {
            off++;
        }
    }

    if (npk->count == 0) {
        npk_free(npk);
        return false;
    }
    return true;
}

bool npk_load(NpkFile *npk, const char *dir, const char *filename) {
    if (!npk || !dir || !filename) return false;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    if (!npk_load_path(npk, path)) return false;
    snprintf(npk->name, sizeof(npk->name), "%s", filename);
    return true;
}

bool npk_block_info(const NpkFile *npk, int index, uint16_t *width,
                    uint16_t *height, size_t *size) {
    if (!npk || !npk->data || index < 0 || index >= npk->count) return false;
    size_t off = npk->offsets[index];
    if (off + 16 > npk->size) return false;

    if (width) *width = (uint16_t)(npk->data[off + 12] | (npk->data[off + 13] << 8));
    if (height) *height = (uint16_t)(npk->data[off + 14] | (npk->data[off + 15] << 8));
    if (size) {
        size_t end = (index + 1 < npk->count) ? npk->offsets[index + 1] : npk->size;
        *size = end - off;
    }
    return true;
}

bool npk_decode(const NpkFile *npk, int index, ImageDecoded *out) {
    if (!npk || !npk->data || !out || index < 0 || index >= npk->count) return false;
    return nup4_decompress_block(npk->data, npk->size, npk->offsets[index], out, NULL);
}

void npk_free(NpkFile *npk) {
    if (!npk) return;
    free(npk->data);
    npk->data = NULL;
    npk->size = 0;
    npk->count = 0;
}

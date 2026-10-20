/*
 * raw_sprite.c — PC-98 4-plane raw bitmap decoder (子任务 11)
 *
 * The on-disk layout and the solved geometry are documented in
 * raw_sprite.h; tools/solve_raw_layout.py reproduces the analysis.
 */
#include "raw_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool raw_sprite_decode_4plane(const uint8_t *data, size_t data_len,
                              uint16_t width, uint16_t height,
                              RawSprite *out)
{
    if (!data || !out || width == 0 || height == 0)
        return false;

    const uint16_t groups = (uint16_t)((width + 7) / 8);
    const size_t   row_bytes = (size_t)groups * 4;
    if (data_len < row_bytes * height)
        return false;

    out->width = width;
    out->height = height;
    out->pixels = (uint8_t *)malloc((size_t)width * height);
    if (!out->pixels) {
        out->width = 0;
        out->height = 0;
        return false;
    }
    memset(out->pixels, 0, (size_t)width * height);

    for (uint16_t y = 0; y < height; y++) {
        const uint8_t *row = data + (size_t)y * row_bytes;
        uint8_t *dst = out->pixels + (size_t)y * width;
        for (uint16_t g = 0; g < groups; g++) {
            const uint8_t b0 = row[g * 4 + 0];
            const uint8_t b1 = row[g * 4 + 1];
            const uint8_t b2 = row[g * 4 + 2];
            const uint8_t b3 = row[g * 4 + 3];
            for (int bit = 0; bit < 8; bit++) {
                const uint16_t x = g * 8 + (uint16_t)bit;
                if (x >= width)
                    break;
                const int shift = 7 - bit;
                dst[x] = (uint8_t)(((b0 >> shift) & 1) |
                                   (((b1 >> shift) & 1) << 1) |
                                   (((b2 >> shift) & 1) << 2) |
                                   (((b3 >> shift) & 1) << 3));
            }
        }
    }
    return true;
}

int raw_sprite_decode_tile_bank(const uint8_t *data, size_t data_len,
                                uint16_t tile_w, uint16_t tile_h,
                                int max_tiles, RawSpriteBank *bank)
{
    if (!data || !bank || tile_w == 0 || tile_h == 0 || max_tiles <= 0)
        return 0;

    const size_t tile_bytes = (size_t)((tile_w + 7) / 8) * 4 * tile_h;
    if (tile_bytes == 0 || data_len < tile_bytes)
        return 0;

    int n = (int)(data_len / tile_bytes);
    if (n > max_tiles)
        n = max_tiles;
    if (n > RAW_SPRITE_MAX_TILES)
        n = RAW_SPRITE_MAX_TILES;

    bank->count = 0;
    bank->tile_w = tile_w;
    bank->tile_h = tile_h;
    for (int i = 0; i < n; i++) {
        if (!raw_sprite_decode_4plane(data + (size_t)i * tile_bytes, tile_bytes,
                                      tile_w, tile_h, &bank->tiles[i]))
            break;
        bank->count = i + 1;
    }
    return bank->count;
}

bool raw_sprite_load_tile_bank(const char *path, uint16_t tile_w, uint16_t tile_h,
                               int max_tiles, RawSpriteBank *bank)
{
    if (!path || !bank)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    const long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    rewind(fp);

    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return false;
    }
    const size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(data);
        return false;
    }

    const int n = raw_sprite_decode_tile_bank(data, got, tile_w, tile_h,
                                               max_tiles, bank);
    free(data);
    return n > 0;
}

bool raw_sprite_load_sheet(const char *path, RawSprite *out)
{
    if (!path || !out)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    const long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    rewind(fp);

    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return false;
    }
    const size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(data);
        return false;
    }

    const uint16_t rows = (uint16_t)(size / RAW_SPRITE_ROW_BYTES);
    const bool ok = raw_sprite_decode_4plane(data, (size_t)size,
                                              RAW_SPRITE_WIDTH, rows, out);
    free(data);
    return ok;
}

void raw_sprite_free_bank(RawSpriteBank *bank)
{
    if (!bank)
        return;
    for (int i = 0; i < bank->count && i < RAW_SPRITE_MAX_TILES; i++) {
        free(bank->tiles[i].pixels);
        bank->tiles[i].pixels = NULL;
        bank->tiles[i].width = 0;
        bank->tiles[i].height = 0;
    }
    bank->count = 0;
}

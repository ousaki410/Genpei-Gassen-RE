/*
 * raw_sprite.c — PC-98 4-plane raw bitmap decoder (subtask 10 part 2)
 *
 * On-disk layout of the raw battle sheets (Hunitpat.gp / Hchikei.gp /
 * Mainobj.gp).  They are *not* NPK016 containers: every scanline stores the
 * four colour planes of that line back to back.
 *
 *     row bytes = 4 * ceil(width / 8)
 *
 * and inside a row the four plane segments appear in plane order
 *
 *     [ plane 0 ][ plane 1 ][ plane 2 ][ plane 3 ]
 *
 * Each plane segment is ceil(width/8) bytes, one bit per pixel, bit 7 being
 * the leftmost pixel.  A pixel's colour index is
 *
 *     p0 | p1<<1 | p2<<2 | p3<<3
 *
 * which is the same index space (and the same 16-colour PC-98 palette) that
 * the NPK016 decoder in decompress.c produces.
 *
 * Sizes that follow from the layout:
 *
 *     Hunitpat.gp  151936 B = 128 B * 1187   -> 1187 tiles of 16x16
 *     Hchikei.gp    10060 B = 76 + 78 * 128 B -> 78 tiles of 16x16
 *     Mainobj.gp    24544 B = 104 B * 236     -> 208x236 sheet
 *                   (26 B per plane per row x 4 planes = 104 B per row)
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

    const uint16_t plane_bytes = (uint16_t)((width + 7) / 8);
    const size_t   row_bytes = (size_t)plane_bytes * 4;
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
        for (uint16_t plane = 0; plane < 4; plane++) {
            const uint8_t *bits = row + (size_t)plane * plane_bytes;
            for (uint16_t g = 0; g < plane_bytes; g++) {
                const uint8_t byte = bits[g];
                if (byte == 0)
                    continue;
                for (int bit = 0; bit < 8; bit++) {
                    const uint16_t x = g * 8 + (uint16_t)bit;
                    if (x >= width)
                        break;
                    if ((byte >> (7 - bit)) & 1)
                        dst[x] |= (uint8_t)(1u << plane);
                }
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

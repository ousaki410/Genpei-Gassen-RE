/*
 * raw_sprite.c — 4‑plane raw sprite decoder (subtask 10 part 2)
 *
 * Implements the exact PC‑98 4‑plane layout used by Hunitpat.gp (1187 × 16×16)
 * and Hchikei.gp (78 × 16×16). Also covers Mainobj.gp (208×236 sheet).
 */
#include "raw_sprite.h"
#include <stdlib.h>
#include <string.h>

bool raw_sprite_decode(const uint8_t *data, size_t data_len,
                       uint16_t raw_w, uint16_t raw_h,
                       RawSprite *sprite)
{
    if (!data || data_len < 4 * ((raw_w + 7) / 8) * raw_h)
        return false;

    uint16_t wb = (raw_w + 7) / 8;               /* bytes per plane per row */
    uint16_t plane_row_bytes = wb * 4;            /* 4 planes × wb */
    uint16_t row_size = wb * 4;                   /* total per row (interleaved planes) */

    sprite->width = raw_w;
    sprite->height = raw_h;

    /* Allocate 1‑bpp indexed output (0 = transparent) */
    size_t pixel_sz = ((raw_w + 7) / 8) * raw_h;
    sprite->pixels = malloc(pixel_sz);
    if (!sprite->pixels)
        return false;
    memset(sprite->pixels, 0, pixel_sz);

    const uint8_t *src = data;
    uint8_t *dst = sprite->pixels;

    for (uint16_t y = 0; y < raw_h; y++) {
        for (uint16_t row_start = 0; row_start < wb; row_start += 1) {
            /* Each 4‑byte chunk = 8 pixels */
            for (uint16_t chunk = 0; chunk < 4; chunk++) {
                uint8_t b0 = src[0];
                uint8_t b1 = src[1];
                uint8_t b2 = src[2];
                uint8_t b3 = src[3];
                src += 4;

                for (int bit = 7; bit >= 0; bit--) {
                    uint16_t x = row_start * 8 + chunk * 8 + (7 - bit);
                    if (x >= raw_w)
                        continue;

                    uint8_t color = ((b3 >> bit) & 1) | (((b2 >> bit) & 1) << 1) |
                                    (((b1 >> bit) & 1) << 2) | ((b0 >> bit) & 1);
                    if (color != 0) {
                        dst[x / 8] |= (color << (x % 8));
                    }
                }
            }
        }
    }

    return true;
}

bool raw_sprite_decode_16x16(const uint8_t *tile_data,
                             const uint8_t *palette_16,
                             RawSprite *sprite)
{
    /* Tile has stride 2 bytes per row (4 planes × (16/8) bytes/row). */
    return raw_sprite_decode(tile_data, 128, 16, 16, sprite);
}

void raw_sprite_free(RawSprite *sprite)
{
    free(sprite->pixels);
    sprite->pixels = NULL;
    sprite->width = 0;
    sprite->height = 0;
}
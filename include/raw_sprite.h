/*
 * raw_sprite.h — PC-98 4-plane (16 colour) raw sprite decoder
 *
 * 子任务 10 part 2: the battle assets that are *not* packed into NPK016
 * containers (Hunitpat.gp, Hchikei.gp, Mainobj.gp) store their pixels as raw
 * 4-plane bitmaps in the same planar order the engine already uses for
 * decompressed NPK blocks (see decompress.c):
 *
 *   - a scanline is a sequence of 8-pixel groups;
 *   - each group is 4 bytes, one byte per colour plane (plane 0 = LSB);
 *   - inside a byte, bit 7 is the leftmost pixel;
 *   - a pixel's colour index is  b0 | b1<<1 | b2<<2 | b3<<3.
 *
 * Sizes that follow from that layout:
 *
 *   Hunitpat.gp  151936 B = 1187 x 128 B  -> 1187 tiles of 16x16
 *   Hchikei.gp    10060 B = 76 + 78 x 128 B (also 100x100 tile ids + 60 B)
 *   Mainobj.gp    24544 B = 104 x 236 B    -> 208x236 sheet
 *                 (26 B per plane per row x 4 planes = 104 B per row)
 */
#ifndef YPHZ_RAW_SPRITE_H
#define YPHZ_RAW_SPRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Geometry of the unit-pattern bank (Hunitpat.gp). */
#define RAW_SPRITE_TILE_W    16
#define RAW_SPRITE_TILE_H    16
#define RAW_SPRITE_TILE_BYTES (RAW_SPRITE_TILE_W * RAW_SPRITE_TILE_H / 2)

/* Upper bound for a decoded tile bank (Hunitpat.gp holds 1187 tiles). */
#define RAW_SPRITE_MAX_TILES 2048

/* A decoded 4-bit-per-pixel image, expanded to 8 bit indices (0..15). */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;      /* row major, width * height bytes */
} RawSprite;

/* A bank of equally sized tiles decoded from one file. */
typedef struct {
    RawSprite tiles[RAW_SPRITE_MAX_TILES];
    int      count;
    uint16_t tile_w;
    uint16_t tile_h;
} RawSpriteBank;

/* Decode one raw 4-plane bitmap.  Returns false when `data` is too short. */
bool raw_sprite_decode_4plane(const uint8_t *data, size_t data_len,
                              uint16_t width, uint16_t height,
                              RawSprite *out);

/* Decode a bank of tw x th tiles.  Returns the number of decoded tiles. */
int raw_sprite_decode_tile_bank(const uint8_t *data, size_t data_len,
                                 uint16_t tile_w, uint16_t tile_h,
                                 int max_tiles, RawSpriteBank *bank);

/* Load and decode a tile bank from a file. */
bool raw_sprite_load_tile_bank(const char *path, uint16_t tile_w, uint16_t tile_h,
                               int max_tiles, RawSpriteBank *bank);

/* Release every pixel buffer owned by a bank. */
void raw_sprite_free_bank(RawSpriteBank *bank);

#endif // YPHZ_RAW_SPRITE_H

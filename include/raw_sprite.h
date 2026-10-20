/*
 * raw_sprite.h — PC-98 4-plane (16 colour) raw sprite decoder
 *
 * 子任务 11.  The raw battle sheets (Hunitpat.gp / Hchikei.gp / Mainobj.gp)
 * are *not* NPK016 containers: they store their pixels directly in the
 * game's native planar order, the same one the NPK/NUP4 decoder in
 * decompress.c already produces:
 *
 *   - a scanline is a sequence of 8-pixel groups;
 *   - each group is 4 bytes, one byte per colour plane (plane 0 = LSB);
 *   - inside a byte, bit 7 is the leftmost pixel;
 *   - a pixel's colour index is  b0 | b1<<1 | b2<<2 | b3<<3.
 *
 * The row stride therefore is 4 * ceil(width/8) bytes, and the solved
 * geometry of the shipped files is:
 *
 *   Hunitpat.gp  151936 B  stride 12 B -> 24 px wide, 12661 rows.
 *                             Unit sprites are 24x32 px on a 36-row pitch;
 *                             the 12 sprite types are the 12 factions.
 *   Mainobj.gp    24544 B  stride 12 B -> 24 px wide, 2045 rows.
 *                             Same 24x32 unit sprites plus terrain tiles.
 *   Hchikei.gp    10060 B  not a bitmap at all: a 100x100 grid of one-byte
 *                             terrain tile ids (13 distinct ids) followed by
 *                             a 60 byte formation table.
 *
 * The layout was recovered with tools/solve_raw_layout.py (byte
 * autocorrelation for the stride, vertical coherence for the width, spatial
 * coherence for the plane order).
 */
#ifndef YPHZ_RAW_SPRITE_H
#define YPHZ_RAW_SPRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The solved geometry of the shipped raw sheets. */
#define RAW_SPRITE_WIDTH      24     /* pixels per scanline            */
#define RAW_SPRITE_ROW_BYTES  12     /* 3 groups x 4 plane bytes       */
#define RAW_SPRITE_UNIT_W     24     /* unit sprite width              */
#define RAW_SPRITE_UNIT_H     32     /* unit sprite height             */
#define RAW_SPRITE_UNIT_PITCH 36     /* rows between sprite origins    */

/* A decoded 4-bit-per-pixel image, expanded to 8 bit indices (0..15). */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;      /* row major, width * height bytes */
} RawSprite;

/* Hchikei.gp: a 100x100 grid of one-byte terrain tile ids followed by a
 * 60-byte formation table. 子任务 13 exposes it through --dump-chikei. */
#define CHIKEI_GRID_W          100
#define CHIKEI_GRID_H          100
#define CHIKEI_GRID_BYTES      (CHIKEI_GRID_W * CHIKEI_GRID_H)
#define CHIKEI_FORMATION_BYTES 60

/* Upper bound for a decoded tile bank. */
#define RAW_SPRITE_MAX_TILES 2048

/* A sheet of equally sized tiles decoded from one raw file. */
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

/* Decode a whole raw sheet (the solved 24 px stride). */
bool raw_sprite_load_sheet(const char *path, RawSprite *out);

/* Release every pixel buffer owned by a bank. */
void raw_sprite_free_bank(RawSpriteBank *bank);

#endif // YPHZ_RAW_SPRITE_H

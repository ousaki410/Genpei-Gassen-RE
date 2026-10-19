/*
 * raw_sprite.h — DOS Genpei Kassen 4‑plane raw sprite decoder
 *
 * 子任务 10 part 2: decode Hunitpat.gp (1187 × 16×16), Hchikei.gp (78 × 16×16),
 * and Mainobj.gp (208×236) in their native 4‑plane (4 × (16/8) bytes per row)
 * form. No compression, no NPK016; straight PS/VRAM‑compatible layout.
 */
#pragma once

#include <stdint.h>

/* Maximum sprite dimensions (square) supported by the decoder */
#define RAW_SPRITE_MAX_W 256
#define RAW_SPRITE_MAX_H 256

/* Output pixel format: 1bpp indexed (0 = transparent/background, >0 = color) */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;          /* big-endian indexed (8‑bytes per 64‑pixel batch) */
} RawSprite;

/* 4‑plane (bitplane) decoder for a raw .gp block.
 * Expected format:
 *   - For Hunitpat/Hchikei: each 16×16 tile = 4 planes × 128 bytes (row stride = 2 bytes).
 *   - For Mainobj: 208×236 sheet = 4 planes × 104 bytes/row × 236 rows.
 *
 * Plane order in file: MSB → LSB (bit 7 = first bit). Decode exactly as
 *   color = (b0<<0) | (b1<<1) | (b2<<2) | (b3<<3).
 *
 * Returns true on success and fills out `sprite`. Caller must free sprite->pixels.
 */
bool raw_sprite_decode(const uint8_t *data, size_t data_len,
                       uint16_t raw_w, uint16_t raw_h,
                       RawSprite *sprite);

/* Helper: a single 16×16 decomposition (matching Hunitpat/Hchikei stride 2). */
bool raw_sprite_decode_16x16(const uint8_t *tile_data,
                             const uint8_t *palette_16, /* unused for now, reserved for v2 */
                             RawSprite *sprite);

/* Helper: release a decoded sprite. */
void raw_sprite_free(RawSprite *sprite);
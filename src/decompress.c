#include "decompress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void free_decoded_image(ImageDecoded *img) {
    if (!img) return;
    if (img->pixels) { free(img->pixels); img->pixels = NULL; }
    if (img->mask)   { free(img->mask);   img->mask = NULL; }
    img->width = 0;
    img->height = 0;
}

// ---------------------------------------------------------------------------
// NPK016 / NUP4 Decompressor
// ---------------------------------------------------------------------------
static inline void nup4_transform_4bytes(uint16_t w1, uint16_t w2, uint8_t out[4]) {
    uint16_t bx = w1;
    uint16_t ax = w2;
    uint8_t bh = (bx >> 8) & 0xFF;
    uint8_t bl = bx & 0xFF;
    uint8_t ah = (ax >> 8) & 0xFF;
    uint8_t al = ax & 0xFF;

    uint8_t tmp = bh; bh = al; al = tmp;
    bx = ((uint16_t)bh << 8) | bl;
    ax = ((uint16_t)ah << 8) | al;

    bx = ((bx << 4) | (bx >> 12)) & 0xFFFF;
    bl = ((bx & 0xFF) << 4 | (bx & 0xFF) >> 4) & 0xFF;
    bx = (((bx & 0xFF00) | bl) << 4 | ((bx & 0xFF00) | bl) >> 12) & 0xFFFF;

    ax = ((ax << 4) | (ax >> 12)) & 0xFFFF;
    al = ((ax & 0xFF) << 4 | (ax & 0xFF) >> 4) & 0xFF;
    ax = (((ax & 0xFF00) | al) << 4 | ((ax & 0xFF00) | al) >> 12) & 0xFFFF;

    out[0] = ax & 0xFF;
    out[1] = (ax >> 8) & 0xFF;
    out[2] = bx & 0xFF;
    out[3] = (bx >> 8) & 0xFF;
}

static inline void nup4_transform_odd_word(uint16_t w, uint8_t out[4]) {
    uint16_t ax = w;
    for (int i = 0; i < 4; i++) {
        ax = ((ax >> 4) | (ax << 12)) & 0xFFFF;
        out[i] = ax & 0xF0;
    }
}

bool nup4_decompress_block(const uint8_t *data, size_t data_len, size_t offset, ImageDecoded *out_img, size_t *out_consumed) {
    if (!data || offset + 48 > data_len || !out_img) return false;

    if (memcmp(data + offset, "NPK016", 6) != 0) {
        return false;
    }

    int img_w = data[offset + 12] | (data[offset + 13] << 8);
    int img_h = data[offset + 14] | (data[offset + 15] << 8);

    for (int i = 0; i < 16; i++) {
        out_img->palette[i] = data[offset + 16 + i * 2] | (data[offset + 17 + i * 2] << 8);
    }

    int words_per_line = (img_w + 3) / 4;
    int ring_size = 1600;
    uint8_t ring[1600];
    memset(ring, 0, sizeof(ring));
    int ring_di = 0;

    const uint8_t *payload = data + offset + 48;
    size_t payload_len = data_len - (offset + 48);
    size_t si = 0;
    uint8_t bit_buf = 0;
    int bits_left = 0;

    int line_raw_bytes = (img_w + 7) / 8 * 4;
    uint8_t *planar_decomp = (uint8_t*)malloc(img_h * line_raw_bytes);
    if (!planar_decomp) return false;
    int planar_ptr = 0;

    for (int line = 0; line < img_h; line++) {
        int line_start = ring_di;
        int words_needed = words_per_line;

        while (words_needed > 0) {
            if (bits_left == 0) {
                if (si >= payload_len) { free(planar_decomp); return false; }
                bit_buf = payload[si++];
                bits_left = 8;
            }
            int bit = bit_buf & 1;
            bit_buf >>= 1;
            bits_left--;

            if (bit == 0) {
                if (si + 2 > payload_len) { free(planar_decomp); return false; }
                ring[ring_di] = payload[si++];
                ring[(ring_di + 1) % ring_size] = payload[si++];
                ring_di = (ring_di + 2) % ring_size;
                words_needed--;
            } else {
                if (si >= payload_len) { free(planar_decomp); return false; }
                uint8_t ctrl = payload[si++];
                int count = (ctrl & 0x1F) + 1;
                int step = ((ctrl >> 5) & 3) + 1;
                int offset_val = (ctrl & 0x80) ? (step * 320) : (step * 2);
                int src_pos = (ring_di - offset_val + ring_size * 4) % ring_size;

                for (int c = 0; c < count; c++) {
                    ring[ring_di] = ring[src_pos];
                    ring[(ring_di + 1) % ring_size] = ring[(src_pos + 1) % ring_size];
                    ring_di = (ring_di + 2) % ring_size;
                    src_pos = (src_pos + 2) % ring_size;
                }
                words_needed -= count;
            }
        }

        // Apply planar transformation
        int p = line_start;
        int pairs = words_per_line / 2;
        for (int pr = 0; pr < pairs; pr++) {
            uint16_t w1 = ring[p] | (ring[(p + 1) % ring_size] << 8);
            p = (p + 2) % ring_size;
            uint16_t w2 = ring[p] | (ring[(p + 1) % ring_size] << 8);
            p = (p + 2) % ring_size;

            uint8_t out4[4];
            nup4_transform_4bytes(w1, w2, out4);
            memcpy(planar_decomp + planar_ptr, out4, 4);
            planar_ptr += 4;
        }
        if (words_per_line % 2 != 0) {
            uint16_t w = ring[p] | (ring[(p + 1) % ring_size] << 8);
            p = (p + 2) % ring_size;
            uint8_t out_odd[4];
            nup4_transform_odd_word(w, out_odd);
            memcpy(planar_decomp + planar_ptr, out_odd, 4);
            planar_ptr += 4;
        }

        ring_di = (line_start + 320) % ring_size;
    }

    // Convert planar chunks to 8-bit indexed pixels
    uint8_t *pixels = (uint8_t*)malloc(img_w * img_h);
    if (!pixels) { free(planar_decomp); return false; }

    for (int y = 0; y < img_h; y++) {
        const uint8_t *line_raw = planar_decomp + y * line_raw_bytes;
        int out_x = 0;
        for (int chk = 0; chk < line_raw_bytes && out_x < img_w; chk += 4) {
            uint8_t b0 = line_raw[chk + 0];
            uint8_t b1 = line_raw[chk + 1];
            uint8_t b2 = line_raw[chk + 2];
            uint8_t b3 = line_raw[chk + 3];
            for (int bit = 0; bit < 8 && out_x < img_w; bit++) {
                int shift = 7 - bit;
                uint8_t c = (((b3 >> shift) & 1) << 3) |
                            (((b2 >> shift) & 1) << 2) |
                            (((b1 >> shift) & 1) << 1) |
                            ((b0 >> shift) & 1);
                pixels[y * img_w + out_x++] = c;
            }
        }
    }

    free(planar_decomp);

    out_img->width = img_w;
    out_img->height = img_h;
    out_img->pixels = pixels;
    out_img->mask = NULL;

    if (out_consumed) {
        *out_consumed = 48 + si;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Kaodata 0x728 Decompressor
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t n0, n1, n2;
} KaoUnit;

static size_t decompress_kao_single_block(const uint8_t *data, size_t data_len, size_t offset,
                                          int *out_w, int *out_h, KaoUnit **out_units) {
    if (offset + 4 > data_len) return 0;
    int w = data[offset] | (data[offset + 1] << 8);
    int h = data[offset + 2] | (data[offset + 3] << 8);
    int dx = (w + 3) / 4; // 32

    KaoUnit *units = (KaoUnit*)calloc(h * dx, sizeof(KaoUnit));
    if (!units) return 0;

    size_t si = offset + 4;
    for (int y = 0; y < h; y++) {
        int line_start = y * dx;
        int rem = dx;
        int cur_x = 0;

        while (rem > 0) {
            if (si >= data_len) { free(units); return 0; }
            uint8_t ctrl = data[si++];

            if ((ctrl & 0x80) == 0) {
                // Type A (0x8bb): RLE constant 3-nibble
                if (si >= data_len) { free(units); return 0; }
                uint8_t b2 = data[si++];
                int count = ((ctrl >> 4) & 7) + 1;
                uint8_t n0 = b2 & 0x0F;
                uint8_t n1 = (b2 >> 4) & 0x0F;
                uint8_t n2 = ctrl & 0x0F;
                for (int i = 0; i < count && cur_x + i < dx; i++) {
                    units[line_start + cur_x + i].n0 = n0;
                    units[line_start + cur_x + i].n1 = n1;
                    units[line_start + cur_x + i].n2 = n2;
                }
                cur_x += count;
                rem -= count;
            } else if ((ctrl & 0x40) == 0) {
                // Type B (0x77e): Copy from horizontal offset
                int count = (ctrl & 0x0F) + 1;
                int step = ((ctrl >> 4) & 3) + 1;
                for (int i = 0; i < count && cur_x + i < dx; i++) {
                    int src_u = line_start + cur_x + i - step;
                    units[line_start + cur_x + i] = units[src_u];
                }
                cur_x += count;
                rem -= count;
            } else {
                // Type C (0x83c): Copy from vertical offset
                int step = ((ctrl >> 4) & 3) + 1;
                int count = (ctrl & 0x0F) + 1;
                for (int i = 0; i < count && cur_x + i < dx; i++) {
                    int src_u = (y - step) * dx + cur_x + i;
                    units[line_start + cur_x + i] = units[src_u];
                }
                cur_x += count;
                rem -= count;
            }
        }
    }

    *out_w = w;
    *out_h = h;
    *out_units = units;
    return si;
}

static uint8_t* kao_units_to_pixels(const KaoUnit *units, int w, int h) {
    int dx = (w + 3) / 4;
    uint8_t *pix = (uint8_t*)malloc(w * h);
    if (!pix) return NULL;

    for (int y = 0; y < h; y++) {
        for (int x_u = 0; x_u < dx; x_u++) {
            KaoUnit u = units[y * dx + x_u];
            for (int bit_idx = 0; bit_idx < 4; bit_idx++) {
                int shift = 3 - bit_idx;
                uint8_t b0 = (u.n0 >> shift) & 1;
                uint8_t b1 = (u.n1 >> shift) & 1;
                uint8_t b2 = (u.n2 >> shift) & 1;
                uint8_t c = (b2 << 2) | (b1 << 1) | b0;
                pix[y * w + (x_u * 4 + bit_idx)] = c;
            }
        }
    }
    return pix;
}

bool kaodata_decompress_officer(const uint8_t *kaodata, size_t data_len, uint32_t file_offset, ImageDecoded *out_img) {
    if (!kaodata || file_offset >= data_len || !out_img) return false;

    int w_a, h_a;
    KaoUnit *units_a = NULL;
    size_t end_a = decompress_kao_single_block(kaodata, data_len, file_offset, &w_a, &h_a, &units_a);
    if (!end_a || !units_a) return false;

    int w_b, h_b;
    KaoUnit *units_b = NULL;
    size_t end_b = decompress_kao_single_block(kaodata, data_len, end_a, &w_b, &h_b, &units_b);

    uint8_t *pixels = kao_units_to_pixels(units_a, w_a, h_a);
    free(units_a);

    uint8_t *mask = NULL;
    if (end_b && units_b) {
        mask = kao_units_to_pixels(units_b, w_b, h_b);
        free(units_b);
    }

    out_img->width = w_a;
    out_img->height = h_a;
    out_img->pixels = pixels;
    out_img->mask = mask;
    return true;
}

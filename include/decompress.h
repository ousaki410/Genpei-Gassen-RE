#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// DOS / PC-98 16-bit compact memory layout: byte-level packing, no padding.
#pragma pack(push, 1)

typedef struct {
    int width;
    int height;
    uint8_t *pixels;       // 8-bit indexed pixels [width * height]
    uint8_t *mask;         // Optional 8-bit mask (for Kaodata: 0=opaque, 7=trans)
    uint16_t palette[16];  // Raw PC-98 palette if present in header
} ImageDecoded;

#pragma pack(pop)

// Native NPK016 / NUP4 decompressor (from Open.exe / Grpdrv.exe)
bool nup4_decompress_block(const uint8_t *data, size_t data_len, size_t offset, ImageDecoded *out_img, size_t *out_consumed);

// Native Kaodata 0x728 decompressor (from Main.exe:0x728-0x966)
bool kaodata_decompress_officer(const uint8_t *kaodata, size_t data_len, uint32_t file_offset, ImageDecoded *out_img);

void free_decoded_image(ImageDecoded *img);

#endif // DECOMPRESS_H

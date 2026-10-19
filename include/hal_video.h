#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PC98_WIDTH  640
#define PC98_HEIGHT 400

// DOS / PC-98 16-bit compact memory layout: byte-level packing, no compiler
// padding. Every struct in this project is laid out with #pragma pack(push, 1)
// so that host-side records match the original little-endian DOS images
// byte-for-byte.
#pragma pack(push, 1)

typedef struct {
    uint8_t r, g, b, a;
} ColorRGBA;

#pragma pack(pop)

bool hal_video_init(const char *title, int window_scale);
void hal_video_shutdown(void);
void hal_video_set_title(const char *title);

void hal_video_set_palette_entry(int index, uint8_t r, uint8_t g, uint8_t b);
void hal_video_load_pld_bank(const uint8_t *pld_data, int bank_idx);

void hal_video_clear(uint8_t color_idx);
void hal_video_draw_pixel(int x, int y, uint8_t color_idx);
void hal_video_fill_rect(int x, int y, int w, int h, uint8_t color_idx);
void hal_video_draw_rect(int x, int y, int w, int h, uint8_t color_idx);

void hal_video_blit_indexed(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, int src_pitch);
void hal_video_blit_transparent(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, int src_pitch, uint8_t transparent_idx);
void hal_video_blit_masked(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, const uint8_t *mask_pixels, int src_pitch);

void hal_video_present(void);
bool hal_video_save_bmp(const char *bmp_path);

// Input-support helpers. The HAL owns the window/renderer, so window <-> logical
// coordinate conversion and mouse-focus queries live here; the input layer
// (input.c) builds on top of them.
void hal_video_window_to_logical(int win_x, int win_y, int *out_x, int *out_y);
bool hal_video_has_mouse_focus(void);

uint8_t* hal_video_get_framebuffer(void);

// PC-98 / JIS font rendering subsystem
bool hal_video_load_fonts(const uint8_t *jis_data, size_t jis_size, const uint8_t *chr_data, size_t chr_size);
void hal_video_draw_char_8x16(int x, int y, uint8_t ch, uint8_t color_idx);
void hal_video_draw_string_8x16(int x, int y, const char *str, uint8_t color_idx);
void hal_video_draw_kanji_16x16(int x, int y, uint8_t s1, uint8_t s2, uint8_t color_idx);
void hal_video_draw_text_sjis(int x, int y, const char *sjis_str, uint8_t color_idx);

#endif // HAL_VIDEO_H

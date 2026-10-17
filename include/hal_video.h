#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#define PC98_WIDTH  640
#define PC98_HEIGHT 400

typedef struct {
    uint8_t r, g, b, a;
} ColorRGBA;

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
bool hal_video_poll_event(int *out_key, bool *out_quit);
bool hal_video_save_bmp(const char *bmp_path);

uint8_t* hal_video_get_framebuffer(void);

#endif // HAL_VIDEO_H

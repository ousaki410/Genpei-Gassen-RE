#include "hal_video.h"
#include "sdl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window   *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture = NULL;

static uint8_t  s_vram[PC98_WIDTH * PC98_HEIGHT];
static uint32_t s_rgba_buffer[PC98_WIDTH * PC98_HEIGHT];
static uint32_t s_palette[256];

bool hal_video_init(const char *title, int window_scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "[HAL_VIDEO] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    if (window_scale < 1) window_scale = 2;
    int win_w = PC98_WIDTH * window_scale;
    int win_h = PC98_HEIGHT * window_scale;

    s_window = SDL_CreateWindow(
        title ? title : "Genpei Kassen (PC-98 Linux Port)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!s_window) {
        fprintf(stderr, "[HAL_VIDEO] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        fprintf(stderr, "[HAL_VIDEO] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return false;
    }

    SDL_RenderSetLogicalSize(s_renderer, PC98_WIDTH, PC98_HEIGHT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        PC98_WIDTH, PC98_HEIGHT
    );
    if (!s_texture) {
        fprintf(stderr, "[HAL_VIDEO] SDL_CreateTexture failed: %s\n", SDL_GetError());
        hal_video_shutdown();
        return false;
    }

    // Default 16-color PC-98 palette setup (GRB hardware mapping fallback)
    for (int i = 0; i < 256; i++) {
        s_palette[i] = 0x000000FF;
    }

    memset(s_vram, 0, sizeof(s_vram));
    printf("[HAL_VIDEO] Initialized PC-98 640x400 virtual framebuffer (Scale: %dx)\n", window_scale);
    return true;
}

void hal_video_shutdown(void) {
    if (s_texture) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}

void hal_video_set_title(const char *title) {
    if (s_window && title) {
        SDL_SetWindowTitle(s_window, title);
    }
}

void hal_video_set_palette_entry(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 0 && index < 256) {
        // RGBA8888 packing: (R << 24) | (G << 16) | (B << 8) | 0xFF
        s_palette[index] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
    }
}

void hal_video_load_pld_bank(const uint8_t *pld_data, int bank_idx) {
    if (!pld_data || bank_idx < 0 || bank_idx >= 4) return;
    const uint8_t *bank = pld_data + (bank_idx * 16 * 3);
    for (int c = 0; c < 16; c++) {
        // Reverse-engineered Mainpal.pld format: [c*3]=B, [c*3+1]=R, [c*3+2]=G
        uint8_t b_raw = bank[c * 3];
        uint8_t r_raw = bank[c * 3 + 1];
        uint8_t g_raw = bank[c * 3 + 2];
        uint8_t r = (r_raw << 4) | r_raw;
        uint8_t g = (g_raw << 4) | g_raw;
        uint8_t b = (b_raw << 4) | b_raw;
        hal_video_set_palette_entry(c, r, g, b);
    }
}

void hal_video_clear(uint8_t color_idx) {
    memset(s_vram, color_idx, sizeof(s_vram));
}

void hal_video_draw_pixel(int x, int y, uint8_t color_idx) {
    if (x >= 0 && x < PC98_WIDTH && y >= 0 && y < PC98_HEIGHT) {
        s_vram[y * PC98_WIDTH + x] = color_idx;
    }
}

void hal_video_fill_rect(int x, int y, int w, int h, uint8_t color_idx) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PC98_WIDTH)  w = PC98_WIDTH - x;
    if (y + h > PC98_HEIGHT) h = PC98_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        memset(&s_vram[(y + row) * PC98_WIDTH + x], color_idx, w);
    }
}

void hal_video_draw_rect(int x, int y, int w, int h, uint8_t color_idx) {
    for (int i = 0; i < w; i++) {
        hal_video_draw_pixel(x + i, y, color_idx);
        hal_video_draw_pixel(x + i, y + h - 1, color_idx);
    }
    for (int j = 0; j < h; j++) {
        hal_video_draw_pixel(x, y + j, color_idx);
        hal_video_draw_pixel(x + w - 1, y + j, color_idx);
    }
}

void hal_video_blit_indexed(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, int src_pitch) {
    if (!src_pixels) return;
    for (int y = 0; y < h; y++) {
        int py = dst_y + y;
        if (py < 0 || py >= PC98_HEIGHT) continue;
        const uint8_t *src_row = src_pixels + y * src_pitch;
        for (int x = 0; x < w; x++) {
            int px = dst_x + x;
            if (px < 0 || px >= PC98_WIDTH) continue;
            s_vram[py * PC98_WIDTH + px] = src_row[x];
        }
    }
}

void hal_video_blit_transparent(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, int src_pitch, uint8_t transparent_idx) {
    if (!src_pixels) return;
    for (int y = 0; y < h; y++) {
        int py = dst_y + y;
        if (py < 0 || py >= PC98_HEIGHT) continue;
        const uint8_t *src_row = src_pixels + y * src_pitch;
        for (int x = 0; x < w; x++) {
            int px = dst_x + x;
            if (px < 0 || px >= PC98_WIDTH) continue;
            uint8_t c = src_row[x];
            if (c != transparent_idx) {
                s_vram[py * PC98_WIDTH + px] = c;
            }
        }
    }
}

void hal_video_blit_masked(int dst_x, int dst_y, int w, int h, const uint8_t *src_pixels, const uint8_t *mask_pixels, int src_pitch) {
    if (!src_pixels || !mask_pixels) return;
    for (int y = 0; y < h; y++) {
        int py = dst_y + y;
        if (py < 0 || py >= PC98_HEIGHT) continue;
        const uint8_t *src_row = src_pixels + y * src_pitch;
        const uint8_t *mask_row = mask_pixels + y * src_pitch;
        for (int x = 0; x < w; x++) {
            int px = dst_x + x;
            if (px < 0 || px >= PC98_WIDTH) continue;
            // Mask value: 7 is transparent, 0 is opaque
            if (mask_row[x] != 7) {
                s_vram[py * PC98_WIDTH + px] = src_row[x];
            }
        }
    }
}

void hal_video_present(void) {
    // Fast palette lookup to 32-bit RGBA
    for (int i = 0; i < PC98_WIDTH * PC98_HEIGHT; i++) {
        s_rgba_buffer[i] = s_palette[s_vram[i]];
    }

    SDL_UpdateTexture(s_texture, NULL, s_rgba_buffer, PC98_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

void hal_video_window_to_logical(int win_x, int win_y, int *out_x, int *out_y) {
    float lx = (float)win_x;
    float ly = (float)win_y;

    // SDL knows how it letterboxes the 640x400 logical size inside the window,
    // including any HiDPI scaling, so ask it rather than re-deriving it.
    if (s_renderer) {
        SDL_RenderWindowToLogical(s_renderer, win_x, win_y, &lx, &ly);
    }

    if (out_x) *out_x = (int)(lx >= 0.0f ? lx + 0.5f : lx - 0.5f);
    if (out_y) *out_y = (int)(ly >= 0.0f ? ly + 0.5f : ly - 0.5f);
}

bool hal_video_has_mouse_focus(void) {
    if (!s_window) return false;
    return (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

uint8_t* hal_video_get_framebuffer(void) {
    return s_vram;
}

bool hal_video_save_bmp(const char *bmp_path) {
    FILE *f = fopen(bmp_path, "wb");
    if (!f) return false;

    int w = PC98_WIDTH;
    int h = PC98_HEIGHT;
    uint32_t file_size = 54 + (uint32_t)w * h * 3;
    uint8_t header[54] = {
        'B', 'M',
        file_size & 0xFF, (file_size >> 8) & 0xFF, (file_size >> 16) & 0xFF, (file_size >> 24) & 0xFF,
        0, 0, 0, 0,
        54, 0, 0, 0, // offset
        40, 0, 0, 0, // DIB header size
        w & 0xFF, (w >> 8) & 0xFF, 0, 0,
        h & 0xFF, (h >> 8) & 0xFF, 0, 0,
        1, 0, 24, 0, // 24-bit
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);

    // BMP is bottom-up BGR
    uint8_t *row_buf = (uint8_t*)malloc(w * 3);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t rgba = s_palette[s_vram[y * w + x]];
            uint8_t r = (rgba >> 24) & 0xFF;
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t b = (rgba >> 8) & 0xFF;
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        fwrite(row_buf, 1, w * 3, f);
    }
    free(row_buf);
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// PC-98 / JIS Font Subsystem
// ---------------------------------------------------------------------------
static const uint8_t *s_jis_fnt = NULL;
static size_t s_jis_size = 0;
static const uint8_t *s_chr_fnt = NULL;
static size_t s_chr_size = 0;

bool hal_video_load_fonts(const uint8_t *jis_data, size_t jis_size, const uint8_t *chr_data, size_t chr_size) {
    s_jis_fnt = jis_data;
    s_jis_size = jis_size;
    s_chr_fnt = chr_data;
    s_chr_size = chr_size;
    return (s_jis_fnt != NULL || s_chr_fnt != NULL);
}

void hal_video_draw_char_8x16(int x, int y, uint8_t ch, uint8_t color_idx) {
    if (!s_chr_fnt || (size_t)(ch * 16 + 16) > s_chr_size) return;
    const uint8_t *glyph = s_chr_fnt + ch * 16;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                hal_video_draw_pixel(x + col, y + row, color_idx);
            }
        }
    }
}

void hal_video_draw_string_8x16(int x, int y, const char *str, uint8_t color_idx) {
    if (!str) return;
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_y += 18;
            cur_x = x;
        } else {
            hal_video_draw_char_8x16(cur_x, cur_y, (uint8_t)*str, color_idx);
            cur_x += 8;
        }
        str++;
    }
}

static inline void sjis_to_kuten(uint8_t s1, uint8_t s2, int *out_ku, int *out_ten) {
    int k, t;
    if (s1 <= 0x9F) {
        k = (s1 - 0x81) * 2 + 1;
    } else {
        k = (s1 - 0xE0) * 2 + 63;
    }
    if (s2 >= 0x9F) {
        k += 1;
        t = s2 - 0x7E - 0x20;
    } else {
        if (s2 > 0x7F) s2--;
        t = s2 - 0x1F - 0x20;
    }
    *out_ku = k;
    *out_ten = t;
}

void hal_video_draw_kanji_16x16(int x, int y, uint8_t s1, uint8_t s2, uint8_t color_idx) {
    if (!s_jis_fnt) return;
    int ku = 0, ten = 0;
    sjis_to_kuten(s1, s2, &ku, &ten);
    int glyph_idx = -1;
    if (ku >= 1) {
        glyph_idx = 112 + (ku - 1) * 94 + (ten - 1);
    }
    if (glyph_idx < 0 || (size_t)(glyph_idx * 32 + 32) > s_jis_size) return;

    const uint8_t *glyph = s_jis_fnt + glyph_idx * 32;
    for (int row = 0; row < 16; row++) {
        uint8_t b0 = glyph[row * 2];
        uint8_t b1 = glyph[row * 2 + 1];
        for (int col = 0; col < 8; col++) {
            if ((b0 >> (7 - col)) & 1) hal_video_draw_pixel(x + col, y + row, color_idx);
            if ((b1 >> (7 - col)) & 1) hal_video_draw_pixel(x + 8 + col, y + row, color_idx);
        }
    }
}

void hal_video_draw_text_sjis(int x, int y, const char *sjis_str, uint8_t color_idx) {
    if (!sjis_str) return;
    const uint8_t *p = (const uint8_t*)sjis_str;
    int cur_x = x;
    int cur_y = y;
    while (*p) {
        if (*p == '\n') {
            cur_y += 18;
            cur_x = x;
            p++;
        } else if (((*p >= 0x81 && *p <= 0x9F) || (*p >= 0xE0 && *p <= 0xFC)) && *(p + 1)) {
            uint8_t s1 = *p++;
            uint8_t s2 = *p++;
            hal_video_draw_kanji_16x16(cur_x, cur_y, s1, s2, color_idx);
            cur_x += 16;
        } else {
            hal_video_draw_char_8x16(cur_x, cur_y, *p++, color_idx);
            cur_x += 8;
        }
    }
}

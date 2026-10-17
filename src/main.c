#include "hal_video.h"
#include "decompress.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_asset_dir[512] = "";

static uint8_t* load_file(const char *rel_path, size_t *out_size) {
    char candidate[1024];
    FILE *f = NULL;

    // 1. If explicit asset dir is set
    if (s_asset_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%s/%s", s_asset_dir, rel_path);
        f = fopen(candidate, "rb");
    }

    // 2. Direct path relative to cwd
    if (!f) {
        f = fopen(rel_path, "rb");
    }

    // 3. One level up (e.g. running from bin/)
    if (!f) {
        snprintf(candidate, sizeof(candidate), "../%s", rel_path);
        f = fopen(candidate, "rb");
    }

    // 4. Relative to executable binary directory via SDL_GetBasePath
    if (!f) {
        char *base = SDL_GetBasePath();
        if (base) {
            // try bin/../games/...
            snprintf(candidate, sizeof(candidate), "%s../%s", base, rel_path);
            f = fopen(candidate, "rb");
            if (!f) {
                // try bin/games/...
                snprintf(candidate, sizeof(candidate), "%s%s", base, rel_path);
                f = fopen(candidate, "rb");
            }
            SDL_free(base);
        }
    }

    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open file: %s (searched cwd, ../, and binary directory)\n", rel_path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t*)malloc(sz);
    if (buf) {
        fread(buf, 1, sz, f);
        if (out_size) *out_size = (size_t)sz;
    }
    fclose(f);
    return buf;
}
static void update_window_title(int cur_officer, bool use_mask, int bg_mode) {
    char title[256];
    bool is_npc = (cur_officer >= 80 && cur_officer <= 88);
    const char *bg_names[] = {"纯黑展台", "军议大殿(640x400)", "野战军营(288x176)"};
    snprintf(title, sizeof(title),
             "《源平合战》[人物 %02d/90] %s (%dx%d) | 遮罩:%s | 背景:%s | TAB:切背景 空格:遮罩 I:信息框",
             cur_officer + 1,
             is_npc ? "NPC/姬/町人/使者小立绘" : (cur_officer == 89 ? "僧兵立绘" : "大名/名将立绘"),
             is_npc ? 64 : 128,
             is_npc ? 80 : 160,
             use_mask ? "开" : "关",
             bg_names[bg_mode % 3]);
    hal_video_set_title(title);
}

int main(int argc, char *argv[]) {
    const char *screenshot_path = NULL;
    int max_frames = 0;

    const char *env_dir = getenv("YPHZ_DATA_DIR");
    if (env_dir) {
        snprintf(s_asset_dir, sizeof(s_asset_dir), "%s", env_dir);
    }

    int cur_officer = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
            max_frames = 10;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--data") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            snprintf(s_asset_dir, sizeof(s_asset_dir), "%s", argv[++i]);
        } else if ((strcmp(argv[i], "--officer") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            cur_officer = atoi(argv[++i]);
            if (cur_officer < 0) cur_officer = 0;
            if (cur_officer >= 90) cur_officer = 89;
        }
    }

    if (!hal_video_init("《源平合战》 - Linux Native Video Engine (SDL2/PC-98)", 2)) {
        return 1;
    }

    // 1. Load Mainpal.pld
    size_t pld_size = 0;
    uint8_t *pld_data = load_file("games/yphz/Mainpal.pld", &pld_size);
    if (pld_data) {
        hal_video_load_pld_bank(pld_data, 0);
        printf("[+] Loaded Mainpal.pld Bank 0 (16-color PC-98 GBR palette)\n");
    }

    // 2. Read 90 officer offsets from Main.exe at 0x4d3bb
    size_t exe_size = 0;
    uint8_t *exe_data = load_file("games/yphz/Main.exe", &exe_size);
    uint32_t officer_offsets[90];
    officer_offsets[0] = 0;
    if (exe_data && exe_size > 0x4D3BB + 89 * 4) {
        for (int i = 1; i < 90; i++) {
            uint32_t off = exe_data[0x4D3BB + (i - 1) * 4] |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 1] << 8) |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 2] << 16) |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 3] << 24);
            officer_offsets[i] = off;
        }
        printf("[+] Parsed 90 officer offsets from Main.exe:0x4d3bb\n");
    }

    // 3. Load Kaodata.gp
    size_t kao_size = 0;
    uint8_t *kao_data = load_file("games/yphz/Kaodata.gp", &kao_size);
    if (!kao_data) {
        fprintf(stderr, "[ERROR] Failed to load games/yphz/Kaodata.gp\n");
        return 1;
    }
    printf("[+] Loaded Kaodata.gp (%zu bytes)\n", kao_size);

    // 4. Load scenic backgrounds
    size_t bg_hik_sz = 0;
    uint8_t *bg_hik_data = load_file("games/yphz/Hikback.gp", &bg_hik_sz);
    ImageDecoded bg_hik = {0};
    if (bg_hik_data) {
        nup4_decompress_block(bg_hik_data, bg_hik_sz, 0, &bg_hik, NULL);
        printf("[+] Loaded Hikback.gp background (%dx%d)\n", bg_hik.width, bg_hik.height);
    }

    size_t bg_cmd_sz = 0;
    uint8_t *bg_cmd_data = load_file("games/yphz/Maincmd2.gp", &bg_cmd_sz);
    ImageDecoded bg_palace = {0};
    if (bg_cmd_data) {
        nup4_decompress_block(bg_cmd_data, bg_cmd_sz, 0, &bg_palace, NULL);
        printf("[+] Loaded Maincmd2.gp palace screen (%dx%d)\n", bg_palace.width, bg_palace.height);
    }

    bool use_mask = true;
    int bg_mode = 1; // 0: Pure black, 1: 640x400 Palace, 2: 288x176 Tactical
    bool show_info = true;
    ImageDecoded cur_kao = {0};

    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
    update_window_title(cur_officer, use_mask, bg_mode);

    bool running = true;
    int frame_count = 0;
    while (running) {
        int key = 0;
        bool quit = false;
        while (hal_video_poll_event(&key, &quit)) {
            if (quit) running = false;
            if (key == SDLK_RIGHT) {
                cur_officer = (cur_officer + 1) % 90;
                free_decoded_image(&cur_kao);
                kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode);
            } else if (key == SDLK_LEFT) {
                cur_officer = (cur_officer - 1 + 90) % 90;
                free_decoded_image(&cur_kao);
                kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode);
            } else if (key == SDLK_SPACE) {
                use_mask = !use_mask;
                update_window_title(cur_officer, use_mask, bg_mode);
            } else if (key == SDLK_TAB) {
                bg_mode = (bg_mode + 1) % 3;
                update_window_title(cur_officer, use_mask, bg_mode);
            } else if (key == SDLK_i) {
                show_info = !show_info;
            }
        }

        // --- Render Frame ---
        hal_video_clear(0); // Color 0 (Black)

        // Draw background according to mode
        if (bg_mode == 1 && bg_palace.pixels) {
            hal_video_blit_indexed(0, 0, bg_palace.width, bg_palace.height, bg_palace.pixels, bg_palace.width);
        } else if (bg_mode == 2 && bg_hik.pixels) {
            int bg_x = (PC98_WIDTH - bg_hik.width) / 2;
            int bg_y = 40;
            hal_video_blit_indexed(bg_x, bg_y, bg_hik.width, bg_hik.height, bg_hik.pixels, bg_hik.width);
            hal_video_draw_rect(bg_x - 1, bg_y - 1, bg_hik.width + 2, bg_hik.height + 2, 7);
        }

        // Determine effective portrait bounds (Officers 80..88 are 64x80 NPC avatars)
        bool is_npc = (cur_officer >= 80 && cur_officer <= 88);
        int frame_w = is_npc ? 64 : 128;
        int frame_h = is_npc ? 80 : 160;

        // Draw Japanese PC-98 style border box for Officer Card
        int card_x = 40;
        int card_y = 50;
        hal_video_fill_rect(card_x - 4, card_y - 4, frame_w + 8, frame_h + 8, 1);
        hal_video_draw_rect(card_x - 4, card_y - 4, frame_w + 8, frame_h + 8, 6);
        hal_video_draw_rect(card_x - 2, card_y - 2, frame_w + 4, frame_h + 4, 7);

        // Blit Officer Portrait
        if (cur_kao.pixels) {
            if (use_mask && cur_kao.mask) {
                hal_video_blit_masked(card_x, card_y, frame_w, frame_h, cur_kao.pixels, cur_kao.mask, cur_kao.width);
            } else {
                hal_video_blit_indexed(card_x, card_y, frame_w, frame_h, cur_kao.pixels, cur_kao.width);
            }
        }

        // Draw Info Box (Authentic KOEI Navy Blue dialog card, toggled via 'I')
        if (show_info) {
            int info_x = card_x + frame_w + 16;
            int info_y = card_y;
            int info_w = PC98_WIDTH - info_x - 30;
            if (info_w > 420) info_w = 420;
            int info_h = frame_h + 8;
            hal_video_fill_rect(info_x, info_y, info_w, info_h, 1); // Authentic KOEI Navy Blue (Color 1)
            hal_video_draw_rect(info_x, info_y, info_w, info_h, 6); // Gold border
            hal_video_draw_rect(info_x + 2, info_y + 2, info_w - 4, info_h - 4, 7); // White inner
        }

        // Draw Title Bar
        hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
        hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);

        // Draw Color Palette Swatches at the bottom
        int sw_x = 40;
        int sw_y = 350;
        int sw_w = 32;
        int sw_h = 16;
        for (int c = 0; c < 16; c++) {
            hal_video_fill_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, c);
            hal_video_draw_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, 7);
        }

        hal_video_present();

        if (screenshot_path && frame_count == 2) {
            hal_video_save_bmp(screenshot_path);
            printf("[+] Captured engine screenshot to %s\n", screenshot_path);
        }

        frame_count++;
        if (max_frames > 0 && frame_count >= max_frames) {
            running = false;
        }

        SDL_Delay(16); // ~60 FPS
    }

    free_decoded_image(&cur_kao);
    if (bg_hik.pixels) free_decoded_image(&bg_hik);
    if (bg_palace.pixels) free_decoded_image(&bg_palace);
    if (pld_data) free(pld_data);
    if (exe_data) free(exe_data);
    if (kao_data) free(kao_data);

    hal_video_shutdown();
    printf("[+] Engine cleanly stopped.\n");
    return 0;
}

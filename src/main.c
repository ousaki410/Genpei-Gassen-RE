#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#include "hal_video.h"
#include "decompress.h"
#include "scenario.h"

static char s_asset_dir[256] = "games/yphz";

static uint8_t* load_file(const char *rel_path, size_t *out_size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_asset_dir, rel_path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        f = fopen(rel_path, "rb");
        if (!f) {
            fprintf(stderr, "[ERROR] Cannot open file: %s\n", rel_path);
            return NULL;
        }
    }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) != sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_size) *out_size = sz;
    return buf;
}

static void refresh_officer_portrait(int cur_officer, const Scenario *sc, const uint8_t *kao_data, size_t kao_size, const uint32_t *officer_offsets, ImageDecoded *cur_kao) {
    if (!sc || cur_officer < 0 || cur_officer >= sc->num_officers) return;
    int fid = sc->officers[cur_officer].face_id;
    int use_fid = (fid >= 0 && fid < 90) ? fid : (cur_officer % 15) + 40;
    free_decoded_image(cur_kao);
    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[use_fid], cur_kao);
}

static void update_window_title(int cur_officer, bool use_mask, int bg_mode, bool map_mode, int camera_x, int scenario_idx, const Scenario *sc, int cur_castle) {
    char title[256];
    if (map_mode) {
        const char *c_name = (sc && cur_castle >= 0 && cur_castle < sc->num_castles) ? sc->castles[cur_castle].name_sjis : "";
        snprintf(title, sizeof(title),
                 "《源平合战》[剧本%d] 选城:%s | 视口X:%04d/1056 | 左右:平移 ［］:换城 1-4:换剧本 C:详情 M:武将 O:查看城主",
                 scenario_idx + 1, c_name, camera_x);
    } else {
        const char *bg_names[] = {"纯黑展台", "军议大殿(640x400)", "野战军营(288x176)"};
        const ScenarioOfficer *off = (sc && cur_officer >= 0 && cur_officer < sc->num_officers) ? &sc->officers[cur_officer] : NULL;
        const char *name = off ? off->full_name_sjis : "武将";
        int fid = off ? off->face_id : -1;
        snprintf(title, sizeof(title),
                 "《源平合战》[剧本%d 武将 %03d/401] %s (%s) | 遮罩:%s | 背景:%s | 左右:换人 上下:±10 F:名将 1-4:剧本 M:地图",
                 scenario_idx + 1,
                 cur_officer + 1,
                 name,
                 (fid >= 0) ? "专属立绘" : "通用立绘",
                 use_mask ? "开" : "关",
                 bg_names[bg_mode % 3]);
    }
    hal_video_set_title(title);
}

int main(int argc, char *argv[]) {
    const char *screenshot_path = NULL;
    int max_frames = 0;
    bool start_map_mode = false;
    int cur_scenario = 0; // Scenario 0..3 (1180, 1183, 1184, 1185)
    int cur_castle = 30;  // Default to Kamakura (Castle 30)
    bool show_castle_card = true;

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
            if (cur_officer >= MAX_OFFICERS) cur_officer = MAX_OFFICERS - 1;
        } else if (strcmp(argv[i], "--map") == 0 || strcmp(argv[i], "-m") == 0) {
            start_map_mode = true;
        } else if ((strcmp(argv[i], "--scenario") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            cur_scenario = atoi(argv[++i]);
            if (cur_scenario >= 1 && cur_scenario <= 4) cur_scenario--;
            if (cur_scenario < 0) cur_scenario = 0;
            if (cur_scenario > 3) cur_scenario = 3;
        } else if ((strcmp(argv[i], "--castle") == 0 || strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
            cur_castle = atoi(argv[++i]);
            if (cur_castle < 0) cur_castle = 0;
            if (cur_castle >= MAX_CASTLES) cur_castle = MAX_CASTLES - 1;
        }
    }

    if (!hal_video_init("《源平合戦》 - Linux Native Video Engine (SDL2/PC-98)", 2)) {
        return 1;
    }

    // 1. Load Mainpal.pld Bank 0
    size_t pld_size = 0;
    uint8_t *pld_data = load_file("Mainpal.pld", &pld_size);
    if (pld_data) {
        hal_video_load_pld_bank(pld_data, 0);
        printf("[+] Loaded Mainpal.pld Bank 0 (16-color PC-98 GBR palette)\n");
    }

    // 2. Load JIS.FNT & FONT.CHR
    size_t jis_size = 0, chr_size = 0;
    uint8_t *jis_data = load_file("JIS.FNT", &jis_size);
    uint8_t *chr_data = load_file("FONT.CHR", &chr_size);
    if (jis_data && chr_data) {
        hal_video_load_fonts(jis_data, jis_size, chr_data, chr_size);
        printf("[+] Loaded JIS.FNT (%zu bytes) & FONT.CHR (%zu bytes)\n", jis_size, chr_size);
    }

    // 3. Load Main.exe officer table (90 portraits)
    size_t exe_size = 0;
    uint8_t *exe_data = load_file("Main.exe", &exe_size);
    uint32_t officer_offsets[90] = {0};
    if (exe_data && exe_size >= 0x4D3BB + 89 * 4) {
        for (int i = 1; i <= 89; i++) {
            uint32_t off = exe_data[0x4D3BB + (i - 1) * 4] |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 1] << 8) |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 2] << 16) |
                          (exe_data[0x4D3BB + (i - 1) * 4 + 3] << 24);
            officer_offsets[i] = off;
        }
        printf("[+] Parsed 90 officer offsets from Main.exe:0x4d3bb\n");
    }

    // 4. Load Kaodata.gp
    size_t kao_size = 0;
    uint8_t *kao_data = load_file("Kaodata.gp", &kao_size);
    if (!kao_data) {
        fprintf(stderr, "[ERROR] Failed to load Kaodata.gp\n");
        return 1;
    }
    printf("[+] Loaded Kaodata.gp (%zu bytes)\n", kao_size);

    // 5. Load Scenic backgrounds
    size_t bg_hik_sz = 0;
    uint8_t *bg_hik_data = load_file("Hikback.gp", &bg_hik_sz);
    ImageDecoded bg_hik = {0};
    if (bg_hik_data) {
        nup4_decompress_block(bg_hik_data, bg_hik_sz, 0, &bg_hik, NULL);
    }

    size_t bg_cmd_sz = 0;
    uint8_t *bg_cmd_data = load_file("Maincmd2.gp", &bg_cmd_sz);
    ImageDecoded bg_palace = {0};
    if (bg_cmd_data) {
        nup4_decompress_block(bg_cmd_data, bg_cmd_sz, 0, &bg_palace, NULL);
        printf("[+] Loaded Maincmd2.gp palace screen (640x400)\n");
    }

    // 6. Load & Seamlessly Stitch Strategic Japan Map (1696x360)
    size_t map_file_sz = 0;
    uint8_t *map_file_data = load_file("Mainmap.gp", &map_file_sz);
    uint8_t *japan_map_pixels = NULL;
    int map_w = 1696;
    int map_h = 360;
    if (map_file_data) {
        japan_map_pixels = (uint8_t*)calloc(1, map_w * map_h);
        if (japan_map_pixels) {
            size_t off = 0;
            int cur_x = 0;
            for (int b = 0; b < 20; b++) {
                while (off + 6 <= map_file_sz && memcmp(map_file_data + off, "NPK016", 6) != 0) {
                    off++;
                }
                if (off + 6 > map_file_sz) break;
                ImageDecoded blk = {0};
                size_t consumed = 0;
                if (nup4_decompress_block(map_file_data, map_file_sz, off, &blk, &consumed)) {
                    if (b == 4 || b == 8 || b == 12 || b == 16) {
                        cur_x -= 16; // Eliminate overlap seam
                    }
                    for (int y = 0; y < blk.height && y < map_h; y++) {
                        memcpy(japan_map_pixels + y * map_w + cur_x, blk.pixels + y * blk.width, blk.width);
                    }
                    cur_x += blk.width;
                    free_decoded_image(&blk);
                }
                off += consumed;
            }
            printf("[+] Seamlessly stitched Mainmap.gp into 1696x360 strategic Japan map\n");
        }
    }

    // 7. Load Scenario Subsystem (Sndata.gp)
    char snd_path[512];
    snprintf(snd_path, sizeof(snd_path), "%s/Sndata.gp", s_asset_dir);
    Scenario g_scenario = {0};
    if (!scenario_load(snd_path, cur_scenario, &g_scenario)) {
        scenario_load("Sndata.gp", cur_scenario, &g_scenario);
    }

    // Initial officer decode
    ImageDecoded cur_kao = {0};
    refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);

    bool running = true;
    bool use_mask = true;
    int bg_mode = 1;
    bool map_mode = start_map_mode;
    
    // Set initial camera to center on selected castle
    int camera_x = g_scenario.castles[cur_castle].map_x - 320;
    if (camera_x < 0) camera_x = 0;
    if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;

    int frame_count = 0;
    show_castle_card = true;

    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) running = false;
                
                // Mode Toggle: M
                if (key == SDLK_m) {
                    map_mode = !map_mode;
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                }

                if (map_mode) {
                    // Scenario hot-switching: 1, 2, 3, 4
                    if (key >= SDLK_1 && key <= SDLK_4) {
                        cur_scenario = key - SDLK_1;
                        scenario_load(snd_path, cur_scenario, &g_scenario);
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                    // Castle selection: [ and ]
                    else if (key == SDLK_LEFTBRACKET) {
                        cur_castle = (cur_castle - 1 + MAX_CASTLES) % MAX_CASTLES;
                        camera_x = g_scenario.castles[cur_castle].map_x - 320;
                        if (camera_x < 0) camera_x = 0;
                        if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_RIGHTBRACKET) {
                        cur_castle = (cur_castle + 1) % MAX_CASTLES;
                        camera_x = g_scenario.castles[cur_castle].map_x - 320;
                        if (camera_x < 0) camera_x = 0;
                        if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                    // Inspect Castle Lord in Officer Mode: O or ENTER
                    else if (key == SDLK_o || key == SDLK_RETURN) {
                        ScenarioCastle *sel = &g_scenario.castles[cur_castle];
                        if (sel->lord_officer_id >= 0 && sel->lord_officer_id < g_scenario.num_officers) {
                            cur_officer = sel->lord_officer_id;
                            map_mode = false;
                            refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                            update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                        }
                    }
                    // Toggle Castle Details Card: C or SPACE
                    else if (key == SDLK_c || key == SDLK_SPACE) {
                        show_castle_card = !show_castle_card;
                    }
                    // Jump to major strategic headquarters: TAB
                    else if (key == SDLK_TAB) {
                        static const int major_castles[] = {0, 6, 16, 17, 25, 30, 38};
                        static int m_idx = 0;
                        m_idx = (m_idx + 1) % 7;
                        cur_castle = major_castles[m_idx];
                        camera_x = g_scenario.castles[cur_castle].map_x - 320;
                        if (camera_x < 0) camera_x = 0;
                        if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                    // Smooth manual camera scroll
                    else if (key == SDLK_LEFT || key == SDLK_a) {
                        camera_x -= 40;
                        if (camera_x < 0) camera_x = 0;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_RIGHT || key == SDLK_d) {
                        camera_x += 40;
                        if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                } else {
                    // Officer Showcase Controls: Full 401 Officer Database
                    if (key == SDLK_RIGHT) {
                        cur_officer = (cur_officer + 1) % g_scenario.num_officers;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_LEFT) {
                        cur_officer = (cur_officer - 1 + g_scenario.num_officers) % g_scenario.num_officers;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_DOWN) {
                        cur_officer = (cur_officer + 10) % g_scenario.num_officers;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_UP) {
                        cur_officer = (cur_officer - 10 + g_scenario.num_officers) % g_scenario.num_officers;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_f) {
                        // Jump to next historical officer with dedicated portrait
                        int next_off = cur_officer;
                        for (int step = 1; step < g_scenario.num_officers; step++) {
                            int check = (cur_officer + step) % g_scenario.num_officers;
                            if (g_scenario.officers[check].face_id >= 0) {
                                next_off = check;
                                break;
                            }
                        }
                        cur_officer = next_off;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key >= SDLK_1 && key <= SDLK_4) {
                        cur_scenario = key - SDLK_1;
                        scenario_load(snd_path, cur_scenario, &g_scenario);
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_SPACE) {
                        use_mask = !use_mask;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else if (key == SDLK_TAB) {
                        bg_mode = (bg_mode + 1) % 3;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                }
            }
        }

        // --- Render Frame ---
        hal_video_clear(0);

        if (map_mode && japan_map_pixels) {
            // ===============================================================
            // Strategic Map Mode (1696x360 Full Japan Seamless Panorama)
            // ===============================================================
            hal_video_blit_indexed(0, 20, PC98_WIDTH, 360, japan_map_pixels + camera_x, map_w);

            // Draw faction flags over all visible castles
            for (int i = 0; i < g_scenario.num_castles; i++) {
                ScenarioCastle *cs = &g_scenario.castles[i];
                if (cs->map_x >= camera_x && cs->map_x < camera_x + PC98_WIDTH) {
                    int sx = cs->map_x - camera_x;
                    int sy = cs->map_y + 20;
                    const FactionDef *fac = scenario_get_faction(cs->faction_id);
                    // Flag pole
                    for (int h = 0; h < 14; h++) {
                        hal_video_draw_pixel(sx, sy - 16 + h, 7);
                    }
                    // Flag pennant (12x8 colored rectangle with white outline)
                    hal_video_fill_rect(sx + 1, sy - 16, 12, 8, fac->flag_color);
                    hal_video_draw_rect(sx + 1, sy - 16, 12, 8, 7);
                }
            }

            // Draw animated gold reticle for currently selected castle
            ScenarioCastle *sel = &g_scenario.castles[cur_castle];
            if (sel->map_x >= camera_x && sel->map_x < camera_x + PC98_WIDTH) {
                int sx = sel->map_x - camera_x;
                int sy = sel->map_y + 20;
                int anim_color = (frame_count / 15) % 2 ? 6 : 7;
                hal_video_draw_rect(sx - 16, sy - 12, 32, 24, anim_color);
                hal_video_draw_pixel(sx - 17, sy - 13, 7);
                hal_video_draw_pixel(sx + 16, sy - 13, 7);
                hal_video_draw_pixel(sx - 17, sy + 12, 7);
                hal_video_draw_pixel(sx + 16, sy + 12, 7);
            }

            // Top Status Bar (Color 1 Navy Blue)
            hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);
            if (g_scenario.title_sjis) {
                hal_video_draw_text_sjis(16, 2, g_scenario.title_sjis, 7);
            }
            hal_video_draw_text_sjis(400, 2, "\x81\x6d\x31\x2d\x34\x3a\x8c\x80\x96\x7b\x20\x43\x3a\x8f\xda\x8f\xee\x20\x81\x6d\x81\x6e\x3a\x8a\xb7\x8f\xe9\x20\x4d\x3a\x95\x90\x8f\xab\x81\x6e", 6); // ［1-4:劇本 C:詳情 ［］:換城 M:武将］

            // Bottom Status Bar
            hal_video_fill_rect(0, 380, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 380, PC98_WIDTH, 20, 7);
            
            // Format quick bottom bar info
            const FactionDef *fac = scenario_get_faction(sel->faction_id);
            const char *lord_name = (sel->lord_officer_id >= 0 && sel->lord_officer_id < g_scenario.num_officers) ?
                                    g_scenario.officers[sel->lord_officer_id].full_name_sjis : "\x81\x69\x96\xb3\x81\x6a";
            char bot_str[160];
            snprintf(bot_str, sizeof(bot_str),
                     "\x81\x79%s\x81\x7a %s(%s) \x8f\xe9\x8e\xe5\x81\x46%s  \x95\xba\x97\xcd\x81\x46%5d  \x90\xce\x8d\x82\x81\x46%5d  \x8a\x58\x93\xb9\x81\x46%d\x89\xd3\x8f\x8a",
                     sel->name_sjis, fac->name_sjis, fac->flag_sjis, lord_name, sel->troops, sel->rice, sel->num_conns);
            hal_video_draw_text_sjis(16, 382, bot_str, 7);

            // Castle Detail Card Modal Overlay
            if (show_castle_card) {
                int cx = 70;
                int cy = 40;
                int cw = 500;
                int ch = 310;
                hal_video_fill_rect(cx, cy, cw, ch, 1);      // Navy background
                hal_video_draw_rect(cx, cy, cw, ch, 6);      // Gold outer
                hal_video_draw_rect(cx + 2, cy + 2, cw - 4, ch - 4, 7); // White inner

                // Header: 【城池詳報】 城名（管轄地区）
                char hdr[128];
                const char *circ_name = scenario_get_circuit_name(sel->circuit_id);
                snprintf(hdr, sizeof(hdr), "\x81\x79\x8f\xe9\x92\x72\x8f\xda\x95\xf1\x81\x7a %s \x81\x69%s\x81\x6a",
                         sel->name_sjis, circ_name);
                hal_video_draw_text_sjis(cx + 16, cy + 12, hdr, 6);

                // Row 1: 支配勢力
                char row1[128];
                snprintf(row1, sizeof(row1), "\x8e\x78\x94\x7a\x81\x46%s \x81\x69%s\x81\x6a",
                         fac->name_sjis, fac->flag_sjis);
                hal_video_draw_text_sjis(cx + 16, cy + 38, row1, 7);

                // Row 2: 城主太守
                char row2[128];
                if (sel->lord_officer_id >= 0 && sel->lord_officer_id < g_scenario.num_officers) {
                    ScenarioOfficer *lord = &g_scenario.officers[sel->lord_officer_id];
                    snprintf(row2, sizeof(row2), "\x8f\xe9\x8e\xe5\x81\x46%s \x81\x69\x93\x9d\x97\xa6\x81\x46%2d \x95\x90\x97\xcd\x81\x46%2d \x92\x6d\x96\x64\x81\x46%2d\x81\x6a",
                             lord->full_name_sjis, lord->lead, lord->war, lord->intel);
                } else {
                    snprintf(row2, sizeof(row2), "\x8f\xe9\x8e\xe5\x81\x46\x81\x69\x96\xb3\x81\x6a");
                }
                hal_video_draw_text_sjis(cx + 16, cy + 62, row2, 7);

                // Divider 1
                hal_video_draw_rect(cx + 14, cy + 86, cw - 28, 1, 7);

                // Row 3: 兵力 / 城防御
                char row3[128];
                snprintf(row3, sizeof(row3), "\x95\xba\x97\xcd\x81\x46%5d           \x96\x68\x8c\xe4\x81\x46%4d",
                         sel->troops, sel->defense);
                hal_video_draw_text_sjis(cx + 16, cy + 96, row3, 7);

                // Row 4: 士気 / 訓練
                char row4[128];
                snprintf(row4, sizeof(row4), "\x8e\x6d\x8b\x43\x81\x46%3d             \x8c\x50\x97\xfb\x81\x46%3d",
                         sel->morale, sel->training);
                hal_video_draw_text_sjis(cx + 16, cy + 120, row4, 7);

                // Row 5: 石高 / 兵糧
                char row5[128];
                snprintf(row5, sizeof(row5), "\x90\xce\x8d\x82\x81\x46%5d           \x95\xba\x97\xc6\x81\x46%5d",
                         sel->rice, sel->gold);
                hal_video_draw_text_sjis(cx + 16, cy + 144, row5, 7);

                // Divider 2
                hal_video_draw_rect(cx + 14, cy + 168, cw - 28, 1, 7);

                // Row 6: 街道接続 (Connected Castles list)
                hal_video_draw_text_sjis(cx + 16, cy + 178, "\x8a\x58\x93\xb9\x90\xda\x91\xb1\x81\x46", 6); // 街道接続：
                
                char conn_line1[256] = {0};
                char conn_line2[256] = {0};
                for (int c_idx = 0; c_idx < sel->num_conns; c_idx++) {
                    int neighbor_id = sel->conns[c_idx];
                    if (neighbor_id >= 0 && neighbor_id < g_scenario.num_castles) {
                        const char *c_name = g_scenario.castles[neighbor_id].name_sjis;
                        if (c_idx < 4) {
                            if (conn_line1[0]) strncat(conn_line1, " \x81\x45 ", sizeof(conn_line1)-1);
                            strncat(conn_line1, c_name, sizeof(conn_line1)-1);
                        } else {
                            if (conn_line2[0]) strncat(conn_line2, " \x81\x45 ", sizeof(conn_line2)-1);
                            strncat(conn_line2, c_name, sizeof(conn_line2)-1);
                        }
                    }
                }
                hal_video_draw_text_sjis(cx + 32, cy + 202, conn_line1, 7);
                if (conn_line2[0]) {
                    hal_video_draw_text_sjis(cx + 32, cy + 224, conn_line2, 7);
                }

                // Footer hint
                hal_video_draw_text_sjis(cx + 16, cy + 276,
                    "\x81\x6d\x88\xc2\x20\x43\x20\x8c\xae\x95\xc2\x20\x20\x20\x81\x6d\x20\x81\x6e\x90\xd8\x91\xd6\x8f\xe9\x92\x72\x20\x20\x20\x31\x2d\x34\x90\xd8\x91\xd6\x8c\x80\x96\x7b\x81\x6e",
                    6); // ［按 C 鍵閉   ［ ］切替城池   1-4切替劇本］
            }

        } else {
            // ===============================================================
            // Officer Showcase Mode: Full 401 Historical Officers from Scenario
            // ===============================================================
            if (bg_mode == 1 && bg_palace.pixels) {
                hal_video_blit_indexed(0, 0, bg_palace.width, bg_palace.height, bg_palace.pixels, bg_palace.width);
            } else if (bg_mode == 2 && bg_hik.pixels) {
                int bg_x = (PC98_WIDTH - bg_hik.width) / 2;
                int bg_y = 40;
                hal_video_blit_indexed(bg_x, bg_y, bg_hik.width, bg_hik.height, bg_hik.pixels, bg_hik.width);
                hal_video_draw_rect(bg_x - 1, bg_y - 1, bg_hik.width + 2, bg_hik.height + 2, 7);
            }

            const ScenarioOfficer *off = &g_scenario.officers[cur_officer];
            int fid = off->face_id;
            int use_fid = (fid >= 0 && fid < 90) ? fid : (off->id % 15) + 40;
            bool is_npc = (use_fid >= 80 && use_fid <= 88);
            int frame_w = is_npc ? 64 : 128;
            int frame_h = is_npc ? 80 : 160;
            int card_x = 36;
            int card_y = 36;

            hal_video_fill_rect(card_x - 4, card_y - 4, frame_w + 8, frame_h + 8, 1);
            hal_video_draw_rect(card_x - 4, card_y - 4, frame_w + 8, frame_h + 8, 6);
            hal_video_draw_rect(card_x - 2, card_y - 2, frame_w + 4, frame_h + 4, 7);

            if (cur_kao.pixels) {
                if (use_mask && cur_kao.mask) {
                    hal_video_blit_masked(card_x, card_y, frame_w, frame_h, cur_kao.pixels, cur_kao.mask, cur_kao.width);
                } else {
                    hal_video_blit_indexed(card_x, card_y, frame_w, frame_h, cur_kao.pixels, cur_kao.width);
                }
            }

            // Portrait label under image
            char kao_tag[64];
            if (fid >= 0) {
                snprintf(kao_tag, sizeof(kao_tag), "\x81\x79\x90\xea\x97\x70\x97\xa7\x8a\x47\x20\x23%02d\x81\x7a", fid); // 【専用立絵 #xx】
                hal_video_draw_text_sjis(card_x, card_y + frame_h + 10, kao_tag, 6);
            } else {
                snprintf(kao_tag, sizeof(kao_tag), "\x81\x79\x94\xc4\x97\x70\x97\xa7\x8a\x47\x20\x23%02d\x81\x7a", use_fid); // 【汎用立絵 #xx】
                hal_video_draw_text_sjis(card_x, card_y + frame_h + 10, kao_tag, 3);
            }

            int info_x = 184;
            int info_y = card_y;
            int info_w = PC98_WIDTH - info_x - 30; // ~426 px wide
            int info_h = 280;

            hal_video_fill_rect(info_x, info_y, info_w, info_h, 1); // Navy Blue
            hal_video_draw_rect(info_x, info_y, info_w, info_h, 6); // Gold border
            hal_video_draw_rect(info_x + 2, info_y + 2, info_w - 4, info_h - 4, 7); // White inner

            // 1. Officer Full Name & Kana & ID
            char name_line[128];
            snprintf(name_line, sizeof(name_line), "%s \x81\x69%s %s\x81\x6a",
                     off->full_name_sjis, off->family_kana, off->given_kana);
            hal_video_draw_text_sjis(info_x + 16, info_y + 12, name_line, 7);

            char id_tag[32];
            snprintf(id_tag, sizeof(id_tag), "No.%03d", cur_officer + 1);
            hal_video_draw_text_sjis(info_x + info_w - 76, info_y + 12, id_tag, 6);

            // 2. Faction & Location
            const FactionDef *fac = scenario_get_faction(off->faction_id);
            const char *c_name = (off->castle_id >= 0 && off->castle_id < MAX_CASTLES) ?
                                  g_scenario.castles[off->castle_id].name_sjis : "\x81\x69\x96\xec\x95\xfa\x81\x6a";
            bool is_lord = (off->castle_id >= 0 && g_scenario.castles[off->castle_id].lord_officer_id == off->id);
            char fac_line[128];
            snprintf(fac_line, sizeof(fac_line), "\x90\xa8\x97\xcd\x81\x46%s(%s)  \x8f\x8a\x8d\xdd\x81\x46%s%s",
                     (off->faction_id >= 0) ? fac->name_sjis : "\x96\xb3\x8f\x8a\x91\xae",
                     (off->faction_id >= 0) ? fac->flag_sjis : "\x96\xb3",
                     c_name, is_lord ? " \x81\x79\x8f\xe9\x8e\xe5\x81\x7a" : "");
            hal_video_draw_text_sjis(info_x + 16, info_y + 38, fac_line, (off->faction_id >= 0) ? fac->flag_color : 7);

            // Divider line
            hal_video_draw_rect(info_x + 14, info_y + 64, info_w - 28, 1, 7);

            // 3. Age, Loyalty, Troops
            char stat_line1[128];
            snprintf(stat_line1, sizeof(stat_line1),
                     "\x94\x4e\x97\xee\x81\x46%2d \x8d\xce    \x92\x89\x90\xdf\x81\x46%3d    \x95\xba\x90\x94\x81\x46%5d",
                     off->age, off->loyalty, off->troops);
            hal_video_draw_text_sjis(info_x + 16, info_y + 74, stat_line1, 6);

            // 4. Martial & Cultural Attributes: 武力 / 知才 / 優雅 / 弓術
            char stat_line2[128];
            snprintf(stat_line2, sizeof(stat_line2),
                     "\x95\x90\x97\xcd\x81\x46%2d   \x92\x6d\x8d\xcb\x81\x46%2d   \x97\x44\x89\xeb\x81\x46%2d   \x8b\x7c\x8f\x70\x81\x46%2d",
                     off->war, off->intel, off->politics, off->charm);
            hal_video_draw_text_sjis(info_x + 16, info_y + 102, stat_line2, 7);

            // 5. Drill & Rank
            const char *rank_name = "\x8e\x98\x81\x45\x8c\x52\x8e\x6d"; // 侍・軍士
            if (off->id == 0 || off->id == 20 || off->id == 45 || off->id == 78 || off->id == 5) {
                rank_name = "\x93\x8f\x97\xc0\x81\x45\x91\xe5\x96\xbc"; // 棟梁・大名
            } else if (is_lord) {
                rank_name = "\x91\xe5\x8f\xab\x81\x45\x91\xe3\x8a\xaf"; // 大将・代官
            } else if (off->faction_id < 0) {
                rank_name = "\x98\x51\x90\x6c\x81\x45\x8d\xdd\x96\xec"; // 浪人・在野
            }
            char stat_line3[128];
            snprintf(stat_line3, sizeof(stat_line3),
                     "\x95\xba\x8e\xbf\x81\x46%2d        \x90\x67\x95\xaa\x81\x46%s",
                     off->lead, rank_name);
            hal_video_draw_text_sjis(info_x + 16, info_y + 130, stat_line3, 7);

            // Divider line
            hal_video_draw_rect(info_x + 14, info_y + 158, info_w - 28, 1, 7);

            // 6. Current Scenario
            char sc_hdr[128];
            snprintf(sc_hdr, sizeof(sc_hdr), "\x81\x79\x8c\x80\x96\x7b%d\x81\x7a %s",
                     cur_scenario + 1, g_scenario.title_sjis);
            hal_video_draw_text_sjis(info_x + 16, info_y + 170, sc_hdr, 6);

            // 7. Navigation hints
            hal_video_draw_text_sjis(info_x + 16, info_y + 200,
                "\x81\x6d\x8d\xb6\x89\x45\x81\x46\x90\xd8\x8a\xb7\x95\x90\x8f\xab\x20\x20\x20\x8f\xe3\x89\xba\x81\x46\x81\x7d\x31\x30\x20\x20\x20\x46\x81\x46\x96\xbc\x8f\xab\x81\x6e", 7);
            hal_video_draw_text_sjis(info_x + 16, info_y + 228,
                "\x81\x6d\x31\x2d\x34\x81\x46\x90\xd8\x8a\xb7\x8c\x80\x96\x7b\x20\x20\x20\x4d\x81\x46\x91\xe5\x92\x6e\x90\x7d\x20\x20\x20\x54\x41\x42\x81\x46\x8a\xb7\x8c\x69\x81\x6e", 6);

            // Top Banner
            hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);
            hal_video_draw_text_sjis(16, 2, "\x81\x73\x8c\xb9\x95\xbd\x8d\x87\x90\xed\x81\x74\x20\x91\x53\x8e\x6c\x95\x53\x88\xea\x96\xbc\x95\x90\x8f\xab\x96\xbc\x8a\xd3", 7);
            char top_right[64];
            snprintf(top_right, sizeof(top_right), "\x81\x6d%d\x94\x4e%d\x8c\x8e\x20\x4d\x81\x46\x91\xe5\x92\x6e\x90\x7d\x81\x6e", g_scenario.year, g_scenario.month);
            hal_video_draw_text_sjis(450, 2, top_right, 6);

            // Palette Swatches at the bottom
            int sw_x = 36;
            int sw_y = 345;
            int sw_w = 34;
            int sw_h = 16;
            for (int c = 0; c < 16; c++) {
                hal_video_fill_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, c);
                hal_video_draw_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, 7);
            }

            // Bottom Status Bar
            char bot_status[160];
            snprintf(bot_status, sizeof(bot_status),
                     "\x95\x90\x8f\xab\x81\x46%s  \x90\xa8\x97\xcd\x81\x46%s  \x8f\x8a\x8d\xdd\x81\x46%s  \x95\xba\x90\x94\x81\x46%d  \x81\x6d\x4d\x81\x46\x91\xe5\x92\x6e\x90\x7d\x81\x6e",
                     off->full_name_sjis, (off->faction_id >= 0) ? fac->name_sjis : "\x96\xb3", c_name, off->troops);
            hal_video_fill_rect(0, 380, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 380, PC98_WIDTH, 20, 7);
            hal_video_draw_text_sjis(16, 382, bot_status, 7);
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
    if (japan_map_pixels) free(japan_map_pixels);
    if (pld_data) free(pld_data);
    if (jis_data) free(jis_data);
    if (chr_data) free(chr_data);
    if (exe_data) free(exe_data);
    if (kao_data) free(kao_data);
    if (map_file_data) free(map_file_data);

    hal_video_shutdown();
    printf("[+] Engine cleanly stopped.\n");
    return 0;
}

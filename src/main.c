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
            snprintf(candidate, sizeof(candidate), "%s../%s", base, rel_path);
            f = fopen(candidate, "rb");
            if (!f) {
                snprintf(candidate, sizeof(candidate), "%s%s", base, rel_path);
                f = fopen(candidate, "rb");
            }
            SDL_free(base);
        }
    }

    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open file: %s\n", rel_path);
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

// 90 Historical officer names in Shift-JIS & English metadata
static void draw_str_upper(int x, int y, const char *str, uint8_t color) {
    if (!str) return;
    char buf[256];
    int i = 0;
    while (str[i] && i < 255) {
        char ch = str[i];
        if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
        buf[i++] = ch;
    }
    buf[i] = 0;
    hal_video_draw_string_8x16(x, y, buf, color);
}

static const struct {
    const char *sjis_name;
    const char *en_name;
    const char *title;
    int lead, war, intel;
} s_officer_info[90] = {
    {"\x8c\xb9\x97\x8a\x92\xa9",     "Minamoto no Yoritomo",  "Kamakura Shogun / Genji Lord", 95, 80, 94},
    {"\x8c\xb9\x8b\x60\x8c\x6f",     "Minamoto no Yoshitsune","Genji Grand General / Kuro",   98, 99, 90},
    {"\x8c\xb9\x94\xcd\x97\x8a",     "Minamoto no Noriyori",  "Kaba no Kanja / General",      82, 78, 80},
    {"\x88\xa2\x96\xec\x91\x53\x90\xac", "Adachi Morinaga",      "Senior Retainer of Yoritomo",  80, 82, 85},
    {"\x8c\xb9\x8b\x60\x89\x7e",     "Minamoto no Gien",      "Warrior Monk of Kumano",       75, 76, 78},
    {"\x95\xbd\x90\xb4\x90\xb9",     "Taira no Kiyomori",     "Grand Chancellor of Heike",    94, 82, 96},
    {"\x95\xbd\x8f\x64\x90\xb9",     "Taira no Shigemori",    "Lord of Komatsu / Heike",      88, 86, 92},
    {"\x95\xbd\x8f\x47\x90\xb9",     "Taira no Munemori",     "Heike Supreme Commander",      70, 68, 72},
    {"\x95\xbd\x92\x6d\x90\xb9",     "Taira no Tomomori",     "Shin-Chunagon / Naval Admiral",96, 92, 94},
    {"\x95\xbd\x8f\x64\x8c\x79",     "Taira no Shigehira",    "Heike Southern Commander",     85, 84, 80},
    {"\x95\xbd\x92\x89\x93\x78",     "Taira no Tadanori",     "Satsuma-no-Kami / Poet-Warrior",88, 90, 88},
    {"\x95\xbd\x88\xdb\x90\xb9",     "Taira no Koremori",     "Heike Commander at Fuji River", 72, 75, 70},
    {"\x95\xbd\x8b\x90\x8c\x6f",     "Taira no Noritsune",    "Noto-no-Kami / Fiercest Archer",94, 98, 82},
    {"\x8c\xb9\x8b\x60\x92\x86",     "Minamoto no Yoshinaka", "Kiso Yoshinaka / Asahi Shogun", 96, 97, 78},
    {"\x8c\xb9\x97\x7a\x8d\x4c",     "Minamoto no Yukiie",    "Shingu Lord / Anti-Heike Plotter",68, 70, 75},
    {"\x8c\xb9\x97\x8a\x8c\xf5",     "Minamoto no Yorimasa",  "Genji Veteran of Uji Battle",   88, 89, 91},
    {"\x95\x90\x93\x63\x8f\x64\x92\x89", "Hatakeyama Shigetada",  "Model of the Bando Samurai",   92, 96, 88},
    {"\x8a\xef\x90\xec\x8c\x69\x90\xb4", "Kajiwara Kagetoki",    "Chief Military Inspector",     84, 82, 93},
    {"\x8c\xb9\x8b\x60\x8c\xa4",     "Minamoto no Yoshitaka", "Yoshinaka's Son / Hostage",    72, 74, 76},
    {"\x95\x90\x8f\xe3\x90\xb3\x8f\x6c", "Hojo Masako",          "The Nun Shogun of Kamakura",   90, 65, 98},
    {"\x95\x90\x8f\xe3\x8e\x9e\x90\xad", "Hojo Tokimasa",        "First Regent (Shikken)",       86, 75, 92},
    {"\x8e\x4f\x89\x79\x8b\x60\x91\xa5", "Miura Yoshizumi",      "Lord of Sagami Miura Clan",    85, 86, 84},
    {"\x98\x61\x93\x63\x8b\x60\x90\xb7", "Wada Yoshimori",       "Director of Samurai-dokoro",   89, 95, 78},
    {"\x8c\x76\x92\x4a\x92\xbc\x8e\xc0", "Kumagai Naozane",      "Brave Warrior of Ichinotani",  88, 94, 82},
    {"\x93\xc7\x8e\xef\x97\x5e\x88\xea", "Nasu no Yoichi",        "Master Archer of Yashima",     85, 99, 80},
    {"\x95\x92\x91\xa0\x96\x5b\x95\xd9\x8c\x6f", "Musashibo Benkei", "Loyal Giant Warrior Monk",     92, 100, 88},
    {"\x93\xa1\x8c\xb4\x8f\x47\x8c\x79", "Fujiwara no Hidehira", "Lord of Oshu Hiraizumi",      94, 85, 95},
    {"\x93\xa1\x8c\xb4\x91\xd1\x8c\x79", "Fujiwara no Yasuhira", "Heir of Oshu Fujiwara Clan",  74, 72, 70},
    {"\x94\xca\x8c\xe4\x91\x4f",         "Tomoe Gozen",           "Heroic Female General of Kiso",93, 98, 82},
    {"\x8d\xa1\x88\xe4\x8c\x92\x95\xbd", "Imai Kanehira",        "Four Guardians of Yoshinaka",  90, 94, 84},
    // Special NPCs 80..88 (64x80 avatars)
    [80] = {"\x8b\x53",                 "Princess (Hime)",       "Noble Clan Daughter",           60, 50, 85},
    [81] = {"\x98\x59\x90\x6c",         "Wild Samurai (Ronin)",  "Masterless Outlaw Warrior",     70, 86, 65},
    [82] = {"\x92\xac\x98\x56",         "Town Elder",            "Respected Village Headman",     50, 40, 88},
    [83] = {"\x8e\x67\x8e\xd2",         "Diplomatic Envoy",      "Messenger between Daimyos",     72, 70, 84},
    [84] = {"\x8f\xa4\x90\x6c",         "Merchant / Trader",     "Traveling Goods Merchant",      45, 45, 90},
    [85] = {"\x8b\x4d\x95\x7b\x90\x6c", "Noblewoman",            "Imperial Court Lady",           55, 40, 88},
    [86] = {"\x96\xbd\x95\x7b",         "Court Consort",         "Lady of the Imperial Palace",   62, 45, 92},
    [87] = {"\x90\xc3\x8c\xe4\x91\x4f", "Shizuka Gozen",         "Master Shirabyoshi Dancer",     75, 60, 95},
    [88] = {"\x94\x5f\x95\x7b",         "Peasant Farmer",        "Provincial Land Cultivator",    40, 50, 60},
    // 89: Monk
    [89] = {"\x91\x65\x95\x73",         "Warrior Monk (Sohei)",  "Enryakuji Temple Guard",        86, 92, 80},
};

static void update_window_title(int cur_officer, bool use_mask, int bg_mode, bool map_mode, int camera_x) {
    char title[256];
    if (map_mode) {
        snprintf(title, sizeof(title),
                 "《源平合战》[全日本66国战略大地图] 视口X:%04d/1120 | 左右方向键:平移镜头 | M:返回人物立绘",
                 camera_x);
    } else {
        bool is_npc = (cur_officer >= 80 && cur_officer <= 88);
        const char *bg_names[] = {"纯黑展台", "军议大殿(640x400)", "野战军营(288x176)"};
        snprintf(title, sizeof(title),
                 "《源平合战》[人物 %02d/90] %s (%dx%d) | 遮罩:%s | 背景:%s | 左右键:换人 M:看大地图 TAB:换景",
                 cur_officer + 1,
                 is_npc ? "NPC/姬/町人/使者小立绘" : (cur_officer == 89 ? "僧兵立绘" : "大名/名将立绘"),
                 is_npc ? 64 : 128,
                 is_npc ? 80 : 160,
                 use_mask ? "开" : "关",
                 bg_names[bg_mode % 3]);
    }
    hal_video_set_title(title);
}

int main(int argc, char *argv[]) {
    const char *screenshot_path = NULL;
    int max_frames = 0;
    bool start_map_mode = false;

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
        } else if (strcmp(argv[i], "--map") == 0 || strcmp(argv[i], "-m") == 0) {
            start_map_mode = true;
        }
    }

    if (!hal_video_init("《源平合战》 - Linux Native Video Engine (SDL2/PC-98)", 2)) {
        return 1;
    }

    // 1. Load Mainpal.pld Bank 0
    size_t pld_size = 0;
    uint8_t *pld_data = load_file("games/yphz/Mainpal.pld", &pld_size);
    if (pld_data) {
        hal_video_load_pld_bank(pld_data, 0);
        printf("[+] Loaded Mainpal.pld Bank 0 (16-color PC-98 GBR palette)\n");
    }

    // 2. Load JIS.FNT & FONT.CHR (Native Font Subsystem)
    size_t jis_size = 0, chr_size = 0;
    uint8_t *jis_data = load_file("games/yphz/JIS.FNT", &jis_size);
    uint8_t *chr_data = load_file("games/yphz/FONT.CHR", &chr_size);
    if (jis_data && chr_data) {
        hal_video_load_fonts(jis_data, jis_size, chr_data, chr_size);
        printf("[+] Loaded JIS.FNT (%zu bytes, 8948 Kanji) & FONT.CHR (%zu bytes, 256 ASCII)\n", jis_size, chr_size);
    }

    // 3. Read 90 officer offsets from Main.exe at 0x4d3bb
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

    // 4. Load Kaodata.gp
    size_t kao_size = 0;
    uint8_t *kao_data = load_file("games/yphz/Kaodata.gp", &kao_size);
    if (!kao_data) {
        fprintf(stderr, "[ERROR] Failed to load games/yphz/Kaodata.gp\n");
        return 1;
    }
    printf("[+] Loaded Kaodata.gp (%zu bytes)\n", kao_size);

    // 5. Load Scenic backgrounds (Hikback.gp & Maincmd2.gp)
    size_t bg_hik_sz = 0;
    uint8_t *bg_hik_data = load_file("games/yphz/Hikback.gp", &bg_hik_sz);
    ImageDecoded bg_hik = {0};
    if (bg_hik_data) {
        nup4_decompress_block(bg_hik_data, bg_hik_sz, 0, &bg_hik, NULL);
    }

    size_t bg_cmd_sz = 0;
    uint8_t *bg_cmd_data = load_file("games/yphz/Maincmd2.gp", &bg_cmd_sz);
    ImageDecoded bg_palace = {0};
    if (bg_cmd_data) {
        nup4_decompress_block(bg_cmd_data, bg_cmd_sz, 0, &bg_palace, NULL);
        printf("[+] Loaded Maincmd2.gp palace screen (640x400)\n");
    }

    // 6. Load & Stitch Panoramic Japan Strategic Map (Mainmap.gp: 20 blocks -> 1760x360)
    size_t map_file_sz = 0;
    uint8_t *map_file_data = load_file("games/yphz/Mainmap.gp", &map_file_sz);
    uint8_t *japan_map_pixels = NULL;
    int map_w = 1760;
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
                    for (int y = 0; y < blk.height && y < map_h; y++) {
                        memcpy(japan_map_pixels + y * map_w + cur_x, blk.pixels + y * blk.width, blk.width);
                    }
                    cur_x += blk.width;
                    off += (consumed > 0 ? consumed : 48);
                    free_decoded_image(&blk);
                } else {
                    off += 6;
                }
            }
            printf("[+] Stitched Mainmap.gp into %dx%d strategic Japan map\n", cur_x, map_h);
        }
    }

    bool use_mask = true;
    int bg_mode = 1; // 0: Pure black, 1: 640x400 Palace, 2: 288x176 Tactical
    bool show_info = true;
    bool map_mode = start_map_mode;
    int camera_x = 520; // Default center around Kansai / Kyoto / Seto Inland Sea

    ImageDecoded cur_kao = {0};
    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);

    bool running = true;
    int frame_count = 0;
    while (running) {
        int key = 0;
        bool quit = false;
        while (hal_video_poll_event(&key, &quit)) {
            if (quit) running = false;
            if (key == SDLK_m) {
                map_mode = !map_mode;
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
            }

            if (map_mode) {
                // Strategic Map Navigation
                if (key == SDLK_LEFT || key == SDLK_a) {
                    camera_x -= 40;
                    if (camera_x < 0) camera_x = 0;
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                } else if (key == SDLK_RIGHT || key == SDLK_d) {
                    camera_x += 40;
                    if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                }
            } else {
                // Officer Showcase Controls
                if (key == SDLK_RIGHT) {
                    cur_officer = (cur_officer + 1) % 90;
                    free_decoded_image(&cur_kao);
                    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                } else if (key == SDLK_LEFT) {
                    cur_officer = (cur_officer - 1 + 90) % 90;
                    free_decoded_image(&cur_kao);
                    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[cur_officer], &cur_kao);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                } else if (key == SDLK_SPACE) {
                    use_mask = !use_mask;
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                } else if (key == SDLK_TAB) {
                    bg_mode = (bg_mode + 1) % 3;
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x);
                } else if (key == SDLK_i) {
                    show_info = !show_info;
                }
            }
        }

        // --- Render Frame ---
        hal_video_clear(0);

        if (map_mode && japan_map_pixels) {
            // ===============================================================
            // Strategic Map Mode (1760x360 Full Japan Panorama)
            // ===============================================================
            hal_video_blit_indexed(0, 20, PC98_WIDTH, 360, japan_map_pixels + camera_x, map_w);

            // Top Status Bar (Color 1 Navy Blue)
            hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);
            hal_video_draw_string_8x16(16, 2, "GENPEI KASSEN - PANORAMIC STRATEGIC MAP OF JAPAN", 7);

            char pos_str[64];
            snprintf(pos_str, sizeof(pos_str), "VIEWPORT: %04d/1120 [M:EXIT]", camera_x);
            hal_video_draw_string_8x16(430, 2, pos_str, 6);

            // Bottom Navigation Bar
            hal_video_fill_rect(0, 380, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 380, PC98_WIDTH, 20, 7);
            hal_video_draw_string_8x16(16, 382, "<- KYUSHU / NAGATO (WEST)   [ARROWS: SCROLL]   KANTO / MUTSU (EAST) ->", 7);

        } else {
            // ===============================================================
            // Officer Showcase Mode (With Real 16x16 Kanji Font Rendering)
            // ===============================================================
            // 1. Draw Background
            if (bg_mode == 1 && bg_palace.pixels) {
                hal_video_blit_indexed(0, 0, bg_palace.width, bg_palace.height, bg_palace.pixels, bg_palace.width);
            } else if (bg_mode == 2 && bg_hik.pixels) {
                int bg_x = (PC98_WIDTH - bg_hik.width) / 2;
                int bg_y = 40;
                hal_video_blit_indexed(bg_x, bg_y, bg_hik.width, bg_hik.height, bg_hik.pixels, bg_hik.width);
                hal_video_draw_rect(bg_x - 1, bg_y - 1, bg_hik.width + 2, bg_hik.height + 2, 7);
            }

            // 2. Draw Officer Portrait
            bool is_npc = (cur_officer >= 80 && cur_officer <= 88);
            int frame_w = is_npc ? 64 : 128;
            int frame_h = is_npc ? 80 : 160;
            int card_x = 40;
            int card_y = 50;

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

            // 3. Draw Info Card with 16x16 Kanji Font
            if (show_info) {
                int info_x = card_x + frame_w + 16;
                int info_y = card_y;
                int info_w = PC98_WIDTH - info_x - 30;
                if (info_w > 420) info_w = 420;
                int info_h = frame_h + 8;

                hal_video_fill_rect(info_x, info_y, info_w, info_h, 1); // Navy Blue
                hal_video_draw_rect(info_x, info_y, info_w, info_h, 6); // Gold border
                hal_video_draw_rect(info_x + 2, info_y + 2, info_w - 4, info_h - 4, 7); // White inner

                const char *s_name = s_officer_info[cur_officer].sjis_name;
                const char *e_name = s_officer_info[cur_officer].en_name;
                const char *t_desc = s_officer_info[cur_officer].title;
                if (!s_name) s_name = "\x95\x92\x8f\xec";
                if (!e_name) e_name = "Samurai Commander";
                if (!t_desc) t_desc = "Historical General";

                // Draw Shift-JIS Officer Name in 16x16 Kanji
                hal_video_draw_text_sjis(info_x + 16, info_y + 12, s_name, 7);

                // Draw Romanized Name & Title
                draw_str_upper(info_x + 16, info_y + 34, e_name, 6);
                draw_str_upper(info_x + 16, info_y + 52, t_desc, 5);

                // Divider line
                hal_video_draw_rect(info_x + 14, info_y + 72, info_w - 28, 1, 7);

                // Numerical Attributes
                char stat_buf[128];
                int l = s_officer_info[cur_officer].lead ? s_officer_info[cur_officer].lead : 80;
                int w = s_officer_info[cur_officer].war ? s_officer_info[cur_officer].war : 80;
                int in = s_officer_info[cur_officer].intel ? s_officer_info[cur_officer].intel : 80;
                snprintf(stat_buf, sizeof(stat_buf), "COMMAND : %2d   WAR : %2d   INTEL : %2d", l, w, in);
                hal_video_draw_string_8x16(info_x + 16, info_y + 82, stat_buf, 7);

                hal_video_draw_string_8x16(info_x + 16, info_y + 102, "FACTION : GENJI (WHITE) / HEIKE (RED)", 3);
                hal_video_draw_string_8x16(info_x + 16, info_y + 120, "PROVINCE: SAGAMI (KAMAKURA) / YASHIMA", 7);
                hal_video_draw_string_8x16(info_x + 16, info_y + 138, "[PRESS 'M' KEY: SWITCH TO JAPAN MAP]", 6);
            }

            // Top Banner
            hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);
            hal_video_draw_string_8x16(16, 2, "KOEI 1994 - GENPEI KASSEN (PC-98 / LINUX NATIVE ENGINE)", 7);
            hal_video_draw_string_8x16(480, 2, "[PRESS 'M': MAP]", 6);

            // Palette Swatches at the bottom
            int sw_x = 40;
            int sw_y = 350;
            int sw_w = 32;
            int sw_h = 16;
            for (int c = 0; c < 16; c++) {
                hal_video_fill_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, c);
                hal_video_draw_rect(sw_x + c * (sw_w + 2), sw_y, sw_w, sw_h, 7);
            }
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

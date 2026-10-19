#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sdl_compat.h"
#include "hal_video.h"
#include "input.h"
#include "decompress.h"
#include "message.h"
#include "turn.h"
#include "npk.h"
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

static void decode_officer_portrait(int officer_id, const Scenario *sc, const uint8_t *kao_data, size_t kao_size, const uint32_t *officer_offsets, ImageDecoded *out) {
    if (!out) return;
    free_decoded_image(out);
    if (!sc || officer_id < 0 || officer_id >= sc->num_officers) return;
    int fid = sc->officers[officer_id].face_id;
    int use_fid = (fid >= 0 && fid < 90) ? fid : (officer_id % 15) + 40;
    kaodata_decompress_officer(kao_data, kao_size, officer_offsets[use_fid], out);
}

static void refresh_officer_portrait(int cur_officer, const Scenario *sc, const uint8_t *kao_data, size_t kao_size, const uint32_t *officer_offsets, ImageDecoded *cur_kao) {
    decode_officer_portrait(cur_officer, sc, kao_data, kao_size, officer_offsets, cur_kao);
}

// Populate the report dialogue state from a domestic-command result.
static void show_turn_result(const TurnResult *r, bool *show_dialog,
                             bool *dlg_custom, int *dlg_officer,
                             char *dlg_text, size_t dlg_sz, ImageDecoded *dlg_kao,
                             const Scenario *sc, const uint8_t *kao_data,
                             size_t kao_size, const uint32_t *officer_offsets) {
    *dlg_custom = true;
    *dlg_officer = r->officer_id;
    snprintf(dlg_text, dlg_sz, "%s", r->text);
    *show_dialog = true;
    if (r->officer_id >= 0) {
        decode_officer_portrait(r->officer_id, sc, kao_data, kao_size,
                                officer_offsets, dlg_kao);
    }
}

static void update_window_title(int cur_officer, bool use_mask, int bg_mode, bool map_mode, int camera_x, int scenario_idx, const Scenario *sc, int cur_castle) {
    char title[256];
    if (map_mode) {
        const char *c_name = (sc && cur_castle >= 0 && cur_castle < sc->num_castles) ? sc->castles[cur_castle].name_sjis : "";
        snprintf(title, sizeof(title),
                 "《源平合战》[剧本%d] 选城:%s | 视口X:%04d/1056 | 左右:平移 ［］:换城 1-4:换剧本 C:详情 T:会话 M:武将 O:查看城主",
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

// ---------------------------------------------------------------------------
// Strategic map interaction helpers
// ---------------------------------------------------------------------------

// The 1696x360 map panorama is blitted at y = 20 (below the 20px status bar),
// so screen y + this offset == world (map image) y.
#define MAP_VIEW_Y_OFFSET 20

static int clamp_camera_x(int x, int map_w) {
    if (x < 0) return 0;
    if (x > map_w - PC98_WIDTH) return map_w - PC98_WIDTH;
    return x;
}

// Hit-test the 39 castle markers against a click given in *logical* (640x400)
// coordinates. The clickable box is centred on the pennant/pole drawn for each
// castle, so it covers the flag and its selection reticle. Returns the castle
// index, or -1 when nothing was hit.
static int map_castle_hit_test(const Scenario *sc, int camera_x, int mouse_x, int mouse_y) {
    int world_x = mouse_x + camera_x;
    int world_y = mouse_y - MAP_VIEW_Y_OFFSET;
    for (int i = 0; i < sc->num_castles; i++) {
        int dx = world_x - sc->castles[i].map_x;
        int dy = world_y - sc->castles[i].map_y;
        if (dx >= -16 && dx <= 16 && dy >= -18 && dy <= 12) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Koei-style dialogue box
// ---------------------------------------------------------------------------

// Draw a Shift-JIS string with soft wrapping at max_w (full-width glyphs are
// 16px, half-width 8px). Explicit '\n' always breaks the line.
static void draw_wrapped_sjis(int x, int y, int max_w, int line_h,
                              const char *sjis, uint8_t color_idx) {
    if (!sjis) return;
    const uint8_t *p = (const uint8_t*)sjis;
    char line[512];
    int li = 0;
    int lw = 0;
    int cy = y;

    while (*p) {
        uint8_t c = *p;
        if (c == '\n') {
            line[li] = '\0';
            if (li > 0) hal_video_draw_text_sjis(x, cy, line, color_idx);
            cy += line_h; li = 0; lw = 0; p++;
            continue;
        }

        int is_full = ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC));
        int byte_count = (is_full && p[1]) ? 2 : 1;
        int adv = is_full ? 16 : 8;

        if (lw + adv > max_w && li > 0) {
            line[li] = '\0';
            hal_video_draw_text_sjis(x, cy, line, color_idx);
            cy += line_h; li = 0; lw = 0;
        }
        if (li + byte_count < (int)sizeof(line)) {
            line[li++] = (char)c;
            if (byte_count == 2) line[li++] = (char)p[1];
        }
        lw += adv;
        p += byte_count;
    }

    if (li > 0) {
        line[li] = '\0';
        hal_video_draw_text_sjis(x, cy, line, color_idx);
    }
}

// ---------------------------------------------------------------------------
// Domestic-affairs UI (command menu / officer roster / result dialogue)
// ---------------------------------------------------------------------------

#define SJIS_CMD_TITLE "\x8e\x77\x97\xdf"                   // 指令
#define SJIS_RESULT    "\x81\x79\x95\x5d\x92\xe8\x81\x7a"   // 【評定】
#define SJIS_ROSTER    "\x95\x90\x8f\xab\x88\xea\x97\x97"   // 武将一覧
#define SJIS_LEAD      "\x93\x9d\x97\xa6"                   // 統率
#define SJIS_WAR       "\x95\x90\x97\xcd"                   // 武力
#define SJIS_INTEL     "\x92\x6d\x96\x64"                   // 知謀

#define CMD_MENU_X        496
#define CMD_MENU_Y        40
#define CMD_MENU_W        128
#define CMD_MENU_TITLE_H  22
#define CMD_MENU_ITEM_H   26
#define CMD_MENU_ITEMS    5
#define CMD_MENU_H        (CMD_MENU_TITLE_H + CMD_MENU_ITEMS * CMD_MENU_ITEM_H + 8)

#define ROSTER_X          52
#define ROSTER_Y          40
#define ROSTER_W          436
#define ROSTER_H          310
#define ROSTER_ROWS_TOP   52
#define ROSTER_ROW_H      22
#define ROSTER_ROWS       11

static int sjis_width(const char *s) {
    int w = 0;
    const uint8_t *p = (const uint8_t*)s;
    while (p && *p) {
        if (*p == '\n') { p++; continue; }
        if (((*p >= 0x81 && *p <= 0x9F) || (*p >= 0xE0 && *p <= 0xFC)) && p[1]) {
            w += 16; p += 2;
        } else {
            w += 8; p++;
        }
    }
    return w;
}

// Which command-menu row is under the pointer? -1 when outside.
static int cmd_menu_hit(int mouse_x, int mouse_y) {
    if (mouse_x < CMD_MENU_X || mouse_x >= CMD_MENU_X + CMD_MENU_W) return -1;
    int top = CMD_MENU_Y + CMD_MENU_TITLE_H;
    if (mouse_y < top) return -1;
    int idx = (mouse_y - top) / CMD_MENU_ITEM_H;
    if (idx < 0 || idx >= CMD_MENU_ITEMS) return -1;
    return idx;
}

static void draw_command_menu(int hover) {
    static const char *labels[CMD_MENU_ITEMS] = {
        "\x8a\x4a\x94\xad",   // 開発
        "\x92\xa5\x95\xba",   // 徴兵
        "\x92\xa5\x90\xc5",   // 徴税
        "\x95\x90\x8f\xab",   // 武将
        "\x8f\x49\x97\xb9"    // 終了
    };

    hal_video_fill_rect(CMD_MENU_X, CMD_MENU_Y, CMD_MENU_W, CMD_MENU_H, 1);
    hal_video_draw_rect(CMD_MENU_X, CMD_MENU_Y, CMD_MENU_W, CMD_MENU_H, 6);
    hal_video_draw_rect(CMD_MENU_X + 2, CMD_MENU_Y + 2,
                        CMD_MENU_W - 4, CMD_MENU_H - 4, 7);
    hal_video_draw_text_sjis(CMD_MENU_X + 10, CMD_MENU_Y + 4, SJIS_CMD_TITLE, 6);

    for (int i = 0; i < CMD_MENU_ITEMS; i++) {
        int iy = CMD_MENU_Y + CMD_MENU_TITLE_H + i * CMD_MENU_ITEM_H;
        bool hl = (i == hover);
        if (hl) {
            hal_video_fill_rect(CMD_MENU_X + 5, iy, CMD_MENU_W - 10,
                                CMD_MENU_ITEM_H - 2, 6);
        }
        uint8_t fg = hl ? 1 : 7;
        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        hal_video_draw_text_sjis(CMD_MENU_X + 10, iy + 5, num, fg);
        hal_video_draw_text_sjis(CMD_MENU_X + 26, iy + 5, labels[i], fg);
    }
}

static void draw_officer_roster(const Scenario *sc, const TurnState *ts,
                               const int *ids, int count, int scroll,
                               const char *title) {
    hal_video_fill_rect(ROSTER_X, ROSTER_Y, ROSTER_W, ROSTER_H, 1);
    hal_video_draw_rect(ROSTER_X, ROSTER_Y, ROSTER_W, ROSTER_H, 6);
    hal_video_draw_rect(ROSTER_X + 2, ROSTER_Y + 2,
                        ROSTER_W - 4, ROSTER_H - 4, 7);

    hal_video_draw_text_sjis(ROSTER_X + 12, ROSTER_Y + 6, title, 6);

    char counted[32];
    snprintf(counted, sizeof(counted), "%d", count);
    hal_video_draw_text_sjis(ROSTER_X + ROSTER_W - 24 - sjis_width(counted),
                             ROSTER_Y + 6, counted, 7);

    hal_video_draw_text_sjis(ROSTER_X + 232, ROSTER_Y + 30,
                             SJIS_LEAD "    " SJIS_WAR "    " SJIS_INTEL, 6);
    hal_video_draw_rect(ROSTER_X + 10, ROSTER_Y + 48, ROSTER_W - 20, 1, 7);

    for (int r = 0; r < ROSTER_ROWS; r++) {
        int i = scroll + r;
        if (i >= count) break;
        const ScenarioOfficer *o = &sc->officers[ids[i]];
        int ry = ROSTER_Y + ROSTER_ROWS_TOP + r * ROSTER_ROW_H;
        uint8_t fg = turn_officer_has_acted(ts, o->id) ? 5 : 7;

        hal_video_draw_text_sjis(ROSTER_X + 14, ry, o->full_name_sjis, fg);
        char stats[64];
        snprintf(stats, sizeof(stats), "%3d / %3d / %3d", o->lead, o->war, o->intel);
        hal_video_draw_text_sjis(ROSTER_X + 232, ry, stats, fg);
    }
}

static void draw_dialog_box(const char *text, const char *tag,
                            const ImageDecoded *portrait) {
    const int cx = 16;
    const int cy = portrait ? 200 : 286;
    const int cw = 608;
    const int ch = portrait ? 178 : 92;

    hal_video_fill_rect(cx, cy, cw, ch, 1);                       // navy panel
    hal_video_draw_rect(cx, cy, cw, ch, 6);                       // gold outer
    hal_video_draw_rect(cx + 2, cy + 2, cw - 4, ch - 4, 7);       // white inner

    int tx = cx + 18;
    int ty = cy + 14;
    int tw = cw - 36;

    if (portrait && portrait->pixels) {
        int px = cx + 8;
        int py = cy + 9;
        hal_video_draw_rect(px - 2, py - 2, portrait->width + 4, portrait->height + 4, 6);
        if (portrait->mask) {
            hal_video_blit_masked(px, py, portrait->width, portrait->height,
                                  portrait->pixels, portrait->mask, portrait->width);
        } else {
            hal_video_blit_indexed(px, py, portrait->width, portrait->height,
                                   portrait->pixels, portrait->width);
        }
        tx = px + portrait->width + 16;
        tw = cx + cw - tx - 18;
    }

    draw_wrapped_sjis(tx, ty, tw, 18, text, 7);

    // Right-aligned source/label tag. The PC-98 half-width font only renders
    // upper-case ASCII reliably, so labels use SJIS with ASCII indices.
    if (tag && tag[0]) {
        int tag_x = cx + cw - 16 - sjis_width(tag);
        hal_video_draw_text_sjis(tag_x, cy + ch - 20, tag, 6);
    }
}

// ---------------------------------------------------------------------------
// NPK016 gallery (L) / item viewer (I) and headless click injection
// ---------------------------------------------------------------------------

#define MAX_CLICKS 32

typedef struct {
    int x, y;
    int frame;
} ClickEvent;

static const char *const GALLERY_PACKS[] = {
    "Logo.gp", "Mainevt.gp", "Mainitem.gp", "Mainstl.gp",
    "Maincmd2.gp", "Hkeshiki.gp", "Hkumi.gp", "Hikback.gp"
};
#define GALLERY_PACK_COUNT ((int)(sizeof(GALLERY_PACKS) / sizeof(GALLERY_PACKS[0])))

// PC-98 FONT.CHR renders lower-case ASCII unreliably; labels stay upper-case.
static void ascii_upper(char *s) {
    for (; s && *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
    }
}

static void draw_centered_image(const ImageDecoded *img) {
    if (!img || !img->pixels) return;
    int px = (PC98_WIDTH - img->width) / 2;
    int py = (PC98_HEIGHT - img->height) / 2;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    hal_video_blit_indexed(px, py, img->width, img->height,
                           img->pixels, img->width);
}

// Load/decode the requested pack block, caching the last decoded image.
// `block` is clamped to the pack's range; *cur_block reports the result.
static bool gallery_update(int pack, int *block,
                           NpkFile *npk, ImageDecoded *img,
                           int *cur_pack, int *cur_block) {
    if (pack < 0 || pack >= GALLERY_PACK_COUNT) return false;
    if (pack == *cur_pack && *block == *cur_block && img->pixels) return true;

    npk_free(npk);
    free_decoded_image(img);
    *cur_pack = -1;
    *cur_block = -1;

    if (!npk_load(npk, s_asset_dir, GALLERY_PACKS[pack])) return false;
    if (*block < 0) *block = 0;
    if (*block >= npk->count) *block = npk->count - 1;
    if (!npk_decode(npk, *block, img)) return false;
    *cur_pack = pack;
    *cur_block = *block;
    return true;
}

static void draw_gallery_overlay(int pack, int block, int count,
                                 int img_w, int img_h) {
    hal_video_fill_rect(0, 0, PC98_WIDTH, 20, 1);
    hal_video_draw_rect(0, 0, PC98_WIDTH, 20, 7);

    char hdr[128];
    char pack_name[64];
    snprintf(pack_name, sizeof(pack_name), "%s", GALLERY_PACKS[pack]);
    ascii_upper(pack_name);
    snprintf(hdr, sizeof(hdr), "NPK %s  BLOCK %d/%d  %dx%d",
             pack_name, block + 1, count, img_w, img_h);
    hal_video_draw_text_sjis(16, 2, hdr, 7);

    hal_video_fill_rect(0, PC98_HEIGHT - 20, PC98_WIDTH, 20, 1);
    hal_video_draw_rect(0, PC98_HEIGHT - 20, PC98_WIDTH, 20, 7);
    hal_video_draw_text_sjis(16, PC98_HEIGHT - 18,
        "LEFT/RIGHT:BLOCK  UP/DOWN:PACK  L/ESC:CLOSE", 6);
}

// ASCII command name for headless logs.
static const char* turn_cmd_ascii(TurnCommand cmd) {
    switch (cmd) {
        case TURN_CMD_DEVELOP:  return "DEVELOP";
        case TURN_CMD_RECRUIT:  return "RECRUIT";
        case TURN_CMD_TAX:      return "TAX";
        case TURN_CMD_OFFICERS: return "OFFICERS";
        case TURN_CMD_END:      return "END";
        default:                return "NONE";
    }
}

int main(int argc, char *argv[]) {
    const char *screenshot_path = NULL;
    int max_frames = 0;
    bool start_map_mode = false;
    int cur_scenario = 0; // Scenario 0..3 (1180, 1183, 1184, 1185)
    int cur_castle = 30;  // Default to Kamakura (Castle 30)
    bool show_castle_card = true;
    bool select_castle_mode = false;  // --select-castle: headless-friendly selection

    // Message.gp dialogue (LS11) state.
    int cur_msg = 0;             // current string index within msg_entry
    int msg_entry = 0;           // --msg-entry: which LS11 entry to browse
    int test_msg = -1;           // --test-msg: force the dialogue box open
    bool show_dialog = false;    // dialogue box visibility (T toggles)

    // Turn / domestic-affairs state.
    int faction_override = -1;             // --faction
    TurnCommand do_cmd = TURN_CMD_NONE;    // --do-cmd
    int end_turns = 0;                     // --end-turn
    bool show_cmd_menu = false;            // player-castle command menu
    bool show_officer_list = false;        // officer roster (modal)
    bool dlg_custom = false;               // dialogue shows dlg_custom_text
    char dlg_custom_text[TURN_TEXT_MAX];   // domestic result / roster text
    int  dlg_officer = -1;                 // portrait to show in the dialogue
    int  roster_ids[MAX_OFFICERS];
    int  roster_count = 0;
    int  roster_scroll = 0;
    bool roster_select_mode = false;              // roster is choosing an actor
    TurnCommand roster_cmd = TURN_CMD_NONE;       // command awaiting an officer

    // Headless mouse-click injection: --click x,y[@frame], repeatable / ';'-list.
    ClickEvent clicks[MAX_CLICKS];
    int click_count = 0;
    int next_click_frame = 1;

    // --preview-npk <file> <block> standalone viewer.
    const char *preview_npk_name = NULL;
    int  preview_npk_index = 0;
    bool preview_mode = false;
    NpkFile preview_pack = {0};
    ImageDecoded preview_img = {0};

    // In-game NPK gallery (L) / item viewer (I).
    bool gallery_mode = false;
    int  gallery_pack = 0;
    int  gallery_block = 0;
    int  gallery_loaded_pack = -1;
    int  gallery_loaded_block = -1;
    NpkFile gallery_npk = {0};
    ImageDecoded gallery_img = {0};

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
        } else if (strcmp(argv[i], "--select-castle") == 0 && i + 1 < argc) {
            cur_castle = atoi(argv[++i]);
            if (cur_castle < 0) cur_castle = 0;
            if (cur_castle >= MAX_CASTLES) cur_castle = MAX_CASTLES - 1;
            select_castle_mode = true;
        } else if (strcmp(argv[i], "--test-msg") == 0 && i + 1 < argc) {
            test_msg = atoi(argv[++i]);
            if (test_msg < 0) test_msg = 0;
        } else if (strcmp(argv[i], "--msg-entry") == 0 && i + 1 < argc) {
            msg_entry = atoi(argv[++i]);
            if (msg_entry < 0) msg_entry = 0;
        } else if (strcmp(argv[i], "--faction") == 0 && i + 1 < argc) {
            faction_override = atoi(argv[++i]);
            if (faction_override < 0) faction_override = 0;
            if (faction_override > 14) faction_override = 14;
        } else if (strcmp(argv[i], "--do-cmd") == 0 && i + 1 < argc) {
            const char *c = argv[++i];
            if (strcmp(c, "develop") == 0)      do_cmd = TURN_CMD_DEVELOP;
            else if (strcmp(c, "recruit") == 0) do_cmd = TURN_CMD_RECRUIT;
            else if (strcmp(c, "tax") == 0)     do_cmd = TURN_CMD_TAX;
            else if (strcmp(c, "officers") == 0) do_cmd = TURN_CMD_OFFICERS;
            else if (strcmp(c, "end") == 0)     do_cmd = TURN_CMD_END;
            else fprintf(stderr, "[WARN] unknown --do-cmd '%s' (develop|recruit|tax|officers|end)\n", c);
        } else if (strcmp(argv[i], "--end-turn") == 0 && i + 1 < argc) {
            end_turns = atoi(argv[++i]);
            if (end_turns < 0) end_turns = 0;
        } else if (strcmp(argv[i], "--click") == 0 && i + 1 < argc) {
            char *spec = argv[++i];
            char *p = spec;
            while (*p && click_count < MAX_CLICKS) {
                char *tok = p;
                char *semi = strchr(p, ';');
                if (semi) {
                    *semi = '\0';
                    p = semi + 1;
                } else {
                    p += strlen(p);
                }

                int x = 0, y = 0, fr = -1;
                if (sscanf(tok, "%d,%d@%d", &x, &y, &fr) < 2) {
                    fprintf(stderr, "[WARN] bad --click token '%s' (want x,y[@frame])\n", tok);
                    continue;
                }
                if (x < 0) x = 0;
                if (x >= PC98_WIDTH) x = PC98_WIDTH - 1;
                if (y < 0) y = 0;
                if (y >= PC98_HEIGHT) y = PC98_HEIGHT - 1;
                if (fr < 0) {
                    fr = next_click_frame++;
                } else if (fr >= next_click_frame) {
                    next_click_frame = fr + 1;
                }
                clicks[click_count].x = x;
                clicks[click_count].y = y;
                clicks[click_count].frame = fr;
                click_count++;
            }
        } else if (strcmp(argv[i], "--preview-npk") == 0 && i + 2 < argc) {
            preview_npk_name = argv[++i];
            preview_npk_index = atoi(argv[++i]);
            if (preview_npk_index < 0) preview_npk_index = 0;
        } else if (strcmp(argv[i], "--gallery") == 0 && i + 1 < argc) {
            gallery_pack = atoi(argv[++i]);
            if (gallery_pack < 0) gallery_pack = 0;
            if (gallery_pack >= GALLERY_PACK_COUNT) gallery_pack = GALLERY_PACK_COUNT - 1;
            gallery_mode = true;
            gallery_loaded_pack = -1;
        }
    }

    // --select-castle renders the strategic map with the chosen castle selected
    // and its info card open, so a headless --frames/--screenshot run captures
    // exactly that state.
    if (select_castle_mode) {
        start_map_mode = true;
        show_castle_card = true;
    }

    // --test-msg renders the dialogue box over the strategic map with the
    // requested Message.gp string, for headless screenshot verification.
    if (test_msg >= 0) {
        cur_msg = test_msg;
        start_map_mode = true;
        show_castle_card = false;
        show_dialog = true;
    }

    // --do-cmd / --end-turn: run domestic commands headlessly and show the
    // resulting report over the strategic map.
    if (do_cmd != TURN_CMD_NONE || end_turns > 0) {
        start_map_mode = true;
        show_castle_card = true;
        show_cmd_menu = (do_cmd == TURN_CMD_NONE || do_cmd == TURN_CMD_OFFICERS);
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

    // 8. Load Message.gp dialogue (LS11 text container)
    MessageFile g_messages = {0};
    {
        char msg_path[512];
        snprintf(msg_path, sizeof(msg_path), "%s/Message.gp", s_asset_dir);
        if (!message_load(msg_path, &g_messages)) {
            message_load("Message.gp", &g_messages);
        }
        if (g_messages.loaded) {
            printf("[+] Loaded Message.gp: %d LS11 entries (%d strings in entry %d)\n",
                   g_messages.num_entries, message_string_count(&g_messages, 0), 0);
        }
    }

    // 9. Turn / domestic state (子任务 3)
    int player_faction;
    if (faction_override >= 0) {
        player_faction = faction_override;
    } else if ((do_cmd != TURN_CMD_NONE || end_turns > 0) &&
               cur_castle >= 0 && cur_castle < g_scenario.num_castles) {
        // Headless domestic tests operate on the selected castle's clan.
        player_faction = g_scenario.castles[cur_castle].faction_id;
    } else {
        player_faction = g_scenario.castles[30].faction_id;  // 鎌倉 (源氏)
    }
    if (player_faction < 0) player_faction = 0;
    TurnState g_turn;
    turn_init(&g_turn, &g_scenario, player_faction);
    show_cmd_menu = false;
    if (show_castle_card && cur_castle >= 0 && cur_castle < g_scenario.num_castles) {
        show_cmd_menu = (g_scenario.castles[cur_castle].faction_id == player_faction);
    }
    printf("[+] Turn state: faction %d, %d/%d\n",
           g_turn.player_faction, g_turn.year, g_turn.month);

    // Initial officer decode
    ImageDecoded cur_kao = {0};
    refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);

    // Portrait embedded in domestic-affairs report dialogues.
    ImageDecoded dlg_kao = {0};

    // --preview-npk: decode one NPK016 block and display it standalone.
    if (preview_npk_name) {
        if (npk_load(&preview_pack, s_asset_dir, preview_npk_name) &&
            npk_decode(&preview_pack, preview_npk_index, &preview_img)) {
            preview_mode = true;
            printf("[+] Preview %s block %d/%d: %dx%d\n",
                   preview_npk_name, preview_npk_index, preview_pack.count,
                   preview_img.width, preview_img.height);
        } else {
            fprintf(stderr, "[ERROR] --preview-npk failed: %s block %d\n",
                    preview_npk_name, preview_npk_index);
        }
    }

    bool running = true;
    bool use_mask = true;
    int bg_mode = 1;
    bool map_mode = start_map_mode;
    
    // Set initial camera to center on selected castle
    int camera_x = g_scenario.castles[cur_castle].map_x - 320;
    if (camera_x < 0) camera_x = 0;
    if (camera_x > map_w - PC98_WIDTH) camera_x = map_w - PC98_WIDTH;

    int frame_count = 0;

    // --- Headless domestic-command execution (--do-cmd / --end-turn) --------
    // Runs before the first frame so --screenshot captures the resulting state.
    if (do_cmd != TURN_CMD_NONE || end_turns > 0) {
        show_cmd_menu = false;

        if (do_cmd == TURN_CMD_OFFICERS) {
            roster_count = turn_list_castle_officers(&g_scenario, player_faction,
                                                     cur_castle, roster_ids, MAX_OFFICERS);
            roster_scroll = 0;
            show_officer_list = true;
            printf("[TURN] roster castle=%d officers=%d\n", cur_castle, roster_count);
        } else if (do_cmd != TURN_CMD_NONE) {
            const ScenarioCastle *before = &g_scenario.castles[cur_castle];
            int rice0 = before->rice, gold0 = before->gold, tr0 = before->troops;
            int df0 = before->defense, mo0 = before->morale;

            TurnResult r;
            turn_run_command(&g_turn, &g_scenario, cur_castle, do_cmd, &r);

            const ScenarioCastle *after = &g_scenario.castles[cur_castle];
            printf("[TURN] %s castle=%d officer=%d ok=%d\n",
                   turn_cmd_ascii(do_cmd), cur_castle, r.officer_id, r.ok ? 1 : 0);
            printf("[TURN]   BEFORE rice=%d gold=%d troops=%d def=%d morale=%d\n",
                   rice0, gold0, tr0, df0, mo0);
            printf("[TURN]   AFTER  rice=%d gold=%d troops=%d def=%d morale=%d\n",
                   after->rice, after->gold, after->troops, after->defense, after->morale);
            printf("[TURN]   DATE   %d/%d\n", g_turn.year, g_turn.month);

            show_dialog = true;
            dlg_custom = true;
            dlg_officer = r.officer_id;
            snprintf(dlg_custom_text, sizeof(dlg_custom_text), "%s", r.text);
            if (r.officer_id >= 0) {
                decode_officer_portrait(r.officer_id, &g_scenario, kao_data, kao_size,
                                        officer_offsets, &dlg_kao);
            }
        }

        for (int t = 0; t < end_turns; t++) {
            TurnResult r;
            turn_end(&g_turn, &g_scenario, &r);
            printf("[TURN] END -> %d/%d  player_gold=%+d player_rice=%+d\n",
                   g_turn.year, g_turn.month, r.d_gold, r.d_rice);
            if (t == end_turns - 1) {
                show_dialog = true;
                dlg_custom = true;
                dlg_officer = -1;
                snprintf(dlg_custom_text, sizeof(dlg_custom_text), "%s", r.text);
            }
        }
    }

    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);

    InputState in;
    input_init();

    // Strategic-map camera interaction: auto-scroll when the pointer hugs a
    // screen edge, and drag-panning when it does not start on a castle icon.
    const int EDGE_SCROLL_MARGIN = 32;
    const int EDGE_SCROLL_STEP = 14;
    bool dragging = false;

    while (running) {
        input_poll(&in);

        // --- Headless mouse injection (--click x,y[@frame]) ---
        for (int ci = 0; ci < click_count; ci++) {
            if (clicks[ci].frame == frame_count) {
                in.mouse_x = clicks[ci].x;
                in.mouse_y = clicks[ci].y;
                in.mouse_dx = 0;
                in.mouse_dy = 0;
                in.mouse_inside = true;
                in.lbutton_pressed = true;
                in.lbutton_down = true;
                in.synthetic = true;
                printf("[CLICK] frame=%d at (%d,%d)\n",
                       frame_count, in.mouse_x, in.mouse_y);
            }
        }

        if (in.quit) running = false;

        const YphzKey key = in.key;
        if (key == YPHZ_KEY_ESCAPE) {
            // ESC walks back up the UI stack: NPK gallery -> dialogue ->
            // officer roster -> command menu -> castle card -> quit.
            if (gallery_mode) {
                gallery_mode = false;
            } else if (show_dialog) {
                show_dialog = false;
            } else if (show_officer_list) {
                show_officer_list = false;
                roster_select_mode = false;
                roster_cmd = TURN_CMD_NONE;
                show_cmd_menu = true;
            } else if (map_mode && show_cmd_menu &&
                       g_scenario.castles[cur_castle].faction_id == g_turn.player_faction) {
                show_cmd_menu = false;
            } else if (map_mode && show_castle_card) {
                show_castle_card = false;
            } else {
                running = false;
            }
        }

        // Right-click walks back one level, mirroring ESC but never quits.
        if (in.rbutton_pressed) {
            if (show_dialog) {
                show_dialog = false;
            } else if (show_officer_list) {
                show_officer_list = false;
                roster_select_mode = false;
                roster_cmd = TURN_CMD_NONE;
                show_cmd_menu = true;
            } else if (map_mode && show_cmd_menu &&
                       g_scenario.castles[cur_castle].faction_id == g_turn.player_faction) {
                show_cmd_menu = false;
            } else if (map_mode && show_castle_card) {
                show_castle_card = false;
            }
        }

        // --- NPK016 gallery (L) / item viewer (I) ---
        if (gallery_mode) {
            if (key == YPHZ_KEY_LEFT) {
                gallery_block--;
            } else if (key == YPHZ_KEY_RIGHT) {
                gallery_block++;
            } else if (key == YPHZ_KEY_UP) {
                gallery_pack = (gallery_pack - 1 + GALLERY_PACK_COUNT) % GALLERY_PACK_COUNT;
                gallery_block = 0;
                gallery_loaded_pack = -1;
            } else if (key == YPHZ_KEY_DOWN) {
                gallery_pack = (gallery_pack + 1) % GALLERY_PACK_COUNT;
                gallery_block = 0;
                gallery_loaded_pack = -1;
            }

            if (gallery_update(gallery_pack, &gallery_block, &gallery_npk,
                               &gallery_img, &gallery_loaded_pack,
                               &gallery_loaded_block)) {
                gallery_block = gallery_loaded_block;
            }
        } else if (key == YPHZ_KEY_L) {
            gallery_mode = true;
            gallery_loaded_pack = -1;
        } else if (key == YPHZ_KEY_I) {
            gallery_mode = true;
            gallery_pack = 2;   // Mainitem.gp: 19 x 64x64 heirloom icons
            gallery_block = 0;
            gallery_loaded_pack = -1;
        }

        // Mode Toggle: M
        if (!gallery_mode && key == YPHZ_KEY_M) {
            map_mode = !map_mode;
            dragging = false;
            update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
        }

        if (gallery_mode) {
            // NPK gallery is modal: map/officer input is suspended.
        } else if (map_mode) {
            const bool player_castle =
                (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
            const bool modal_open = show_dialog || show_officer_list ||
                                    (show_cmd_menu && player_castle);

            if (show_officer_list) {
                // ---- Officer roster (武将一覧) ----
                int max_scroll = roster_count - ROSTER_ROWS;
                if (max_scroll < 0) max_scroll = 0;

                if (key == YPHZ_KEY_UP) {
                    if (roster_scroll > 0) roster_scroll--;
                } else if (key == YPHZ_KEY_DOWN) {
                    if (roster_scroll < max_scroll) roster_scroll++;
                } else if (in.lbutton_pressed && in.mouse_inside) {
                    int rows_top = ROSTER_Y + ROSTER_ROWS_TOP;
                    int row = (in.mouse_y - rows_top) / ROSTER_ROW_H;
                    if (in.mouse_x >= ROSTER_X && in.mouse_x < ROSTER_X + ROSTER_W &&
                        in.mouse_y >= rows_top &&
                        in.mouse_y < rows_top + ROSTER_ROWS * ROSTER_ROW_H &&
                        row >= 0 && row < ROSTER_ROWS) {
                        int idx = roster_scroll + row;
                        if (idx >= 0 && idx < roster_count) {
                            int oid = roster_ids[idx];
                            if (roster_select_mode) {
                                // Choose the officer who will execute the
                                // pending domestic command.
                                if (!turn_officer_has_acted(&g_turn, oid)) {
                                    TurnResult r;
                                    turn_run_command_officer(&g_turn, &g_scenario,
                                                             cur_castle, roster_cmd,
                                                             oid, &r);
                                    printf("[TURN] %s castle=%d officer=%d ok=%d "
                                           "gold=%+d rice=%+d troops=%+d morale=%+d def=%+d\n",
                                           turn_cmd_ascii(roster_cmd), cur_castle,
                                           r.officer_id, r.ok ? 1 : 0,
                                           r.d_gold, r.d_rice, r.d_troops,
                                           r.d_morale, r.d_defense);
                                    show_turn_result(&r, &show_dialog, &dlg_custom,
                                                     &dlg_officer, dlg_custom_text,
                                                     sizeof(dlg_custom_text), &dlg_kao,
                                                     &g_scenario, kao_data, kao_size,
                                                     officer_offsets);
                                    show_officer_list = false;
                                    roster_select_mode = false;
                                    roster_cmd = TURN_CMD_NONE;
                                    update_window_title(cur_officer, use_mask, bg_mode,
                                                        map_mode, camera_x, cur_scenario,
                                                        &g_scenario, cur_castle);
                                }
                            } else {
                                const ScenarioOfficer *o = &g_scenario.officers[oid];
                                dlg_custom = true;
                                dlg_officer = oid;
                                snprintf(dlg_custom_text, sizeof(dlg_custom_text),
                                         "%s\n"
                                         "\x93\x9d\x97\xa6 %d   \x95\x90\x97\xcd %d   \x92\x6d\x96\x64 %d\n"
                                         "\x90\xad\x8e\xa1 %d   \x92\x89\x90\xbd %d   \x95\xba\x97\xcd %d",
                                         o->full_name_sjis,
                                         o->lead, o->war, o->intel,
                                         o->politics, o->loyalty, o->troops);
                                decode_officer_portrait(oid, &g_scenario, kao_data, kao_size,
                                                        officer_offsets, &dlg_kao);
                                show_dialog = true;
                                show_officer_list = false;
                            }
                        }
                    }
                }
            } else if (show_dialog) {
                // Dialogue dismissal is handled centrally by ESC / right-click.
            } else if (show_cmd_menu && player_castle) {
                // ---- Castle command menu (指令) ----
                int sel = -1;
                if (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_5) {
                    sel = key - YPHZ_KEY_1;
                }
                if (in.lbutton_pressed && in.mouse_inside) {
                    int h = cmd_menu_hit(in.mouse_x, in.mouse_y);
                    if (h >= 0) sel = h;
                }

                if (sel >= 0) {
                    TurnCommand cmd = (TurnCommand)(TURN_CMD_DEVELOP + sel);
                    if (cmd == TURN_CMD_OFFICERS) {
                        // Roster view-only (no action consumed).
                        roster_count = turn_list_castle_officers(
                            &g_scenario, player_faction, cur_castle,
                            roster_ids, MAX_OFFICERS);
                        roster_scroll = 0;
                        roster_select_mode = false;
                        roster_cmd = TURN_CMD_NONE;
                        show_officer_list = true;
                        show_cmd_menu = false;
                    } else if (cmd == TURN_CMD_END) {
                        TurnResult r;
                        turn_run_command(&g_turn, &g_scenario, cur_castle, cmd, &r);
                        printf("[TURN] %s castle=%d ok=%d gold=%+d rice=%+d\n",
                               turn_cmd_ascii(cmd), cur_castle, r.ok ? 1 : 0,
                               r.d_gold, r.d_rice);
                        show_turn_result(&r, &show_dialog, &dlg_custom, &dlg_officer,
                                         dlg_custom_text, sizeof(dlg_custom_text),
                                         &dlg_kao, &g_scenario, kao_data, kao_size,
                                         officer_offsets);
                        show_cmd_menu = false;
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode,
                                            camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else {
                        // DEVELOP / RECRUIT / TAX: pick the acting officer first.
                        roster_count = turn_list_castle_officers(
                            &g_scenario, player_faction, cur_castle,
                            roster_ids, MAX_OFFICERS);
                        roster_scroll = 0;
                        roster_select_mode = true;
                        roster_cmd = cmd;
                        show_officer_list = true;
                        show_cmd_menu = false;
                    }
                }
            } else {
                // Scenario hot-switching: 1, 2, 3, 4
                if (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_4) {
                    cur_scenario = key - YPHZ_KEY_1;
                    scenario_load(snd_path, cur_scenario, &g_scenario);
                    turn_init(&g_turn, &g_scenario, player_faction);
                    refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                    show_cmd_menu = show_castle_card &&
                        (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                }
                // Castle selection: [ and ]
                else if (key == YPHZ_KEY_LEFTBRACKET) {
                    cur_castle = (cur_castle - 1 + MAX_CASTLES) % MAX_CASTLES;
                    camera_x = clamp_camera_x(g_scenario.castles[cur_castle].map_x - 320, map_w);
                    show_cmd_menu = show_castle_card &&
                        (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                } else if (key == YPHZ_KEY_RIGHTBRACKET) {
                    cur_castle = (cur_castle + 1) % MAX_CASTLES;
                    camera_x = clamp_camera_x(g_scenario.castles[cur_castle].map_x - 320, map_w);
                    show_cmd_menu = show_castle_card &&
                        (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                }
                // Inspect Castle Lord in Officer Mode: O or ENTER
                else if (key == YPHZ_KEY_O || key == YPHZ_KEY_ENTER) {
                    ScenarioCastle *sel = &g_scenario.castles[cur_castle];
                    if (sel->lord_officer_id >= 0 && sel->lord_officer_id < g_scenario.num_officers) {
                        cur_officer = sel->lord_officer_id;
                        map_mode = false;
                        show_cmd_menu = false;
                        show_officer_list = false;
                        refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    }
                }
                // Toggle Castle Details Card: C or SPACE
                else if (key == YPHZ_KEY_C || key == YPHZ_KEY_SPACE) {
                    show_castle_card = !show_castle_card;
                    show_officer_list = false;
                    show_cmd_menu = show_castle_card &&
                        (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                }
                // Jump to major strategic headquarters: TAB
                else if (key == YPHZ_KEY_TAB) {
                    static const int major_castles[] = {0, 6, 16, 17, 25, 30, 38};
                    static int m_idx = 0;
                    m_idx = (m_idx + 1) % 7;
                    cur_castle = major_castles[m_idx];
                    camera_x = clamp_camera_x(g_scenario.castles[cur_castle].map_x - 320, map_w);
                    show_cmd_menu = show_castle_card &&
                        (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                }
                // Message.gp dialogue box: T opens it, further presses advance
                else if (key == YPHZ_KEY_T) {
                    int n = message_string_count(&g_messages, msg_entry);
                    if (!show_dialog) {
                        show_dialog = true;
                    } else if (n > 0) {
                        cur_msg = (cur_msg + 1) % n;
                    }
                }
                // Smooth manual camera scroll (keyboard)
                else if (key == YPHZ_KEY_LEFT || key == YPHZ_KEY_A) {
                    camera_x = clamp_camera_x(camera_x - 40, map_w);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                } else if (key == YPHZ_KEY_RIGHT || key == YPHZ_KEY_D) {
                    camera_x = clamp_camera_x(camera_x + 40, map_w);
                    update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                }
            }

            // Map mouse interaction is disabled while a modal panel is up.
            if (!modal_open) {
                // --- Mouse: edge auto-scroll ---
                if (in.mouse_inside && !in.synthetic) {
                    if (in.mouse_x < EDGE_SCROLL_MARGIN) {
                        camera_x = clamp_camera_x(camera_x - EDGE_SCROLL_STEP, map_w);
                    } else if (in.mouse_x >= PC98_WIDTH - EDGE_SCROLL_MARGIN) {
                        camera_x = clamp_camera_x(camera_x + EDGE_SCROLL_STEP, map_w);
                    }
                }

                // --- Mouse: click a castle to select it; empty space starts a drag ---
                if (in.lbutton_pressed) {
                    int hit = in.mouse_inside
                                  ? map_castle_hit_test(&g_scenario, camera_x, in.mouse_x, in.mouse_y)
                                  : -1;
                    if (hit >= 0) {
                        cur_castle = hit;
                        show_castle_card = true;
                        show_officer_list = false;
                        show_cmd_menu =
                            (g_scenario.castles[cur_castle].faction_id == g_turn.player_faction);
                        update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
                    } else {
                        dragging = true;
                    }
                }
                if (!in.lbutton_down) {
                    dragging = false;
                } else if (dragging) {
                    // Drag pans the panorama: the terrain follows the pointer.
                    camera_x = clamp_camera_x(camera_x - in.mouse_dx, map_w);
                }
            }
        } else {
            // Officer Showcase Controls: Full 401 Officer Database
            if (key == YPHZ_KEY_RIGHT) {
                cur_officer = (cur_officer + 1) % g_scenario.num_officers;
                refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_LEFT) {
                cur_officer = (cur_officer - 1 + g_scenario.num_officers) % g_scenario.num_officers;
                refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_DOWN) {
                cur_officer = (cur_officer + 10) % g_scenario.num_officers;
                refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_UP) {
                cur_officer = (cur_officer - 10 + g_scenario.num_officers) % g_scenario.num_officers;
                refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_F) {
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
            } else if (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_4) {
                cur_scenario = key - YPHZ_KEY_1;
                scenario_load(snd_path, cur_scenario, &g_scenario);
                refresh_officer_portrait(cur_officer, &g_scenario, kao_data, kao_size, officer_offsets, &cur_kao);
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_SPACE) {
                use_mask = !use_mask;
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            } else if (key == YPHZ_KEY_TAB) {
                bg_mode = (bg_mode + 1) % 3;
                update_window_title(cur_officer, use_mask, bg_mode, map_mode, camera_x, cur_scenario, &g_scenario, cur_castle);
            }
        }

        // --- Render Frame ---
        hal_video_clear(0);

        if (preview_mode) {
            // Standalone NPK016 block viewer (--preview-npk).
            draw_centered_image(&preview_img);
            char cap[128];
            char npk_name[64];
            snprintf(npk_name, sizeof(npk_name), "%s",
                     preview_npk_name ? preview_npk_name : "?");
            ascii_upper(npk_name);
            snprintf(cap, sizeof(cap), "NPK %s  BLOCK %d/%d  %dx%d",
                     npk_name, preview_npk_index, preview_pack.count,
                     preview_img.width, preview_img.height);
            hal_video_fill_rect(0, PC98_HEIGHT - 20, PC98_WIDTH, 20, 1);
            hal_video_draw_rect(0, PC98_HEIGHT - 20, PC98_WIDTH, 20, 7);
            hal_video_draw_text_sjis(16, PC98_HEIGHT - 18, cap, 7);
        } else if (gallery_mode) {
            // In-game NPK gallery (L) / heirloom icons (I).
            draw_centered_image(&gallery_img);
            draw_gallery_overlay(gallery_pack, gallery_block, gallery_npk.count,
                                 gallery_img.width, gallery_img.height);
        } else if (map_mode && japan_map_pixels) {
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
            // Current calendar (kept in sync by turn_end()).
            char date_str[64];
            snprintf(date_str, sizeof(date_str), "%d\x94\x4e%d\x8c\x8e",
                     g_turn.year, g_turn.month);
            hal_video_draw_text_sjis(320, 2, date_str, 6);
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
                bool card_player = (sel->faction_id == g_turn.player_faction);
                int cx = card_player ? 52 : 70;
                int cy = 40;
                int cw = card_player ? 436 : 500;
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
                if (card_player) {
                    hal_video_draw_text_sjis(cx + 16, cy + 276,
                        "\x81\x6d\x31\x2d\x35\x81\x46\x8e\x77\x97\xdf\x20\x20\x45\x53\x43\x81\x46\x96\xdf\x82\xe9\x81\x6e",
                        6); // ［1-5:指令  ESC:戻る］
                } else {
                    hal_video_draw_text_sjis(cx + 16, cy + 276,
                        "\x81\x6d\x88\xc2\x20\x43\x20\x8c\xae\x95\xc2\x20\x20\x20\x81\x6d\x20\x81\x6e\x90\xd8\x91\xd6\x8f\xe9\x92\x72\x20\x20\x20\x31\x2d\x34\x90\xd8\x91\xd6\x8c\x80\x96\x7b\x81\x6e",
                        6); // ［按 C 鍵閉   ［ ］切替城池   1-4切替劇本］
                }

                // Command menu beside the card (player-owned castles only).
                if (card_player && show_cmd_menu) {
                    int hover = in.mouse_inside
                                    ? cmd_menu_hit(in.mouse_x, in.mouse_y) : -1;
                    draw_command_menu(hover);
                }
            }

            // Officer roster modal overlays the card.
            if (show_officer_list) {
                draw_officer_roster(&g_scenario, &g_turn, roster_ids,
                                    roster_count, roster_scroll,
                                    roster_select_mode ? turn_command_name(roster_cmd)
                                                       : SJIS_ROSTER);
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

        // Dialogue box overlays both modes (drawn last so it sits on top).
        if (show_dialog) {
            char tag[64];
            const char *text = NULL;
            const ImageDecoded *portrait = NULL;

            if (dlg_custom) {
                text = dlg_custom_text;
                snprintf(tag, sizeof(tag), "\x81\x79\x8c\x8b\x89\xca\x81\x7a"); // 【結果】
                if (dlg_officer >= 0 && dlg_kao.pixels) {
                    portrait = &dlg_kao;
                }
            } else if (g_messages.loaded) {
                char dlg[1024];
                message_get_text(&g_messages, msg_entry, cur_msg, dlg, sizeof(dlg));
                text = dlg;
                snprintf(tag, sizeof(tag),
                         "\x81\x79\x89\xef\x98\x62\x81\x7a[%d:%04d]", msg_entry, cur_msg);
            }

            if (text) {
                draw_dialog_box(text, tag, portrait);
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
    free_decoded_image(&dlg_kao);
    free_decoded_image(&preview_img);
    npk_free(&preview_pack);
    free_decoded_image(&gallery_img);
    npk_free(&gallery_npk);
    if (bg_hik.pixels) free_decoded_image(&bg_hik);
    if (bg_palace.pixels) free_decoded_image(&bg_palace);
    if (japan_map_pixels) free(japan_map_pixels);
    if (pld_data) free(pld_data);
    if (jis_data) free(jis_data);
    if (chr_data) free(chr_data);
    if (exe_data) free(exe_data);
    if (kao_data) free(kao_data);
    if (map_file_data) free(map_file_data);
    message_free(&g_messages);

    hal_video_shutdown();
    printf("[+] Engine cleanly stopped.\n");
    return 0;
}

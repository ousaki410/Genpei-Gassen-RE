#include "opening.h"

#include <stdio.h>
#include <string.h>

#include "decompress.h"
#include "hal_video.h"

// --- SJIS labels ------------------------------------------------------------
#define SJIS_TITLE      "\x8c\xb9\x95\xbd\x8d\x87\x90\xed"   // 源平合戦
#define SJIS_SCENARIO   "\x83\x56\x83\x69\x83\x8a\x83\x49"   // シナリオ
#define SJIS_SELECT     "\x91\x49\x91\xf0"                   // 選択
#define SJIS_DAIMYO     "\x91\xe5\x96\xbc"                   // 大名
#define SJIS_LORD       "\x93\x96\x8e\xe5"                   // 当主

// --- Layout -----------------------------------------------------------------
#define BG_COLOR        1
#define FRAME_COLOR     6
#define TEXT_COLOR      7
#define DIM_COLOR       4

// title
#define TITLE_HINT_Y    384

// scenario select
#define SC_ROW_X        40
#define SC_ROW_W        560
#define SC_ROW_Y        88
#define SC_ROW_H        52
#define SC_ROW_STEP     56
#define SC_HINT_Y       374

// daimyo select
#define DM_LIST_X       16
#define DM_LIST_W       392
#define DM_ROW_Y        64
#define DM_ROW_H        19
#define DM_ROW_STEP     20
#define DM_KAO_X        430
#define DM_KAO_Y        80
#define DM_KAO_W        128
#define DM_KAO_H        160
#define DM_HINT_Y       380

static void blit_image(const ImageDecoded *img, int x, int y) {
    if (!img || !img->pixels) return;
    hal_video_blit_indexed(x, y, img->width, img->height, img->pixels,
                           img->width);
}

// Draw a SJIS string with a fixed advance per full-width glyph (used to space
// out the 16x16 game title).
static void draw_spaced_sjis(const char *sjis, int x, int y, int advance,
                             uint8_t color) {
    if (!sjis) return;
    for (size_t i = 0; sjis[i]; ) {
        unsigned char c = (unsigned char)sjis[i];
        int n = ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) ? 2 : 1;
        char buf[3];
        if (n == 2 && sjis[i + 1]) {
            buf[0] = sjis[i];
            buf[1] = sjis[i + 1];
            buf[2] = 0;
        } else {
            n = 1;
            buf[0] = sjis[i];
            buf[1] = 0;
        }
        hal_video_draw_text_sjis(x, y, buf, color);
        x += (n == 2) ? advance : (advance + 1) / 2;
        i += (size_t)n;
    }
}

static void draw_header(const char *sjis_left, const char *sjis_right) {
    hal_video_fill_rect(0, 0, PC98_WIDTH, 30, 0);
    hal_video_draw_rect(0, 0, PC98_WIDTH, 30, FRAME_COLOR);
    if (sjis_left) hal_video_draw_text_sjis(16, 6, sjis_left, FRAME_COLOR);
    if (sjis_right) {
        int w = 0;
        for (const unsigned char *p = (const unsigned char *)sjis_right; *p; ) {
            int n = ((*p >= 0x81 && *p <= 0x9f) || (*p >= 0xe0 && *p <= 0xfc)) ? 2 : 1;
            w += (n == 2) ? 16 : 8;
            p += n;
        }
        int x = PC98_WIDTH - 16 - w;
        if (x > 300) hal_video_draw_text_sjis(x, 6, sjis_right, DIM_COLOR);
    }
}

static int sjis_width(const char *s) {
    int w = 0;
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        int n = ((*p >= 0x81 && *p <= 0x9f) || (*p >= 0xe0 && *p <= 0xfc)) ? 2 : 1;
        w += (n == 2) ? 16 : 8;
        p += n;
    }
    return w;
}

static void draw_centered_text(int y, const char *s, uint8_t color) {
    if (!s) return;
    int x = (PC98_WIDTH - sjis_width(s)) / 2;
    if (x < 0) x = 0;
    hal_video_draw_text_sjis(x, y, s, color);
}

// --- Scenario / faction tables ---------------------------------------------

static bool faction_owns_castle(const Scenario *sc, int faction) {
    for (int c = 0; c < sc->num_castles; c++) {
        if (sc->castles[c].faction_id == faction) return true;
    }
    return false;
}

static void build_factions(OpeningState *op, const Scenario *sc) {
    op->fac_count = 0;
    for (int f = 0; f < 15 && op->fac_count < OPENING_MAX_FACTIONS; f++) {
        if (!faction_owns_castle(sc, f)) continue;
        op->factions[op->fac_count] = f;
        op->leaders[op->fac_count] = scenario_faction_leader(sc, f);
        op->fac_count++;
    }
}

// --- Title screen -----------------------------------------------------------

static void render_title(const OpeningState *op) {
    hal_video_clear(0);

    if (op->logo_ready) {
        blit_image(&op->logo_img[0], (PC98_WIDTH - 208) / 2, 16);
        blit_image(&op->logo_img[2], (PC98_WIDTH - 368) / 2, 136);
        blit_image(&op->logo_img[1], (PC98_WIDTH - 208) / 2, 216);
        blit_image(&op->logo_img[3], (PC98_WIDTH - 400) / 2, 242);
        blit_image(&op->logo_img[4], (PC98_WIDTH - 272) / 2, 268);
    }

    // 源平合戦 — spaced 16x16 glyphs (drawn twice for a bolder look), with a
    // gold rule underneath.
    int tx = (PC98_WIDTH - 4 * 32) / 2;
    draw_spaced_sjis(SJIS_TITLE, tx, 188, 32, FRAME_COLOR);
    draw_spaced_sjis(SJIS_TITLE, tx + 1, 188, 32, FRAME_COLOR);
    hal_video_fill_rect(tx, 206, 4 * 32 - 32 + 16, 2, FRAME_COLOR);

    if ((op->anim / 30) % 2 == 0) {
        draw_centered_text(TITLE_HINT_Y, "CLICK OR PRESS ANY KEY",
                           TEXT_COLOR);
    }
}

// --- Scenario select --------------------------------------------------------

static void render_scenario(const OpeningState *op) {
    hal_video_clear(BG_COLOR);
    char header[128];
    snprintf(header, sizeof(header), "%.64s %.64s", SJIS_SCENARIO, SJIS_SELECT);
    draw_header(header, NULL);

    for (int i = 0; i < MAX_SCENARIOS; i++) {
        int y = SC_ROW_Y + i * SC_ROW_STEP;
        bool sel = (i == op->sel);
        hal_video_fill_rect(SC_ROW_X, y, SC_ROW_W, SC_ROW_H - 4,
                            sel ? 0 : BG_COLOR);
        hal_video_draw_rect(SC_ROW_X, y, SC_ROW_W, SC_ROW_H - 4,
                            sel ? FRAME_COLOR : DIM_COLOR);

        char date[64];
        snprintf(date, sizeof(date), "%d/%d", scenario_get_year(i),
                 scenario_get_month(i));
        hal_video_draw_text_sjis(SC_ROW_X + 16, y + 8, date,
                                 TEXT_COLOR);
        hal_video_draw_text_sjis(SC_ROW_X + 110, y + 8,
                                 scenario_get_title(i),
                                 TEXT_COLOR);
    }

    draw_centered_text(SC_HINT_Y,
                       "1-4: SELECT   UP/DOWN: MOVE   ENTER: OK   ESC: BACK",
                       TEXT_COLOR);
}

// --- Daimyo select ----------------------------------------------------------

static void ensure_leader_portrait(OpeningState *op, const Scenario *sc,
                                   const uint8_t *kao_data, size_t kao_size,
                                   const uint32_t *offsets) {
    int leader = (op->sel >= 0 && op->sel < op->fac_count)
                     ? op->leaders[op->sel]
                     : -1;
    if (leader == op->leader_kao_id && op->leader_kao.pixels) return;

    free_decoded_image(&op->leader_kao);
    op->leader_kao_id = -1;
    if (!kao_data || !offsets || !sc || leader < 0 ||
        leader >= sc->num_officers) {
        return;
    }
    int fid = sc->officers[leader].face_id;
    int use_fid = (fid >= 0 && fid < 90) ? fid : (leader % 15) + 40;
    if (kaodata_decompress_officer(kao_data, kao_size, offsets[use_fid],
                                   &op->leader_kao)) {
        op->leader_kao_id = leader;
    }
}

static void render_daimyo(OpeningState *op, const Scenario *sc,
                          const uint8_t *kao_data, size_t kao_size,
                          const uint32_t *offsets) {
    hal_video_clear(BG_COLOR);

    char header[128];
    snprintf(header, sizeof(header), "%.64s %.64s", SJIS_DAIMYO, SJIS_SELECT);
    draw_header(header, scenario_get_title(op->scenario));
    char date[64];
    snprintf(date, sizeof(date), "%d/%d", scenario_get_year(op->scenario),
             scenario_get_month(op->scenario));
    hal_video_draw_text_sjis(16, 34, date, DIM_COLOR);

    for (int i = 0; i < op->fac_count; i++) {
        int y = DM_ROW_Y + i * DM_ROW_STEP;
        bool sel = (i == op->sel);
        hal_video_fill_rect(DM_LIST_X, y, DM_LIST_W, DM_ROW_H - 2,
                            sel ? 0 : BG_COLOR);
        hal_video_draw_rect(DM_LIST_X, y, DM_LIST_W, DM_ROW_H - 2,
                            sel ? FRAME_COLOR : DIM_COLOR);

        const FactionDef *fd = scenario_get_faction(op->factions[i]);
        if (fd) {
            hal_video_fill_rect(DM_LIST_X + 6, y + 4, 12, DM_ROW_H - 10,
                                fd->flag_color);
            hal_video_draw_text_sjis(DM_LIST_X + 26, y + 2, fd->name_sjis,
                                     TEXT_COLOR);
        }
        int leader = op->leaders[i];
        if (leader >= 0 && leader < sc->num_officers) {
            hal_video_draw_text_sjis(DM_LIST_X + 230, y + 2,
                                     sc->officers[leader].full_name_sjis,
                                     TEXT_COLOR);
        }
    }

    ensure_leader_portrait(op, sc, kao_data, kao_size, offsets);

    hal_video_draw_rect(DM_KAO_X - 6, DM_KAO_Y - 6, DM_KAO_W + 12,
                        DM_KAO_H + 12, FRAME_COLOR);
    if (op->leader_kao.pixels) {
        blit_image(&op->leader_kao, DM_KAO_X, DM_KAO_Y);
    } else {
        hal_video_fill_rect(DM_KAO_X, DM_KAO_Y, DM_KAO_W, DM_KAO_H, 0);
    }

    int leader = (op->sel >= 0 && op->sel < op->fac_count)
                     ? op->leaders[op->sel]
                     : -1;
    if (leader >= 0 && leader < sc->num_officers) {
        hal_video_draw_text_sjis(DM_KAO_X, DM_KAO_Y + DM_KAO_H + 10,
                                 sc->officers[leader].full_name_sjis,
                                 TEXT_COLOR);
    }
    hal_video_draw_text_sjis(DM_KAO_X, DM_KAO_Y + DM_KAO_H + 30, SJIS_LORD,
                             FRAME_COLOR);

    draw_centered_text(DM_HINT_Y,
                       "UP/DOWN: SELECT   ENTER: START   ESC: BACK",
                       TEXT_COLOR);
}

// --- Public API -------------------------------------------------------------

void opening_begin(OpeningState *op, const char *asset_dir) {
    if (!op) return;
    memset(op, 0, sizeof(*op));
    op->screen = OPENING_TITLE;
    op->scenario = 0;
    op->sel = 0;
    op->leader_kao_id = -1;
    op->leader_kao.pixels = NULL;

    if (!asset_dir) return;
    if (!npk_load(&op->logo, asset_dir, "Logo.gp")) return;
    for (int i = 0; i < 5 && i < op->logo.count; i++) {
        npk_decode(&op->logo, i, &op->logo_img[i]);
    }
    op->logo_ready = true;
    printf("[+] Opening: Logo.gp %d blocks loaded\n", op->logo.count);
}

bool opening_active(const OpeningState *op) {
    return op && op->screen != OPENING_NONE;
}

bool opening_step(OpeningState *op, Scenario *sc, TurnState *ts,
                  const InputState *in, YphzKey key, const char *sndata_path) {
    if (!op || !opening_active(op)) return false;
    op->anim++;

    bool click = in && in->lbutton_pressed && in->mouse_inside;
    bool any_button = in && (in->lbutton_pressed || in->rbutton_pressed);
    bool confirm_scenario = false;

    switch (op->screen) {
    case OPENING_TITLE:
        if (key != YPHZ_KEY_NONE || any_button) {
            op->screen = OPENING_SCENARIO;
            op->sel = op->scenario;
        }
        break;

    case OPENING_SCENARIO:
        if (key == YPHZ_KEY_ESCAPE) {
            op->screen = OPENING_TITLE;
            break;
        }
        if (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_4) {
            op->sel = key - YPHZ_KEY_1;
            confirm_scenario = true;
        } else if (key == YPHZ_KEY_UP) {
            if (op->sel > 0) op->sel--;
        } else if (key == YPHZ_KEY_DOWN) {
            if (op->sel + 1 < MAX_SCENARIOS) op->sel++;
        } else if (key == YPHZ_KEY_ENTER) {
            confirm_scenario = true;
        }
        if (click) {
            int row = (in->mouse_y - SC_ROW_Y) / SC_ROW_STEP;
            if (in->mouse_x >= SC_ROW_X &&
                in->mouse_x < SC_ROW_X + SC_ROW_W &&
                row >= 0 && row < MAX_SCENARIOS) {
                op->sel = row;
                confirm_scenario = true;
            }
        }
        if (confirm_scenario) {
            op->scenario = op->sel;
            if (!scenario_load(sndata_path, op->scenario, sc)) {
                fprintf(stderr, "[ERROR] opening: cannot load scenario %d\n",
                        op->scenario);
                break;
            }
            // Provisional turn state so the daimyo list and the subsequent map
            // both see a consistent Scenario.
            turn_init(ts, sc, 0);
            build_factions(op, sc);
            op->screen = OPENING_DAIMYO;
            op->sel = 0;
            free_decoded_image(&op->leader_kao);
            op->leader_kao_id = -1;
        }
        break;

    case OPENING_DAIMYO: {
        if (key == YPHZ_KEY_ESCAPE) {
            op->screen = OPENING_SCENARIO;
            free_decoded_image(&op->leader_kao);
            op->leader_kao_id = -1;
            break;
        }
        if (key == YPHZ_KEY_UP) {
            if (op->sel > 0) op->sel--;
        } else if (key == YPHZ_KEY_DOWN) {
            if (op->sel + 1 < op->fac_count) op->sel++;
        } else if (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_8) {
            int row = key - YPHZ_KEY_1;
            if (row < op->fac_count) op->sel = row;
        }
        bool confirm = (key == YPHZ_KEY_ENTER);
        if (click) {
            int row = (in->mouse_y - DM_ROW_Y) / DM_ROW_STEP;
            if (in->mouse_x >= DM_LIST_X &&
                in->mouse_x < DM_LIST_X + DM_LIST_W &&
                row >= 0 && row < op->fac_count) {
                op->sel = row;
                confirm = true;
            }
        }
        if (confirm && op->sel >= 0 && op->sel < op->fac_count) {
            int faction = op->factions[op->sel];
            turn_init(ts, sc, faction);
            op->screen = OPENING_NONE;
            printf("[OPENING] scenario=%d faction=%d leader=%d\n",
                   op->scenario, faction, op->leaders[op->sel]);
            return true;
        }
        break;
    }

    default:
        break;
    }

    return false;
}

void opening_render(OpeningState *op, const Scenario *sc,
                    const uint8_t *kao_data, size_t kao_size,
                    const uint32_t *offsets) {
    if (!op) return;
    switch (op->screen) {
    case OPENING_TITLE:
        render_title(op);
        break;
    case OPENING_SCENARIO:
        render_scenario(op);
        break;
    case OPENING_DAIMYO:
        if (sc) render_daimyo(op, sc, kao_data, kao_size, offsets);
        break;
    default:
        break;
    }
}

void opening_free(OpeningState *op) {
    if (!op) return;
    for (int i = 0; i < 5; i++) free_decoded_image(&op->logo_img[i]);
    npk_free(&op->logo);
    free_decoded_image(&op->leader_kao);
    op->logo_ready = false;
    op->leader_kao_id = -1;
    op->screen = OPENING_NONE;
}

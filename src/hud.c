#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "encoding.h"
#include "event.h"
#include "hal_video.h"

// ---------------------------------------------------------------------------
// 子任务 14/15: HUD, onboarding, testbench and council-chamber rendering.
//
// Presentation only. Every action is reported back to main.c, which owns the
// gameplay state. Source literals are UTF-8 and converted to cp932 on demand
// for the PC-98 font; ASCII is always upper-case because FONT.CHR has no
// reliable lower-case glyphs.
// ---------------------------------------------------------------------------

#define SJ_RING 128
#define SJ_SIZE 512

static const char *sj(const char *utf8) {
    static char ring[SJ_RING][SJ_SIZE];
    static unsigned next;
    char *dst = ring[next++ % SJ_RING];
    size_t n = encoding_utf8_to_sjis(utf8, dst, SJ_SIZE);
    if (n == 0) {
        snprintf(dst, SJ_SIZE, "%s", utf8);
    }
    return dst;
}

// Width in pixels of one line (16 px full-width, 8 px half-width/ASCII).
static int text_width(const char *s) {
    int w = 0;
    while (s && *s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n') break;
        if (c < 0x80 || (c >= 0xa1 && c <= 0xdf)) {
            w += 8;
            s++;
        } else if (c >= 0x81 && c <= 0x9f) {
            w += 16;
            s += 2;
        } else if (c >= 0xe0) {
            w += 16;
            s += 2;
        } else {
            w += 8;
            s++;
        }
    }
    return w;
}

static void text(int x, int y, const char *s, uint8_t color) {
    if (s && s[0]) hal_video_draw_text_sjis(x, y, s, color);
}

static void text_centered(int x, int w, int y, const char *s, uint8_t color) {
    int tw = text_width(s);
    text(x + (w - tw) / 2, y, s, color);
}

static void panel(int x, int y, int w, int h) {
    hal_video_fill_rect(x, y, w, h, 1);
    hal_video_draw_rect(x, y, w, h, 7);
    hal_video_draw_rect(x + 2, y + 2, w - 4, h - 4, 6);
}

static void draw_multiline(int x, int y, const char *s, int line_h,
                           uint8_t color) {
    if (!s) return;
    char line[256];
    int cx = x;
    int cy = y;
    size_t n = 0;
    for (const char *p = s;; p++) {
        if (*p == '\n' || *p == '\0') {
            line[n] = '\0';
            text(cx, cy, line, color);
            n = 0;
            cy += line_h;
            if (*p == '\0') break;
        } else if (n + 1 < sizeof(line)) {
            line[n++] = *p;
        }
    }
}

static void draw_portrait(const ImageDecoded *img, int x, int y) {
    hal_video_fill_rect(x, y, 128, 160, 1);
    hal_video_draw_rect(x, y, 128, 160, 6);
    hal_video_draw_rect(x + 2, y + 2, 124, 156, 7);
    if (!img || !img->pixels) return;
    int px = x + (128 - img->width) / 2;
    int py = y + (160 - img->height) / 2;
    if (img->mask) {
        hal_video_blit_masked(px, py, img->width, img->height,
                              img->pixels, img->mask, img->width);
    } else {
        hal_video_blit_indexed(px, py, img->width, img->height,
                               img->pixels, img->width);
    }
}

// ---------------------------------------------------------------------------
// Persistent toolbar
// ---------------------------------------------------------------------------

static const char *const s_button_label[HUD_TOOLBAR_BUTTONS] = {
    "政庁", "地図", "家宝 B", "武将一覧", "保存 F5", "読込 F9",
    "案内 F1", "試験台 F2"
};

static const char *const s_button_ascii[HUD_TOOLBAR_BUTTONS] = {
    "council", "map", "treasure", "roster", "save", "load",
    "guide", "testbench"
};

const char *hud_button_label(int index) {
    if (index < 0 || index >= HUD_TOOLBAR_BUTTONS) return "";
    return sj(s_button_label[index]);
}

const char *hud_button_ascii(int index) {
    if (index < 0 || index >= HUD_TOOLBAR_BUTTONS) return "?";
    return s_button_ascii[index];
}

void hud_toolbar_render(int hover) {
    hal_video_fill_rect(0, HUD_TOOLBAR_Y, PC98_WIDTH, HUD_TOOLBAR_H, 1);
    hal_video_draw_rect(0, HUD_TOOLBAR_Y, PC98_WIDTH, HUD_TOOLBAR_H, 7);
    for (int i = 0; i < HUD_TOOLBAR_BUTTONS; i++) {
        int x = i * HUD_TOOLBAR_W;
        if (hover == i) {
            hal_video_fill_rect(x + 1, HUD_TOOLBAR_Y + 1,
                                HUD_TOOLBAR_W - 2, HUD_TOOLBAR_H - 2, 5);
        }
        if (i > 0) {
            hal_video_draw_rect(x, HUD_TOOLBAR_Y + 2, 1,
                                HUD_TOOLBAR_H - 4, 6);
        }
        text_centered(x, HUD_TOOLBAR_W, HUD_TOOLBAR_Y + 3,
                      hud_button_label(i), (hover == i) ? 0 : 7);
    }
}

int hud_toolbar_hit(int mx, int my) {
    if (my < HUD_TOOLBAR_Y || my >= HUD_TOOLBAR_Y + HUD_TOOLBAR_H) return -1;
    if (mx < 0 || mx >= PC98_WIDTH) return -1;
    return mx / HUD_TOOLBAR_W;
}

// ---------------------------------------------------------------------------
// Beginner's guide
// ---------------------------------------------------------------------------

static const char *const s_guide_title[HUD_GUIDE_STEPS] = {
    "目的", "内政", "人事", "外交", "計略", "合戦"
};

static const char *const s_guide_body[HUD_GUIDE_STEPS] = {
    "天下統一を目指します。\n"
    "隣国へ出陣し、全ての城を\n"
    "自勢力のものにしましょう。\n"
    "\n"
    "画面下のボタンからマウスで\n"
    "全て操作できます。",

    "開発: 金を費やし石高と城防を\n"
    "上げます。\n"
    "徴兵: 金と兵糧で兵力を増やします。\n"
    "徴税: 金を得ますが士気が下がります。\n"
    "施し: 金と兵糧で士気を戻します。",

    "登用: 在野の武将を仲間にします。\n"
    "褒美: 金で武将の忠誠を上げます。\n"
    "移動: 隣接する自城へ武将と兵を\n"
    "移します。",

    "親善: 隣国の友好度を上げ、\n"
    "80以上で停戦になります。\n"
    "朝廷献金: 朝廷に金を納め、\n"
    "威望と位を上げ、全城の士気を\n"
    "高めます。",

    "調略: 隣国武将を寝返らせます。\n"
    "知謀が高いほど成功しやすい。\n"
    "流言: 隣国の士気と兵力を下げます。",

    "街道で繋がる敵城へ出陣できます。\n"
    "突撃/斉射/一騎討/退却、\n"
    "守城では籠城を選べます。\n"
    "一騎討は武力、調略は知謀が\n"
    "重要です。"
};

#define GUIDE_X 16
#define GUIDE_Y 14
#define GUIDE_W 608
#define GUIDE_H 372
#define GUIDE_BTN_Y (GUIDE_Y + GUIDE_H - 68)
#define GUIDE_BTN_W 110
#define GUIDE_BTN_H 28

void hud_guide_open(HudGuide *g) {
    if (!g) return;
    g->active = true;
    g->step = 0;
}

void hud_guide_close(HudGuide *g) {
    if (!g) return;
    g->active = false;
}

void hud_guide_move(HudGuide *g, int delta) {
    if (!g) return;
    g->step += delta;
    if (g->step < 0) g->step = 0;
    if (g->step >= HUD_GUIDE_STEPS) g->step = HUD_GUIDE_STEPS - 1;
}

void hud_guide_render(const HudGuide *g, const ImageDecoded *advisor,
                      const Scenario *sc, const TurnState *ts,
                      int castle_id) {
    if (!g || !g->active) return;
    int step = g->step;
    if (step < 0) step = 0;
    if (step >= HUD_GUIDE_STEPS) step = HUD_GUIDE_STEPS - 1;

    panel(GUIDE_X, GUIDE_Y, GUIDE_W, GUIDE_H);

    hal_video_fill_rect(GUIDE_X + 4, GUIDE_Y + 4, GUIDE_W - 8, 28, 5);
    text(GUIDE_X + 14, GUIDE_Y + 10, sj("案内  初心者ガイド"), 0);
    char counter[64];
    snprintf(counter, sizeof(counter), "%d/%d", step + 1, HUD_GUIDE_STEPS);
    text(GUIDE_X + GUIDE_W - 70, GUIDE_Y + 10, counter, 0);

    // Section heading + body.
    text(GUIDE_X + 20, GUIDE_Y + 44, sj(s_guide_title[step]), 6);
    hal_video_draw_rect(GUIDE_X + 20, GUIDE_Y + 66,
                        GUIDE_W - 180, 1, 6);
    draw_multiline(GUIDE_X + 20, GUIDE_Y + 78, sj(s_guide_body[step]),
                   20, 7);

    // Advisor portrait.
    draw_portrait(advisor, GUIDE_X + GUIDE_W - 152, GUIDE_Y + 52);

    // Context line: current clan / castle / date.
    char ctx[256];
    const char *fac_name = "?";
    const char *castle_name = "?";
    if (sc && castle_id >= 0 && castle_id < sc->num_castles) {
        castle_name = sc->castles[castle_id].name_sjis;
        const FactionDef *f = scenario_get_faction(
            sc->castles[castle_id].faction_id);
        if (f) fac_name = f->name_sjis;
    }
    snprintf(ctx, sizeof(ctx), "%s  %s  %d\x94\x4e%d\x8c\x8e",
             fac_name, castle_name, ts ? ts->year : 0, ts ? ts->month : 0);
    text(GUIDE_X + 20, GUIDE_Y + GUIDE_H - 88, ctx, 6);

    // Navigation buttons.
    const int bx[3] = { GUIDE_X + 20, GUIDE_X + 150,
                        GUIDE_X + GUIDE_W - 130 };
    const char *bl[3] = { "前へ", "次へ", "閉じる" };
    for (int i = 0; i < 3; i++) {
        hal_video_fill_rect(bx[i], GUIDE_BTN_Y, GUIDE_BTN_W,
                            GUIDE_BTN_H, 6);
        hal_video_draw_rect(bx[i], GUIDE_BTN_Y, GUIDE_BTN_W,
                            GUIDE_BTN_H, 7);
        text_centered(bx[i], GUIDE_BTN_W, GUIDE_BTN_Y + 6,
                      sj(bl[i]), 0);
    }
    text(GUIDE_X + 20, GUIDE_Y + GUIDE_H - 28,
         sj("ESC:閉じる  ←→:ページ クリックでも操作可"), 7);
}

int hud_guide_hit_button(int mx, int my) {
    if (my < GUIDE_BTN_Y || my >= GUIDE_BTN_Y + GUIDE_BTN_H) return -1;
    int xs[3] = { GUIDE_X + 20, GUIDE_X + 150, GUIDE_X + GUIDE_W - 130 };
    for (int i = 0; i < 3; i++) {
        if (mx >= xs[i] && mx < xs[i] + GUIDE_BTN_W) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Fast testbench
// ---------------------------------------------------------------------------

#define TB_X 10
#define TB_Y 8
#define TB_W 620
#define TB_H 384
#define TB_TAB_X 24
#define TB_TAB_Y 44
#define TB_TAB_W 146
#define TB_TAB_H 24
#define TB_ITEM_X 34
#define TB_ITEM_Y 80
#define TB_ITEM_W 572
#define TB_ITEM_H 26
#define TB_VISIBLE 10
#define TB_FOOTER_Y 350

static const char *const s_tb_tab[HUD_TB_TAB_COUNT] = {
    "内政・外交", "合戦・一騎討", "歴史・動画", "シナリオ・勢力"
};

static const char *const s_tb_domestic[8] = {
    "開発", "徴兵", "徴税", "親善", "調略", "登用",
    "金・兵糧・兵力 MAX", "全員未行動リセット"
};

static const char *const s_tb_battle[5] = {
    "出陣テスト (最寄り敵城)", "守城戦テスト", "一騎討テスト",
    "組討テスト", "自動決着テスト"
};

static char s_tb_buf[192];

void hud_testbench_open(HudTestbench *tb) {
    if (!tb) return;
    tb->visible = true;
    tb->tab = 0;
    tb->sel = 0;
}

void hud_testbench_close(HudTestbench *tb) {
    if (tb) tb->visible = false;
}

void hud_testbench_move(HudTestbench *tb, int dtab, int dsel) {
    if (!tb) return;
    tb->tab += dtab;
    if (tb->tab < 0) tb->tab = HUD_TB_TAB_COUNT - 1;
    if (tb->tab >= HUD_TB_TAB_COUNT) tb->tab = 0;
    tb->sel += dsel;
    int n = hud_testbench_item_count(tb->tab);
    if (tb->sel < 0) tb->sel = 0;
    if (tb->sel >= n) tb->sel = n - 1;
}

int hud_testbench_item_count(int tab) {
    switch (tab) {
        case HUD_TB_TAB_DOMESTIC: return 8;
        case HUD_TB_TAB_BATTLE:   return 5;
        case HUD_TB_TAB_EVENT:    return 10;
        case HUD_TB_TAB_SCENARIO: return 4 + MAX_FACTIONS;
        default:                  return 0;
    }
}

const char *hud_testbench_item_label(int tab, int idx) {
    if (idx < 0 || idx >= hud_testbench_item_count(tab)) return "";
    switch (tab) {
        case HUD_TB_TAB_DOMESTIC:
            return sj(s_tb_domestic[idx]);
        case HUD_TB_TAB_BATTLE:
            return sj(s_tb_battle[idx]);
        case HUD_TB_TAB_EVENT:
            if (idx < event_count()) {
                const HistoricalEvent *ev = event_at(idx);
                return ev ? ev->name_sjis : "";
            }
            return idx == 8 ? "OPENDAT 52 FRAMES" : "ENDDAT 84 FRAMES";
        case HUD_TB_TAB_SCENARIO:
            if (idx < MAX_SCENARIOS) {
                snprintf(s_tb_buf, sizeof(s_tb_buf), "SCENARIO %d  %s",
                         idx + 1, scenario_get_title(idx));
                return s_tb_buf;
            } else {
                int f = idx - MAX_SCENARIOS;
                const FactionDef *fac = scenario_get_faction(f);
                snprintf(s_tb_buf, sizeof(s_tb_buf), "FACTION %02d  %s",
                         f, fac ? fac->name_sjis : "?");
                return s_tb_buf;
            }
        default:
            return "";
    }
}

int hud_testbench_action(int tab, int idx) {
    if (idx < 0 || idx >= hud_testbench_item_count(tab)) return HUD_TB_NONE;
    switch (tab) {
        case HUD_TB_TAB_DOMESTIC: {
            static const int acts[8] = {
                HUD_TB_DEVELOP, HUD_TB_RECRUIT, HUD_TB_TAX,
                HUD_TB_DIPLOMACY, HUD_TB_ESPIONAGE,
                HUD_TB_RECRUIT_OFFICER, HUD_TB_MAX_RESOURCES,
                HUD_TB_RESET_ACTIONS
            };
            return acts[idx];
        }
        case HUD_TB_TAB_BATTLE: {
            static const int acts[5] = {
                HUD_TB_ATTACK, HUD_TB_DEFEND, HUD_TB_DUEL,
                HUD_TB_GRAPPLE, HUD_TB_AUTO_RESOLVE
            };
            return acts[idx];
        }
        case HUD_TB_TAB_EVENT:
            if (idx < event_count()) return HUD_TB_EVENT_BASE + idx;
            return (idx == 8) ? HUD_TB_MOVIE_OPEN : HUD_TB_MOVIE_END;
        case HUD_TB_TAB_SCENARIO:
            if (idx < MAX_SCENARIOS) return HUD_TB_SCENARIO_BASE + idx;
            return HUD_TB_FACTION_BASE + (idx - MAX_SCENARIOS);
        default:
            return HUD_TB_NONE;
    }
}

int hud_testbench_hit_tab(int mx, int my) {
    if (my < TB_TAB_Y || my >= TB_TAB_Y + TB_TAB_H) return -1;
    for (int i = 0; i < HUD_TB_TAB_COUNT; i++) {
        int x = TB_TAB_X + i * TB_TAB_W;
        if (mx >= x && mx < x + TB_TAB_W - 4) return i;
    }
    return -1;
}

static int tb_scroll(const HudTestbench *tb) {
    int scroll = tb->sel - (TB_VISIBLE - 1);
    if (scroll < 0) scroll = 0;
    return scroll;
}

int hud_testbench_hit_item(const HudTestbench *tb, int mx, int my) {
    if (!tb) return -1;
    if (mx < TB_ITEM_X || mx >= TB_ITEM_X + TB_ITEM_W) return -1;
    if (my < TB_ITEM_Y || my >= TB_ITEM_Y + TB_VISIBLE * TB_ITEM_H) {
        return -1;
    }
    int row = (my - TB_ITEM_Y) / TB_ITEM_H;
    int idx = tb_scroll(tb) + row;
    if (idx < 0 || idx >= hud_testbench_item_count(tb->tab)) return -1;
    return idx;
}

void hud_testbench_render(const HudTestbench *tb, const Scenario *sc,
                          const TurnState *ts, int castle_id) {
    if (!tb || !tb->visible) return;
    panel(TB_X, TB_Y, TB_W, TB_H);

    hal_video_fill_rect(TB_X + 4, TB_Y + 4, TB_W - 8, 28, 5);
    text(TB_X + 14, TB_Y + 10, sj("試験台  TESTBENCH"), 0);

    char info[192];
    const char *cname = "?";
    if (sc && castle_id >= 0 && castle_id < sc->num_castles) {
        cname = sc->castles[castle_id].name_sjis;
    }
    snprintf(info, sizeof(info),
             "%s  %d\x94\x4e%d\x8c\x8e  \x88\xd0\x96\x5d %d",
             cname, ts ? ts->year : 0, ts ? ts->month : 0,
             ts ? ts->prestige : 0);
    text(TB_X + TB_W - 250, TB_Y + 10, info, 0);

    // Category tabs.
    for (int i = 0; i < HUD_TB_TAB_COUNT; i++) {
        int x = TB_TAB_X + i * TB_TAB_W;
        bool on = (i == tb->tab);
        hal_video_fill_rect(x, TB_TAB_Y, TB_TAB_W - 4, TB_TAB_H,
                            on ? 6 : 1);
        hal_video_draw_rect(x, TB_TAB_Y, TB_TAB_W - 4, TB_TAB_H, 7);
        text_centered(x, TB_TAB_W - 4, TB_TAB_Y + 4,
                      sj(s_tb_tab[i]), on ? 0 : 7);
    }

    // Item list.
    int count = hud_testbench_item_count(tb->tab);
    int scroll = tb_scroll(tb);
    for (int row = 0; row < TB_VISIBLE; row++) {
        int idx = scroll + row;
        if (idx >= count) break;
        int y = TB_ITEM_Y + row * TB_ITEM_H;
        bool on = (idx == tb->sel);
        hal_video_fill_rect(TB_ITEM_X, y, TB_ITEM_W, TB_ITEM_H - 2,
                            on ? 5 : 1);
        hal_video_draw_rect(TB_ITEM_X, y, TB_ITEM_W, TB_ITEM_H - 2,
                            on ? 7 : 6);
        char num[16];
        snprintf(num, sizeof(num), "%02d", idx + 1);
        text(TB_ITEM_X + 10, y + 4, num, on ? 0 : 6);
        text(TB_ITEM_X + 48, y + 4,
             hud_testbench_item_label(tb->tab, idx), on ? 0 : 7);
    }
    if (scroll > 0) {
        text(TB_ITEM_X + TB_ITEM_W - 40, TB_ITEM_Y - 14, "UP", 6);
    }
    if (scroll + TB_VISIBLE < count) {
        text(TB_ITEM_X + TB_ITEM_W - 40,
             TB_ITEM_Y + TB_VISIBLE * TB_ITEM_H - 4, "DN", 6);
    }

    // Footer.
    hal_video_draw_rect(TB_X + 12, TB_FOOTER_Y - 4, TB_W - 24, 1, 6);
    text(TB_X + 16, TB_FOOTER_Y,
         sj("ENTER:実行  ESC/F2:閉じる  上下:選択  左右:分類"), 7);
    char foot[192];
    snprintf(foot, sizeof(foot), "%d/%d", tb->sel + 1, count);
    text(TB_X + TB_W - 70, TB_FOOTER_Y, sj(foot), 6);
}

// ---------------------------------------------------------------------------
// Council chamber (Maincmd2.gp)
// ---------------------------------------------------------------------------

#define CO_PORTRAIT_X 16
#define CO_PORTRAIT_Y 50
#define CO_TAB_X 158
#define CO_TAB_Y 42
#define CO_TAB_W 92
#define CO_TAB_H 22
#define CO_ITEM_X 158
#define CO_ITEM_Y 76
#define CO_ITEM_W 466
#define CO_ITEM_H 30
#define CO_INFO_Y 226

static const char *const s_co_tab[HUD_COUNCIL_TAB_COUNT] = {
    "内政", "人事", "外交・朝廷", "計略", "軍事"
};

static const TurnCommand s_co_cmd[HUD_COUNCIL_TAB_COUNT][4] = {
    { TURN_CMD_DEVELOP, TURN_CMD_RECRUIT, TURN_CMD_TAX,
      TURN_CMD_CHARITY },
    { TURN_CMD_RECRUIT_OFFICER, TURN_CMD_REWARD, TURN_CMD_MOVE,
      TURN_CMD_NONE },
    { TURN_CMD_DIPLOMACY, TURN_CMD_COURT, TURN_CMD_NONE,
      TURN_CMD_NONE },
    { TURN_CMD_ENTICE, TURN_CMD_RUMOR, TURN_CMD_NONE,
      TURN_CMD_NONE },
    { TURN_CMD_ATTACK, TURN_CMD_OFFICERS, TURN_CMD_END,
      TURN_CMD_NONE }
};

static const int s_co_count[HUD_COUNCIL_TAB_COUNT] = { 4, 3, 2, 2, 3 };

void hud_council_open(HudCouncil *c) {
    if (!c) return;
    c->visible = true;
    c->tab = 0;
    c->sel = 0;
}

void hud_council_close(HudCouncil *c) {
    if (c) c->visible = false;
}

void hud_council_move(HudCouncil *c, int dtab, int dsel) {
    if (!c) return;
    c->tab += dtab;
    if (c->tab < 0) c->tab = HUD_COUNCIL_TAB_COUNT - 1;
    if (c->tab >= HUD_COUNCIL_TAB_COUNT) c->tab = 0;
    c->sel += dsel;
    int n = s_co_count[c->tab];
    if (c->sel < 0) c->sel = 0;
    if (c->sel >= n) c->sel = n - 1;
}

int hud_council_item_count(int tab) {
    if (tab < 0 || tab >= HUD_COUNCIL_TAB_COUNT) return 0;
    return s_co_count[tab];
}

TurnCommand hud_council_command(int tab, int idx) {
    if (tab < 0 || tab >= HUD_COUNCIL_TAB_COUNT) return TURN_CMD_NONE;
    if (idx < 0 || idx >= s_co_count[tab]) return TURN_CMD_NONE;
    return s_co_cmd[tab][idx];
}

const char *hud_council_item_label(int tab, int idx) {
    TurnCommand cmd = hud_council_command(tab, idx);
    if (cmd == TURN_CMD_END) return sj("評定終了");
    return turn_command_name(cmd);
}

int hud_council_hit_tab(int mx, int my) {
    if (my < CO_TAB_Y || my >= CO_TAB_Y + CO_TAB_H) return -1;
    for (int i = 0; i < HUD_COUNCIL_TAB_COUNT; i++) {
        int x = CO_TAB_X + i * CO_TAB_W;
        if (mx >= x && mx < x + CO_TAB_W) return i;
    }
    return -1;
}

int hud_council_hit_item(const HudCouncil *c, int mx, int my) {
    if (!c) return -1;
    if (mx < CO_ITEM_X || mx >= CO_ITEM_X + CO_ITEM_W) return -1;
    if (my < CO_ITEM_Y || my >= CO_ITEM_Y + 4 * CO_ITEM_H) return -1;
    int idx = (my - CO_ITEM_Y) / CO_ITEM_H;
    if (idx < 0 || idx >= s_co_count[c->tab]) return -1;
    return idx;
}

void hud_council_render(const HudCouncil *c, const Scenario *sc,
                        const TurnState *ts, int castle_id,
                        const ImageDecoded *palace,
                        const ImageDecoded *portrait) {
    if (!c || !c->visible) return;

    if (palace && palace->pixels) {
        hal_video_blit_indexed(0, 0, palace->width, palace->height,
                               palace->pixels, palace->width);
    } else {
        hal_video_fill_rect(0, 0, PC98_WIDTH, 400, 1);
    }

    // Header.
    hal_video_fill_rect(0, 0, PC98_WIDTH, 30, 1);
    hal_video_draw_rect(0, 0, PC98_WIDTH, 30, 7);
    text(12, 7, sj("政庁 / 評定"), 6);

    if (!sc || castle_id < 0 || castle_id >= sc->num_castles) return;
    const ScenarioCastle *cs = &sc->castles[castle_id];
    const FactionDef *fac = scenario_get_faction(cs->faction_id);

    char head[256];
    snprintf(head, sizeof(head), "%s  %s  %d\x94\x4e%d\x8c\x8e",
             fac ? fac->name_sjis : "?", cs->name_sjis,
             ts ? ts->year : 0, ts ? ts->month : 0);
    text(PC98_WIDTH - 280, 7, head, 7);

    // Category tabs.
    for (int i = 0; i < HUD_COUNCIL_TAB_COUNT; i++) {
        int x = CO_TAB_X + i * CO_TAB_W;
        bool on = (i == c->tab);
        hal_video_fill_rect(x, CO_TAB_Y, CO_TAB_W - 4, CO_TAB_H,
                            on ? 6 : 1);
        hal_video_draw_rect(x, CO_TAB_Y, CO_TAB_W - 4, CO_TAB_H, 7);
        text_centered(x, CO_TAB_W - 4, CO_TAB_Y + 3,
                      sj(s_co_tab[i]), on ? 0 : 7);
    }

    // Command list.
    int n = s_co_count[c->tab];
    for (int i = 0; i < n; i++) {
        int y = CO_ITEM_Y + i * CO_ITEM_H;
        bool on = (i == c->sel);
        hal_video_fill_rect(CO_ITEM_X, y, CO_ITEM_W, CO_ITEM_H - 3,
                            on ? 5 : 1);
        hal_video_draw_rect(CO_ITEM_X, y, CO_ITEM_W, CO_ITEM_H - 3,
                            on ? 7 : 6);
        text(CO_ITEM_X + 12, y + 6, hud_council_item_label(c->tab, i),
             on ? 0 : 7);
    }

    // Portrait + commander.
    draw_portrait(portrait, CO_PORTRAIT_X, CO_PORTRAIT_Y);
    const char *lord = "?";
    if (cs->lord_officer_id >= 0 &&
        cs->lord_officer_id < sc->num_officers) {
        lord = sc->officers[cs->lord_officer_id].full_name_sjis;
    }
    text(CO_PORTRAIT_X, CO_PORTRAIT_Y + 168, lord, 7);
    text(CO_PORTRAIT_X, CO_PORTRAIT_Y + 186, fac ? fac->name_sjis : "?", 6);

    // Castle stats.
    hal_video_fill_rect(CO_ITEM_X, CO_INFO_Y, CO_ITEM_W, 134, 1);
    hal_video_draw_rect(CO_ITEM_X, CO_INFO_Y, CO_ITEM_W, 134, 7);
    char line[256];
    snprintf(line, sizeof(line), "金 %d   兵糧 %d   兵力 %d",
             cs->gold, cs->rice, cs->troops);
    text(CO_ITEM_X + 10, CO_INFO_Y + 8, sj(line), 7);
    snprintf(line, sizeof(line), "士気 %d   城防 %d   威望 %d   位 %d",
             cs->morale, cs->defense, ts ? ts->prestige : 0,
             ts ? ts->court_rank : 0);
    text(CO_ITEM_X + 10, CO_INFO_Y + 30, sj(line), 7);

    // Lowest-relation adjacent clan (the natural diplomacy target).
    int best_rel = 101;
    const char *best_name = "?";
    if (ts) {
        for (int k = 0; k < cs->num_conns; k++) {
            int cc = cs->conns[k];
            if (cc < 0 || cc >= sc->num_castles) continue;
            int cf = sc->castles[cc].faction_id;
            if (cf < 0 || cf == cs->faction_id) continue;
            int rel = turn_relation(ts, cs->faction_id, cf);
            if (rel < best_rel) {
                best_rel = rel;
                const FactionDef *ef = scenario_get_faction(cf);
                best_name = ef ? ef->name_sjis : "?";
            }
        }
    }
    if (best_rel > 100) best_rel = 0;
    const char *rel_label = sj("隣国友好: ");
    snprintf(line, sizeof(line), "%s%s %d", rel_label, best_name, best_rel);
    text(CO_ITEM_X + 10, CO_INFO_Y + 52, line, 6);

    text(CO_ITEM_X + 10, CO_INFO_Y + 80,
         sj("親善は友好80以上で停戦。"), 6);
    text(CO_ITEM_X + 10, CO_INFO_Y + 100,
         sj("ESC:閉じる  ENTER:実行"), 6);
}


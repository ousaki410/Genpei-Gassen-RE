#ifndef YPHZ_HUD_H
#define YPHZ_HUD_H

#include <stdbool.h>

#include "decompress.h"
#include "scenario.h"
#include "turn.h"

// ---------------------------------------------------------------------------
// 子任务 14/15: in-game HUD, onboarding, testbench and council chamber.
//
// This module owns *presentation only*: geometry, hit-testing and drawing.
// main.c performs the actions (turn commands, battles, events, movies,
// scenario switches) so the gameplay state stays in one place.
// ---------------------------------------------------------------------------

#define HUD_TOOLBAR_BUTTONS 8
#define HUD_TOOLBAR_Y 358
#define HUD_TOOLBAR_H 20
#define HUD_TOOLBAR_W (PC98_WIDTH / HUD_TOOLBAR_BUTTONS)

typedef enum {
    HUD_BTN_COUNCIL = 0,   // 政庁/評定
    HUD_BTN_MAP,           // 地図
    HUD_BTN_TREASURE,      // 家宝
    HUD_BTN_ROSTER,        // 武将
    HUD_BTN_SAVE,          // 保存 F5
    HUD_BTN_LOAD,          // 読込 F9
    HUD_BTN_GUIDE,         // 案内 F1
    HUD_BTN_TESTBENCH      // 試験 F2
} HudButton;

void        hud_toolbar_render(int hover);
int         hud_toolbar_hit(int mx, int my);
const char *hud_button_label(int index);   // SJIS
const char *hud_button_ascii(int index);

// ---------------------------------------------------------------------------
// Beginner's guide (F1 / [案内])
// ---------------------------------------------------------------------------

#define HUD_GUIDE_STEPS 6

typedef struct {
    bool active;
    int  step;
} HudGuide;

void hud_guide_open(HudGuide *g);
void hud_guide_close(HudGuide *g);
void hud_guide_move(HudGuide *g, int delta);
void hud_guide_render(const HudGuide *g, const ImageDecoded *advisor,
                      const Scenario *sc, const TurnState *ts, int castle_id);
// Returns 0 = prev, 1 = next, 2 = close, -1 = none.
int  hud_guide_hit_button(int mx, int my);

// ---------------------------------------------------------------------------
// Fast testbench (F2 / [試験])
// ---------------------------------------------------------------------------

typedef enum {
    HUD_TB_TAB_DOMESTIC = 0,
    HUD_TB_TAB_BATTLE,
    HUD_TB_TAB_EVENT,
    HUD_TB_TAB_SCENARIO,
    HUD_TB_TAB_COUNT
} HudTbTab;

typedef enum {
    HUD_TB_NONE = 0,
    HUD_TB_DEVELOP,
    HUD_TB_RECRUIT,
    HUD_TB_TAX,
    HUD_TB_DIPLOMACY,
    HUD_TB_ESPIONAGE,
    HUD_TB_RECRUIT_OFFICER,
    HUD_TB_MAX_RESOURCES,
    HUD_TB_RESET_ACTIONS,
    HUD_TB_ATTACK,
    HUD_TB_DEFEND,
    HUD_TB_DUEL,
    HUD_TB_GRAPPLE,
    HUD_TB_AUTO_RESOLVE,
    HUD_TB_MOVIE_OPEN,
    HUD_TB_MOVIE_END
} HudTestAction;

// Event / scenario / faction items are offset by these bases.
#define HUD_TB_EVENT_BASE     100
#define HUD_TB_SCENARIO_BASE  200
#define HUD_TB_FACTION_BASE   300

typedef struct {
    bool visible;
    int  tab;
    int  sel;
} HudTestbench;

void hud_testbench_open(HudTestbench *tb);
void hud_testbench_close(HudTestbench *tb);
void hud_testbench_move(HudTestbench *tb, int dtab, int dsel);
int  hud_testbench_item_count(int tab);
const char *hud_testbench_item_label(int tab, int idx);
int  hud_testbench_action(int tab, int idx);   // HudTestAction or base+index
int  hud_testbench_hit_tab(int mx, int my);    // 0..3, -1
int  hud_testbench_hit_item(const HudTestbench *tb, int mx, int my);
void hud_testbench_render(const HudTestbench *tb, const Scenario *sc,
                          const TurnState *ts, int castle_id);

// ---------------------------------------------------------------------------
// Council chamber ([政庁/評定], Maincmd2.gp)
// ---------------------------------------------------------------------------

typedef enum {
    HUD_COUNCIL_TAB_DOMESTIC = 0,
    HUD_COUNCIL_TAB_PERSONNEL,
    HUD_COUNCIL_TAB_DIPLOMACY,
    HUD_COUNCIL_TAB_ESPIONAGE,
    HUD_COUNCIL_TAB_MILITARY,
    HUD_COUNCIL_TAB_COUNT
} HudCouncilTab;

typedef struct {
    bool visible;
    int  tab;
    int  sel;
} HudCouncil;

void hud_council_open(HudCouncil *c);
void hud_council_close(HudCouncil *c);
void hud_council_move(HudCouncil *c, int dtab, int dsel);
int  hud_council_item_count(int tab);
const char *hud_council_item_label(int tab, int idx);
TurnCommand hud_council_command(int tab, int idx);
int  hud_council_hit_tab(int mx, int my);
int  hud_council_hit_item(const HudCouncil *c, int mx, int my);
void hud_council_render(const HudCouncil *c, const Scenario *sc,
                        const TurnState *ts, int castle_id,
                        const ImageDecoded *palace,
                        const ImageDecoded *portrait);

#endif // YPHZ_HUD_H

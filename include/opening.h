#ifndef YPHZ_OPENING_H
#define YPHZ_OPENING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "input.h"
#include "npk.h"
#include "scenario.h"
#include "turn.h"

// ---------------------------------------------------------------------------
// Opening flow (子任务 8): Logo.gp title -> scenario select -> daimyo select.
//
// The original game boots into this sequence; the engine keeps every direct
// CLI path (--map, --attack, --do-cmd, ...) by skipping the opening whenever a
// jump argument is present. The flow only touches *presentation* and the
// chosen Scenario/TurnState; main.c decides when to enter map mode.
// ---------------------------------------------------------------------------

#define OPENING_MAX_FACTIONS 15

typedef enum {
    OPENING_NONE = 0,
    OPENING_TITLE,
    OPENING_SCENARIO,
    OPENING_DAIMYO
} OpeningScreen;

#pragma pack(push, 1)

typedef struct {
    OpeningScreen screen;
    int   scenario;                  // highlighted scenario (0..MAX_SCENARIOS-1)
    int   sel;                       // highlighted row on the current screen
    int   factions[OPENING_MAX_FACTIONS]; // playable factions of the scenario
    int   leaders[OPENING_MAX_FACTIONS];  // 当主 officer id per faction
    int   fac_count;
    uint32_t anim;                   // frame counter (blinking hints)

    bool         logo_ready;
    NpkFile      logo;
    ImageDecoded logo_img[5];

    ImageDecoded leader_kao;
    int          leader_kao_id;      // officer id currently decoded
} OpeningState;

#pragma pack(pop)

// Load Logo.gp and show the title screen.
void opening_begin(OpeningState *op, const char *asset_dir);

// True while the title/scenario/daimyo flow is still on screen.
bool opening_active(const OpeningState *op);

// Drive one frame of input. Returns true exactly once, when the player has
// confirmed a daimyo: `sc` and `ts` then hold the chosen game, ready for map
// mode. `sndata_path` is used to reload the selected scenario.
bool opening_step(OpeningState *op, Scenario *sc, TurnState *ts,
                  const InputState *in, YphzKey key, const char *sndata_path);

// Render the current screen. `kao_data`/`kao_size`/`offsets` supply the
// Kaodata portrait bank; the portrait is skipped when they are NULL.
void opening_render(OpeningState *op, const Scenario *sc,
                    const uint8_t *kao_data, size_t kao_size,
                    const uint32_t *offsets);

void opening_free(OpeningState *op);

#endif // YPHZ_OPENING_H

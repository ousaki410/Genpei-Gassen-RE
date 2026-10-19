#ifndef YPHZ_TURN_H
#define YPHZ_TURN_H

#include <stdint.h>
#include <stdbool.h>

#include "scenario.h"

// ---------------------------------------------------------------------------
// Turn / domestic-affairs state machine.
//
// The game data (Sndata.gp) already carries the calendar and every castle's
// economy (rice = kokudaka, gold = treasury/provisions, troops, defense,
// morale, training). This module adds the missing *logic* layer: whose turn it
// is, which officers have already acted this month, the three domestic
// commands, and the end-of-month settlement.
//
// NOTE: the original scenario records have no separate "民忠 / public order"
// field, so morale (士気) doubles as the population-loyalty proxy for taxation.
// ---------------------------------------------------------------------------

#define TURN_TEXT_MAX 512

typedef enum {
    TURN_CMD_NONE = 0,
    TURN_CMD_DEVELOP,   // 開発  : spend gold -> raise kokudaka (rice) + defense
    TURN_CMD_RECRUIT,   // 徴兵  : spend gold + provisions -> raise troops
    TURN_CMD_TAX,       // 徴税  : raise gold, lower morale
    TURN_CMD_MOVE,      // 移動  : transfer an officer + troops to an own castle
    TURN_CMD_ATTACK,    // 出陣  : march on an adjacent enemy castle (starts 合戦)
    TURN_CMD_OFFICERS,  // 武将  : UI-only officer roster (no action consumed)
    TURN_CMD_END        // 終了  : advance the calendar + settle income
} TurnCommand;

typedef struct {
    int     player_faction;         // clan the human is currently playing (0..14)
    int     year;                   // mirrors Scenario::year
    int     month;                  // mirrors Scenario::month (1..12)
    uint8_t acted[MAX_OFFICERS];    // 1 = this officer has already acted this month
} TurnState;

typedef struct {
    bool        ok;
    TurnCommand cmd;
    int         officer_id;         // acting officer (-1 for end-turn/failure)
    int         castle_id;
    char        text[TURN_TEXT_MAX];// SJIS/ASCII report for the dialogue box
    int         d_gold;
    int         d_rice;
    int         d_troops;
    int         d_morale;
    int         d_defense;
} TurnResult;

// Initialise the turn state, seeding year/month from the loaded scenario.
void turn_init(TurnState *ts, const Scenario *sc, int faction_id);

// Clear every officer's "already acted" flag (start of a new month).
void turn_reset_actions(TurnState *ts);

bool turn_officer_has_acted(const TurnState *ts, int officer_id);

// First idle, active officer of `faction_id` stationed at `castle_id`
// (the castle lord is preferred). Returns -1 when everyone has acted.
int turn_find_idle_officer(const Scenario *sc, const TurnState *ts, int castle_id);

// Same, for an explicit faction (used by the non-player AI).
int turn_find_idle_officer_faction(const Scenario *sc, const TurnState *ts,
                                   int faction_id, int castle_id);

// Enumerate the active officers of `faction_id` stationed at `castle_id`.
// Returns the number written to ids (up to max_ids), ordered by officer id.
int turn_list_castle_officers(const Scenario *sc, int faction_id, int castle_id,
                              int *ids, int max_ids);

// Enumerate adjacent castles owned by the same faction (移動 destinations).
int turn_list_move_targets(const Scenario *sc, int from_castle, int faction_id,
                           int *out, int max_out);

// Highest-war idle officer at `castle_id` (for 出陣), -1 when none is free.
int turn_find_attack_officer(const Scenario *sc, const TurnState *ts,
                             int castle_id);

// Same, for an explicit faction (used by the non-player AI).
int turn_find_attack_officer_faction(const Scenario *sc, const TurnState *ts,
                                     int faction_id, int castle_id);

// Transfer `officer_id` plus part of the garrison from `from_castle` to a
// connected friendly castle. Consumes the officer's action.
bool turn_move_officer(TurnState *ts, Scenario *sc, int from_castle, int to_castle,
                       int officer_id, TurnResult *res);

// Command costs / caps (used by the UI to grey out or explain actions).
int turn_develop_cost(const ScenarioCastle *cs);
int turn_recruit_gold_cost(const ScenarioCastle *cs);
int turn_recruit_rice_cost(const ScenarioCastle *cs);
int turn_recruit_cap(const ScenarioCastle *cs);

// Execute a domestic command for `castle_id`, auto-selecting the first idle
// officer. DEVELOP/RECRUIT/TAX consume that officer's action; END advances the
// month and settles income for every castle. Returns res->ok.
bool turn_run_command(TurnState *ts, Scenario *sc, int castle_id,
                      TurnCommand cmd, TurnResult *res);

// Same, but with an explicit acting officer (-1 = auto-select the first idle
// officer). Used by the roster-driven "choose an officer" UI flow.
bool turn_run_command_officer(TurnState *ts, Scenario *sc, int castle_id,
                              TurnCommand cmd, int officer_id, TurnResult *res);

// Faction-parameterised domestic command used by the non-player AI. Accepts
// DEVELOP / RECRUIT / TAX for any faction (not just the player) and marks the
// acting officer as spent. END / MOVE / ATTACK are rejected here.
bool turn_run_faction_command(TurnState *ts, Scenario *sc, int faction_id,
                              int castle_id, TurnCommand cmd, int officer_id,
                              TurnResult *res);

// End-of-month settlement: month++ (with year rollover), per-castle income,
// quarterly harvest growth, and action reset. Also used via TURN_CMD_END.
bool turn_end(TurnState *ts, Scenario *sc, TurnResult *res);

const char* turn_command_name(TurnCommand cmd);  // SJIS label

#endif // YPHZ_TURN_H

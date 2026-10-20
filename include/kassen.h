#ifndef YPHZ_KASSEN_H
#define YPHZ_KASSEN_H

#include <stdbool.h>
#include <stdint.h>

#include "scenario.h"

// ---------------------------------------------------------------------------
// 合戦 / 攻城 (Kassen) state machine.
//
// A battle is launched from a player-owned castle against an adjacent enemy
// castle.  The module owns only the *logic*: troop/morale/defense attrition,
// the four tactical commands, the duel overlay flag, and the post-battle
// transfer of the captured castle.  Rendering lives in main.c so that the
// battle UI can reuse the existing HAL/font/portrait pipeline.
//
// Determinism: every roll comes from a per-battle xorshift seeded from the
// castle ids, the acting officer and the calendar month.  Headless runs are
// therefore reproducible.
// ---------------------------------------------------------------------------

#define KASSEN_LOG_LINES 5
#define KASSEN_LOG_LEN   80
#define KASSEN_TEXT_MAX  512

#pragma pack(push, 1)

typedef enum {
    KASSEN_CMD_NONE = 0,
    KASSEN_CMD_CHARGE,   // 突撃 : all-out charge, heavy losses on both sides
    KASSEN_CMD_VOLLEY,   // 斉射 : ranged volley, safer for the attacker
    KASSEN_CMD_DUEL,     // 一騎討 : single combat, big morale swing
    KASSEN_CMD_RETREAT,  // 退却 : withdraw the army
    KASSEN_CMD_HOLD      // 籠城 : (defence only) hold the walls, weather the assault
} KassenCommand;

typedef struct {
    bool     active;
    bool     player_defends;   // the human commands the garrison (AI attacks)
    bool     finished;
    bool     victory;          // attacker took the castle
    bool     retreated;        // attacker withdrew voluntarily
    int      from_castle;
    int      to_castle;
    int      attacker_officer; // -1 if the garrison has no named commander
    int      defender_officer; // -1 => garrison commanded by the castle lord
    int      att_troops;
    int      def_troops;
    int      home_troops;      // garrison left behind at from_castle
    int      att_morale;
    int      def_morale;
    int      defense;          // remaining castle defense (0 => captured)
    int      turn;
    int      bg;               // Hkeshiki.gp block 0..3
    bool     duel_active;      // draw the Hkumi/Hikback close-up this frame
    int      duel_bg;          // 0 = Hkumi (208x160), 1 = Hikback (288x176)
    int      duel_winner;      // 0 attacker, 1 defender, -1 none
    int      last_att_loss;
    int      last_def_loss;
    uint32_t rng;
    char     log[KASSEN_LOG_LINES][KASSEN_LOG_LEN];
    int      log_count;
} KassenState;

#pragma pack(pop)

// True when `from` is owned by `faction`, `to` is an adjacent enemy castle.
bool kassen_can_attack(const Scenario *sc, int from_castle, int to_castle,
                       int faction_id);

// Fill `out` with adjacent enemy castle ids. Returns the count.
int kassen_list_targets(const Scenario *sc, int from_castle, int faction_id,
                        int *out, int max_out);

// Set up a battle.  `attacker_officer` must be stationed at from_castle.
// Seizes 3/4 of the garrison as the field army (all of it for tiny garrisons).
void kassen_begin(KassenState *ks, Scenario *sc, int from_castle, int to_castle,
                  int attacker_officer);

// Same, but marks the battle as a defensive one: the human commands the
// garrison of `to_castle` while the AI drives the attackers from `from_castle`.
void kassen_begin_defense(KassenState *ks, Scenario *sc, int from_castle,
                          int to_castle, int attacker_officer);

// Resolve one tactical command.  Returns true while the battle continues.
bool kassen_command(KassenState *ks, Scenario *sc, KassenCommand cmd);

// Deterministic AI heuristics (no RNG outside KassenState::rng).
KassenCommand kassen_ai_attack_decide(const KassenState *ks, const Scenario *sc);
KassenCommand kassen_ai_defend_decide(const KassenState *ks, const Scenario *sc);

// One defensive exchange: the AI attacker commits a tactic, then the human's
// defensive command resolves.  Returns true while the battle continues.
bool kassen_defense_round(KassenState *ks, Scenario *sc, KassenCommand def_cmd);

// Headless auto-resolve of an AI-vs-AI siege.  Applies the outcome to `sc`
// and returns true when the attacker captured the castle.  `out` is optional.
bool kassen_auto_resolve(Scenario *sc, int from_castle, int to_castle,
                         int attacker_officer, KassenState *out);

// Apply the finished battle to the scenario: transfer the castle on victory,
// return survivors on defeat/retreat.  Safe to call only once per battle.
void kassen_apply_result(KassenState *ks, Scenario *sc);

// True when every castle belongs to the same faction (天下統一).
bool kassen_is_unified(const Scenario *sc);

// ---------------------------------------------------------------------------
// Unit sprite bank (子任务 10 part 2)
//
// Hunitpat.gp is a raw PC-98 4-plane sheet in the game's native planar
// order: 24 px per scanline (12 byte stride), 12661 rows.  The unit sprites
// are 24x32 px on a 36-row pitch - one per faction, twelve in all.  The
// sheet is decoded once at start-up and the battlefield draws a small
// formation of unit sprites on each side, the number of sprites scaling with
// the current troop count.
// ---------------------------------------------------------------------------
#define KASSEN_UNIT_TILE_W 24
#define KASSEN_UNIT_TILE_H 32
#define KASSEN_MAX_UNITS   12      // per side (4 columns x 3 rows)

// One unit sprite placed on the battlefield.
typedef struct {
    int  x, y;        // top-left pixel in the 640x400 battle view
    int  tile;        // row of the unit sheet where the sprite starts
    bool attacker;    // true = attacking side, false = defending side
} KassenUnitSprite;

// Decode the unit pattern bank from a raw 4-plane file (Hunitpat.gp).
bool kassen_units_load(const char *path);
void kassen_units_free(void);
int  kassen_units_loaded(void);   // number of decoded tiles

// Build the formation for one side.  Returns the sprite count written to
// `out` (never more than max_out).  `officer` seeds the tile choice so that
// different battles show different unit patterns, deterministically.
int  kassen_unit_formation(int troops, bool attacker, int officer,
                            KassenUnitSprite *out, int max_out);

// Draw both formations on the battlefield (called from the battle renderer).
void kassen_units_draw(int att_troops, int def_troops,
                        int att_officer, int def_officer);

const char *kassen_command_name(KassenCommand cmd);   // SJIS label
const char *kassen_command_ascii(KassenCommand cmd);  // headless log label
const char *kassen_defense_name(KassenCommand cmd);   // SJIS label (defence set)

#endif // YPHZ_KASSEN_H

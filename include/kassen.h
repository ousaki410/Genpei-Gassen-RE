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
    KASSEN_CMD_RETREAT   // 退却 : withdraw the army
} KassenCommand;

typedef struct {
    bool     active;
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

// Resolve one tactical command.  Returns true while the battle continues.
bool kassen_command(KassenState *ks, Scenario *sc, KassenCommand cmd);

// Apply the finished battle to the scenario: transfer the castle on victory,
// return survivors on defeat/retreat.  Safe to call only once per battle.
void kassen_apply_result(KassenState *ks, Scenario *sc);

// True when every castle belongs to the same faction (天下統一).
bool kassen_is_unified(const Scenario *sc);

const char *kassen_command_name(KassenCommand cmd);   // SJIS label
const char *kassen_command_ascii(KassenCommand cmd);  // headless log label

#endif // YPHZ_KASSEN_H

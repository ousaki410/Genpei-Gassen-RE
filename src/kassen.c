#include "kassen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_video.h"
#include "raw_sprite.h"

// ---------------------------------------------------------------------------
// SJIS labels (PC-98 FONT.CHR has no usable lower-case ASCII)
// ---------------------------------------------------------------------------

#define SJIS_CHARGE   "\x93\xcb\x8c\x82"                    // 突撃
#define SJIS_VOLLEY   "\x90\xc4\x8e\xcb"                    // 斉射
#define SJIS_DUEL     "\x88\xea\x8b\x52\x93\xa2"            // 一騎討
#define SJIS_RETREAT  "\x91\xde\x8b\x70"                    // 退却
#define SJIS_HOLD     "\xe2\xc4\x8f\xe9"                    // 籠城
#define SJIS_VICTORY  "\x8f\x9f\x97\x98"                    // 勝利
#define SJIS_DEFEAT   "\x94\x73\x96\x6b"                    // 敗北
#define SJIS_UNIFIED  "\x93\x56\x89\xba\x93\x9d\x88\xea"    // 天下統一
#define SJIS_ATTACK   "\x8d\x55\x95\xfb"                    // 攻方
#define SJIS_DEFEND   "\x8e\xe7\x95\xfb"                    // 守方
#define SJIS_TROOPS   "\x95\xba\x97\xcd"                    // 兵力
#define SJIS_MORALE   "\x8e\x6d\x8b\x43"                    // 士気
#define SJIS_DEFENSE  "\x8f\xe9\x96\x68"                    // 城防
#define SJIS_FALL     "\x8f\xe9\x8a\x74\x97\x8e"            // 城陥落
#define SJIS_WITHDRAW "\x93\x50\x91\xde"                    // 撤退

const char *kassen_command_name(KassenCommand cmd) {
    switch (cmd) {
        case KASSEN_CMD_CHARGE:  return SJIS_CHARGE;
        case KASSEN_CMD_VOLLEY:  return SJIS_VOLLEY;
        case KASSEN_CMD_DUEL:    return SJIS_DUEL;
        case KASSEN_CMD_RETREAT: return SJIS_RETREAT;
        case KASSEN_CMD_HOLD:    return SJIS_HOLD;
        default:                 return "";
    }
}

// The defensive command set swaps 退却 for 籠城 (the garrison cannot withdraw).
const char *kassen_defense_name(KassenCommand cmd) {
    switch (cmd) {
        case KASSEN_CMD_CHARGE:  return SJIS_CHARGE;
        case KASSEN_CMD_VOLLEY:  return SJIS_VOLLEY;
        case KASSEN_CMD_DUEL:    return SJIS_DUEL;
        case KASSEN_CMD_HOLD:    return SJIS_HOLD;
        default:                 return "";
    }
}

const char *kassen_command_ascii(KassenCommand cmd) {
    switch (cmd) {
        case KASSEN_CMD_CHARGE:  return "CHARGE";
        case KASSEN_CMD_VOLLEY:  return "VOLLEY";
        case KASSEN_CMD_DUEL:    return "DUEL";
        case KASSEN_CMD_RETREAT: return "RETREAT";
        case KASSEN_CMD_HOLD:    return "HOLD";
        default:                 return "NONE";
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t kassen_rand(KassenState *ks) {
    uint32_t x = ks->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 0x9e3779b9u;
    ks->rng = x;
    return x;
}

static void push_log(KassenState *ks, const char *fmt, ...) {
    if (ks->log_count >= KASSEN_LOG_LINES) {
        for (int i = 1; i < KASSEN_LOG_LINES; i++) {
            memcpy(ks->log[i - 1], ks->log[i], KASSEN_LOG_LEN);
        }
        ks->log_count = KASSEN_LOG_LINES - 1;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ks->log[ks->log_count], KASSEN_LOG_LEN, fmt, ap);
    va_end(ap);
    ks->log[ks->log_count][KASSEN_LOG_LEN - 1] = '\0';
    ks->log_count++;
}

static void clamp_troops(int *v) {
    if (*v < 0) *v = 0;
}

static void clamp_morale(int *v) {
    if (*v < 0) *v = 0;
    if (*v > MAX_CASTLE_MORALE) *v = MAX_CASTLE_MORALE;
}

static int find_defender(const Scenario *sc, int castle_id, int faction_id) {
    int lord = sc->castles[castle_id].lord_officer_id;
    if (lord >= 0 && lord < sc->num_officers) {
        const ScenarioOfficer *o = &sc->officers[lord];
        if (o->is_active && o->faction_id == faction_id &&
            o->castle_id == castle_id) {
            return lord;
        }
    }
    for (int i = 0; i < sc->num_officers; i++) {
        const ScenarioOfficer *o = &sc->officers[i];
        if (o->is_active && o->faction_id == faction_id &&
            o->castle_id == castle_id) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Battle lifecycle
// ---------------------------------------------------------------------------

bool kassen_can_attack(const Scenario *sc, int from_castle, int to_castle,
                       int faction_id) {
    if (!sc) return false;
    if (from_castle < 0 || from_castle >= sc->num_castles) return false;
    if (to_castle < 0 || to_castle >= sc->num_castles) return false;
    if (from_castle == to_castle) return false;

    const ScenarioCastle *from = &sc->castles[from_castle];
    const ScenarioCastle *to = &sc->castles[to_castle];
    if (from->faction_id != faction_id) return false;
    if (to->faction_id == faction_id) return false;

    for (int i = 0; i < from->num_conns; i++) {
        if (from->conns[i] == to_castle) return true;
    }
    return false;
}

int kassen_list_targets(const Scenario *sc, int from_castle, int faction_id,
                        int *out, int max_out) {
    if (!sc || !out || max_out <= 0) return 0;
    if (from_castle < 0 || from_castle >= sc->num_castles) return 0;

    const ScenarioCastle *from = &sc->castles[from_castle];
    int n = 0;
    for (int i = 0; i < from->num_conns && n < max_out; i++) {
        int to = from->conns[i];
        if (to < 0 || to >= sc->num_castles) continue;
        if (sc->castles[to].faction_id == faction_id) continue;
        out[n++] = to;
    }
    return n;
}

void kassen_begin(KassenState *ks, Scenario *sc, int from_castle, int to_castle,
                  int attacker_officer) {
    if (!ks || !sc) return;
    memset(ks, 0, sizeof(*ks));

    ks->active = true;
    ks->from_castle = from_castle;
    ks->to_castle = to_castle;
    ks->attacker_officer = attacker_officer;
    ks->duel_winner = -1;

    const ScenarioCastle *from = &sc->castles[from_castle];
    const ScenarioCastle *to = &sc->castles[to_castle];

    // Field army: 3/4 of the garrison, but commit everything for tiny garrisons.
    int army = from->troops * 3 / 4;
    if (army < 80) army = from->troops;
    if (army < 0) army = 0;
    ks->att_troops = army;
    ks->home_troops = from->troops - army;

    ks->def_troops = to->troops;
    ks->att_morale = from->morale;
    ks->def_morale = to->morale;
    ks->defense = to->defense;
    ks->bg = (to_castle * 7 + sc->month) % 4;
    ks->rng = (uint32_t)(from_castle * 7919 + to_castle * 104729 +
                         attacker_officer * 31 + sc->month * 13 + 1);

    ks->defender_officer = find_defender(sc, to_castle, to->faction_id);

    const char *att_name = (attacker_officer >= 0)
                               ? sc->officers[attacker_officer].full_name_sjis
                               : from->name_sjis;
    const char *def_name = (ks->defender_officer >= 0)
                               ? sc->officers[ks->defender_officer].full_name_sjis
                               : to->name_sjis;

    push_log(ks, SJIS_ATTACK " %s  VS  " SJIS_DEFEND " %s", att_name, def_name);
    push_log(ks, SJIS_TROOPS " %d  :  %d", ks->att_troops, ks->def_troops);
}

void kassen_begin_defense(KassenState *ks, Scenario *sc, int from_castle,
                          int to_castle, int attacker_officer) {
    kassen_begin(ks, sc, from_castle, to_castle, attacker_officer);
    if (ks) ks->player_defends = true;
}

static void check_finished(KassenState *ks) {
    if (ks->def_troops <= 0 || ks->defense <= 0) {
        ks->finished = true;
        ks->victory = true;
        push_log(ks, SJIS_FALL "  " SJIS_VICTORY);
    } else if (ks->att_troops <= 0) {
        ks->finished = true;
        ks->victory = false;
        push_log(ks, SJIS_ATTACK " " SJIS_DEFEAT);
    }
}

bool kassen_command(KassenState *ks, Scenario *sc, KassenCommand cmd) {
    if (!ks || !sc || !ks->active || ks->finished) return false;

    ks->turn++;
    ks->duel_active = false;
    ks->duel_winner = -1;

    const ScenarioOfficer *a = (ks->attacker_officer >= 0)
                                   ? &sc->officers[ks->attacker_officer] : NULL;
    const ScenarioOfficer *d = (ks->defender_officer >= 0)
                                   ? &sc->officers[ks->defender_officer] : NULL;

    // V9 / 8eba: low-strength units decay before the exchange.  The original
    // applies this per 55-byte unit record; the single-stack battle uses the
    // same rule on each side's aggregate troop count.
    ks->att_morale = kassen_original_low_troop_decay(ks->att_morale,
                                                     ks->att_troops);
    ks->def_morale = kassen_original_low_troop_decay(ks->def_morale,
                                                     ks->def_troops);

    int a_lead = a ? a->lead : 40;
    int a_war = a ? a->war : 40;
    int a_intel = a ? a->intel : 40;
    int d_lead = d ? d->lead : 40;
    int d_war = d ? d->war : 40;

    int att_power = a_lead + a_war + ks->att_morale / 8 + ks->att_troops / 100;
    int def_power = d_lead + d_war + ks->defense / 12 + ks->def_morale / 8 +
                    ks->def_troops / 100;

    uint32_t r = kassen_rand(ks);
    int att_loss = 0;
    int def_loss = 0;

    switch (cmd) {
        case KASSEN_CMD_CHARGE:
            att_loss = 25 + def_power / 3 + (int)(r % 25);
            def_loss = 55 + att_power / 2 + (int)((r >> 8) % 35);
            ks->defense -= 18 + att_power / 15;
            ks->att_morale += 2;
            ks->def_morale -= 5;
            push_log(ks, SJIS_CHARGE "  " SJIS_ATTACK " -%d  " SJIS_DEFEND " -%d",
                     att_loss, def_loss);
            break;

        case KASSEN_CMD_VOLLEY:
            att_loss = 12 + def_power / 6 + (int)(r % 15);
            def_loss = 20 + att_power / 3 + a_intel / 2 + (int)((r >> 8) % 25);
            ks->defense -= 10 + a_intel / 12;
            ks->def_morale -= 3;
            push_log(ks, SJIS_VOLLEY "  " SJIS_ATTACK " -%d  " SJIS_DEFEND " -%d",
                     att_loss, def_loss);
            break;

        case KASSEN_CMD_DUEL: {
            int a_roll = a_war * 3 + (int)(r % 50);
            int d_roll = d_war * 3 + (int)((r >> 8) % 50);
            ks->duel_active = true;
            ks->duel_bg = (ks->turn & 1);
            ks->defense -= 8;

            if (a_roll >= d_roll) {
                ks->duel_winner = 0;
                def_loss = 60 + a_war + (int)((r >> 16) % 40);
                att_loss = 30 + d_war / 2;
                ks->att_morale += 18;
                ks->def_morale -= 22;
                push_log(ks, SJIS_DUEL "  " SJIS_ATTACK " " SJIS_VICTORY);
            } else {
                ks->duel_winner = 1;
                att_loss = 60 + d_war + (int)((r >> 16) % 40);
                def_loss = 30 + a_war / 2;
                ks->def_morale += 18;
                ks->att_morale -= 22;
                push_log(ks, SJIS_DUEL "  " SJIS_DEFEND " " SJIS_VICTORY);
            }
            break;
        }

        case KASSEN_CMD_RETREAT:
            ks->finished = true;
            ks->retreated = true;
            ks->victory = false;
            ks->last_att_loss = 0;
            ks->last_def_loss = 0;
            push_log(ks, SJIS_RETREAT "  " SJIS_ATTACK " " SJIS_WITHDRAW);
            return false;

        default:
            return true;
    }

    clamp_troops(&att_loss);
    clamp_troops(&def_loss);
    if (att_loss > ks->att_troops) att_loss = ks->att_troops;
    if (def_loss > ks->def_troops) def_loss = ks->def_troops;

    ks->last_att_loss = att_loss;
    ks->last_def_loss = def_loss;
    ks->att_troops -= att_loss;
    ks->def_troops -= def_loss;

    if (ks->defense < 0) ks->defense = 0;
    clamp_morale(&ks->att_morale);
    clamp_morale(&ks->def_morale);

    check_finished(ks);
    return !ks->finished;
}

// ---------------------------------------------------------------------------
// AI heuristics + defensive battles
// ---------------------------------------------------------------------------

KassenCommand kassen_ai_attack_decide(const KassenState *ks, const Scenario *sc) {
    (void)sc;
    if (!ks) return KASSEN_CMD_CHARGE;
    if (ks->att_troops <= 40) return KASSEN_CMD_RETREAT;
    // Well-fortified walls favour a ranged approach; a weak defence invites a
    // decisive charge.  The pattern is deterministic in (turn, castle ids).
    int r = (ks->turn * 7 + ks->from_castle * 3 + ks->to_castle) % 10;
    if (ks->defense > 350 || ks->att_troops < ks->def_troops) {
        return (r < 7) ? KASSEN_CMD_VOLLEY : KASSEN_CMD_CHARGE;
    }
    if (r < 5) return KASSEN_CMD_CHARGE;
    if (r < 8) return KASSEN_CMD_VOLLEY;
    return KASSEN_CMD_DUEL;
}

KassenCommand kassen_ai_defend_decide(const KassenState *ks, const Scenario *sc) {
    (void)sc;
    if (!ks) return KASSEN_CMD_HOLD;
    if (ks->defense < 160 && ks->def_troops > ks->att_troops) {
        return KASSEN_CMD_CHARGE;   // sortie while we still outnumber them
    }
    int r = (ks->turn * 5 + ks->to_castle) % 10;
    if (r < 5) return KASSEN_CMD_VOLLEY;
    if (r < 7) return KASSEN_CMD_DUEL;
    return KASSEN_CMD_HOLD;
}

// Resolve the defending side's chosen tactic (the attacker already acted).
static void kassen_defender_action(KassenState *ks, Scenario *sc,
                                   KassenCommand cmd) {
    if (!ks || !sc || ks->finished) return;
    if (cmd != KASSEN_CMD_CHARGE && cmd != KASSEN_CMD_VOLLEY &&
        cmd != KASSEN_CMD_DUEL && cmd != KASSEN_CMD_HOLD) {
        cmd = KASSEN_CMD_HOLD;
    }

    const ScenarioOfficer *a = (ks->attacker_officer >= 0)
                                   ? &sc->officers[ks->attacker_officer] : NULL;
    const ScenarioOfficer *d = (ks->defender_officer >= 0)
                                   ? &sc->officers[ks->defender_officer] : NULL;
    int a_war = a ? a->war : 40;
    int a_lead = a ? a->lead : 40;
    int d_war = d ? d->war : 40;
    int d_lead = d ? d->lead : 40;
    int d_intel = d ? d->intel : 40;

    int def_power = d_lead + d_war + ks->defense / 12 + ks->def_morale / 8 +
                    ks->def_troops / 100;
    int att_power = a_lead + a_war + ks->att_morale / 8 + ks->att_troops / 100;

    uint32_t r = kassen_rand(ks);
    int att_loss = 0;
    int def_loss = 0;
    ks->duel_active = false;
    ks->duel_winner = -1;

    switch (cmd) {
        case KASSEN_CMD_HOLD:
            ks->defense += 6;
            ks->def_morale += 5;
            clamp_morale(&ks->def_morale);
            push_log(ks, SJIS_HOLD "  " SJIS_DEFEND " +6");
            break;

        case KASSEN_CMD_CHARGE:
            att_loss = 45 + def_power / 3 + (int)(r % 25);
            def_loss = 15 + att_power / 5 + (int)((r >> 8) % 15);
            ks->att_morale -= 4;
            push_log(ks, SJIS_CHARGE "  " SJIS_ATTACK " -%d  " SJIS_DEFEND " -%d",
                     att_loss, def_loss);
            break;

        case KASSEN_CMD_VOLLEY:
            att_loss = 25 + def_power / 4 + d_intel / 2 + (int)(r % 20);
            def_loss = 6 + att_power / 8 + (int)((r >> 8) % 10);
            ks->att_morale -= 3;
            push_log(ks, SJIS_VOLLEY "  " SJIS_ATTACK " -%d  " SJIS_DEFEND " -%d",
                     att_loss, def_loss);
            break;

        case KASSEN_CMD_DUEL: {
            int a_roll = a_war * 3 + (int)(r % 50);
            int d_roll = d_war * 3 + (int)((r >> 8) % 50);
            ks->duel_active = true;
            ks->duel_bg = ((ks->turn + 1) & 1);
            if (d_roll >= a_roll) {
                ks->duel_winner = 1;
                att_loss = 70 + d_war + (int)((r >> 16) % 40);
                def_loss = 20 + a_war / 2;
                ks->def_morale += 18;
                ks->att_morale -= 22;
                push_log(ks, SJIS_DUEL "  " SJIS_DEFEND " " SJIS_VICTORY);
            } else {
                ks->duel_winner = 0;
                def_loss = 70 + a_war + (int)((r >> 16) % 40);
                att_loss = 20 + d_war / 2;
                ks->att_morale += 18;
                ks->def_morale -= 22;
                push_log(ks, SJIS_DUEL "  " SJIS_ATTACK " " SJIS_VICTORY);
            }
            break;
        }

        default:
            return;
    }

    if (att_loss > ks->att_troops) att_loss = ks->att_troops;
    if (def_loss > ks->def_troops) def_loss = ks->def_troops;
    if (att_loss < 0) att_loss = 0;
    if (def_loss < 0) def_loss = 0;

    ks->last_att_loss = att_loss;
    ks->last_def_loss = def_loss;
    ks->att_troops -= att_loss;
    ks->def_troops -= def_loss;
    clamp_morale(&ks->att_morale);
    clamp_morale(&ks->def_morale);
    check_finished(ks);
}

bool kassen_defense_round(KassenState *ks, Scenario *sc, KassenCommand def_cmd) {
    if (!ks || !sc || !ks->active || ks->finished) return false;

    // 1. The AI attacker commits a tactic (this also advances the turn).
    KassenCommand ai = kassen_ai_attack_decide(ks, sc);
    kassen_command(ks, sc, ai);
    if (ks->finished) return false;

    // 2. The human answers with a defensive command.
    kassen_defender_action(ks, sc, def_cmd);
    return !ks->finished;
}

void kassen_apply_result(KassenState *ks, Scenario *sc) {
    if (!ks || !sc || !ks->active || !ks->finished) return;

    ScenarioCastle *from = &sc->castles[ks->from_castle];
    ScenarioCastle *to = &sc->castles[ks->to_castle];

    if (!ks->victory) {
        // Survivors return home (half of them on a rout, everyone on retreat).
        int survivors = ks->retreated ? ks->att_troops : ks->att_troops / 2;
        castle_set_troops(from, ks->home_troops + survivors);
        if (from->troops < 20) castle_set_troops(from, 20);
        ks->active = false;
        return;
    }

    int old_faction = to->faction_id;

    // The defending commander falls back to another friendly castle, or is
    // removed from the roster when the clan has no castle left.
    if (ks->defender_officer >= 0) {
        ScenarioOfficer *def = &sc->officers[ks->defender_officer];
        int refuge = -1;
        for (int i = 0; i < sc->num_castles; i++) {
            if (i == ks->to_castle) continue;
            if (sc->castles[i].faction_id == old_faction) {
                refuge = i;
                break;
            }
        }
        if (refuge >= 0) {
            def->castle_id = refuge;
        } else {
            def->is_active = false;
            def->castle_id = -1;
            def->troops = 0;
        }
    }

    // Castle changes hands.
    to->faction_id = from->faction_id;
    to->lord_officer_id = ks->attacker_officer;
    castle_set_troops(to, ks->att_troops);
    castle_set_morale(to, ks->att_morale);
    if (to->defense > 0) {
        to->defense = ks->defense;
    } else {
        to->defense = 100;
    }
    if (to->defense < 100) to->defense = 100;

    // The attacking officer garrisons the captured castle.
    if (ks->attacker_officer >= 0) {
        ScenarioOfficer *att = &sc->officers[ks->attacker_officer];
        att->castle_id = ks->to_castle;
        att->faction_id = to->faction_id;
        att->troops = ks->att_troops;
    }

    // Keep some troops at home; if the lord marched out, appoint a new lord.
    castle_set_troops(from, ks->home_troops);
    if (from->troops < 20) castle_set_troops(from, 20);
    if (from->lord_officer_id == ks->attacker_officer) {
        int new_lord = -1;
        for (int i = 0; i < sc->num_officers; i++) {
            const ScenarioOfficer *o = &sc->officers[i];
            if (o->is_active && o->faction_id == from->faction_id &&
                o->castle_id == ks->from_castle) {
                new_lord = i;
                break;
            }
        }
        if (new_lord >= 0) from->lord_officer_id = new_lord;
    }

    ks->active = false;
}

bool kassen_auto_resolve(Scenario *sc, int from_castle, int to_castle,
                         int attacker_officer, KassenState *out) {
    if (!sc) return false;

    KassenState ks;
    kassen_begin(&ks, sc, from_castle, to_castle, attacker_officer);

    int guard = 0;
    while (!ks.finished && guard++ < 40) {
        KassenCommand ai = kassen_ai_attack_decide(&ks, sc);
        kassen_command(&ks, sc, ai);
        if (ks.finished) break;
        kassen_defender_action(&ks, sc, kassen_ai_defend_decide(&ks, sc));
    }

    kassen_apply_result(&ks, sc);
    if (out) *out = ks;
    return ks.victory;
}

bool kassen_is_unified(const Scenario *sc) {
    if (!sc || sc->num_castles <= 0) return false;
    int faction = sc->castles[0].faction_id;
    for (int i = 1; i < sc->num_castles; i++) {
        if (sc->castles[i].faction_id != faction) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Unit sprite bank (子任务 10 part 2)
//
// Hunitpat.gp is a raw PC-98 4-plane sheet: 151936 B = 1187 tiles of 16x16
// pixels, 128 bytes each (4 planes x 2 bytes/row x 16 rows).  The bank is
// decoded once and the battlefield paints a small formation of unit sprites
// for each side; the number of sprites scales with the troop count.
// ---------------------------------------------------------------------------

#define UNIT_COLS        4      // sprites per row
#define UNIT_ROWS        3      // rows per side
#define UNIT_STEP        26     // pixel spacing between sprites
#define UNIT_ATK_X       150    // left edge of the attacking formation
#define UNIT_DEF_X       466    // left edge of the defending formation
#define UNIT_TOP_Y       60
#define UNIT_TROOPS_PER  50     // one sprite per this many troops

static RawSprite s_unit_sheet;     // the whole 24 px wide sheet
static bool      s_unit_sheet_ok;

bool kassen_units_load(const char *path) {
    kassen_units_free();
    if (!path) return false;
    if (!raw_sprite_load_sheet(path, &s_unit_sheet))
        return false;
    s_unit_sheet_ok = true;
    return true;
}

void kassen_units_free(void) {
    if (s_unit_sheet_ok) {
        free(s_unit_sheet.pixels);
        s_unit_sheet.pixels = NULL;
        s_unit_sheet.width = 0;
        s_unit_sheet.height = 0;
        s_unit_sheet_ok = false;
    }
}

int kassen_units_loaded(void) {
    return s_unit_sheet_ok ? (int)s_unit_sheet.height : 0;
}

int kassen_unit_formation(int troops, bool attacker, int officer,
                           KassenUnitSprite *out, int max_out) {
    if (!out || max_out <= 0) return 0;
    if (troops < 1) troops = 1;

    // Sprite count scales with the troop strength.
    int count = (troops + UNIT_TROOPS_PER - 1) / UNIT_TROOPS_PER;
    if (count < 1) count = 1;
    if (count > KASSEN_MAX_UNITS) count = KASSEN_MAX_UNITS;
    if (count > max_out) count = max_out;

    const int rows = kassen_units_loaded();
    const int base_x = attacker ? UNIT_ATK_X : UNIT_DEF_X;

    for (int i = 0; i < count; i++) {
        const int col = i % UNIT_COLS;
        const int row = i / UNIT_COLS;
        out[i].x = base_x + col * UNIT_STEP;
        out[i].y = UNIT_TOP_Y + row * UNIT_STEP;
        out[i].attacker = attacker;
        // The sheet holds one 24x32 unit sprite per faction (twelve in
        // total, on a 36-row pitch), so the commander picks the faction art
        // and the formation slot varies the frame.
        int top = 0;
        if (rows > 0) {
            const int types = rows / RAW_SPRITE_UNIT_PITCH;
            const int n_types = (types > 0) ? types : 1;
            int type = attacker ? (officer % n_types)
                                : ((officer * 5 + 7) % n_types);
            if (type < 0) type += n_types;
            top = type * RAW_SPRITE_UNIT_PITCH;
        }
        out[i].tile = top;
    }
    return count;
}

// Extract the 24x32 unit sprite that starts at row `top` of the sheet.
static const uint8_t *unit_sprite_pixels(int top) {
    if (!s_unit_sheet_ok || !s_unit_sheet.pixels)
        return NULL;
    if (top < 0 || top + KASSEN_UNIT_TILE_H > (int)s_unit_sheet.height)
        return NULL;
    return s_unit_sheet.pixels + (size_t)top * s_unit_sheet.width;
}

void kassen_units_draw(int att_troops, int def_troops,
                       int att_officer, int def_officer) {
    if (!s_unit_sheet_ok || !s_unit_sheet.pixels) return;

    KassenUnitSprite sprites[KASSEN_MAX_UNITS];

    int n = kassen_unit_formation(att_troops, true, att_officer,
                                  sprites, KASSEN_MAX_UNITS);
    for (int i = 0; i < n; i++) {
        const uint8_t *px = unit_sprite_pixels(sprites[i].tile);
        if (!px) continue;
        hal_video_blit_transparent(sprites[i].x, sprites[i].y,
                                   KASSEN_UNIT_TILE_W, KASSEN_UNIT_TILE_H,
                                   px, KASSEN_UNIT_TILE_W, 0);
    }

    n = kassen_unit_formation(def_troops, false, def_officer,
                               sprites, KASSEN_MAX_UNITS);
    for (int i = 0; i < n; i++) {
        const uint8_t *px = unit_sprite_pixels(sprites[i].tile);
        if (!px) continue;
        hal_video_blit_transparent(sprites[i].x, sprites[i].y,
                                   KASSEN_UNIT_TILE_W, KASSEN_UNIT_TILE_H,
                                   px, KASSEN_UNIT_TILE_W, 0);
    }
}

// ---------------------------------------------------------------------------
// V9: original battlefield helpers (43B3 group)
// ---------------------------------------------------------------------------

#include "kassen_original_templates.h"

const uint8_t *kassen_original_template(int index, int *size_out) {
    if (index < 0 || index >= KASSEN_ORIGINAL_TEMPLATE_COUNT) return NULL;
    if (size_out) *size_out = KASSEN_ORIGINAL_TEMPLATE_SIZE;
    return kassen_original_templates[index];
}

int kassen_original_low_troop_decay(int morale, int troops) {
    if (morale <= 0) return 0;
    if (troops == 0) morale -= morale / 10;
    else if (troops < 10) morale -= morale / 50;
    if (morale < 0) morale = 0;
    return morale;
}

static uint32_t original_roll(uint32_t *rng, uint32_t limit) {
    if (!rng || limit == 0) return 0;
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 0x9e3779b9u;
    *rng = x;
    return x % limit;
}

bool kassen_original_target_choice(const KassenOriginalUnit *actor,
                                   const KassenOriginalUnit *target,
                                   int flag, const int side_power[2],
                                   uint32_t *rng) {
    if (!actor || !target || !side_power || !rng) return false;

    int a_side = (actor->side != 0) ? 1 : 0;
    int own = side_power[a_side];
    int enemy = side_power[1 - a_side];
    int threshold;

    // The first block replaces the opposite-side strength with either 0 or 60.
    if (actor->hp * 3 > target->hp &&
        (enemy >> 1) < own &&
        target->action == 5 &&
        (enemy * 2 <= own) &&
        actor->hp <= target->hp) {
        threshold = 60;
    } else {
        threshold = 0;
    }

    // 8c70's type score is 2*stat31 + stat42; flag==0 doubles both scores.
    int actor_type_score = actor->stat31 * 2 + actor->stat42;
    int target_type_score = target->stat31 * 2 + target->stat42;
    if (flag == 0) {
        actor_type_score *= 2;
        target_type_score *= 2;
    }

    int roll = (int)original_roll(rng, 200) + target_type_score;
    int base;
    if (actor->leader_type == actor->type) {
        if (actor->stat42 <= 70) return false;
        base = (flag == 1) ? 0x28 : 0x3c;
        base += threshold + actor_type_score;
    } else {
        if (actor->stat42 <= 50) return false;
        base = (flag == 1) ? 0x3c : 0x50;
        base += threshold * 2 + actor_type_score;
    }
    return base > roll;
}

int kassen_original_side_average_troops(const KassenOriginalUnit *units,
                                        int count) {
    if (!units || count <= 0) return 0;
    int total = 0;
    int active = 0;
    for (int i = 0; i < count; i++) {
        if (units[i].status == 0) continue;
        total += units[i].troops;
        active++;
    }
    return active ? total / active : 0;
}

int kassen_original_side_reserve_hp(const KassenOriginalUnit *units,
                                    int count) {
    if (!units || count <= 0) return 0;
    int total = 0;
    for (int i = 5; i < count; i++) {
        if (units[i].status == 0) continue;
        total += units[i].hp;
    }
    return total;
}

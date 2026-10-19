#include "kassen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// SJIS labels (PC-98 FONT.CHR has no usable lower-case ASCII)
// ---------------------------------------------------------------------------

#define SJIS_CHARGE   "\x93\xcb\x8c\x82"                    // 突撃
#define SJIS_VOLLEY   "\x90\xc4\x8e\xcb"                    // 斉射
#define SJIS_DUEL     "\x88\xea\x8b\x52\x93\xa2"            // 一騎討
#define SJIS_RETREAT  "\x91\xde\x8b\x70"                    // 退却
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
        default:                 return "";
    }
}

const char *kassen_command_ascii(KassenCommand cmd) {
    switch (cmd) {
        case KASSEN_CMD_CHARGE:  return "CHARGE";
        case KASSEN_CMD_VOLLEY:  return "VOLLEY";
        case KASSEN_CMD_DUEL:    return "DUEL";
        case KASSEN_CMD_RETREAT: return "RETREAT";
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
    if (*v > 300) *v = 300;
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

void kassen_apply_result(KassenState *ks, Scenario *sc) {
    if (!ks || !sc || !ks->active || !ks->finished) return;

    ScenarioCastle *from = &sc->castles[ks->from_castle];
    ScenarioCastle *to = &sc->castles[ks->to_castle];

    if (!ks->victory) {
        // Survivors return home (half of them on a rout, everyone on retreat).
        int survivors = ks->retreated ? ks->att_troops : ks->att_troops / 2;
        from->troops = ks->home_troops + survivors;
        if (from->troops < 20) from->troops = 20;
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
    to->troops = ks->att_troops;
    to->morale = ks->att_morale;
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
    from->troops = ks->home_troops;
    if (from->troops < 20) from->troops = 20;
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

bool kassen_is_unified(const Scenario *sc) {
    if (!sc || sc->num_castles <= 0) return false;
    int faction = sc->castles[0].faction_id;
    for (int i = 1; i < sc->num_castles; i++) {
        if (sc->castles[i].faction_id != faction) return false;
    }
    return true;
}

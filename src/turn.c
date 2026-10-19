#include "turn.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Domestic commands + monthly settlement
// ---------------------------------------------------------------------------

#define SJIS_DEVELOP   "\x8a\x4a\x94\xad"  // 開発
#define SJIS_RECRUIT   "\x92\xa5\x95\xba"  // 徴兵
#define SJIS_TAX       "\x92\xa5\x90\xc5"  // 徴税
#define SJIS_MOVE      "\x88\xda\x93\xae"  // 移動
#define SJIS_ATTACK    "\x8f\x6f\x90\x77"  // 出陣
#define SJIS_OFFICERS  "\x95\x90\x8f\xab"  // 武将
#define SJIS_END       "\x8f\x49\x97\xb9"  // 終了

#define SJIS_RICE      "\x90\xce\x8d\x82"  // 石高
#define SJIS_TROOPS    "\x95\xba\x97\xcd"  // 兵力
#define SJIS_GOLD      "\x8b\xe0"          // 金
#define SJIS_PROVISION "\x95\xba\x97\xc6"  // 兵糧
#define SJIS_MORALE    "\x8e\x6d\x8b\x43"  // 士気
#define SJIS_DEFENSE   "\x8f\xe9\x96\x68"  // 城防
#define SJIS_DONE      "\x8a\xae\x97\xb9"  // 完了
#define SJIS_NO_NEIGH  "\x97\xd7\x90\xda\x8f\xe9\x82\xc8\x82\xb5" // 隣接城なし
#define SJIS_NO_OFF    "\x91\xce\x8f\xdb\x95\x90\x8f\xab\x82\xc8\x82\xb5" // 対象武将なし
#define SJIS_ALL_ACTED "\x91\x53\x88\xf5 \x8d\x73\x93\xae\x8d\xcf"        // 全員 行動済
#define SJIS_NO_GOLD   "\x8b\xe0\x95\x73\x91\xab"                        // 金不足
#define SJIS_NO_RICE   "\x95\xba\x97\xc6\x95\x73\x91\xab"                // 兵糧不足
#define SJIS_LORD_ONLY "\x91\xe5\x96\xbc \x82\xcc \x8f\xe9 \x82\xcc\x82\xdd" // 大名 の 城 のみ
#define SJIS_END_TITLE "\x95\x5d\x92\xe8\x8f\x49\x97\xb9"                // 評定終了
#define SJIS_INCOME    "\x8e\xfb\x93\xfc"                                // 収入
#define SJIS_YEAR      "\x94\x4e"                                        // 年
#define SJIS_MONTH     "\x8c\x8e"                                        // 月
#define SJIS_ROSTER    "\x95\x90\x8f\xab\x88\xea\x97\x97"                // 武将一覧

const char* turn_command_name(TurnCommand cmd) {
    switch (cmd) {
        case TURN_CMD_DEVELOP:  return SJIS_DEVELOP;
        case TURN_CMD_RECRUIT:  return SJIS_RECRUIT;
        case TURN_CMD_TAX:      return SJIS_TAX;
        case TURN_CMD_MOVE:     return SJIS_MOVE;
        case TURN_CMD_ATTACK:   return SJIS_ATTACK;
        case TURN_CMD_OFFICERS: return SJIS_OFFICERS;
        case TURN_CMD_END:      return SJIS_END;
        default:                return "";
    }
}

void turn_init(TurnState *ts, const Scenario *sc, int faction_id) {
    if (!ts) return;
    memset(ts, 0, sizeof(*ts));
    ts->player_faction = faction_id;
    ts->year = sc ? sc->year : 1180;
    ts->month = sc ? sc->month : 9;
}

void turn_reset_actions(TurnState *ts) {
    if (!ts) return;
    memset(ts->acted, 0, sizeof(ts->acted));
}

bool turn_officer_has_acted(const TurnState *ts, int officer_id) {
    if (!ts || officer_id < 0 || officer_id >= MAX_OFFICERS) return false;
    return ts->acted[officer_id] != 0;
}

static bool officer_serves_at(const ScenarioOfficer *o, int faction_id, int castle_id) {
    return o->is_active && o->faction_id == faction_id && o->castle_id == castle_id;
}

int turn_find_idle_officer_faction(const Scenario *sc, const TurnState *ts,
                                   int faction_id, int castle_id) {
    if (!sc || !ts || castle_id < 0 || castle_id >= sc->num_castles) return -1;

    // The castle lord gets first crack at domestic affairs, then by officer id.
    int lord = sc->castles[castle_id].lord_officer_id;
    if (lord >= 0 && lord < sc->num_officers) {
        const ScenarioOfficer *o = &sc->officers[lord];
        if (officer_serves_at(o, faction_id, castle_id) &&
            !turn_officer_has_acted(ts, lord)) {
            return lord;
        }
    }

    for (int i = 0; i < sc->num_officers; i++) {
        const ScenarioOfficer *o = &sc->officers[i];
        if (officer_serves_at(o, faction_id, castle_id) &&
            !turn_officer_has_acted(ts, i)) {
            return i;
        }
    }
    return -1;
}

int turn_find_idle_officer(const Scenario *sc, const TurnState *ts, int castle_id) {
    return turn_find_idle_officer_faction(sc, ts, ts ? ts->player_faction : 0,
                                          castle_id);
}

int turn_list_castle_officers(const Scenario *sc, int faction_id, int castle_id,
                              int *ids, int max_ids) {
    if (!sc || !ids || max_ids <= 0) return 0;
    int n = 0;
    for (int i = 0; i < sc->num_officers && n < max_ids; i++) {
        if (officer_serves_at(&sc->officers[i], faction_id, castle_id)) {
            ids[n++] = i;
        }
    }
    return n;
}

int turn_list_move_targets(const Scenario *sc, int from_castle, int faction_id,
                           int *out, int max_out) {
    if (!sc || !out || max_out <= 0) return 0;
    if (from_castle < 0 || from_castle >= sc->num_castles) return 0;

    const ScenarioCastle *from = &sc->castles[from_castle];
    int n = 0;
    for (int i = 0; i < from->num_conns && n < max_out; i++) {
        int to = from->conns[i];
        if (to < 0 || to >= sc->num_castles) continue;
        if (sc->castles[to].faction_id == faction_id) out[n++] = to;
    }
    return n;
}

int turn_find_attack_officer_faction(const Scenario *sc, const TurnState *ts,
                                     int faction_id, int castle_id) {
    if (!sc || !ts || castle_id < 0 || castle_id >= sc->num_castles) return -1;

    int best = -1;
    int best_war = -1;
    for (int i = 0; i < sc->num_officers; i++) {
        const ScenarioOfficer *o = &sc->officers[i];
        if (!officer_serves_at(o, faction_id, castle_id)) continue;
        if (turn_officer_has_acted(ts, i)) continue;
        if (o->war > best_war) {
            best_war = o->war;
            best = i;
        }
    }
    if (best >= 0) return best;

    // Fall back to the lord even if flagged as acted (headless convenience).
    int lord = sc->castles[castle_id].lord_officer_id;
    if (lord >= 0 && lord < sc->num_officers &&
        officer_serves_at(&sc->officers[lord], faction_id, castle_id)) {
        return lord;
    }
    return -1;
}

int turn_find_attack_officer(const Scenario *sc, const TurnState *ts,
                             int castle_id) {
    return turn_find_attack_officer_faction(sc, ts, ts ? ts->player_faction : 0,
                                            castle_id);
}

bool turn_move_officer(TurnState *ts, Scenario *sc, int from_castle, int to_castle,
                       int officer_id, TurnResult *res) {
    if (!ts || !sc || !res) return false;

    memset(res, 0, sizeof(*res));
    res->cmd = TURN_CMD_MOVE;
    res->castle_id = from_castle;
    res->officer_id = -1;

    if (from_castle < 0 || from_castle >= sc->num_castles ||
        to_castle < 0 || to_castle >= sc->num_castles) {
        snprintf(res->text, sizeof(res->text), SJIS_NO_NEIGH);
        return false;
    }

    ScenarioCastle *from = &sc->castles[from_castle];
    ScenarioCastle *to = &sc->castles[to_castle];
    if (from->faction_id != ts->player_faction ||
        to->faction_id != ts->player_faction) {
        snprintf(res->text, sizeof(res->text), SJIS_LORD_ONLY);
        return false;
    }

    bool connected = false;
    for (int i = 0; i < from->num_conns; i++) {
        if (from->conns[i] == to_castle) { connected = true; break; }
    }
    if (!connected) {
        snprintf(res->text, sizeof(res->text), SJIS_NO_NEIGH);
        return false;
    }

    int oid = officer_id;
    if (oid < 0) oid = turn_find_idle_officer(sc, ts, from_castle);
    if (oid < 0 || oid >= sc->num_officers ||
        !officer_serves_at(&sc->officers[oid], ts->player_faction, from_castle)) {
        snprintf(res->text, sizeof(res->text), SJIS_NO_OFF);
        return false;
    }
    if (turn_officer_has_acted(ts, oid)) {
        res->officer_id = oid;
        snprintf(res->text, sizeof(res->text), "%s\n" SJIS_ALL_ACTED,
                 sc->officers[oid].full_name_sjis);
        return false;
    }

    // Transfer roughly a third of the garrison, keeping at least 20 behind.
    int troops = from->troops / 3;
    if (troops < 50) troops = from->troops / 2;
    if (troops > from->troops - 20) troops = from->troops - 20;
    if (troops < 0) troops = 0;

    from->troops -= troops;
    to->troops += troops;
    sc->officers[oid].castle_id = to_castle;
    ts->acted[oid] = 1;

    // If the lord left, appoint the next officer still stationed there.
    if (from->lord_officer_id == oid) {
        int new_lord = -1;
        for (int i = 0; i < sc->num_officers; i++) {
            const ScenarioOfficer *o = &sc->officers[i];
            if (i != oid && officer_serves_at(o, ts->player_faction, from_castle)) {
                new_lord = i;
                break;
            }
        }
        if (new_lord >= 0) from->lord_officer_id = new_lord;
    }

    res->officer_id = oid;
    res->ok = true;
    res->d_troops = troops;
    snprintf(res->text, sizeof(res->text),
             "%s\n" SJIS_MOVE " " SJIS_DONE "\n"
             SJIS_TROOPS " +%d  ->  %s",
             sc->officers[oid].full_name_sjis, troops, to->name_sjis);
    return true;
}

int turn_develop_cost(const ScenarioCastle *cs) {
    return cs ? 200 + cs->rice / 50 : 0;
}

int turn_recruit_gold_cost(const ScenarioCastle *cs) {
    return cs ? 300 + cs->troops / 2 : 0;
}

int turn_recruit_rice_cost(const ScenarioCastle *cs) {
    return cs ? 100 + cs->troops / 4 : 0;
}

int turn_recruit_cap(const ScenarioCastle *cs) {
    return cs ? 500 + cs->rice / 8 : 0;
}

// Month advance + per-castle settlement. Shared with TURN_CMD_END.
bool turn_end(TurnState *ts, Scenario *sc, TurnResult *res) {
    if (!ts || !sc || !res) return false;

    ts->month++;
    if (ts->month > 12) {
        ts->month = 1;
        ts->year++;
    }
    // Keep the scenario's calendar in sync: the HUD and every renderer read it.
    sc->year = ts->year;
    sc->month = ts->month;

    int player_gold = 0;
    int player_rice = 0;
    bool harvest = (ts->month % 3 == 0);

    for (int i = 0; i < sc->num_castles; i++) {
        ScenarioCastle *cs = &sc->castles[i];

        // Monthly tax income scales with the castle's kokudaka.
        int gold_gain = cs->rice / 50;
        if (gold_gain < 20) gold_gain = 20;
        cs->gold += gold_gain;

        // Quarterly harvest: rice (kokudaka) grows.
        int rice_gain = 0;
        if (harvest) {
            rice_gain = cs->rice / 60;
            if (rice_gain < 10) rice_gain = 10;
            cs->rice += rice_gain;
        }

        if (cs->faction_id == ts->player_faction) {
            player_gold += gold_gain;
            player_rice += rice_gain;
        }
    }

    turn_reset_actions(ts);

    res->cmd = TURN_CMD_END;
    res->ok = true;
    res->officer_id = -1;
    res->castle_id = -1;
    res->d_gold = player_gold;
    res->d_rice = player_rice;
    snprintf(res->text, sizeof(res->text),
             SJIS_END_TITLE "  %d" SJIS_YEAR "%d" SJIS_MONTH "\n"
             SJIS_INCOME "  " SJIS_GOLD " +%d   " SJIS_PROVISION " +%d\n"
             SJIS_ALL_ACTED " RESET",
             ts->year, ts->month, player_gold, player_rice);
    return true;
}

static bool run_domestic_command(TurnState *ts, Scenario *sc, int faction_id,
                                 int castle_id, TurnCommand cmd, int officer_id,
                                 TurnResult *res);

bool turn_run_command(TurnState *ts, Scenario *sc, int castle_id,
                      TurnCommand cmd, TurnResult *res) {
    return turn_run_command_officer(ts, sc, castle_id, cmd, -1, res);
}

bool turn_run_command_officer(TurnState *ts, Scenario *sc, int castle_id,
                              TurnCommand cmd, int officer_id, TurnResult *res) {
    if (!ts || !sc || !res) return false;

    memset(res, 0, sizeof(*res));
    res->cmd = cmd;
    res->castle_id = castle_id;
    res->officer_id = -1;

    if (castle_id < 0 || castle_id >= sc->num_castles) {
        snprintf(res->text, sizeof(res->text), SJIS_NO_OFF);
        return false;
    }

    ScenarioCastle *cs = &sc->castles[castle_id];
    if (cs->faction_id != ts->player_faction) {
        snprintf(res->text, sizeof(res->text), SJIS_LORD_ONLY);
        return false;
    }

    if (cmd == TURN_CMD_END) return turn_end(ts, sc, res);

    if (cmd == TURN_CMD_OFFICERS) {
        // Pure UI command: never consumes an officer action.
        snprintf(res->text, sizeof(res->text), SJIS_ROSTER);
        res->ok = true;
        return true;
    }

    return run_domestic_command(ts, sc, ts->player_faction, castle_id,
                                cmd, officer_id, res);
}

// Domestic command for an arbitrary faction (non-player AI). Rejects the
// player-only UI/military commands so the AI can never end the turn itself.
bool turn_run_faction_command(TurnState *ts, Scenario *sc, int faction_id,
                              int castle_id, TurnCommand cmd, int officer_id,
                              TurnResult *res) {
    if (!ts || !sc || !res) return false;

    memset(res, 0, sizeof(*res));
    res->cmd = cmd;
    res->castle_id = castle_id;
    res->officer_id = -1;

    if (castle_id < 0 || castle_id >= sc->num_castles) {
        snprintf(res->text, sizeof(res->text), SJIS_NO_OFF);
        return false;
    }
    if (cmd != TURN_CMD_DEVELOP && cmd != TURN_CMD_RECRUIT && cmd != TURN_CMD_TAX) {
        snprintf(res->text, sizeof(res->text), SJIS_LORD_ONLY);
        return false;
    }
    if (sc->castles[castle_id].faction_id != faction_id) {
        snprintf(res->text, sizeof(res->text), SJIS_LORD_ONLY);
        return false;
    }

    return run_domestic_command(ts, sc, faction_id, castle_id, cmd, officer_id, res);
}

// Shared DEVELOP / RECRUIT / TAX executor (used by the player and the AI).
static bool run_domestic_command(TurnState *ts, Scenario *sc, int faction_id,
                                 int castle_id, TurnCommand cmd, int officer_id,
                                 TurnResult *res) {
    ScenarioCastle *cs = &sc->castles[castle_id];

    int oid;
    if (officer_id >= 0) {
        if (officer_id >= sc->num_officers ||
            !officer_serves_at(&sc->officers[officer_id], faction_id, castle_id)) {
            snprintf(res->text, sizeof(res->text), SJIS_NO_OFF);
            return false;
        }
        if (turn_officer_has_acted(ts, officer_id)) {
            res->officer_id = officer_id;
            snprintf(res->text, sizeof(res->text), "%s\n" SJIS_ALL_ACTED,
                     sc->officers[officer_id].full_name_sjis);
            return false;
        }
        oid = officer_id;
    } else {
        oid = turn_find_idle_officer_faction(sc, ts, faction_id, castle_id);
        if (oid < 0) {
            snprintf(res->text, sizeof(res->text), SJIS_NO_OFF "\n" SJIS_ALL_ACTED);
            return false;
        }
    }

    const ScenarioOfficer *o = &sc->officers[oid];
    res->officer_id = oid;

    switch (cmd) {
        case TURN_CMD_DEVELOP: {
            int cost = turn_develop_cost(cs);
            if (cs->gold < cost) {
                snprintf(res->text, sizeof(res->text), "%s\n" SJIS_NO_GOLD, o->full_name_sjis);
                return false;
            }
            int rice_gain = 100 + o->politics * 3 + o->intel;
            int def_gain = 1 + o->politics / 25;
            cs->gold -= cost;
            cs->rice += rice_gain;
            cs->defense += def_gain;

            res->d_gold = -cost;
            res->d_rice = rice_gain;
            res->d_defense = def_gain;
            snprintf(res->text, sizeof(res->text),
                     "%s\n" SJIS_DEVELOP " " SJIS_DONE "\n"
                     SJIS_RICE " +%d   " SJIS_DEFENSE " +%d   " SJIS_GOLD " -%d",
                     o->full_name_sjis, rice_gain, def_gain, cost);
            break;
        }

        case TURN_CMD_RECRUIT: {
            int gold_cost = turn_recruit_gold_cost(cs);
            int rice_cost = turn_recruit_rice_cost(cs);
            int cap = turn_recruit_cap(cs);

            if (cs->gold < gold_cost) {
                snprintf(res->text, sizeof(res->text), "%s\n" SJIS_NO_GOLD, o->full_name_sjis);
                return false;
            }
            if (cs->rice < rice_cost) {
                snprintf(res->text, sizeof(res->text), "%s\n" SJIS_NO_RICE, o->full_name_sjis);
                return false;
            }

            int room = cap - cs->troops;
            if (room < 0) room = 0;
            int gain = 100 + o->lead * 2;
            if (gain > room) gain = room;
            if (gain <= 0) {
                snprintf(res->text, sizeof(res->text), "%s\n" SJIS_TROOPS " MAX", o->full_name_sjis);
                return false;
            }

            cs->gold -= gold_cost;
            cs->rice -= rice_cost;
            cs->troops += gain;

            res->d_gold = -gold_cost;
            res->d_rice = -rice_cost;
            res->d_troops = gain;
            snprintf(res->text, sizeof(res->text),
                     "%s\n" SJIS_RECRUIT " " SJIS_DONE "\n"
                     SJIS_TROOPS " +%d   " SJIS_PROVISION " -%d   " SJIS_GOLD " -%d",
                     o->full_name_sjis, gain, rice_cost, gold_cost);
            break;
        }

        case TURN_CMD_TAX: {
            int income = 100 + o->politics * 20;
            int loss = 3 + (100 - o->politics) / 25;
            if (loss < 1) loss = 1;
            int before = cs->morale;
            cs->gold += income;
            cs->morale -= loss;
            if (cs->morale < 0) cs->morale = 0;

            res->d_gold = income;
            res->d_morale = cs->morale - before;
            snprintf(res->text, sizeof(res->text),
                     "%s\n" SJIS_TAX " " SJIS_DONE "\n"
                     SJIS_GOLD " +%d   " SJIS_MORALE " %d",
                     o->full_name_sjis, income, res->d_morale);
            break;
        }

        default:
            return false;
    }

    ts->acted[oid] = 1;
    res->ok = true;
    return true;
}

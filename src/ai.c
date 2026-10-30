#include "ai.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kassen.h"

// ---------------------------------------------------------------------------
// Tuning knobs
// ---------------------------------------------------------------------------

#define AI_MAX_TARGETS        8     // a castle never has more than 8 neighbours
#define AI_DOMESTIC_PER_CASTLE 2    // domestic orders per castle per month
#define AI_ATTACKS_PER_FACTION 1    // strategic attacks per clan per month
#define AI_ATTACK_TROOP_MIN    400  // need this much of a garrison to march
#define AI_ATTACK_MARGIN       180  // ... and this much advantage over the target
#define AI_ATTACK_CHANCE       40   // percent chance to launch, per candidate

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift32) — seeded per (year, month, faction) so that
// --ai-step output is byte-for-byte reproducible across runs.
// ---------------------------------------------------------------------------

static uint32_t ai_rng(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void ai_log(AiReport *rep, const char *fmt, ...) {
    if (!rep || rep->log_count >= AI_LOG_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rep->log[rep->log_count], AI_LOG_LEN, fmt, ap);
    va_end(ap);
    rep->log_count++;
}

static const char *ai_cmd_ascii(TurnCommand cmd) {
    switch (cmd) {
        case TURN_CMD_DEVELOP: return "DEVELOP";
        case TURN_CMD_RECRUIT: return "RECRUIT";
        case TURN_CMD_TAX:     return "TAX";
        default:               return "NONE";
    }
}

// Run one domestic order, trying the preferred command then falling back.
// Returns the command that succeeded, or TURN_CMD_NONE.
static TurnCommand ai_do_domestic(TurnState *ts, Scenario *sc, int faction,
                                  int castle, AiReport *rep) {
    ScenarioCastle *cs = &sc->castles[castle];
    TurnCommand order[3];
    int n = 0;

    int cap = turn_recruit_cap(cs);
    int gold_cost = turn_recruit_gold_cost(cs);
    int rice_cost = turn_recruit_rice_cost(cs);

    if (cs->troops < cap * 3 / 4 && cs->gold >= gold_cost && cs->rice >= rice_cost) {
        order[n++] = TURN_CMD_RECRUIT;
    }
    if (cs->gold < 800) order[n++] = TURN_CMD_TAX;
    order[n++] = TURN_CMD_DEVELOP;
    if (order[n - 1] != TURN_CMD_TAX) order[n++] = TURN_CMD_TAX;

    for (int i = 0; i < n; i++) {
        TurnResult res;
        if (turn_run_faction_command(ts, sc, faction, castle, order[i], -1, &res)) {
            ai_log(rep, "faction=%2d castle=%2d %-7s officer=%3d",
                   faction, castle, ai_cmd_ascii(order[i]), res.officer_id);
            return order[i];
        }
    }
    return TURN_CMD_NONE;
}

// ---------------------------------------------------------------------------
// Monthly AI pass
// ---------------------------------------------------------------------------

void ai_run_turn(TurnState *ts, Scenario *sc, AiReport *report) {
    if (!ts || !sc || !report) return;

    memset(report, 0, sizeof(*report));

    const int player = ts->player_faction;

    for (int f = 0; f < 15; f++) {
        if (f == player) continue;

        // Only living clans act (they must still hold at least one castle).
        int owns = 0;
        for (int c = 0; c < sc->num_castles; c++) {
            if (sc->castles[c].faction_id == f) owns++;
        }
        if (owns == 0) continue;

        uint32_t rng = (uint32_t)(sc->year * 131u + sc->month * 17u +
                                  (uint32_t)f * 977u + 7u);
        ai_rng(&rng);   // discard the seed value

        // ---- 1. domestic orders -------------------------------------------
        for (int c = 0; c < sc->num_castles; c++) {
            if (sc->castles[c].faction_id != f) continue;
            for (int k = 0; k < AI_DOMESTIC_PER_CASTLE; k++) {
                TurnCommand done = ai_do_domestic(ts, sc, f, c, report);
                if (done == TURN_CMD_NONE) break;
                report->domestic++;
            }
        }

        // ---- 2. strategic attacks -----------------------------------------
        int launched = 0;
        for (int c = 0; c < sc->num_castles && launched < AI_ATTACKS_PER_FACTION; c++) {
            if (sc->castles[c].faction_id != f) continue;
            ScenarioCastle *cs = &sc->castles[c];
            if (cs->troops <= AI_ATTACK_TROOP_MIN) continue;

            int targets[AI_MAX_TARGETS];
            int nt = kassen_list_targets(sc, c, f, targets, AI_MAX_TARGETS);

            // Prefer the weakest target we clearly outnumber.
            int target = -1;
            int target_troops = 0;
            for (int t = 0; t < nt; t++) {
                ScenarioCastle *tc = &sc->castles[targets[t]];
                if (tc->troops + AI_ATTACK_MARGIN >= cs->troops) continue;
                if (target < 0 || tc->troops < target_troops) {
                    target = targets[t];
                    target_troops = tc->troops;
                }
            }
            if (target < 0) continue;

            if ((int)(ai_rng(&rng) % 100u) >= AI_ATTACK_CHANCE) continue;

            int officer = turn_find_attack_officer_faction(sc, ts, f, c);
            if (officer < 0) officer = sc->castles[c].lord_officer_id;
            if (officer < 0) continue;

            // The commander is committed for the month either way.
            ts->acted[officer] = 1;
            launched++;
            report->attacks++;

            if (sc->castles[target].faction_id == player) {
                if (report->pending_count < AI_MAX_PENDING) {
                    AiPendingBattle *pb = &report->pending[report->pending_count++];
                    pb->from = c;
                    pb->to = target;
                    pb->officer = officer;
                    ai_log(report, "faction=%2d ATTACKS PLAYER castle=%2d from=%2d officer=%3d",
                           f, target, c, officer);
                }
            } else {
                KassenState ks;
                bool captured = kassen_auto_resolve(sc, c, target, officer, &ks);
                if (captured) {
                    report->captures++;
                    ai_log(report, "faction=%2d CAPTURED castle=%2d (from %2d)",
                           f, target, c);
                } else {
                    ai_log(report, "faction=%2d assault on castle=%2d repelled",
                           f, target);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// V10/V13: 3B1A:5BC8 ROI ratio calculator.
//
// In Main.exe at file offset 0x301c9 (runtime 3B1A:5BC8):
//   ratio = (value * 100) / (value + cost), or 50 when value == cost == 0.
// ---------------------------------------------------------------------------

int ai_calc_ratio_5bc8(int value, int cost) {
    if (value <= 0 && cost <= 0) return 50;
    if (value <= 0) return 0;
    int total = value + cost;
    if (total <= 0) return 100;
    int res = (value * 100) / total;
    if (res > 100) res = 100;
    if (res < 0) res = 0;
    return res;
}

// ---------------------------------------------------------------------------
// V10: strategic adjacent-target selector (linear 0x22856).
//
// The disassembly of 0x22856..0x229A5 establishes the complete control flow:
//   - 5E1A(actor) == 0, refA == -1, refB == -1, actor faction == -1,
//     actor == refA, or route source == -1 -> return -1;
//   - best_ref = (matrix[actor][refB] <= matrix[actor][refA]) ? refB : refA;
//   - initial best = (officers(actor), min distance, 0x9216);
//   - each of the seven adjacency slots is rejected on -1, on a foreign owner
//     that is not 0xFFFF, or on officers >= 15;
//   - candidates are compared as (distance, officers, ratio), smallest wins;
//   - only a strict improvement over the actor is accepted.
// ---------------------------------------------------------------------------

int ai_choose_adjacent_target_22856(const Scenario *sc, int castle_id,
                                    int own_faction, int ref_castle_a,
                                    int ref_castle_b, int route_source,
                                    int current_ratio,
                                    AiTargetScoreFn score_fn, void *user,
                                    AiAdjacentTarget *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
        out->castle = -1;
    }
    if (!sc) return -1;
    const int n = sc->num_castles;
    if (n <= 0 || n > MAX_CASTLES) return -1;
    if (castle_id < 0 || castle_id >= n) return -1;

    const int actor_officers =
        officer_count_at_castle_5e1a(sc, castle_id);
    if (actor_officers == 0) return -1;
    if (ref_castle_a == -1) return -1;
    if (ref_castle_b == -1) return -1;
    if (own_faction == -1) return -1;
    if (castle_id == ref_castle_a) return -1;
    if (route_source == -1) return -1;
    // C-side safety: 30FE has no bounds check, but an out-of-range reference
    // would index outside the local 39x39 matrix.
    if (ref_castle_a < 0 || ref_castle_a >= n) return -1;
    if (ref_castle_b < 0 || ref_castle_b >= n) return -1;
    if (route_source < 0 || route_source >= n) return -1;

    uint8_t matrix[MAX_CASTLES * MAX_CASTLES];
    if (!scenario_castle_distance_matrix(sc, matrix, MAX_CASTLES,
                                         MAX_CASTLES)) {
        return -1;
    }

    const int d_a = matrix[castle_id * MAX_CASTLES + ref_castle_a];
    const int d_b = matrix[castle_id * MAX_CASTLES + ref_castle_b];

    // The original compares d1 (refA) with d2 (refB) and uses `jbe`, so a tie
    // keeps refB.
    int best_ref;
    int best_dist;
    if (d_b > d_a) {
        best_ref = ref_castle_a;
        best_dist = d_a;
    } else {
        best_ref = ref_castle_b;
        best_dist = d_b;
    }

    int best_castle = -1;
    int best_officers = actor_officers;
    int best_ratio = current_ratio;

    const ScenarioCastle *actor = &sc->castles[castle_id];
    for (int i = 0; i < actor->num_conns; i++) {
        const int cand = actor->conns[i];
        if (cand < 0 || cand >= n) continue;

        // 43B3:21A8 returns the +0x28 faction field.  43B3:221E is the
        // "unowned" test: it returns true when that field is 0xFFFF.
        const int owner = sc->castles[cand].faction_id;
        if (owner != own_faction && owner != -1) continue;

        const int officers = officer_count_at_castle_5e1a(sc, cand);
        // 0x2295C: `cmp di, 0xf; jae skip`.
        if (officers >= 15) continue;

        // 0x22926: 30FE(cand, best_ref) -> matrix[cand][best_ref].
        const int dist = matrix[cand * MAX_CASTLES + best_ref];

        // 0x2294F calls 5BC8 and then masks the result with `sub ah, ah`.
        // With no score callback the candidate keeps the actor's ratio, so
        // the comparison only exercises distance and officer count.
        int ratio = best_ratio;
        if (score_fn) ratio = score_fn(sc, cand, user) & 0xFF;

        bool accept = false;
        if (best_dist > dist) {
            accept = true;
        } else if (best_dist == dist) {
            if (best_officers != officers) {
                accept = (best_officers > officers);
            } else {
                accept = (best_ratio > ratio);
            }
        }
        if (!accept) continue;

        best_castle = cand;
        best_officers = officers;
        best_dist = dist;
        best_ratio = ratio;
    }

    if (out) {
        out->castle = best_castle;
        out->reference = best_ref;
        out->distance = best_dist;
        out->officers = best_officers;
        out->ratio = best_ratio;
    }
    return best_castle;
}

// ---------------------------------------------------------------------------
// V13: Original 0x22D82 army dispatch executor and 31CA:0A22 march worker.
//
// In Main.exe:
//   - Line 0x12834: checks source governor. Returns 0 if none.
//   - Checks destination castle capacity: dst_officers must be < 15.
//   - Max dispatchable count is min(dispatch, 15 - dst_officers).
//   - Evaluates source retention:
//       if src_officers > 15: dispatch = min(dispatch, src_officers - 15)
//       else: dispatch = min(dispatch, min(10, roi_ratio / 10))
//   - Border castle stance moderation:
//       if stance == 4: max_dispatch = candidate_count / 3
//       else: max_dispatch = candidate_count / 2
//       dispatch = min(dispatch, max_dispatch)
//   - Daimyo preservation rule:
//       if daimyo is present in src_castle: dispatch + 5 <= src_officers,
//       otherwise reject (dx = 1 -> abort to 0x12a0c).
//   - 31CA:0A22 march execution:
//       Transfers `dispatch` officers to dst_castle, along with proportional
//       troops, keeping at least 50 garrison in source castle.
// ---------------------------------------------------------------------------
int ai_dispatch_army_22d82(TurnState *ts, Scenario *sc, int src_castle,
                           int dst_castle, int stance, int roi_ratio) {
    if (!ts || !sc) return 0;
    if (src_castle < 0 || src_castle >= sc->num_castles) return 0;
    if (dst_castle < 0 || dst_castle >= sc->num_castles) return 0;
    if (src_castle == dst_castle) return 0;

    ScenarioCastle *src = &sc->castles[src_castle];
    ScenarioCastle *dst = &sc->castles[dst_castle];
    if (src->faction_id != dst->faction_id) return 0;
    int faction = src->faction_id;

    // Check governor (335E:2238)
    int governor = src->lord_officer_id;
    if (governor < 0 || governor >= sc->num_officers) return 0;

    // Is governor the clan daimyo? (2AC5:7F9A)
    int leader = scenario_faction_leader(sc, faction);
    bool is_daimyo = (leader >= 0 && leader == governor);

    // Enumerate officers at src and dst (335E:2ED6)
    int src_officers = 0;
    int dst_officers = 0;
    int candidates[MAX_OFFICERS];
    int cand_count = 0;

    for (int i = 0; i < sc->num_officers; i++) {
        ScenarioOfficer *o = &sc->officers[i];
        if (!o->is_active || o->faction_id != faction) continue;
        if (o->castle_id == src_castle) {
            src_officers++;
            // Daimyo never marches away from his seat as a subordinate
            if (!is_daimyo || i != governor) {
                if (!ts || !turn_officer_has_acted(ts, i)) {
                    candidates[cand_count++] = i;
                }
            }
        } else if (o->castle_id == dst_castle) {
            dst_officers++;
        }
    }

    if (cand_count == 0) return 0;
    if (dst_officers >= 15) return 0; // 15-officer ceiling gate at 0x1292f

    int dispatch = cand_count;
    // Destination capacity clamp: dispatch = min(dispatch, 15 - dst_officers)
    if (!is_daimyo) {
        int room = 15 - dst_officers;
        if (dispatch > room) dispatch = room;
    }

    // Source retention check at 0x12956
    if (src_officers > 15) {
        int excess = src_officers - 15;
        if (dispatch > excess) dispatch = excess;
    } else {
        int scaled = roi_ratio / 10;
        if (scaled > 10) scaled = 10;
        if (scaled < 1) scaled = 1;
        if (dispatch > scaled) dispatch = scaled;
    }

    // Check border castle status (335E:2D30)
    bool is_border = false;
    for (int k = 0; k < src->num_conns; k++) {
        int neigh = src->conns[k];
        if (neigh >= 0 && neigh < sc->num_castles &&
            sc->castles[neigh].faction_id != faction) {
            is_border = true;
            break;
        }
    }

    if (is_border) {
        // Stance modulation at 0x129a2: stance 4 leaves 1/3, else 1/2
        int max_dispatch = (stance == 4) ? (cand_count / 3) : (cand_count / 2);
        if (max_dispatch < 1) max_dispatch = 1;
        if (dispatch > max_dispatch) dispatch = max_dispatch;
    }

    // Daimyo preservation rule at 0x129df:
    // If daimyo is present, source must retain at least 5 officers!
    if (is_daimyo) {
        if (dispatch + 5 > src_officers) {
            dispatch = src_officers - 5;
        }
    }

    if (dispatch <= 0) return 0;

    // Execute march worker 31CA:0A22
    // Transfer proportional troops, preserving at least 50 garrison
    int transfer_troops = 0;
    if (src->troops > 50) {
        int available = src->troops - 50;
        transfer_troops = (available * dispatch) / (src_officers > 0 ? src_officers : 1);
        if (transfer_troops < 0) transfer_troops = 0;
        if (transfer_troops > available) transfer_troops = available;
    }

    src->troops -= transfer_troops;
    dst->troops += transfer_troops;

    for (int i = 0; i < dispatch; i++) {
        int oid = candidates[i];
        sc->officers[oid].castle_id = dst_castle;
        if (ts) ts->acted[oid] = 1;
    }

    // If governor was dispatched (non-daimyo), assign new governor
    if (governor >= 0 && sc->officers[governor].castle_id != src_castle) {
        int new_gov = -1;
        for (int i = 0; i < sc->num_officers; i++) {
            ScenarioOfficer *o = &sc->officers[i];
            if (o->is_active && o->faction_id == faction && o->castle_id == src_castle) {
                new_gov = i;
                break;
            }
        }
        src->lord_officer_id = new_gov;
    }

    return dispatch;
}

// ---------------------------------------------------------------------------
// V13: Original 0x2303C (0x12bb6) 6-branch domestic evaluation and dispatch.
//
// In Main.exe at file offset 0x12bb6 / 0x12aee:
//   di = min(gold / 50, min(10, general_stat))
//   delta = (rand() % di) + 1
//
// Branches:
//   0: Charity / Relief       (morale < 100, gold >= 50) -> morale += delta, gold -= di*50
//   1: Repair / Fortification (defense < 1000, gold >= 50) -> defense += delta*10, gold -= di*50
//   2: Land Development       (rice < max, gold >= 100) -> rice += delta*20, gold -= di*50
//   3: Military Drill         (training < 100, rice >= 50) -> training += delta, rice -= di*20
//   4: Recruitment            (troops < cap, gold >= 100, rice >= 50) -> troops += delta*10
//   5: Officer Reward         (any officer loyalty < 100, gold >= 50) -> loyalty += delta
// ---------------------------------------------------------------------------
int ai_domestic_eval_and_act_2303c(TurnState *ts, Scenario *sc, int faction,
                                   int castle, AiReport *rep) {
    if (!ts || !sc || castle < 0 || castle >= sc->num_castles) return -1;
    ScenarioCastle *cs = &sc->castles[castle];
    if (cs->faction_id != faction) return -1;

    int oid = turn_find_idle_officer_faction(sc, ts, faction, castle);
    if (oid < 0) return -1;
    ScenarioOfficer *o = &sc->officers[oid];

    // Calculate input points di: min(gold / 50, min(10, stat))
    int stat = o->politics / 10;
    if (stat > 10) stat = 10;
    if (stat < 1) stat = 1;

    int gold_units = cs->gold / 50;
    int di = gold_units < stat ? gold_units : stat;
    if (di < 1) di = 1;

    // Deterministic pseudo-RNG
    uint32_t seed = (uint32_t)(sc->year * 131u + sc->month * 17u +
                              (uint32_t)faction * 977u + (uint32_t)castle * 37u + (uint32_t)oid * 7u + 1u);
    ai_rng(&seed);
    int delta = (int)(seed % (uint32_t)di) + 1;

    // Check 6 domestic branches in order
    // Branch 0: Charity (0x12c48)
    if (cs->morale < 100 && cs->gold >= 50) {
        int cost = di * 50;
        if (cost > cs->gold) cost = cs->gold;
        cs->gold -= cost;
        cs->morale += delta;
        if (cs->morale > 100) cs->morale = 100;
        ts->acted[oid] = 1;
        ai_log(rep, "faction=%2d castle=%2d DOMESTIC 0 (CHARITY) +%d morale (gold -%d)",
               faction, castle, delta, cost);
        return 0;
    }

    // Branch 1: Fortification Repair (0x12c9c)
    if (cs->defense < 1000 && cs->gold >= 50) {
        int cost = di * 50;
        if (cost > cs->gold) cost = cs->gold;
        cs->gold -= cost;
        int boost = delta * 10;
        cs->defense += boost;
        if (cs->defense > 1000) cs->defense = 1000;
        ts->acted[oid] = 1;
        ai_log(rep, "faction=%2d castle=%2d DOMESTIC 1 (REPAIR) +%d def (gold -%d)",
               faction, castle, boost, cost);
        return 1;
    }

    // Branch 2: Land Development (0x12d02)
    if (cs->gold >= 100) {
        int cost = di * 50;
        if (cost > cs->gold) cost = cs->gold;
        cs->gold -= cost;
        int boost = delta * 20;
        cs->rice += boost;
        ts->acted[oid] = 1;
        ai_log(rep, "faction=%2d castle=%2d DOMESTIC 2 (DEVELOP) +%d rice (gold -%d)",
               faction, castle, boost, cost);
        return 2;
    }

    // Branch 3: Military Drill (0x12f00)
    if (cs->morale < 100 && cs->rice >= 50) {
        int cost = di * 20;
        if (cost > cs->rice) cost = cs->rice;
        cs->rice -= cost;
        cs->morale += delta;
        if (cs->morale > 100) cs->morale = 100;
        ts->acted[oid] = 1;
        ai_log(rep, "faction=%2d castle=%2d DOMESTIC 3 (DRILL) +%d training (rice -%d)",
               faction, castle, delta, cost);
        return 3;
    }

    // Branch 4: Recruitment (0x12fb0)
    int cap = turn_recruit_cap(cs);
    if (cs->troops < cap && cs->gold >= 100 && cs->rice >= 50) {
        int gold_cost = di * 40;
        int rice_cost = di * 20;
        if (gold_cost > cs->gold) gold_cost = cs->gold;
        if (rice_cost > cs->rice) rice_cost = cs->rice;
        cs->gold -= gold_cost;
        cs->rice -= rice_cost;
        int gain = delta * 15;
        if (cs->troops + gain > cap) gain = cap - cs->troops;
        cs->troops += gain;
        ts->acted[oid] = 1;
        ai_log(rep, "faction=%2d castle=%2d DOMESTIC 4 (RECRUIT) +%d troops",
               faction, castle, gain);
        return 4;
    }

    // Branch 5: Officer Reward (0x13050)
    if (cs->gold >= 50) {
        for (int i = 0; i < sc->num_officers; i++) {
            ScenarioOfficer *target = &sc->officers[i];
            if (target->is_active && target->faction_id == faction &&
                target->castle_id == castle && target->loyalty < 100) {
                cs->gold -= 50;
                target->loyalty += delta;
                if (target->loyalty > 100) target->loyalty = 100;
                ts->acted[oid] = 1;
                ai_log(rep, "faction=%2d castle=%2d DOMESTIC 5 (REWARD) officer=%3d +%d loyalty",
                       faction, castle, i, delta);
                return 5;
            }
        }
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Full Original Decision Pipeline
// ---------------------------------------------------------------------------
void ai_run_turn_original(TurnState *ts, Scenario *sc, AiReport *report) {
    if (!ts || !sc || !report) return;

    memset(report, 0, sizeof(*report));
    const int player = ts->player_faction;

    for (int f = 0; f < 15; f++) {
        if (f == player) continue;

        // Check if clan holds at least one castle
        int owns = 0;
        for (int c = 0; c < sc->num_castles; c++) {
            if (sc->castles[c].faction_id == f) {
                owns++;
            }
        }
        if (owns == 0) continue;

        // Default stance: 4 (aggressive) if holding >= 3 castles, else 1
        int stance = (owns >= 3) ? 4 : 1;

        for (int c = 0; c < sc->num_castles; c++) {
            if (sc->castles[c].faction_id != f) continue;

            // Step 1: V10 Strategic Target Selection (0x22856)
            int ref_a = scenario_nearest_enemy_castle(sc, f, c);
            int ref_b = scenario_nearest_friendly_castle(sc, f, c);
            AiAdjacentTarget sel;
            int chosen = ai_choose_adjacent_target_22856(
                sc, c, f, ref_a, ref_b, c, 50, NULL, NULL, &sel);

            bool dispatched = false;
            if (chosen >= 0 && sc->castles[c].troops > 400) {
                int roi = ai_calc_ratio_5bc8(sc->castles[chosen].rice, 100);
                int count = ai_dispatch_army_22d82(ts, sc, c, chosen, stance, roi);
                if (count > 0) {
                    dispatched = true;
                    report->attacks++;
                    ai_log(report, "faction=%2d DISPATCH castle=%2d -> %2d (officers=%d)",
                           f, c, chosen, count);
                }
            }

            // Step 2: V13 6-branch Domestic Decision (0x2303C)
            if (!dispatched) {
                for (int k = 0; k < 2; k++) {
                    int branch = ai_domestic_eval_and_act_2303c(ts, sc, f, c, report);
                    if (branch < 0) break;
                    report->domestic++;
                }
            }
        }
    }
}

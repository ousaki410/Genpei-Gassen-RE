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

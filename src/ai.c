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

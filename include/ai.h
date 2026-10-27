#ifndef YPHZ_AI_H
#define YPHZ_AI_H

#include <stdbool.h>

#include "scenario.h"
#include "turn.h"

// ---------------------------------------------------------------------------
// Non-player faction AI.
//
// ai_run_turn() is called once per month, right after the player presses 終了
// (turn_end resets the action flags first).  Every living clan other than the
// human's gets:
//
//   1. Domestic orders  — 徴兵 when the garrison is under-strength and gold +
//      provisions allow it, otherwise 徴税 when the treasury is low, otherwise
//      開発.  Handled through turn_run_faction_command().
//   2. Strategic attacks — a castle with a comfortable troop margin may march
//      on a markedly weaker adjacent enemy castle.  AI-vs-AI sieges are
//      auto-resolved through the kassen damage model; attacks aimed at the
//      *player* are queued so the host can launch an interactive defence.
//
// Everything is deterministic (a per-(year,month,faction) xorshift), so
// headless runs with --ai-step are reproducible.
// ---------------------------------------------------------------------------

#define AI_MAX_PENDING 8
#define AI_LOG_LINES   192
#define AI_LOG_LEN     128

#pragma pack(push, 1)

typedef struct {
    int from;       // attacking (AI) castle
    int to;         // defending (player) castle
    int officer;    // AI commander
} AiPendingBattle;

typedef struct {
    int domestic;       // AI domestic commands executed
    int attacks;        // AI attacks launched (both kinds)
    int captures;       // AI-vs-AI captures
    int pending_count;  // attacks on the player awaiting an interactive battle
    int pending_idx;    // next pending battle to play (host bookkeeping)
    AiPendingBattle pending[AI_MAX_PENDING];
    int  log_count;
    char log[AI_LOG_LINES][AI_LOG_LEN];
} AiReport;

#pragma pack(pop)

// Run one AI month for every non-player faction.
void ai_run_turn(TurnState *ts, Scenario *sc, AiReport *report);

// ---------------------------------------------------------------------------
// V10: strategic adjacent-target selector (43B3 overlay, linear 0x22856).
//
// The original function is reached from the per-castle decision context
// prepared at linear 0x226E0.  Its inputs are the shared words:
//   0x920A acting castle, 0x920C reference castle A, 0x920E reference castle B,
//   0x9210 actor faction, 0x9212 route-cost source, 0x9216 current ratio.
//
// For each of the acting castle's seven neighbours (43B3:20C6) it:
//   * keeps the neighbour when owner == actor faction or owner == -1
//     (43B3:21A8 / 221E);
//   * rejects neighbours with 15 or more officers (43B3:2ED6 == 4000:5E1A);
//   * computes matrix[neighbour][best_ref] with 43B3:30FE;
//   * computes the original cost/value ratio through 28FD:7350 / 71B8 and
//     3B1A:5BC8;
//   * keeps the lexicographically smallest (distance, officer count, ratio),
//     but only when it strictly improves on the acting castle itself.
//
// It returns -1 when nothing improves, which is the original's "stay put"
// result.  The 7350/71B8 scoring model is NOT reimplemented here; score_fn
// isolates it.  Passing a NULL score_fn keeps the actor's current ratio for
// every candidate, so scoring cannot break a distance/officer-count tie.
//
// C-side safety: out-of-range ref_castle_a/ref_castle_b/route_source values
// return -1; the original only rejects the 0xFFFF sentinel.
//
// Confidence: B for the control flow, field reads and ordering; C for the
// caller-supplied score model.
// ---------------------------------------------------------------------------

typedef int (*AiTargetScoreFn)(const Scenario *sc, int castle_id, void *user);

typedef struct {
    int castle;     // chosen adjacent castle, or -1
    int reference;  // reference castle actually used (A or B)
    int distance;   // matrix[candidate][reference]
    int officers;   // officer count at the candidate
    int ratio;      // chosen candidate's 8-bit ratio; actor's current ratio when castle == -1
} AiAdjacentTarget;

int ai_choose_adjacent_target_22856(const Scenario *sc, int castle_id,
                                    int own_faction, int ref_castle_a,
                                    int ref_castle_b, int route_source,
                                    int current_ratio,
                                    AiTargetScoreFn score_fn, void *user,
                                    AiAdjacentTarget *out);

#endif // YPHZ_AI_H

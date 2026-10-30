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
// V10/V13: strategic adjacent-target selector (43B3 overlay, linear 0x22856)
// and 3B1A:5BC8 ROI ratio calculator.
//
// The original function is reached from the per-castle decision context
// prepared at linear 0x226E0. Its inputs are the shared words:
//   0x920A acting castle, 0x920C reference castle A, 0x920E reference castle B,
//   0x9210 actor faction, 0x9212 route-cost source, 0x9216 current ratio.
//
// 3B1A:5BC8 computes:
//   ratio = (value * 100) / (value + cost), or 50 when value == cost == 0.
// ---------------------------------------------------------------------------

// 3B1A:5BC8 ROI ratio calculator.
int ai_calc_ratio_5bc8(int value, int cost);

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

// ---------------------------------------------------------------------------
// V13: Original 0x22D82 army dispatch executor and 31CA:0A22 march worker.
//
// In Main.exe at file offset 0x12834 (runtime 0x22D82):
//   - Verifies governor of source castle;
//   - Checks destination capacity (strictly <= 15 officers);
//   - Preserves at least 5 officers if the clan daimyo is present;
//   - Scales garrison transfer by ROI ratio [0x9216] and stance [0x9220] (div 3 on stance 4, div 2 otherwise);
//   - Calls 31CA:0A22 to reassign officers and transfer garrison.
// Returns the number of officers dispatched, or 0 on rejection.
// ---------------------------------------------------------------------------
int ai_dispatch_army_22d82(TurnState *ts, Scenario *sc, int src_castle,
                           int dst_castle, int stance, int roi_ratio);

// ---------------------------------------------------------------------------
// V13: Original 0x2303C (0x12bb6) 6-branch domestic evaluation and dispatch.
//
// In Main.exe at file offset 0x12bb6 / 0x12aee:
//   - Branch 0: Charity / Peasant Loyalty (Morale boost)
//   - Branch 1: Fortification Repair (Defense boost)
//   - Branch 2: Land Development (Kokudaka / Rice boost)
//   - Branch 3: Military Drill (Morale / Training boost)
//   - Branch 4: Troop Recruitment (Garrison boost)
//   - Branch 5: Officer Reward (Loyalty boost)
// Returns the branch executed (0..5), or -1 if no order was performed.
// ---------------------------------------------------------------------------
int ai_domestic_eval_and_act_2303c(TurnState *ts, Scenario *sc, int faction,
                                   int castle, AiReport *rep);

// Run the full original decision pipeline (V10 0x22856 target selection ->
// V13 0x22D82 army dispatch / 0x2303C 6-branch domestic orders).
void ai_run_turn_original(TurnState *ts, Scenario *sc, AiReport *report);

#endif // YPHZ_AI_H

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

#endif // YPHZ_AI_H

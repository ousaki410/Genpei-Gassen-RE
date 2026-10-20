#ifndef YPHZ_SAVE_H
#define YPHZ_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scenario.h"
#include "turn.h"

// ---------------------------------------------------------------------------
// Native save slots (子任务 8).
//
// The original Savedata.gp is 10 fixed 43403-byte records: an 18-byte header
// (year/month at 0..2) followed by a scenario snapshot whose field offsets
// drift versus Sndata.gp, so it is *not* byte-compatible with the current
// parser. This engine therefore writes a self-describing native slot holding
// exactly the mutable state (Scenario castles/officers/calendar + TurnState).
//
// Layout (little-endian, #pragma pack(1)):
//     char     magic[8]      "YPHZSNAP"
//     uint32_t version       2
//     int32_t  scenario_idx
//     int32_t  year, month
//     int32_t  num_castles, num_officers
//     ScenarioCastle  castles[MAX_CASTLES]
//     ScenarioOfficer officers[MAX_OFFICERS]
//     int32_t  player_faction
//     int32_t  turn_year, turn_month
//     uint8_t  acted[MAX_OFFICERS]
//     TreasureState treasures        (version 2, 子任务 9)
//     int32_t  prestige              (version 3, 子任务 14)
//     int32_t  court_rank
//     uint8_t  relations[15][15]     (version 3)
//
// Loading restores every mutable field, so a reloaded game is byte-for-byte
// identical to the saved one. The static metadata (title_sjis, faction defs,
// coordinates) always comes from Sndata.gp and is left untouched.
// ---------------------------------------------------------------------------

#define SAVE_MAGIC    "YPHZSNAP"
#define SAVE_VERSION  3u

// Write the current game to `path`. Returns false on I/O failure.
bool save_game(const char *path, const Scenario *sc, const TurnState *ts);

// Restore `path` into an already-initialised scenario/turn pair.
bool load_game(const char *path, Scenario *sc, TurnState *ts);

// FNV-1a 32-bit digest of the canonical snapshot bytes (--dump-state).
uint32_t save_state_hash(const Scenario *sc, const TurnState *ts);

// Byte size of one on-disk slot.
size_t save_snapshot_size(void);

#endif // YPHZ_SAVE_H

#ifndef YPHZ_TREASURE_H
#define YPHZ_TREASURE_H

#include <stdbool.h>
#include <stdint.h>

#include "scenario.h"

// ---------------------------------------------------------------------------
// 子任务 9: 家宝 / 名物 (heirlooms).
//
// Mainitem.gp is a NPK016 container with 19 64x64 heirloom icons. The original
// data file stores only the artwork, so the names and stat bonuses below are a
// curated table of famous Genpei-era treasures. Ownership is mutable game
// state: an item belongs to a faction and may additionally be granted to one
// of that faction's officers, at which point its bonuses apply to that officer.
// ---------------------------------------------------------------------------

#define TREASURE_COUNT 19

#pragma pack(push, 1)

typedef struct {
    int32_t owner_faction[TREASURE_COUNT]; // -1 = unowned
    int32_t owner_officer[TREASURE_COUNT]; // -1 = faction vault (not granted)
} TreasureState;

#pragma pack(pop)

typedef struct {
    int lead;      // 統率
    int war;       // 武力
    int intel;     // 知略
    int politics;  // 政治
} TreasureBonus;

// Give every item an initial owner: 19 items round-robin across the 15 clans,
// with the four extra treasures going to the clans that hold most castles.
void treasure_init(TreasureState *tr, const Scenario *sc);

// Static metadata.
const char *treasure_name_sjis(int item);
TreasureBonus treasure_bonus(int item);

// Ownership queries.
bool treasure_owned_by(const TreasureState *tr, int item, int faction);
int  treasure_faction_item_count(const TreasureState *tr, int faction);

// Grant a faction-owned item to one of that faction's officers. Returns false
// when the item is not held by the officer's clan.
bool treasure_grant(TreasureState *tr, const Scenario *sc, int item, int officer);

// Sum of the bonuses of all items granted to `officer`.
TreasureBonus treasure_officer_bonus(const TreasureState *tr, int officer);

// Move one item from the defeated clan's hoard to the victor. Deterministic
// for a given seed. Returns the item id, or -1 when nothing was seized.
int treasure_seize(TreasureState *tr, int defeated_faction, int victor_faction,
                   uint32_t seed);

#endif // YPHZ_TREASURE_H

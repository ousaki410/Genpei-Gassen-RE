#include "treasure.h"

#include <string.h>

// ---------------------------------------------------------------------------
// 19 heirlooms. Names are Shift-JIS byte strings (PC-98 text path); bonuses are
// deliberately small (+3 .. +10) so they flavour, rather than dominate, an
// officer's base stats.
// ---------------------------------------------------------------------------

typedef struct {
    const char *name_sjis;
    TreasureBonus bonus;
} TreasureDef;

static const TreasureDef s_treasures[TREASURE_COUNT] = {
    // 0  八咫鏡
    { "\x94\xaa\x9a\x40\x8b\xbe",                         { 8, 0, 0, 8 } },
    // 1  八尺瓊勾玉
    { "\x94\xaa\x8e\xda\xe0\xf9\x8c\xf9\x8b\xca",         { 0, 0, 5, 9 } },
    // 2  草薙剣
    { "\x91\x90\x93\xe3\x8c\x95",                         { 5, 9, 0, 0 } },
    // 3  平家琵琶
    { "\x95\xbd\x89\xc6\x94\xfa\x94\x69",                 { 0, 0, 7, 5 } },
    // 4  源氏太刀
    { "\x8c\xb9\x8e\x81\x91\xbe\x93\x81",                 { 3, 8, 0, 0 } },
    // 5  河内弓
    { "\x89\xcd\x93\xe0\x8b\x7c",                         { 4, 6, 0, 0 } },
    // 6  菊一文字
    { "\x8b\x65\x88\xea\x95\xb6\x8e\x9a",                 { 0, 7, 3, 0 } },
    // 7  小烏丸
    { "\x8f\xac\x89\x47\x8a\xdb",                         { 0, 6, 4, 0 } },
    // 8  蜻蛉切
    { "\xe5\x91\xe5\x78\x90\xd8",                         { 4, 8, 0, 0 } },
    // 9  村正
    { "\x91\xba\x90\xb3",                                 { 0, 9, 3, 0 } },
    // 10 三日月宗近
    { "\x8e\x4f\x93\xfa\x8c\x8e\x8f\x40\x8b\xdf",         { 0, 7, 0, 3 } },
    // 11 小竜景光
    { "\x8f\xac\x97\xb3\x8c\x69\x8c\xf5",                 { 4, 6, 0, 0 } },
    // 12 一期一振
    { "\x88\xea\x8a\xfa\x88\xea\x90\x55",                 { 0, 0, 5, 6 } },
    // 13 骨喰藤四郎
    { "\x8d\x9c\x8b\xf2\x93\xa1\x8e\x6c\x98\x59",         { 0, 0, 7, 3 } },
    // 14 平家納経
    { "\x95\xbd\x89\xc6\x94\x5b\x8c\x6f",                 { 0, 0, 4, 8 } },
    // 15 厳島絵巻
    { "\x8c\xb5\x93\x87\x8a\x47\x8a\xaa",                 { 0, 0, 3, 6 } },
    // 16 源平合戦図屏風
    { "\x8c\xb9\x95\xbd\x8d\x87\x90\xed\x90\x7d\x9b\xa0\x95\x97", { 0, 0, 6, 5 } },
    // 17 菊水の盃
    { "\x8b\x65\x90\x85\x82\xcc\x94\x75",                 { 4, 0, 0, 4 } },
    // 18 名馬磨墨
    { "\x96\xbc\x94\x6e\x96\x81\x96\x6e",                 { 7, 4, 0, 0 } },
};

const char *treasure_name_sjis(int item) {
    if (item < 0 || item >= TREASURE_COUNT) return "\x96\xb3"; // 無
    return s_treasures[item].name_sjis;
}

TreasureBonus treasure_bonus(int item) {
    if (item < 0 || item >= TREASURE_COUNT) {
        TreasureBonus zero = { 0, 0, 0, 0 };
        return zero;
    }
    return s_treasures[item].bonus;
}

void treasure_init(TreasureState *tr, const Scenario *sc) {
    if (!tr) return;
    for (int i = 0; i < TREASURE_COUNT; i++) {
        tr->owner_faction[i] = -1;
        tr->owner_officer[i] = -1;
    }
    if (!sc) return;

    const int num_factions = 15;

    // Base pass: one treasure per clan, round-robin.
    for (int i = 0; i < TREASURE_COUNT && i < num_factions; i++) {
        tr->owner_faction[i] = i;
    }

    // Extra treasures go to the four strongest clans by castle count.
    int castle_count[15] = {0};
    for (int i = 0; i < sc->num_castles; i++) {
        int f = sc->castles[i].faction_id;
        if (f >= 0 && f < num_factions) castle_count[f]++;
    }
    int top[4] = {-1, -1, -1, -1};
    for (int n = 0; n < 4; n++) {
        int best = -1, best_count = -1;
        for (int f = 0; f < num_factions; f++) {
            if (castle_count[f] <= 0) continue;
            bool used = false;
            for (int k = 0; k < n; k++) if (top[k] == f) used = true;
            if (used) continue;
            if (castle_count[f] > best_count) {
                best_count = castle_count[f];
                best = f;
            }
        }
        top[n] = best;
    }
    for (int i = num_factions; i < TREASURE_COUNT; i++) {
        int f = top[(i - num_factions) % 4];
        if (f < 0) f = (i - num_factions) % num_factions;
        tr->owner_faction[i] = f;
    }
}

bool treasure_owned_by(const TreasureState *tr, int item, int faction) {
    if (!tr || item < 0 || item >= TREASURE_COUNT) return false;
    return tr->owner_faction[item] == faction;
}

int treasure_faction_item_count(const TreasureState *tr, int faction) {
    if (!tr) return 0;
    int n = 0;
    for (int i = 0; i < TREASURE_COUNT; i++) {
        if (tr->owner_faction[i] == faction) n++;
    }
    return n;
}

bool treasure_grant(TreasureState *tr, const Scenario *sc, int item, int officer) {
    if (!tr || !sc || item < 0 || item >= TREASURE_COUNT) return false;
    if (officer < 0 || officer >= sc->num_officers) return false;

    int faction = sc->officers[officer].faction_id;
    if (faction < 0) return false;
    if (tr->owner_faction[item] != faction) return false;

    tr->owner_officer[item] = officer;
    return true;
}

TreasureBonus treasure_officer_bonus(const TreasureState *tr, int officer) {
    TreasureBonus total = { 0, 0, 0, 0 };
    if (!tr || officer < 0) return total;
    for (int i = 0; i < TREASURE_COUNT; i++) {
        if (tr->owner_officer[i] != officer) continue;
        TreasureBonus b = s_treasures[i].bonus;
        total.lead += b.lead;
        total.war += b.war;
        total.intel += b.intel;
        total.politics += b.politics;
    }
    return total;
}

int treasure_seize(TreasureState *tr, int defeated_faction, int victor_faction,
                   uint32_t seed) {
    if (!tr) return -1;
    if (defeated_faction < 0 || victor_faction < 0) return -1;
    if (defeated_faction == victor_faction) return -1;

    int candidates[TREASURE_COUNT];
    int n = 0;
    for (int i = 0; i < TREASURE_COUNT; i++) {
        if (tr->owner_faction[i] == defeated_faction) {
            candidates[n++] = i;
        }
    }
    if (n == 0) return -1;

    // 85% chance, deterministic on (seed).
    if ((seed % 100u) >= 85u) return -1;

    int item = candidates[(seed >> 8) % (uint32_t)n];
    tr->owner_faction[item] = victor_faction;
    tr->owner_officer[item] = -1;
    return item;
}

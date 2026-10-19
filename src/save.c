#include "save.h"

#include <stdio.h>
#include <string.h>

// On-disk record. Every struct in this project is packed, so the file image is
// stable across compilers for a given ABI.
//
// Version 2 (子任务 9) appends the 家宝 ownership table. Version 1 slots from
// sub-task 8 are still readable: the leading bytes are unchanged, so a v1 file
// is simply read into the v2 record and its treasure table is initialised.
#pragma pack(push, 1)

typedef struct {
    char     magic[8];              // "YPHZSNAP" (no NUL, exactly 8 bytes)
    uint32_t version;
    int32_t  scenario_idx;
    int32_t  year;
    int32_t  month;
    int32_t  num_castles;
    int32_t  num_officers;
    ScenarioCastle  castles[MAX_CASTLES];
    ScenarioOfficer officers[MAX_OFFICERS];
    int32_t  player_faction;
    int32_t  turn_year;
    int32_t  turn_month;
    uint8_t  acted[MAX_OFFICERS];
} SaveSnapshotV1;

typedef struct {
    SaveSnapshotV1 base;
    TreasureState  treasures;
} SaveSnapshotV2;

#pragma pack(pop)

typedef SaveSnapshotV2 SaveSnapshot;

size_t save_snapshot_size(void) { return sizeof(SaveSnapshotV2); }

static void snapshot_fill(SaveSnapshot *s, const Scenario *sc,
                          const TurnState *ts) {
    memset(s, 0, sizeof(*s));
    memcpy(s->base.magic, SAVE_MAGIC, sizeof(s->base.magic));
    s->base.version = SAVE_VERSION;
    s->base.scenario_idx = sc->scenario_idx;
    s->base.year = sc->year;
    s->base.month = sc->month;
    s->base.num_castles = sc->num_castles;
    s->base.num_officers = sc->num_officers;
    memcpy(s->base.castles, sc->castles, sizeof(s->base.castles));
    memcpy(s->base.officers, sc->officers, sizeof(s->base.officers));
    s->base.player_faction = ts->player_faction;
    s->base.turn_year = ts->year;
    s->base.turn_month = ts->month;
    memcpy(s->base.acted, ts->acted, sizeof(s->base.acted));
    memcpy(&s->treasures, &ts->treasures, sizeof(s->treasures));
}

bool save_game(const char *path, const Scenario *sc, const TurnState *ts) {
    if (!path || !path[0] || !sc || !ts) return false;

    SaveSnapshot snap;
    snapshot_fill(&snap, sc, ts);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] cannot open '%s' for writing\n", path);
        return false;
    }
    size_t written = fwrite(&snap, 1, sizeof(snap), f);
    bool ok = (written == sizeof(snap));
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "[ERROR] short write to '%s'\n", path);
    }
    return ok;
}

bool load_game(const char *path, Scenario *sc, TurnState *ts) {
    if (!path || !path[0] || !sc || !ts) return false;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] cannot open '%s' for reading\n", path);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long file_size = ftell(f);
    if (file_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    SaveSnapshot snap;
    memset(&snap, 0, sizeof(snap));

    bool v1 = ((size_t)file_size == sizeof(SaveSnapshotV1));
    size_t want = v1 ? sizeof(SaveSnapshotV1) : sizeof(SaveSnapshotV2);
    size_t got = fread(&snap, 1, want, f);
    fclose(f);

    if (got != want) {
        fprintf(stderr, "[ERROR] '%s' is truncated (%zu/%zu bytes)\n",
                path, got, want);
        return false;
    }
    if (memcmp(snap.base.magic, SAVE_MAGIC, sizeof(snap.base.magic)) != 0) {
        fprintf(stderr, "[ERROR] '%s' is not a YPHZ snapshot\n", path);
        return false;
    }
    if (snap.base.version != 1u && snap.base.version != SAVE_VERSION) {
        fprintf(stderr, "[ERROR] '%s' has unsupported version %u\n",
                path, snap.base.version);
        return false;
    }
    if (snap.base.num_castles <= 0 || snap.base.num_castles > MAX_CASTLES ||
        snap.base.num_officers <= 0 || snap.base.num_officers > MAX_OFFICERS) {
        fprintf(stderr, "[ERROR] '%s' has invalid counts (%d/%d)\n",
                path, snap.base.num_castles, snap.base.num_officers);
        return false;
    }

    // Copy only the mutable state; title_sjis and the static tables stay as
    // loaded from Sndata.gp.
    sc->scenario_idx = snap.base.scenario_idx;
    sc->year = snap.base.year;
    sc->month = snap.base.month;
    sc->num_castles = snap.base.num_castles;
    sc->num_officers = snap.base.num_officers;
    memcpy(sc->castles, snap.base.castles, sizeof(sc->castles));
    memcpy(sc->officers, snap.base.officers, sizeof(sc->officers));

    ts->player_faction = snap.base.player_faction;
    ts->year = snap.base.turn_year;
    ts->month = snap.base.turn_month;
    memcpy(ts->acted, snap.base.acted, sizeof(ts->acted));

    if (v1) {
        // Legacy slot: rebuild the initial heirloom ownership for this state.
        treasure_init(&ts->treasures, sc);
    } else {
        memcpy(&ts->treasures, &snap.treasures, sizeof(ts->treasures));
    }

    return true;
}

uint32_t save_state_hash(const Scenario *sc, const TurnState *ts) {
    SaveSnapshot snap;
    snapshot_fill(&snap, sc, ts);

    const uint8_t *p = (const uint8_t *)&snap;
    uint32_t h = 2166136261u;              // FNV-1a offset basis
    for (size_t i = 0; i < sizeof(snap); i++) {
        h ^= p[i];
        h *= 16777619u;                    // FNV prime
    }
    return h;
}

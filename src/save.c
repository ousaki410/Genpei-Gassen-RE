#include "save.h"

#include <stdio.h>
#include <string.h>

// On-disk record. Every struct in this project is packed, so the file image is
// stable across compilers for a given ABI.
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
} SaveSnapshot;

#pragma pack(pop)

size_t save_snapshot_size(void) { return sizeof(SaveSnapshot); }

static void snapshot_fill(SaveSnapshot *s, const Scenario *sc,
                          const TurnState *ts) {
    memset(s, 0, sizeof(*s));
    memcpy(s->magic, SAVE_MAGIC, sizeof(s->magic));
    s->version = SAVE_VERSION;
    s->scenario_idx = sc->scenario_idx;
    s->year = sc->year;
    s->month = sc->month;
    s->num_castles = sc->num_castles;
    s->num_officers = sc->num_officers;
    memcpy(s->castles, sc->castles, sizeof(s->castles));
    memcpy(s->officers, sc->officers, sizeof(s->officers));
    s->player_faction = ts->player_faction;
    s->turn_year = ts->year;
    s->turn_month = ts->month;
    memcpy(s->acted, ts->acted, sizeof(s->acted));
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
    SaveSnapshot snap;
    size_t got = fread(&snap, 1, sizeof(snap), f);
    fclose(f);

    if (got != sizeof(snap)) {
        fprintf(stderr, "[ERROR] '%s' is truncated (%zu/%zu bytes)\n",
                path, got, sizeof(snap));
        return false;
    }
    if (memcmp(snap.magic, SAVE_MAGIC, sizeof(snap.magic)) != 0) {
        fprintf(stderr, "[ERROR] '%s' is not a YPHZ snapshot\n", path);
        return false;
    }
    if (snap.version != SAVE_VERSION) {
        fprintf(stderr, "[ERROR] '%s' has unsupported version %u\n",
                path, snap.version);
        return false;
    }
    if (snap.num_castles <= 0 || snap.num_castles > MAX_CASTLES ||
        snap.num_officers <= 0 || snap.num_officers > MAX_OFFICERS) {
        fprintf(stderr, "[ERROR] '%s' has invalid counts (%d/%d)\n",
                path, snap.num_castles, snap.num_officers);
        return false;
    }

    // Copy only the mutable state; title_sjis and the static tables stay as
    // loaded from Sndata.gp.
    sc->scenario_idx = snap.scenario_idx;
    sc->year = snap.year;
    sc->month = snap.month;
    sc->num_castles = snap.num_castles;
    sc->num_officers = snap.num_officers;
    memcpy(sc->castles, snap.castles, sizeof(sc->castles));
    memcpy(sc->officers, snap.officers, sizeof(sc->officers));

    ts->player_faction = snap.player_faction;
    ts->year = snap.turn_year;
    ts->month = snap.turn_month;
    memcpy(ts->acted, snap.acted, sizeof(ts->acted));

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

#include "dos_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sndata.gp offsets (per scenario block).
#define SND_OFF_OFFICERS 0x0024
#define SND_OFF_CASTLES  0x7056
#define SND_OFF_GAP      0x6F5B
#define SND_GAP_SIZE     251
#define SND_OFF_TAIL     0x7BEA
#define SND_TAIL_SIZE    9544

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static uint8_t *read_file(const char *path, size_t *out_size) {
    if (!path || !path[0]) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size) *out_size = (size_t)sz;
    return buf;
}

static bool slot_used(const uint8_t *slot) {
    int year = rd16(slot);
    int month = slot[2];
    return year >= 1100 && year <= 1300 && month >= 1 && month <= 12;
}

// Pick the scenario whose start date is the latest one not after the save
// date. `hint` is the small u16 at slot+4, which empirically holds the
// scenario index (0,0,2 for the three real slots); it is trusted when its
// start date is not in the future.
static int infer_scenario(const uint8_t *snd, size_t snd_size,
                          int year, int month, int hint) {
    int step = (int)(snd_size / MAX_SCENARIOS);
    if (step <= 0) return 0;

    int save_months = year * 12 + month;

    if (hint >= 0 && hint < MAX_SCENARIOS) {
        int base = hint * step;
        int sy = rd16(snd + base);
        int sm = snd[base + 2];
        if (sy * 12 + sm <= save_months + 6) return hint;
    }

    int best = 0;
    int best_key = -1;
    for (int i = 0; i < MAX_SCENARIOS; i++) {
        int base = i * step;
        int sy = rd16(snd + base);
        int sm = snd[base + 2];
        int start = sy * 12 + sm;
        int key = (start <= save_months) ? (save_months - start)
                                         : 100000 + (start - save_months);
        if (best_key < 0 || key < best_key) {
            best_key = key;
            best = i;
        }
    }
    return best;
}

bool dos_save_read_info(const char *path, int slot, const char *snd_path,
                        DosSaveInfo *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->slot = slot;
    out->scenario_idx = -1;
    out->player_faction = -1;
    out->leader_officer = -1;
    out->leader_castle = -1;
    if (slot < 0 || slot >= DOS_SLOT_COUNT) return false;

    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "[ERROR] cannot read DOS save '%s'\n", path);
        return false;
    }
    if (size < (size_t)(slot + 1) * DOS_SLOT_SIZE) {
        fprintf(stderr, "[ERROR] '%s' is too small for slot %d\n", path, slot);
        free(data);
        return false;
    }

    const uint8_t *s = data + (size_t)slot * DOS_SLOT_SIZE;
    out->year = rd16(s);
    out->month = s[2];
    out->used = slot_used(s);
    if (out->used) {
        size_t snd_size = 0;
        uint8_t *snd = read_file(snd_path, &snd_size);
        if (snd) {
            int hint = (int)rd16(s + 4);
            if (hint >= MAX_SCENARIOS) hint = -1;
            out->scenario_idx = infer_scenario(snd, snd_size, out->year,
                                               out->month, hint);
            free(snd);
        } else {
            fprintf(stderr, "[WARN] cannot read '%s'; importing as scenario 0\n",
                    snd_path);
            out->scenario_idx = 0;
        }
    }
    free(data);
    return true;
}

bool dos_save_apply(const char *path, int slot, Scenario *sc, DosSaveInfo *info) {
    if (!sc || !info || slot < 0 || slot >= DOS_SLOT_COUNT) return false;
    if (!info->used) return false;

    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) return false;
    if (size < (size_t)(slot + 1) * DOS_SLOT_SIZE) {
        free(data);
        return false;
    }
    const uint8_t *s = data + (size_t)slot * DOS_SLOT_SIZE;

    // ---- 401 officers (same 71-byte record layout as Sndata.gp) ----
    for (int i = 0; i < MAX_OFFICERS && i < sc->num_officers; i++) {
        const uint8_t *rec = s + DOS_OFF_OFFICERS + (size_t)i * DOS_REC_OFFICER;
        ScenarioOfficer *o = &sc->officers[i];
        memset(o, 0, sizeof(*o));
        o->id = i;
        memcpy(o->family_sjis, rec + 2, 7);
        o->family_sjis[7] = 0;
        memcpy(o->given_sjis, rec + 16, 7);
        o->given_sjis[7] = 0;
        snprintf(o->full_name_sjis, sizeof(o->full_name_sjis), "%s%s",
                 o->family_sjis, o->given_sjis);
        memcpy(o->family_kana, rec + 9, 7);
        o->family_kana[7] = 0;
        memcpy(o->given_kana, rec + 23, 7);
        o->given_kana[7] = 0;
        o->face_id = (rec[29] == 0 && rec[28] < 90) ? rec[28] : -1;
        o->age = rec[32];
        o->lead = rec[49];
        o->war = rec[51];
        o->intel = rec[52];
        o->politics = rec[54];
        o->loyalty = rec[56];
        o->charm = rec[59];
        int fid = rd16(rec + 62);
        o->faction_id = (fid < 15) ? fid : -1;
        int cid = rec[64];
        o->castle_id = (cid < MAX_CASTLES) ? cid : -1;
        o->troops = rd16(rec + 66);
        if (o->troops == 0xFFFF) o->troops = 0;
        o->is_active = (o->age > 0 && o->full_name_sjis[0] != 0);
    }

    // ---- 39 castles ----
    for (int i = 0; i < MAX_CASTLES && i < sc->num_castles; i++) {
        const uint8_t *rec = s + DOS_OFF_CASTLES + (size_t)i * DOS_REC_CASTLE;
        ScenarioCastle *cs = &sc->castles[i];
        int map_x = cs->map_x;
        int map_y = cs->map_y;
        memset(cs, 0, sizeof(*cs));
        cs->id = i;
        cs->map_x = map_x;
        cs->map_y = map_y;
        memcpy(cs->name_sjis, rec, 10);
        cs->name_sjis[10] = 0;
        memcpy(cs->kana, rec + 10, 10);
        cs->kana[10] = 0;
        for (int k = 1; k < 8; k++) {
            uint16_t conn = rd16(rec + 16 + k * 2);
            if (conn != 0xFFFF && conn < MAX_CASTLES) {
                cs->conns[cs->num_conns++] = conn;
            }
        }
        int lord = rd16(rec + 32);
        cs->lord_officer_id = (lord < MAX_OFFICERS) ? lord : -1;
        cs->circuit_id = rd16(rec + 38);
        int fid = rd16(rec + 40);
        cs->faction_id = (fid < 15) ? fid : -1;
        cs->rice = rd16(rec + 42);
        cs->gold = rd16(rec + 44);
        cs->troops = rd16(rec + 46);
        cs->training = rd16(rec + 48);
        cs->defense = rd16(rec + 50);
        cs->morale = rd16(rec + 52);
    }

    sc->year = info->year;
    sc->month = info->month;

    // ---- resolve the saved leader: the summary holds family + given name ----
    char family[8] = {0};
    char given[8] = {0};
    for (int k = 0; k < 7 && s[6 + k]; k++) family[k] = (char)s[6 + k];
    for (int k = 0; k < 5 && s[13 + k]; k++) given[k] = (char)s[13 + k];

    int best = -1;
    int best_score = -1;
    if (given[0]) {
        for (int i = 0; i < sc->num_officers; i++) {
            const ScenarioOfficer *o = &sc->officers[i];
            if (!o->is_active || strcmp(o->given_sjis, given) != 0) continue;
            int score = 2;
            if (family[0] && strncmp(o->family_sjis, family, strlen(family)) == 0) {
                score += 2;
            }
            if (o->castle_id >= 0 && o->castle_id < sc->num_castles &&
                sc->castles[o->castle_id].lord_officer_id == i) {
                score += 1;
            }
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
    }
    if (best < 0) {
        best = scenario_faction_leader(sc, sc->castles[30].faction_id);
    }
    if (best >= 0) {
        info->leader_officer = best;
        info->player_faction = sc->officers[best].faction_id;
        info->leader_castle = sc->officers[best].castle_id;
    } else {
        info->player_faction = sc->castles[30].faction_id;
    }

    printf("[DOS] import slot=%d date=%d/%d scenario=%d leader=%d faction=%d castle=%d\n",
           slot, info->year, info->month, info->scenario_idx, info->leader_officer,
           info->player_faction, info->leader_castle);

    free(data);
    return true;
}

static bool write_all(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] cannot write '%s'\n", path);
        return false;
    }
    size_t n = fwrite(data, 1, size, f);
    bool ok = (n == size);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool dos_save_export(const char *path, int slot, const char *snd_path,
                     const Scenario *sc, const TurnState *ts) {
    if (!path || !path[0] || !snd_path || !sc || !ts) return false;
    if (slot < 0 || slot >= DOS_SLOT_COUNT) return false;

    size_t size = 0;
    uint8_t *file = read_file(path, &size);
    if (!file) {
        fprintf(stderr, "[ERROR] cannot read DOS save '%s' for export\n", path);
        return false;
    }
    if (size != (size_t)DOS_SLOT_COUNT * DOS_SLOT_SIZE) {
        fprintf(stderr, "[ERROR] '%s' has unexpected size %zu (want %d)\n",
                path, size, DOS_SLOT_COUNT * DOS_SLOT_SIZE);
        free(file);
        return false;
    }

    size_t snd_size = 0;
    uint8_t *snd = read_file(snd_path, &snd_size);
    if (!snd) {
        fprintf(stderr, "[ERROR] cannot read '%s' for export\n", snd_path);
        free(file);
        return false;
    }
    int snd_step = (int)(snd_size / MAX_SCENARIOS);
    if (snd_step <= 0 || sc->scenario_idx < 0 || sc->scenario_idx >= MAX_SCENARIOS) {
        free(snd);
        free(file);
        return false;
    }
    const uint8_t *snd_base = snd + (size_t)sc->scenario_idx * snd_step;

    uint8_t *out = (uint8_t *)malloc(DOS_SLOT_SIZE);
    if (!out) {
        free(snd);
        free(file);
        return false;
    }

    uint8_t *target = file + (size_t)slot * DOS_SLOT_SIZE;
    bool target_used = slot_used(target);
    // Empty slots have no usable officer/castle data; clone slot 0's record to
    // keep the save-only blocks structurally valid.
    const uint8_t *base = target_used ? target : file;
    memcpy(out, base, DOS_SLOT_SIZE);

    int hint = (int)rd16(base + 4);
    if (hint >= MAX_SCENARIOS) hint = -1;
    int base_sc = infer_scenario(snd, snd_size, rd16(base), base[2], hint);
    bool preserve = target_used && base_sc == sc->scenario_idx;

    if (!preserve) {
        memcpy(out + DOS_OFF_OFFICERS,
               snd_base + SND_OFF_OFFICERS, MAX_OFFICERS * DOS_REC_OFFICER);
        memcpy(out + DOS_OFF_GAP, snd_base + SND_OFF_GAP, SND_GAP_SIZE);
        memcpy(out + DOS_OFF_CASTLES,
               snd_base + SND_OFF_CASTLES, MAX_CASTLES * DOS_REC_CASTLE);
        memcpy(out + DOS_OFF_TAIL, snd_base + SND_OFF_TAIL, SND_TAIL_SIZE);
        memcpy(out + DOS_OFF_HEADER2, snd_base, DOS_HEADER2_SIZE);
    }

    // ---- summary header (year/month/flags/scenario/clan/leader) ----
    wr16(out + 0, (uint16_t)ts->year);
    out[2] = (uint8_t)ts->month;
    out[3] = 1;
    wr16(out + 4, (uint16_t)sc->scenario_idx);

    int leader = scenario_faction_leader(sc, ts->player_faction);
    // The 18-byte summary stores the leading officer's clan and given name
    // (e.g. 平 / 清盛, 源 / 頼朝, 藤原 / 秀衡), not the faction display name.
    memset(out + 6, 0, 7);
    memset(out + 13, 0, 5);
    if (leader >= 0) {
        size_t n = strlen(sc->officers[leader].family_sjis);
        if (n > 6) n = 6;
        memcpy(out + 6, sc->officers[leader].family_sjis, n);
        n = strlen(sc->officers[leader].given_sjis);
        if (n > 4) n = 4;
        memcpy(out + 13, sc->officers[leader].given_sjis, n);
    } else {
        const FactionDef *fac = scenario_get_faction(ts->player_faction);
        if (fac) {
            size_t n = strlen(fac->name_sjis);
            if (n > 6) n = 6;
            memcpy(out + 6, fac->name_sjis, n);
        }
    }

    // Second header (year/month) always follows the live calendar.
    wr16(out + DOS_OFF_HEADER2, (uint16_t)ts->year);
    out[DOS_OFF_HEADER2 + 2] = (uint8_t)ts->month;

    // ---- patch the 401 officer records ----
    // Names/kana are rewritten too: an exported slot may be rebuilt from
    // Sndata, whose sentinel record 400 holds different trailing bytes than a
    // played save. Writing the parsed fields keeps import(export(x)) == x.
    for (int i = 0; i < MAX_OFFICERS; i++) {
        uint8_t *rec = out + DOS_OFF_OFFICERS + (size_t)i * DOS_REC_OFFICER;
        const ScenarioOfficer *o = &sc->officers[i];
        memcpy(rec + 2, o->family_sjis, 7);
        memcpy(rec + 9, o->family_kana, 7);
        memcpy(rec + 16, o->given_sjis, 7);
        memcpy(rec + 23, o->given_kana, 7);
        if (o->face_id >= 0 && o->face_id < 90) {
            rec[28] = (uint8_t)o->face_id;
            rec[29] = 0;
        }
        rec[32] = (uint8_t)o->age;
        rec[49] = (uint8_t)o->lead;
        rec[51] = (uint8_t)o->war;
        rec[52] = (uint8_t)o->intel;
        rec[54] = (uint8_t)o->politics;
        rec[56] = (uint8_t)o->loyalty;
        rec[59] = (uint8_t)o->charm;
        wr16(rec + 62, (uint16_t)((o->faction_id < 0) ? 0xFFFF : o->faction_id));
        rec[64] = (uint8_t)((o->castle_id < 0) ? 0xFF : o->castle_id);
        wr16(rec + 66, (uint16_t)o->troops);
    }

    // ---- patch the 39 castle records ----
    for (int i = 0; i < MAX_CASTLES; i++) {
        uint8_t *rec = out + DOS_OFF_CASTLES + (size_t)i * DOS_REC_CASTLE;
        const ScenarioCastle *cs = &sc->castles[i];
        wr16(rec + 32, (uint16_t)((cs->lord_officer_id < 0)
                                      ? 0xFFFF : cs->lord_officer_id));
        wr16(rec + 40, (uint16_t)((cs->faction_id < 0) ? 0xFFFF : cs->faction_id));
        wr16(rec + 42, (uint16_t)cs->rice);
        wr16(rec + 44, (uint16_t)cs->gold);
        wr16(rec + 46, (uint16_t)cs->troops);
        wr16(rec + 48, (uint16_t)cs->training);
        wr16(rec + 50, (uint16_t)cs->defense);
        wr16(rec + 52, (uint16_t)cs->morale);
    }

    // ---- back up once, then replace atomically ----
    char bak[600];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    FILE *bf = fopen(bak, "rb");
    if (bf) {
        fclose(bf);
    } else if (!write_all(bak, file, size)) {
        fprintf(stderr, "[WARN] could not create backup '%s'\n", bak);
    }

    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    bool ok = false;
    FILE *tf = fopen(tmp, "wb");
    if (tf) {
        size_t prefix = (size_t)slot * DOS_SLOT_SIZE;
        size_t suffix_off = ((size_t)slot + 1) * DOS_SLOT_SIZE;
        ok = fwrite(file, 1, prefix, tf) == prefix &&
             fwrite(out, 1, DOS_SLOT_SIZE, tf) == DOS_SLOT_SIZE &&
             fwrite(file + suffix_off, 1, size - suffix_off, tf) ==
                 size - suffix_off;
        if (fclose(tf) != 0) ok = false;
    }
    if (ok) {
        remove(path);
        if (rename(tmp, path) != 0) ok = false;
    }
    if (!ok) {
        remove(tmp);
        fprintf(stderr, "[ERROR] DOS export failed for '%s'\n", path);
    } else {
        printf("[DOS] export slot=%d date=%d/%d faction=%d %s\n",
               slot, ts->year, ts->month, ts->player_faction,
               preserve ? "(patched in place)" : "(rebuilt from Sndata)");
        printf("[DOS] backup: %s\n", bak);
    }

    free(out);
    free(snd);
    free(file);
    return ok;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scenario.h"

// Visual coordinates on 1696x360 strategic map for 39 castles
static const struct { int x, y; } s_castle_coords[MAX_CASTLES] = {
    [0] = {95, 285}, // 肥後国府
    [1] = {240, 215}, // 豊後国府
    [2] = {55, 215}, // 太宰府
    [3] = {170, 195}, // 彦島
    [4] = {385, 205}, // 伊予国府
    [5] = {490, 240}, // 土佐国府
    [6] = {565, 195}, // 屋島
    [7] = {635, 275}, // 阿波国府
    [8] = {130, 120}, // 下関
    [9] = {240, 125}, // 周防国府
    [10] = {335, 130}, // 厳島
    [11] = {415, 130}, // 備後国府
    [12] = {495, 135}, // 備中国府
    [13] = {605, 50}, // 美作国府
    [14] = {675, 115}, // 播磨国府
    [15] = {810, 100}, // 丹波国府
    [16] = {780, 175}, // 福原
    [17] = {935, 175}, // 平安京
    [18] = {865, 270}, // 興福寺
    [19] = {900, 335}, // 熊野新宮
    [20] = {995, 300}, // 伊勢国府
    [21] = {1025, 75}, // 越前国府
    [22] = {1075, 220}, // 美濃国府
    [23] = {1310, 50}, // 越中国府
    [24] = {1135, 50}, // 越後国府
    [25] = {1270, 190}, // 木曽谷
    [26] = {1115, 280}, // 尾張国府
    [27] = {1270, 290}, // 遠江国府
    [28] = {1390, 200}, // 甲斐国府
    [29] = {1415, 320}, // 伊豆国府
    [30] = {1480, 290}, // 鎌倉
    [31] = {1520, 220}, // 武蔵国府
    [32] = {1480, 140}, // 上野国府
    [33] = {1575, 155}, // 下野国府
    [34] = {1565, 230}, // 下総国府
    [35] = {1540, 320}, // 上総国府
    [36] = {1625, 220}, // 常陸国府
    [37] = {1480, 50}, // 出羽国府
    [38] = {1625, 50}, // 平泉
};

static const FactionDef s_factions[15] = {
    [0] = {0, "\x8a\x99\x91\x71\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 鎌倉源氏
    [1] = {1, "\x95\xbd\x8e\x81", "\x90\xd4\x8a\xf8", 2}, // 平氏
    [2] = {2, "\x96\xd8\x91\x5d\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 木曽源氏
    [3] = {3, "\x89\x9c\x8f\x42\x93\xa1\x8c\xb4\x8e\x81", "\x8b\xe0\x8a\xf8", 6}, // 奥州藤原氏
    [4] = {4, "\x8d\x62\x94\xe3\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 甲斐源氏
    [5] = {5, "\x8f\xed\x97\xa4\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 常陸源氏
    [6] = {6, "\x8b\xdf\x8d\x5d\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 近江源氏
    [7] = {7, "\x94\xfc\x94\x5a\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 美濃源氏
    [8] = {8, "\x90\xdb\x92\xc3\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 摂津源氏
    [9] = {9, "\x90\x56\x8b\x7b\x8c\xb9\x8e\x81", "\x94\x92\x8a\xf8", 7}, // 新宮源氏
    [10] = {10, "\x89\x7a\x8c\xe3\x8f\xe9\x8e\x81", "\x90\xd4\x8a\xf8", 2}, // 越後城氏
    [11] = {11, "\x8c\x46\x96\xec\x95\xca\x93\x96", "\x8c\x46\x96\xec\x8a\xf8", 3}, // 熊野別当
    [12] = {12, "\x88\xc9\x97\x5c\x89\xcd\x96\xec\x8e\x81", "\x90\x85\x8c\x52\x8a\xf8", 1}, // 伊予河野氏
    [13] = {13, "\x8b\xbb\x95\x9f\x8e\x9b\x91\x6d\x95\xba", "\x91\x6d\x95\xba\x8a\xf8", 8}, // 興福寺僧兵
    [14] = {14, "\x94\xe4\x89\x62\x8e\x52\x91\x6d\x95\xba", "\x91\x6d\x95\xba\x8a\xf8", 8}, // 比叡山僧兵
};

static const char *s_circuits[8] = {
    [0] = "\x90\xbc\x8a\x43\x93\xb9\x81\x69\x8b\xe3\x8f\x42\x81\x6a", // 西海道（九州）
    [1] = "\x93\xec\x8a\x43\x93\xb9\x81\x69\x8e\x6c\x8d\x91\x81\x6a", // 南海道（四国）
    [2] = "\x8e\x52\x97\x7a\x81\x45\x8e\x52\x89\x41\x81\x69\x92\x86\x8d\x91\x81\x6a", // 山陽・山陰（中国）
    [3] = "\x8b\x45\x93\xe0\x81\x69\x8b\xdf\x8b\x45\x81\x6a", // 畿内（近畿）
    [4] = "\x96\x6b\x97\xa4\x93\xb9\x81\x69\x90\x4d\x89\x7a\x81\x6a", // 北陸道（信越）
    [5] = "\x93\x8c\x8a\x43\x93\xb9\x81\x69\x93\x8c\x8a\x43\x81\x6a", // 東海道（東海）
    [6] = "\x8a\xd6\x93\x8c\x81\x69\x8d\xe2\x93\x8c\x94\xaa\x8d\x91\x81\x6a", // 関東（坂東八国）
    [7] = "\x89\x9c\x89\x48\x81\x69\x93\x8c\x96\x6b\x81\x6a", // 奥羽（東北）
};

static const char *s_sc_titles[MAX_SCENARIOS] = {
    [0] = "\x8e\xa1\x8f\xb3\x8e\x6c\x94\x4e\x8b\xe3\x8c\x8e\x81\x46\x8c\xb9\x8e\x81\x8b\x93\x95\xba\x81\x45\x89\xa9\x90\xa3\x90\xec\x91\xce\x96\xca", // 治承四年九月：源氏挙兵・黄瀬川対面
    [1] = "\x8e\xf5\x89\x69\x93\xf1\x94\x4e\x98\x5a\x8c\x8e\x81\x46\x8b\xe4\x97\x98\x89\xbe\x97\x85\x93\xbb\x81\x45\x8b\x60\x92\x87\x93\xfc\x8b\x9e", // 寿永二年六月：倶利伽羅峠・義仲入京
    [2] = "\x8c\xb3\x97\xef\x8c\xb3\x94\x4e\x8b\xe3\x8c\x8e\x81\x46\x88\xea\x83\x6d\x92\x4a\x8d\x87\x90\xed\x81\x45\x8c\xb9\x95\xbd\x91\x88\x94\x65", // 元暦元年九月：一ノ谷合戦・源平争覇
    [3] = "\x95\xb6\x8e\xa1\x8c\xb3\x94\x4e\x8b\xe3\x8c\x8e\x81\x46\x92\x64\x83\x6d\x89\x59\x8c\x88\x90\xed\x81\x45\x95\xbd\x8e\x81\x95\xa2\x96\xc5", // 文治元年九月：壇ノ浦決戦・平氏覆滅
};

const FactionDef* scenario_get_faction(int faction_id) {
    if (faction_id >= 0 && faction_id < 15) return &s_factions[faction_id];
    return &s_factions[0];
}

const char* scenario_get_circuit_name(int circuit_id) {
    if (circuit_id >= 0 && circuit_id < 8) return s_circuits[circuit_id];
    return "";
}

const char* scenario_get_title(int scenario_idx) {
    if (scenario_idx >= 0 && scenario_idx < MAX_SCENARIOS) {
        return s_sc_titles[scenario_idx];
    }
    return "";
}

int scenario_get_year(int scenario_idx) {
    static const int years[MAX_SCENARIOS] = {1180, 1183, 1184, 1185};
    if (scenario_idx >= 0 && scenario_idx < MAX_SCENARIOS) return years[scenario_idx];
    return 0;
}

int scenario_get_month(int scenario_idx) {
    static const int months[MAX_SCENARIOS] = {9, 6, 9, 9};
    if (scenario_idx >= 0 && scenario_idx < MAX_SCENARIOS) return months[scenario_idx];
    return 0;
}

int scenario_faction_leader(const Scenario *sc, int faction_id) {
    if (!sc || faction_id < 0) return -1;

    // The castle records own the reliable faction assignment, so prefer the
    // strongest of the faction's castle lords (historically the daimyo).
    int best = -1;
    int best_score = -1;
    for (int c = 0; c < sc->num_castles; c++) {
        const ScenarioCastle *cs = &sc->castles[c];
        if (cs->faction_id != faction_id) continue;
        int oid = cs->lord_officer_id;
        if (oid < 0 || oid >= sc->num_officers) continue;
        const ScenarioOfficer *o = &sc->officers[oid];
        int score = o->lead + o->war + o->intel + o->politics + o->charm;
        if (score > best_score) {
            best_score = score;
            best = oid;
        }
    }
    if (best >= 0) return best;

    // Fallback for factions whose castles are all neutral.
    for (int i = 0; i < sc->num_officers; i++) {
        const ScenarioOfficer *o = &sc->officers[i];
        if (!o->is_active || o->faction_id != faction_id) continue;
        int score = o->lead + o->war + o->intel + o->politics + o->charm;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

bool scenario_load(const char *sndata_path, int scenario_idx, Scenario *out_sc) {
    if (!sndata_path || !out_sc || scenario_idx < 0 || scenario_idx >= MAX_SCENARIOS) return false;
    memset(out_sc, 0, sizeof(Scenario));

    FILE *f = fopen(sndata_path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open scenario data: %s\n", sndata_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t*)malloc(fsize);
    if (!data) { fclose(f); return false; }
    if (fread(data, 1, fsize, f) != fsize) { free(data); fclose(f); return false; }
    fclose(f);

    size_t step = fsize / 4; // 41266 bytes per scenario
    size_t base = scenario_idx * step;
    if (base + 0x7BEA > fsize) { free(data); return false; }

    out_sc->scenario_idx = scenario_idx;
    out_sc->year = data[base] | (data[base + 1] << 8);
    out_sc->month = data[base + 2];
    out_sc->title_sjis = s_sc_titles[scenario_idx];

    // 1. Parse 401 Historical Officers
    size_t off_officers = base + 0x0024;
    out_sc->num_officers = MAX_OFFICERS;
    for (int i = 0; i < MAX_OFFICERS; i++) {
        size_t off = off_officers + i * 71;
        if (off + 71 > fsize) break;
        const uint8_t *rec = data + off;
        ScenarioOfficer *o = &out_sc->officers[i];
        o->id = i;
        memcpy(o->family_sjis, rec + 2, 7);
        o->family_sjis[7] = 0;
        memcpy(o->given_sjis, rec + 16, 7);
        o->given_sjis[7] = 0;
        snprintf(o->full_name_sjis, sizeof(o->full_name_sjis), "%s%s", o->family_sjis, o->given_sjis);
        memcpy(o->family_kana, rec + 9, 7);
        o->family_kana[7] = 0;
        memcpy(o->given_kana, rec + 23, 7);
        o->given_kana[7] = 0;
        o->face_id = (rec[29] == 0 && rec[28] < 90) ? rec[28] : -1;
        o->age = rec[32];
        o->war = rec[51];        // 武力
        o->intel = rec[52];      // 知才
        o->politics = rec[54];   // 優雅
        o->charm = rec[59];      // 弓術
        o->loyalty = rec[56];    // 忠節
        o->lead = rec[49];       // 兵質 / 指揮
        int fid = rec[62] | (rec[63] << 8);
        o->faction_id = (fid >= 0 && fid < 15) ? fid : -1;
        int cid = rec[64];
        o->castle_id = (cid >= 0 && cid < MAX_CASTLES) ? cid : -1;
        o->troops = (rec[66] | (rec[67] << 8));
        if (o->troops == 65535) o->troops = 0;
        o->is_active = (o->age > 0 && o->full_name_sjis[0] != 0);
    }

    // 2. Parse 39 Strategic Castles
    size_t off_castles = base + 0x7056;
    out_sc->num_castles = MAX_CASTLES;
    for (int i = 0; i < MAX_CASTLES; i++) {
        size_t off = off_castles + i * 76;
        if (off + 76 > fsize) break;
        const uint8_t *c = data + off;
        ScenarioCastle *cs = &out_sc->castles[i];
        cs->id = i;
        memcpy(cs->name_sjis, c, 10);
        cs->name_sjis[10] = 0;
        memcpy(cs->kana, c + 10, 10);
        cs->kana[10] = 0;
        cs->map_x = s_castle_coords[i].x;
        cs->map_y = s_castle_coords[i].y;
        cs->num_conns = 0;
        for (int k = 1; k < 8; k++) {
            uint16_t conn = c[16 + k * 2] | (c[16 + k * 2 + 1] << 8);
            if (conn != 0xFFFF && conn < MAX_CASTLES) {
                cs->conns[cs->num_conns++] = conn;
            }
        }
        cs->lord_officer_id = c[32] | (c[33] << 8);
        cs->circuit_id = c[38] | (c[39] << 8);
        cs->faction_id = c[40] | (c[41] << 8);
        cs->rice = c[42] | (c[43] << 8);
        cs->gold = c[44] | (c[45] << 8);
        cs->troops = c[46] | (c[47] << 8);
        cs->training = c[48] | (c[49] << 8);
        cs->defense = c[50] | (c[51] << 8);
        cs->morale = c[52] | (c[53] << 8);
        cs->node_id = c[74] | (c[75] << 8);
    }
    free(data);
    printf("[+] Loaded Scenario %d (%d AD): %d castles, %d officers\n",
           scenario_idx + 1, out_sc->year, out_sc->num_castles, out_sc->num_officers);
    return true;
}

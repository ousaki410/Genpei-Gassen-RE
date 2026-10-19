#include "event.h"

#include <stddef.h>

// ---------------------------------------------------------------------------
// SJIS labels (PC-98 FONT.CHR has no usable lower-case ASCII)
// ---------------------------------------------------------------------------

#define SJIS_MINAMOTO_FLAG "\x8c\xb9\x8e\x81\x8b\x93\x95\xba"                  // 源氏挙兵
#define SJIS_FUJIKAWA      "\x95\x78\x8e\x6d\x90\xec"                          // 富士川
#define SJIS_KURIKARA      "\x8b\xe4\x97\x98\x89\xbe\x97\x85\x93\xbb"          // 倶利伽羅峠
#define SJIS_ICHINOTANI    "\x88\xea\x83\x6d\x92\x4a"                          // 一ノ谷
#define SJIS_YASHIMA       "\x89\xae\x93\x87"                                  // 屋島
#define SJIS_DANNNOURA     "\x92\x64\x83\x6d\x89\x59"                          // 壇ノ浦
#define SJIS_HIRAIZUMI     "\x95\xbd\x90\xf2"                                  // 平泉
#define SJIS_HEISHI_END    "\x95\xbd\x8e\x81\x96\xc5\x96\x53"                  // 平氏滅亡

#define SJIS_KW_FLAG       "\x8b\x93\x95\xba"                                  // 挙兵
#define SJIS_KW_FUJIKAWA   SJIS_FUJIKAWA
#define SJIS_KW_KURIKARA   SJIS_KURIKARA
#define SJIS_KW_ICHINOTANI SJIS_ICHINOTANI
#define SJIS_KW_YASHIMA    SJIS_YASHIMA
#define SJIS_KW_DANNNOURA  SJIS_DANNNOURA
#define SJIS_KW_HIRAIZUMI  SJIS_HIRAIZUMI
#define SJIS_KW_HEISHI     SJIS_HEISHI_END

// ---------------------------------------------------------------------------
// The event book (chronological).  Fragments of the Genpei war, mapped onto
// the eight large Mainevt.gp CGs.
// ---------------------------------------------------------------------------

static const HistoricalEvent s_events[] = {
    // 1180/9  以仁王の令旨 → 源頼朝挙兵 (伊豆)
    { 1180,  9, 0, -1, SJIS_MINAMOTO_FLAG, "MINAMOTO-UPRISING", SJIS_KW_FLAG },
    // 1180/10 富士川の戦い (平維盛 vs 源頼朝)
    { 1180, 10, 1, -1, SJIS_FUJIKAWA,      "FUJIKAWA",         SJIS_KW_FUJIKAWA },
    // 1183/5  倶利伽羅峠の戦い (源義仲 vs 平維盛)
    { 1183,  5, 2, -1, SJIS_KURIKARA,      "KURIKARA",         SJIS_KW_KURIKARA },
    // 1184/2  一ノ谷の戦い (源義経の鵯越)
    { 1184,  2, 3, -1, SJIS_ICHINOTANI,    "ICHINOTANI",       SJIS_KW_ICHINOTANI },
    // 1185/2  屋島の戦い (那須与一の扇)
    { 1185,  2, 4, -1, SJIS_YASHIMA,       "YASHIMA",          SJIS_KW_YASHIMA },
    // 1185/3  壇ノ浦の戦い (平氏滅亡)
    { 1185,  3, 5, -1, SJIS_DANNNOURA,     "DANNNOURA",        SJIS_KW_DANNNOURA },
    // 奥州平泉 (castle 38) 陥落 → 奥州藤原氏滅亡
    {    0,  0, 6, 38, SJIS_HIRAIZUMI,     "HIRAIZUMI-FALLS",  SJIS_KW_HIRAIZUMI },
    // 屋島 (castle 6) 陥落 → 平氏滅亡
    {    0,  0, 7,  6, SJIS_HEISHI_END,    "HEIKE-DESTROYED",  SJIS_KW_HEISHI },
};

#define EVENT_COUNT ((int)(sizeof(s_events) / sizeof(s_events[0])))

int event_count(void) { return EVENT_COUNT; }

const HistoricalEvent *event_at(int index) {
    if (index < 0 || index >= EVENT_COUNT) return NULL;
    return &s_events[index];
}

int event_match_date(int year, int month) {
    for (int i = 0; i < EVENT_COUNT; i++) {
        const HistoricalEvent *e = &s_events[i];
        if (e->trigger_castle >= 0) continue;
        if (e->year == year && e->month == month) return i;
    }
    return -1;
}

int event_match_castle_fall(int castle_id) {
    if (castle_id < 0) return -1;
    for (int i = 0; i < EVENT_COUNT; i++) {
        if (s_events[i].trigger_castle == castle_id) return i;
    }
    return -1;
}

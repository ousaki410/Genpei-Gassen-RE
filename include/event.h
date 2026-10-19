#ifndef YPHZ_EVENT_H
#define YPHZ_EVENT_H

#include <stdbool.h>

// ---------------------------------------------------------------------------
// Historical event triggers (子任务 7).
//
// A light table links calendar dates and castle-fall milestones to a Mainevt.gp
// CG block plus an optional Message.gp keyword used to pick a line of narration.
// The host (main.c) owns the rendering; this module only answers "does an event
// fire now?".
//
// Dates follow the Genpei war chronology.  Because turn_end() advances the
// calendar *before* the AI pass, matching happens against the post-advance
// (year, month).
//
//   Mainevt.gp: 13 blocks — 0..3 = 160x128, 4..7 = 128x128, 8..12 = 32x64 icons.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

typedef struct {
    int         year;
    int         month;
    int         block;          // Mainevt.gp block (0..12)
    int         trigger_castle; // castle id whose capture fires this, or -1
    const char *name_sjis;      // headline shown over the CG
    const char *name_ascii;     // headless log label
    const char *keyword_sjis;   // Message.gp substring search, or NULL
} HistoricalEvent;

#pragma pack(pop)

int event_count(void);
const HistoricalEvent *event_at(int index);

// Index of the date-triggered event for (year, month), or -1.
int event_match_date(int year, int month);

// Index of the event fired by the fall of `castle_id`, or -1.
int event_match_castle_fall(int castle_id);

#endif // YPHZ_EVENT_H

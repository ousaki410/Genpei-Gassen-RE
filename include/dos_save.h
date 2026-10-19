#ifndef YPHZ_DOS_SAVE_H
#define YPHZ_DOS_SAVE_H

#include <stdbool.h>
#include <stdint.h>

#include "scenario.h"
#include "turn.h"

// ---------------------------------------------------------------------------
// 子任务 10: original DOS Savedata.gp import / export.
//
// The container is ten fixed 43403-byte slots. Reverse-engineering (see
// tools/probe_savedata.py) established this slot layout for the three used
// slots (0..2); slots 3..9 are an identical empty template:
//
//     0x0000  18 B   save summary: u16 year, u8 month, u8 flag, u16 scenario?,
//                    SJIS family name[7], SJIS given name[5]
//     0x0012  36 B   copy of the Sndata scenario header (year/month/flag +
//                    15 x u16 faction list)
//     0x0036  49 B   faction turn-order / ranking block (save-only)
//     0x0067  401 x 71 B  officer table           (Sndata: 0x0024)
//     0x6F9E  251 B  faction statistics table      (Sndata: 0x6F5B)
//     0x7099  2070 B save-only action/stack block
//     0x78AF  39 x 76 B   castle table             (Sndata: 0x7056)
//     0x8443  9544 B tail (event/action state)     (Sndata tail: identical size)
//
// Versus Sndata.gp the payload therefore drifts by +2137 bytes:
//     header      +67  (18-byte summary + 49-byte order block)
//     inter-table +2070 (save-only block between faction stats and castles)
//     tail         +0
// The tables themselves use the same 71/76-byte record layouts as Sndata.gp,
// so only the mutable fields need to be patched on export.
// ---------------------------------------------------------------------------

#define DOS_SLOT_SIZE      43403
#define DOS_SLOT_COUNT     10
#define DOS_REC_OFFICER    71
#define DOS_REC_CASTLE     76
#define DOS_OFF_OFFICERS   0x0067
#define DOS_OFF_GAP        0x6F9E
#define DOS_GAP_SIZE       251
#define DOS_OFF_EXTRA      0x7099
#define DOS_EXTRA_SIZE     2070
#define DOS_OFF_CASTLES    0x78AF
#define DOS_OFF_TAIL       0x8443
#define DOS_TAIL_SIZE      9544
#define DOS_OFF_HEADER2    0x0012
#define DOS_HEADER2_SIZE   36

typedef struct {
    int  slot;
    int  scenario_idx;    // scenario inferred from the slot header/date
    int  year;
    int  month;
    int  player_faction;  // inferred from the saved leader name (-1 unknown)
    int  leader_officer;
    int  leader_castle;
    bool used;            // false for the empty template slots 3..9
} DosSaveInfo;

// Read the slot summary, validate it and infer the owning scenario. `snd_path`
// is Sndata.gp (used to cross-check the scenario start date / faction list).
bool dos_save_read_info(const char *path, int slot, const char *snd_path,
                        DosSaveInfo *out);

// Overwrite the mutable Scenario fields (401 officers, 39 castles, calendar)
// from the slot and resolve the saved leader. Call after scenario_load() with
// the scenario reported by dos_save_read_info().
bool dos_save_apply(const char *path, int slot, Scenario *sc, DosSaveInfo *info);

// Write the current state back into `slot`. Existing used slots are patched in
// place (unknown regions preserved); empty slots are rebuilt from the current
// Sndata scenario (with slot 0's save-only block as a structural template).
// The original file is backed up once as `<path>.bak`.
bool dos_save_export(const char *path, int slot, const char *snd_path,
                     const Scenario *sc, const TurnState *ts);

#endif // YPHZ_DOS_SAVE_H

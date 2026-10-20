#ifndef YPHZ_MOVIE_H
#define YPHZ_MOVIE_H

#include <stdbool.h>

#include "decompress.h"
#include "npk.h"

// ---------------------------------------------------------------------------
// 子任务 14/15: continuous frame animation player for Opendat.gp / Enddat.gp.
//
// Both files are multi-block NPK016 packs (52 and 84 frames). The player
// advances one frame at a time, swaps blocks, and rotates palette banks so the
// intro/ending loops visibly change colour. Palette *loading* stays in main.c
// because it owns Mainpal.pld / Losepal.pld.
// ---------------------------------------------------------------------------

#define MOVIE_KIND_OPEN 0
#define MOVIE_KIND_END  1

#define MOVIE_FRAMES_PER_BLOCK 8
#define MOVIE_PALETTE_FRAMES   16

#pragma pack(push, 1)

typedef struct {
    bool         active;
    int          kind;
    bool         loop;
    NpkFile      pack;
    ImageDecoded img;
    int          block;
    int          loaded_block;
    int          frame;
    int          palette_bank;
    int          count;
} MovieState;

#pragma pack(pop)

void movie_init(MovieState *m);
bool movie_start(MovieState *m, int kind, bool loop, const char *asset_dir);
bool movie_update(MovieState *m);   // false when a non-looping movie finished
void movie_render(const MovieState *m);
void movie_stop(MovieState *m);
void movie_free(MovieState *m);
const char *movie_pack_name(int kind);
int movie_palette_bank(const MovieState *m);

#endif // YPHZ_MOVIE_H

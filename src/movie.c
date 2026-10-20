#include "movie.h"

#include <stdio.h>
#include <string.h>

#include "hal_video.h"

const char *movie_pack_name(int kind) {
    return (kind == MOVIE_KIND_END) ? "Enddat.gp" : "Opendat.gp";
}

int movie_palette_bank(const MovieState *m) {
    return m ? m->palette_bank : 0;
}

void movie_init(MovieState *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->loaded_block = -1;
}

void movie_free(MovieState *m) {
    if (!m) return;
    free_decoded_image(&m->img);
    npk_free(&m->pack);
    memset(m, 0, sizeof(*m));
    m->loaded_block = -1;
}

void movie_stop(MovieState *m) {
    if (!m || !m->active) return;
    printf("[MOVIE] stop file=%s frame=%d\n", movie_pack_name(m->kind),
           m->frame);
    m->active = false;
}

bool movie_start(MovieState *m, int kind, bool loop, const char *asset_dir) {
    if (!m) return false;
    movie_free(m);
    movie_init(m);

    const char *file = movie_pack_name(kind);
    if (!npk_load(&m->pack, asset_dir, file)) {
        fprintf(stderr, "[ERROR] cannot load movie pack '%s'\n", file);
        return false;
    }
    if (m->pack.count <= 0) {
        fprintf(stderr, "[ERROR] movie pack '%s' has no blocks\n", file);
        movie_free(m);
        return false;
    }
    if (!npk_decode(&m->pack, 0, &m->img)) {
        fprintf(stderr, "[ERROR] cannot decode first frame of '%s'\n", file);
        movie_free(m);
        return false;
    }

    m->kind = kind;
    m->loop = loop;
    m->count = m->pack.count;
    m->block = 0;
    m->loaded_block = 0;
    m->frame = 0;
    m->palette_bank = 0;
    m->active = true;

    printf("[MOVIE] start file=%s blocks=%d loop=%d\n", file, m->count,
           loop ? 1 : 0);
    printf("[MOVIE] block=0/%d frame=0 palette=0\n", m->count);
    return true;
}

bool movie_update(MovieState *m) {
    if (!m || !m->active) return false;

    m->frame++;
    int total = m->count * MOVIE_FRAMES_PER_BLOCK;
    if (m->frame >= total) {
        if (m->loop) {
            m->frame = 0;
        } else {
            printf("[MOVIE] done file=%s blocks=%d frames=%d\n",
                   movie_pack_name(m->kind), m->count, total);
            m->active = false;
            return false;
        }
    }

    m->block = m->frame / MOVIE_FRAMES_PER_BLOCK;
    if (m->block >= m->count) m->block = m->count - 1;
    m->palette_bank = (m->frame / MOVIE_PALETTE_FRAMES) % 4;

    if (m->block != m->loaded_block) {
        ImageDecoded next;
        memset(&next, 0, sizeof(next));
        if (npk_decode(&m->pack, m->block, &next)) {
            free_decoded_image(&m->img);
            m->img = next;
            m->loaded_block = m->block;
            printf("[MOVIE] block=%d/%d frame=%d palette=%d\n",
                   m->block, m->count, m->frame, m->palette_bank);
        }
    }
    return true;
}

void movie_render(const MovieState *m) {
    if (!m || !m->active) return;
    hal_video_fill_rect(0, 0, PC98_WIDTH, 400, 0);
    if (!m->img.pixels) return;
    int x = (PC98_WIDTH - m->img.width) / 2;
    int y = (400 - m->img.height) / 2;
    hal_video_blit_indexed(x, y, m->img.width, m->img.height,
                           m->img.pixels, m->img.width);
}

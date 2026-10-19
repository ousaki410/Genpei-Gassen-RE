#include "input.h"

#include <string.h>

#include "sdl_compat.h"
#include "hal_video.h"

/*
 * Input subsystem implementation.
 *
 * Keyboard edges come from SDL events (reliable, never dropped), while the
 * pointer position and held-button state are sampled with SDL_GetMouseState(),
 * which keeps the mouse state correct even on frames with no motion event.
 */

static bool s_prev_lbutton = false;
static bool s_prev_rbutton = false;
static int  s_prev_mouse_x = 0;
static int  s_prev_mouse_y = 0;
static bool s_have_prev_mouse = false;

void input_init(void) {
    s_prev_lbutton = false;
    s_prev_rbutton = false;
    s_prev_mouse_x = 0;
    s_prev_mouse_y = 0;
    s_have_prev_mouse = false;
}

static YphzKey translate_key(SDL_Keycode sym) {
    switch (sym) {
        case SDLK_ESCAPE:       return YPHZ_KEY_ESCAPE;
        case SDLK_RETURN:       return YPHZ_KEY_ENTER;
        case SDLK_KP_ENTER:     return YPHZ_KEY_ENTER;
        case SDLK_TAB:          return YPHZ_KEY_TAB;
        case SDLK_SPACE:        return YPHZ_KEY_SPACE;
        case SDLK_LEFT:         return YPHZ_KEY_LEFT;
        case SDLK_RIGHT:        return YPHZ_KEY_RIGHT;
        case SDLK_UP:           return YPHZ_KEY_UP;
        case SDLK_DOWN:         return YPHZ_KEY_DOWN;
        case SDLK_a:            return YPHZ_KEY_A;
        case SDLK_d:            return YPHZ_KEY_D;
        case SDLK_c:            return YPHZ_KEY_C;
        case SDLK_f:            return YPHZ_KEY_F;
        case SDLK_m:            return YPHZ_KEY_M;
        case SDLK_o:            return YPHZ_KEY_O;
        case SDLK_LEFTBRACKET:  return YPHZ_KEY_LEFTBRACKET;
        case SDLK_RIGHTBRACKET: return YPHZ_KEY_RIGHTBRACKET;
        case SDLK_1:            return YPHZ_KEY_1;
        case SDLK_2:            return YPHZ_KEY_2;
        case SDLK_3:            return YPHZ_KEY_3;
        case SDLK_4:            return YPHZ_KEY_4;
        default:                return YPHZ_KEY_NONE;
    }
}

bool input_poll(InputState *st) {
    if (!st) return false;

    memset(st, 0, sizeof(*st));

    // Keyboard + quit edges are taken from events so nothing is missed even if
    // several events arrive in the same frame.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                st->quit = true;
                break;
            case SDL_KEYDOWN:
                if (!ev.key.repeat) {
                    YphzKey k = translate_key(ev.key.keysym.sym);
                    if (k != YPHZ_KEY_NONE) st->key = k;
                }
                break;
            default:
                break;
        }
    }

    // Pointer position: window coords -> 640x400 logical coords.
    int win_x = 0, win_y = 0;
    int lx = 0, ly = 0;
    SDL_GetMouseState(&win_x, &win_y);
    hal_video_window_to_logical(win_x, win_y, &lx, &ly);

    st->mouse_x = lx;
    st->mouse_y = ly;
    st->mouse_inside = hal_video_has_mouse_focus() &&
                       lx >= 0 && lx < PC98_WIDTH &&
                       ly >= 0 && ly < PC98_HEIGHT;

    if (s_have_prev_mouse) {
        st->mouse_dx = st->mouse_x - s_prev_mouse_x;
        st->mouse_dy = st->mouse_y - s_prev_mouse_y;
    }
    s_prev_mouse_x = st->mouse_x;
    s_prev_mouse_y = st->mouse_y;
    s_have_prev_mouse = true;

    Uint32 buttons = SDL_GetMouseState(NULL, NULL);
    bool lnow = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    bool rnow = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

    st->lbutton_down = lnow;
    st->rbutton_down = rnow;
    st->lbutton_pressed = lnow && !s_prev_lbutton;
    st->rbutton_pressed = rnow && !s_prev_rbutton;

    s_prev_lbutton = lnow;
    s_prev_rbutton = rnow;

    return true;
}

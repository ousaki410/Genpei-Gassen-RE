#ifndef YPHZ_INPUT_H
#define YPHZ_INPUT_H

#include <stdbool.h>

/*
 * Platform input abstraction.
 *
 * input.c owns *all* SDL event handling (keyboard + mouse) and exposes a single
 * InputState that the main loop consumes once per frame. This supersedes the
 * old, never-called hal_video_poll_event() stub that used to live in the video
 * HAL: the HAL now only knows about pixels, while input routing is centralised
 * here.
 *
 * Mouse coordinates are reported in the 640x400 *logical* render space (the
 * same space the framebuffer and every drawing call use), so callers never have
 * to reason about window scaling or letterboxing.
 */

typedef enum {
    YPHZ_KEY_NONE = 0,
    YPHZ_KEY_ESCAPE,
    YPHZ_KEY_ENTER,
    YPHZ_KEY_TAB,
    YPHZ_KEY_SPACE,
    YPHZ_KEY_LEFT,
    YPHZ_KEY_RIGHT,
    YPHZ_KEY_UP,
    YPHZ_KEY_DOWN,
    YPHZ_KEY_A,
    YPHZ_KEY_D,
    YPHZ_KEY_C,
    YPHZ_KEY_F,
    YPHZ_KEY_M,
    YPHZ_KEY_O,
    YPHZ_KEY_T,
    YPHZ_KEY_L,
    YPHZ_KEY_I,
    YPHZ_KEY_B,
    YPHZ_KEY_G,
    YPHZ_KEY_F5,
    YPHZ_KEY_F9,
    YPHZ_KEY_LEFTBRACKET,
    YPHZ_KEY_RIGHTBRACKET,
    /* The scenario hot-keys are kept contiguous so that
     * (key >= YPHZ_KEY_1 && key <= YPHZ_KEY_8) and (key - YPHZ_KEY_1) work. */
    YPHZ_KEY_1,
    YPHZ_KEY_2,
    YPHZ_KEY_3,
    YPHZ_KEY_4,
    YPHZ_KEY_5,
    YPHZ_KEY_6,
    YPHZ_KEY_7,
    YPHZ_KEY_8
} YphzKey;

typedef struct {
    /* --- keyboard --- */
    YphzKey key;          // key pressed this frame, YPHZ_KEY_NONE when none
    bool    quit;         // window-close (SDL_QUIT) requested

    /* --- mouse, in 640x400 logical coordinates --- */
    int  mouse_x, mouse_y;   // current pointer position
    int  mouse_dx, mouse_dy; // movement since the previous frame
    bool mouse_inside;       // pointer is over the viewport AND the window is focused

    /* --- buttons --- */
    bool lbutton_down;       // currently held
    bool rbutton_down;
    bool lbutton_pressed;    // press edge this frame
    bool rbutton_pressed;    // press edge this frame

    // True when this frame's mouse state was synthesised by --click rather
    // than read from SDL. Consumers can skip hover/edge-scroll side effects.
    bool synthetic;
} InputState;

// Initialise the input subsystem. Call once, after the video HAL is up.
void input_init(void);

// Drain all pending events and fill *st for the current frame. Per-frame edge
// flags (key, quit, *_pressed) are reset here. Returns false if st is NULL.
bool input_poll(InputState *st);

#endif // YPHZ_INPUT_H

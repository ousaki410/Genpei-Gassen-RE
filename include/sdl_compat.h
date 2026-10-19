#ifndef YPHZ_SDL_COMPAT_H
#define YPHZ_SDL_COMPAT_H

/*
 * Single point of SDL inclusion for the whole engine.
 *
 *   SDL2 (default) : include <SDL.h>            — include/ dir must point at
 *                                                 the SDL2 header directory
 *                                                 (system or third_party/include/SDL2)
 *   SDL3 (opt-in)  : include <SDL3/SDL.h>       — enabled by configuring CMake
 *                                                 with -DYPHZ_USE_SDL3=ON, which
 *                                                 defines YPHZ_SDL3.
 *
 * NOTE: the engine sources currently call the SDL2 API. The SDL3 path is wired
 * up for dependency detection only; migrating the call sites to the SDL3 API
 * (SDL_CreateRenderer, SDL_RenderTexture, SDL_EVENT_*, ...) is a follow-up task.
 */

#if defined(YPHZ_SDL3)
#  include <SDL3/SDL.h>
#else
#  include <SDL.h>
#endif

#endif /* YPHZ_SDL_COMPAT_H */

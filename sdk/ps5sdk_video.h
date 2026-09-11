#pragma once
#include "ps5sdk_types.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>

#define PS5SDK_W 1920
#define PS5SDK_H 1080

/* The video context: an SDL_Window, its SDL_Renderer and a font. */
typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font;      /* fuente principal */
    TTF_Font     *font_sm;   /* the small font */
    int           width;
    int           height;
} PS5SDKVideo;

/* Rutas de fuentes disponibles en PS5 */
static const char *_ps5sdk_font_paths[] = {
    "/preinst/common/font/n023055ms.ttf",
    "/system/common/font/n023055ms.ttf",
    NULL
};

/* Opens video out through SDL2, with the API the bare-metal SDK had. */
static inline int ps5sdk_video_init(PS5SDKVideo *v, int userId) {
    (void)userId;
    memset(v, 0, sizeof(*v));
    v->width  = PS5SDK_W;
    v->height = PS5SDK_H;

    v->window = SDL_CreateWindow("PS5SDK",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        v->width, v->height, SDL_WINDOW_FULLSCREEN);
    if (!v->window) return -1;

    v->renderer = SDL_CreateRenderer(v->window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_SOFTWARE);
    if (!v->renderer) { SDL_DestroyWindow(v->window); return -2; }

    /* the system's own font */
    for (int i = 0; _ps5sdk_font_paths[i]; i++) {
        v->font    = TTF_OpenFont(_ps5sdk_font_paths[i], 28);
        if (v->font) { v->font_sm = TTF_OpenFont(_ps5sdk_font_paths[i], 20); break; }
    }
    return 0;
}

/* Flip — muestra el frame actual */
static inline void ps5sdk_video_flip(PS5SDKVideo *v) {
    SDL_RenderPresent(v->renderer);
}

/* Fills the screen with one colour. */
static inline void ps5sdk_video_clear(PS5SDKVideo *v, u32 color) {
    SDL_SetRenderDrawColor(v->renderer,
        PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF);
    SDL_RenderClear(v->renderer);
}

/* Liberar recursos */
static inline void ps5sdk_video_close(PS5SDKVideo *v) {
    if (v->font)    TTF_CloseFont(v->font);
    if (v->font_sm) TTF_CloseFont(v->font_sm);
    if (v->renderer) SDL_DestroyRenderer(v->renderer);
    if (v->window)   SDL_DestroyWindow(v->window);
    memset(v, 0, sizeof(*v));
}

/* Reclamar display — en SDL2 ya lo hace SDL_CreateWindow */

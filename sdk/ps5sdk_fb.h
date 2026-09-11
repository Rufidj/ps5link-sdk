#pragma once
/* ps5sdk_fb.h — Primitivas de dibujo usando SDL2
 * The same API the bare-metal SDK had:
 *   ps5sdk_fb_rect, ps5sdk_fb_str, ps5sdk_fb_fill, ps5sdk_fb_line
 * Pero ahora usa PS5SDKVideo en lugar de u32* framebuffer
 */
#include "ps5sdk_video.h"

/* ── Primitivas de dibujo ──────────────────────────────────────── */

static inline void ps5sdk_fb_fill(PS5SDKVideo *v, u32 color) {
    ps5sdk_video_clear(v, color);
}

static inline void ps5sdk_fb_rect(PS5SDKVideo *v,
    int x, int y, int w, int h, u32 color) {
    SDL_SetRenderDrawColor(v->renderer,
        PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(v->renderer, &r);
}

static inline void ps5sdk_fb_line(PS5SDKVideo *v,
    int x1, int y1, int x2, int y2, u32 color) {
    SDL_SetRenderDrawColor(v->renderer,
        PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF);
    SDL_RenderDrawLine(v->renderer, x1, y1, x2, y2);
}

/* Text, with an SDL_ttf font. */
static inline int ps5sdk_fb_str(PS5SDKVideo *v, TTF_Font *fnt,
    int x, int y, const char *text, u32 color) {
    if (!text || !text[0] || !fnt) return -1;
    SDL_Color c = {PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF};
    SDL_Surface *s = TTF_RenderUTF8_Blended(fnt, text, c);
    if (!s) return -1;
    SDL_Texture *t = SDL_CreateTextureFromSurface(v->renderer, s);
    SDL_Rect dst = {x, y, s->w, s->h};
    SDL_RenderCopy(v->renderer, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
    return 0;
}

/* The same, with the system font the video context loaded. */
static inline int ps5sdk_fb_text(PS5SDKVideo *v,
    int x, int y, const char *text, u32 color) {
    return ps5sdk_fb_str(v, v->font ? v->font : v->font_sm, x, y, text, color);
}

static inline int ps5sdk_fb_text_sm(PS5SDKVideo *v,
    int x, int y, const char *text, u32 color) {
    return ps5sdk_fb_str(v, v->font_sm ? v->font_sm : v->font, x, y, text, color);
}

/* Punto individual */
static inline void ps5sdk_fb_pixel(PS5SDKVideo *v, int x, int y, u32 color) {
    SDL_SetRenderDrawColor(v->renderer,
        PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF);
    SDL_RenderDrawPoint(v->renderer, x, y);
}

/* An outlined rectangle. */
static inline void ps5sdk_fb_rect_outline(PS5SDKVideo *v,
    int x, int y, int w, int h, u32 color) {
    SDL_SetRenderDrawColor(v->renderer,
        PS5SDK_R(color), PS5SDK_G(color), PS5SDK_B(color), 0xFF);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderDrawRect(v->renderer, &r);
}

/* Flip — alias de ps5sdk_video_flip */
#define ps5sdk_fb_flip(v) ps5sdk_video_flip(v)

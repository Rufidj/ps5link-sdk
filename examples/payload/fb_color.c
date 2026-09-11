/* fb_color.c — VideoOut via SDL2, entry point SDL_main() */
#include <SDL.h>
#include <stdio.h>

#define W 1920
#define H 1080

int SDL_main(int argc, char *argv[]) {
    printf("fb_color: iniciando\n");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("PS5SDK fb_color",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        W, H, SDL_WINDOW_FULLSCREEN);
    if (!win) { printf("Window error: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_SOFTWARE);
    if (!ren) { printf("Renderer error: %s\n", SDL_GetError()); return 1; }

    printf("fb_color: render loop\n");

    for (int f = 0; f < 300; f++) {
        Uint8 r = (Uint8)(f * 3);
        Uint8 g = (Uint8)(f * 5);
        Uint8 b = (Uint8)(f * 7);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderClear(ren);

        /* Cruz blanca */
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_Rect hline = {W/2-60, H/2-4, 120, 8};
        SDL_Rect vline = {W/2-4, H/2-60, 8, 120};
        SDL_RenderFillRect(ren, &hline);
        SDL_RenderFillRect(ren, &vline);

        SDL_RenderPresent(ren);
    }

    printf("fb_color: fin\n");
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

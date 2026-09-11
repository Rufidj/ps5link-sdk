/* ps5sdk.h - a small PS5 SDK over SDL2 (tested on firmware 9.00)
 *
 * Use:
 *   #include "ps5sdk.h"
 *
 *   int SDL_main(int argc, char *argv[]) {
 *       PS5SDKVideo v;
 *       ps5sdk_video_init(&v, 0);
 *       ps5sdk_fb_fill(&v, PS5SDK_BLUE);
 *       ps5sdk_fb_text(&v, 100, 100, "Hello, PS5!", PS5SDK_WHITE);
 *       ps5sdk_video_flip(&v);
 *       ps5sdk_sleep_ms(3000);
 *       ps5sdk_video_close(&v);
 *       return 0;
 *   }
 *
 * The entry point MUST be SDL_main(), not main(): SDL2main carries the
 * bootstrap the console needs.
 */
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>

#include "ps5sdk_types.h"
#include "ps5sdk_system.h"
#include "ps5sdk_notify.h"
#include "ps5sdk_video.h"
#include "ps5sdk_fb.h"
#include "ps5sdk_pad.h"
#include "ps5sdk_fs.h"

/* Runs before SDL_main(), bringing up SDL and the console's services. */
__attribute__((constructor)) static void _ps5sdk_init(void) {
    sceUserServiceInitialize(0);
    sceSystemServiceHideSplashScreen();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();
}

__attribute__((destructor)) static void _ps5sdk_quit(void) {
    TTF_Quit();
    SDL_Quit();
}

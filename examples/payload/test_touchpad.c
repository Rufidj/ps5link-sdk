/* test_touchpad.c — Diagnose PS5 touchpad via ScePad raw data */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int scePadInit(void);
extern int scePadOpen(int userId, int type, int index, void *reserved);
extern int scePadReadState(int handle, void *data);
extern int scePadClose(int handle);
extern int sceUserServiceGetForegroundUser(int *userId);

#define PAD_DATA_SIZE 128

static void write_log(const char *msg) {
    FILE *f = fopen("/data/touchpad_test.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}
static void log_fmt(const char *fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    write_log(buf);
}

int SDL_main(int argc, char *argv[]) {
    FILE *f = fopen("/data/touchpad_test.log", "w");
    if (f) { fputs("=== PS5 Touchpad Diagnostic ===\n", f); fclose(f); }

    putenv("SDL_VIDEODRIVER=ps5");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    SDL_Window *win = SDL_CreateWindow("tp_test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1920, 1080, SDL_WINDOW_FULLSCREEN);

    /* Open GameController for button detection */
    SDL_GameController *ctrl = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            ctrl = SDL_GameControllerOpen(i); break;
        }
    }
    log_fmt("GameController: %s", ctrl ? "OK" : "FAIL");

    /* Open ScePad */
    scePadInit();
    int userId = 0;
    sceUserServiceGetForegroundUser(&userId);
    int handle = scePadOpen(userId, 0, 0, NULL);
    log_fmt("scePadOpen(userId=%d) handle=%d", userId, handle);

    /* Read 200 frames, log when touchpad data changes */
    uint8_t prev_data[PAD_DATA_SIZE] = {0};
    uint8_t curr_data[PAD_DATA_SIZE] = {0};
    int frame = 0;
    Uint32 start = SDL_GetTicks();

    log_fmt("Move finger on touchpad and click it...");

    while (SDL_GetTicks() - start < 10000) {
        SDL_PumpEvents();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                log_fmt("SDL_BTN: button=%d (%s)", e.cbutton.button,
                    SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
            }
            if (e.type == SDL_JOYBUTTONDOWN) {
                log_fmt("JOY_BTN: btn=%d", e.jbutton.button);
            }
        }

        if (handle >= 0) {
            memset(curr_data, 0, sizeof(curr_data));
            scePadReadState(handle, curr_data);

            /* Log bytes that changed (skip first frame) */
            if (frame > 0) {
                for (int b = 0; b < PAD_DATA_SIZE; b++) {
                    if (curr_data[b] != prev_data[b]) {
                        /* Only log offsets 20-60 (likely touchpad area) OR buttons area 0-3 */
                        if ((b >= 0 && b <= 3) || (b >= 20 && b <= 60)) {
                            log_fmt("offset[%d] changed: 0x%02X -> 0x%02X (frame=%d)",
                                b, prev_data[b], curr_data[b], frame);
                        }
                    }
                }
                /* Log full buttons word at offset 0 */
                uint32_t btns;
                memcpy(&btns, curr_data, 4);
                uint32_t prev_btns;
                memcpy(&prev_btns, prev_data, 4);
                if (btns != prev_btns) {
                    log_fmt("BUTTONS changed: 0x%08X -> 0x%08X", prev_btns, btns);
                }
            }
            memcpy(prev_data, curr_data, sizeof(curr_data));
        }
        frame++;
        SDL_Delay(16);
    }

    log_fmt("Done. %d frames.", frame);
    if (handle >= 0) scePadClose(handle);
    if (ctrl) SDL_GameControllerClose(ctrl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

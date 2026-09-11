#pragma once
#include "ps5sdk_types.h"
#include <SDL2/SDL.h>

/* The DualSense's buttons, under the names the bare-metal SDK used. */
#define PS5SDK_BTN_CROSS     SDL_CONTROLLER_BUTTON_A
#define PS5SDK_BTN_CIRCLE    SDL_CONTROLLER_BUTTON_B
#define PS5SDK_BTN_SQUARE    SDL_CONTROLLER_BUTTON_X
#define PS5SDK_BTN_TRIANGLE  SDL_CONTROLLER_BUTTON_Y
#define PS5SDK_BTN_L1        SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define PS5SDK_BTN_R1        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define PS5SDK_BTN_UP        SDL_CONTROLLER_BUTTON_DPAD_UP
#define PS5SDK_BTN_DOWN      SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define PS5SDK_BTN_LEFT      SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define PS5SDK_BTN_RIGHT     SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define PS5SDK_BTN_OPTIONS   SDL_CONTROLLER_BUTTON_START
#define PS5SDK_BTN_CREATE    SDL_CONTROLLER_BUTTON_BACK
#define PS5SDK_BTN_L3        SDL_CONTROLLER_BUTTON_LEFTSTICK
#define PS5SDK_BTN_R3        SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define PS5SDK_BTN_TOUCHPAD  SDL_CONTROLLER_BUTTON_MISC1

typedef struct {
    SDL_GameController *ctrl;
    /* Botones actuales */
    int buttons[SDL_CONTROLLER_BUTTON_MAX];
    /* the sticks, -32768 to 32767 */
    int lx, ly, rx, ry;
    /* L2/R2 triggers (0..32767) */
    int l2, r2;
} PS5SDKPad;

static inline int ps5sdk_pad_open(int userId) {
    (void)userId;
    return SDL_NumJoysticks() > 0 ? 0 : -1;
}

static inline PS5SDKPad ps5sdk_pad_create(void) {
    PS5SDKPad p;
    int i;
    for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) p.buttons[i] = 0;
    p.lx = p.ly = p.rx = p.ry = p.l2 = p.r2 = 0;
    p.ctrl = NULL;
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            p.ctrl = SDL_GameControllerOpen(i);
            if (p.ctrl) break;
        }
    }
    return p;
}

/* Reads the pad. Call it once a frame. */
static inline void ps5sdk_pad_update(PS5SDKPad *p) {
    if (!p->ctrl) return;
    int i;
    for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        p->buttons[i] = SDL_GameControllerGetButton(p->ctrl,
            (SDL_GameControllerButton)i);
    p->lx = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_LEFTX);
    p->ly = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_LEFTY);
    p->rx = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_RIGHTX);
    p->ry = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_RIGHTY);
    p->l2 = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    p->r2 = SDL_GameControllerGetAxis(p->ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
}

/* Whether a button is held. */
static inline int ps5sdk_pad_pressed(const PS5SDKPad *p,
    SDL_GameControllerButton btn) {
    return p->buttons[btn];
}

static inline void ps5sdk_pad_close(PS5SDKPad *p) {
    if (p->ctrl) SDL_GameControllerClose(p->ctrl);
    p->ctrl = NULL;
}

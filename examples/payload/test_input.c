/* test_input.c — Test PS5 DualSense input via SDL2 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

static void write_log(const char *msg) {
    FILE *f = fopen("/data/input_test.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}

static void log_fmt(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_log(buf);
}

int SDL_main(int argc, char *argv[]) {
    FILE *f = fopen("/data/input_test.log", "w");
    if (f) { fputs("=== PS5 Input Test ===\n", f); fclose(f); }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR);

    /* Abrir ventana minima */
    putenv("SDL_VIDEODRIVER=ps5");
    SDL_Window *win = SDL_CreateWindow("input_test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1920, 1080, SDL_WINDOW_FULLSCREEN);

    /* Listar joysticks/controllers */
    log_fmt("Joysticks: %d", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        log_fmt("  Joy[%d]: %s (IsCtrl=%d)", i,
            SDL_JoystickNameForIndex(i),
            SDL_IsGameController(i));
    }

    /* Abrir el primer controller */
    SDL_GameController *ctrl = NULL;
    SDL_Joystick       *joy  = NULL;
    if (SDL_NumJoysticks() > 0) {
        if (SDL_IsGameController(0)) {
            ctrl = SDL_GameControllerOpen(0);
            log_fmt("GameController opened: %s", ctrl ? SDL_GameControllerName(ctrl) : "FAIL");
            /* Habilitar touchpad */
            if (ctrl) {
                log_fmt("Touchpads: %d", SDL_GameControllerGetNumTouchpads(ctrl));
                log_fmt("Sensors: gyro=%d accel=%d",
                    SDL_GameControllerHasSensor(ctrl, SDL_SENSOR_GYRO),
                    SDL_GameControllerHasSensor(ctrl, SDL_SENSOR_ACCEL));
            }
        } else {
            joy = SDL_JoystickOpen(0);
            log_fmt("Joystick opened: %s axes=%d btns=%d",
                joy ? SDL_JoystickName(joy) : "FAIL",
                joy ? SDL_JoystickNumAxes(joy) : 0,
                joy ? SDL_JoystickNumButtons(joy) : 0);
        }
    }

    log_fmt("Waiting for input (5 seconds)...");

    Uint32 start = SDL_GetTicks();
    int event_count = 0;

    while (SDL_GetTicks() - start < 5000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_CONTROLLERBUTTONDOWN:
                    log_fmt("BTN_DOWN: button=%d (%s)", e.cbutton.button,
                        SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
                    event_count++;
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (abs(e.caxis.value) > 1000)
                        log_fmt("AXIS: axis=%d val=%d", e.caxis.axis, e.caxis.value);
                    break;
                case SDL_CONTROLLERTOUCHPADDOWN:
                    log_fmt("TOUCHPAD_DOWN: pad=%d finger=%d x=%.3f y=%.3f",
                        e.ctouchpad.touchpad, e.ctouchpad.finger,
                        e.ctouchpad.x, e.ctouchpad.y);
                    event_count++;
                    break;
                case SDL_CONTROLLERTOUCHPADMOTION:
                    log_fmt("TOUCHPAD_MOVE: x=%.3f y=%.3f pressure=%.3f",
                        e.ctouchpad.x, e.ctouchpad.y, e.ctouchpad.pressure);
                    break;
                case SDL_CONTROLLERTOUCHPADUP:
                    log_fmt("TOUCHPAD_UP: pad=%d", e.ctouchpad.touchpad);
                    break;
                case SDL_FINGERDOWN:
                    log_fmt("FINGER_DOWN: x=%.3f y=%.3f", e.tfinger.x, e.tfinger.y);
                    event_count++;
                    break;
                case SDL_FINGERMOTION:
                    log_fmt("FINGER_MOVE: dx=%.4f dy=%.4f", e.tfinger.dx, e.tfinger.dy);
                    break;
                case SDL_JOYBUTTONDOWN:
                    log_fmt("JOYBTN_DOWN: btn=%d", e.jbutton.button);
                    event_count++;
                    break;
                case SDL_MOUSEMOTION:
                    log_fmt("MOUSE_MOVE: x=%d y=%d", e.motion.x, e.motion.y);
                    break;
            }
        }
    }

    log_fmt("Done. %d events captured.", event_count);
    log_fmt("SDL_GameControllerButton constants:");
    log_fmt("  A(Cross)=%d B(Circle)=%d X(Square)=%d Y(Triangle)=%d",
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y);
    log_fmt("  L1=%d R1=%d L2=%d R2=%d L3=%d R3=%d",
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    log_fmt("  OPTIONS=%d CREATE=%d TOUCHPAD=%d",
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_TOUCHPAD);

    if (ctrl) SDL_GameControllerClose(ctrl);
    if (joy)  SDL_JoystickClose(joy);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

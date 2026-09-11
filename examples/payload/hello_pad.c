#include "../sdk/ps5sdk.h"

int main(void) {
    int uid = ps5sdk_get_user_id();
    int pad = ps5sdk_pad_open(uid);
    printf("hello_pad: pad handle=%d\n", pad);

    PS5SDKPadState state;
    int frames = 0;

    while (frames < 300) {  /* ~5 segundos a 60fps */
        if (ps5sdk_pad_read(pad, &state) == 0) {
            if (ps5sdk_pad_pressed(&state, PS5SDK_BTN_CROSS))
                printf("CROSS pulsado!\n");
            if (ps5sdk_pad_pressed(&state, PS5SDK_BTN_OPTIONS))
                break;
        }
        ps5sdk_sleep_ms(16);
        frames++;
    }

    scePadClose(pad);
    return 0;
}

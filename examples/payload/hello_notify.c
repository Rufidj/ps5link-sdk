/* hello_notify.c — Entry point: SDL_main() obligatorio en PS5 */
#include <stdio.h>
#include <string.h>

extern int sceKernelSendNotificationRequest(int, void*, int, int);

int SDL_main(int argc, char *argv[]) {
    printf("hello_notify: arrancado\n");

    /* a system notification */
    char req[3120] = {0};
    strncpy(req + 45, "PS5SDK: running from SDL_main!", 3074);
    int r = sceKernelSendNotificationRequest(0, req, sizeof(req), 0);
    printf("notify r=%d\n", r);

    extern int sceKernelUsleep(unsigned int);
    sceKernelUsleep(5000000);
    return 0;
}

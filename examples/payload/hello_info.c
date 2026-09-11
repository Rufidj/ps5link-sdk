#include "../sdk/ps5sdk.h"

extern int sceKernelGetHwModelName(char *buf);
extern int sceKernelGetCompiledSdkVersion(int *ver);

int main(void) {
    char model[256] = {0};
    sceKernelGetHwModelName(model);
    printf("Modelo PS5: %s\n", model);
    printf("User ID: %d\n", ps5sdk_get_user_id());

    /* Guardar info en el USB */
    char info[512];
    snprintf(info, sizeof(info), "Modelo: %s\nUID: %d\n", model, ps5sdk_get_user_id());
    ps5sdk_file_write_str("/mnt/usb0/ps5sdk_info.txt", info);
    printf("Guardado en /mnt/usb0/ps5sdk_info.txt\n");

    ps5sdk_sleep_ms(3000);
    return 0;
}

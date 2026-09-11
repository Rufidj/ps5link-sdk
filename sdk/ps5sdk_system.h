#pragma once
#include "ps5sdk_types.h"
#include <unistd.h>

/* ── System Service ─────────────────────────────────────────────── */
extern int sceSystemServiceHideSplashScreen(void);
extern int sceSystemServiceParamGetInt(int paramId, int *value);

/* ── User Service ───────────────────────────────────────────────── */
extern int sceUserServiceInitialize(const void *params);
extern int sceUserServiceGetInitialUser(int *userId);

/* ── Helpers ────────────────────────────────────────────────────── */
static inline void ps5sdk_sleep_ms(int ms) {
    usleep((unsigned int)(ms * 1000));
}

static inline int ps5sdk_get_user_id(void) {
    int uid = 0xFE;
    sceUserServiceGetInitialUser(&uid);
    return uid;
}

/* Claims the display as the foreground application. Call it before video out. */
static inline void ps5sdk_claim_display(void) {
    sceSystemServiceHideSplashScreen();
}

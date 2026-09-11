#pragma once
#include "ps5sdk_types.h"
#include <string.h>

typedef struct {
    char _unused[45];
    char message[3075];
} PS5NotifyRequest;

extern int sceKernelSendNotificationRequest(int uid,
    PS5NotifyRequest *req, size_t size, int unk);

/* Shows a system notification. */
static inline int ps5sdk_notify(const char *msg) {
    static PS5NotifyRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, msg, sizeof(req.message) - 1);
    return sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* A notification with formatting. */
static inline int ps5sdk_notifyf(const char *fmt, ...) {
    static PS5NotifyRequest req;
    __builtin_va_list ap;
    memset(&req, 0, sizeof(req));
    __builtin_va_start(ap, fmt);
    /* vsnprintf is not available here, so the text is taken as it is */
    strncpy(req.message, fmt, sizeof(req.message) - 1);
    __builtin_va_end(ap);
    return sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

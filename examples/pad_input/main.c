/* Reads the DualSense through ScePad and SceUserService and sends a system
 * notification for every press of CROSS, for ten seconds or until OPTIONS.
 * No SDL and no stdio.
 *
 * The layout of the 1024-byte scePadReadState buffer, as SharpProspero's
 * GamePad.FromSample reads it:
 *   0x00 u32  buttons (bit 0x80000000: the system has taken the controller)
 *   0x04 u8   left stick X   0x05 u8 left stick Y
 *   0x06 u8   right stick X  0x07 u8 right stick Y
 *   0x08 u8   L2 analog      0x09 u8 R2 analog
 */

extern int sceUserServiceInitialize(void *initParams);
extern int sceUserServiceGetInitialUser(int *userId);
extern int scePadInit(void);
extern int scePadOpen(int userId, int type, int index, void *param);
extern int scePadReadState(int handle, void *data);
extern int scePadClose(int handle);
extern int sceKernelSendNotificationRequest(int device, void *request, int size, int flags);
extern int sceKernelUsleep(unsigned int microseconds);

#define BTN_OPTIONS 0x00000008u
#define BTN_CROSS   0x00004000u
#define BTN_INTERCEPTED 0x80000000u

static void notify(const char *msg) {
    char req[3120];
    for (int i = 0; i < 3120; i++) req[i] = 0;
    for (int i = 0; msg[i] && i < 3074; i++) req[45 + i] = msg[i];
    sceKernelSendNotificationRequest(0, req, sizeof(req), 0);
}

/* Appends text to buf at *len. */
static void append(char *buf, int *len, const char *text) {
    while (*text) buf[(*len)++] = *text++;
    buf[*len] = 0;
}

/* Appends a non-negative number to buf at *len. */
static void append_uint(char *buf, int *len, unsigned int v) {
    char tmp[10];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v > 0 && n < 10);
    while (n > 0) buf[(*len)++] = tmp[--n];
    buf[*len] = 0;
}

int main(void) {
    sceUserServiceInitialize(0);

    int userId = 0;
    if (sceUserServiceGetInitialUser(&userId) < 0) {
        notify("pad_input: sceUserServiceGetInitialUser failed");
        return 1;
    }
    if (scePadInit() < 0) {
        notify("pad_input: scePadInit failed");
        return 1;
    }
    int handle = scePadOpen(userId, 0 /* standard port */, 0, 0);
    if (handle < 0) {
        notify("pad_input: scePadOpen failed");
        return 1;
    }

    notify("pad_input: controller open - press CROSS, or OPTIONS to finish early");

    unsigned char buf[1024];
    int cross_presses = 0;
    int was_cross_down = 0;
    const int max_frames = 625;   /* about ten seconds at 16 ms a frame */

    for (int frame = 0; frame < max_frames; frame++) {
        for (int i = 0; i < 1024; i++) buf[i] = 0;

        if (scePadReadState(handle, buf) >= 0) {
            unsigned int buttons = (unsigned int)buf[0] | ((unsigned int)buf[1] << 8) |
                                   ((unsigned int)buf[2] << 16) | ((unsigned int)buf[3] << 24);
            if ((buttons & BTN_INTERCEPTED) == 0) {
                int cross_down = (buttons & BTN_CROSS) != 0;
                if (cross_down && !was_cross_down) {
                    char msg[64];
                    int len = 0;
                    cross_presses++;
                    append(msg, &len, "pad_input: CROSS #");
                    append_uint(msg, &len, (unsigned int)cross_presses);
                    notify(msg);
                }
                was_cross_down = cross_down;
                if (buttons & BTN_OPTIONS) break;
            }
        }
        sceKernelUsleep(16000);
    }

    scePadClose(handle);

    char summary[64];
    int len = 0;
    append(summary, &len, "pad_input: done, CROSS pressed ");
    append_uint(summary, &len, (unsigned int)cross_presses);
    append(summary, &len, " times");
    notify(summary);
    return 0;
}

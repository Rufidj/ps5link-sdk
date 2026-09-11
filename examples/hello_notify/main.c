/* The smallest ps5link title: launched as a real title (not a payload), it
 * sends one system notification, then sleeps so the notification stays
 * visible. No stdio - just two system calls, declared by hand. */
extern int sceKernelSendNotificationRequest(int device, void *request, int size, int flags);
extern int sceKernelUsleep(unsigned int microseconds);

int main(void) {
    char req[3120];
    for (int i = 0; i < 3120; i++) req[i] = 0;
    const char *msg = "ps5link: hello from a real title (not a payload)!";
    for (int i = 0; msg[i] && i < 3074; i++) req[45 + i] = msg[i];

    sceKernelSendNotificationRequest(0, req, sizeof(req), 0);
    sceKernelUsleep(30000000); /* 30s: keep the process alive to see it worked */
    return 0;
}

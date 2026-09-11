#pragma once
#include "ps5sdk_types.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* File I/O: plain POSIX, which ps5-payload-sdk provides. */

static inline int ps5sdk_file_write(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

static inline int ps5sdk_file_write_str(const char *path, const char *str) {
    return ps5sdk_file_write(path, str, strlen(str));
}

static inline long ps5sdk_file_read(const char *path, void *buf, size_t maxlen) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long n = (long)fread(buf, 1, maxlen, f);
    fclose(f);
    return n;
}

static inline int ps5sdk_dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static inline int ps5sdk_mkdir(const char *path) {
    return mkdir(path, 0755);
}

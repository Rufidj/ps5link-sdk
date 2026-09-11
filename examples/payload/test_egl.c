/* test_egl.c — Test EGL/OpenGL ES access from PS5 homebrew */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* EGL declarations (from libSceGLSlimVSH) */
#include <EGL/egl.h>
/* OR manually declare if header not available: */
/* typedef void* EGLDisplay; typedef void* EGLContext; ... */

static void write_log(const char *msg) {
    FILE *f = fopen("/data/egl_test.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}

int SDL_main(int argc, char *argv[]) {
    FILE *f = fopen("/data/egl_test.log", "w");
    if (f) { fputs("=== EGL Test PS5 ===\n", f); fclose(f); }

    write_log("SDL_Init...");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        write_log("SDL_Init FAILED");
        return 1;
    }
    write_log("SDL_Init OK");

    /* Test 1: EGL display */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    char buf[128];
    snprintf(buf, sizeof(buf), "eglGetDisplay: %p (EGL_NO_DISPLAY=%p)", (void*)dpy, (void*)EGL_NO_DISPLAY);
    write_log(buf);

    if (dpy == EGL_NO_DISPLAY) {
        write_log("EGL NOT AVAILABLE - hardware GL ES not accessible from homebrew");
    } else {
        EGLint major, minor;
        EGLBoolean ok = eglInitialize(dpy, &major, &minor);
        snprintf(buf, sizeof(buf), "eglInitialize: %d (version %d.%d)", ok, major, minor);
        write_log(buf);

        if (ok) {
            write_log("EGL WORKS! Hardware OpenGL ES is accessible!");
            /* Test context creation */
            EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
            EGLConfig config;
            EGLint nconfigs;
            eglChooseConfig(dpy, attribs, &config, 1, &nconfigs);
            snprintf(buf, sizeof(buf), "eglChooseConfig: %d configs", nconfigs);
            write_log(buf);
        }
    }

    /* Test 2: GL library via SDL */
    write_log("SDL_GL_LoadLibrary...");
    int gl = SDL_GL_LoadLibrary(NULL);
    snprintf(buf, sizeof(buf), "SDL_GL_LoadLibrary: %d (%s)", gl, SDL_GetError());
    write_log(buf);

    write_log("=== Test done ===");

    SDL_Delay(3000);
    SDL_Quit();
    return 0;
}

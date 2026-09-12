/**
 * FPS limit removal for LTW (iOS port).
 *
 * LTW previously had no interceptor for any swap-interval entry point:
 * eglSwapInterval / glXSwapIntervalEXT / glXSwapIntervalMESA /
 * wglSwapIntervalEXT all resolved straight into the host ANGLE library
 * (external/libEGL.framework, libtinygl4angle -> Metal), which clamps the
 * swap interval to a minimum of 1. On a 60 Hz iPhone display that locks the
 * game loop to 60 FPS even when the render path could go faster.
 *
 * This file intercepts every swap-interval entry point the desktop-GL clients
 * (GLFW, LWJGL, SDL) can possibly resolve, and forces interval 0 (mailbox /
 * immediate) when LTW_UNCAP_FPS=1. The default stays the untouched host
 * behaviour so existing users see no change until they opt in.
 */

#include "proc.h"
#include "egl.h"
#include "env.h"
#include "libraryinternal.h"
#include <EGL/egl.h>
#include <string.h>

/* Resolved lazily on first use; the host library is already loaded by the
 * proc_init() constructor, so dlsym/eglGetProcAddress both work here. */
static int ltw_uncap_enabled(void) {
    /* env_istrue_d caches nothing, but getenv on iOS is a cheap TLS lookup;
     * the flag is also read at most once per swap-interval call, which games
     * issue only at startup and on settings changes - not per frame. */
    return env_istrue_d("LTW_UNCAP_FPS", false);
}

/* EGL: the only standard knob. EGL declares interval 0 legal, but the
 * surface must support it; ANGLE-on-Metal accepts 0 and maps it to
 * CAMetalLayer.additionalSamplerRowCount=0 / presentsWithTransaction off,
 * i.e. no vsync wait. */
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    if (ltw_uncap_enabled() && interval > 0) {
        interval = 0;
    }
    EGLBoolean (*host_swapinterval)(EGLDisplay, EGLint) =
        (EGLBoolean (*)(EGLDisplay, EGLint)) host_eglGetProcAddress("eglSwapInterval");
    if (host_swapinterval == NULL) return EGL_FALSE;
    return host_swapinterval(dpy, interval);
}

/* Desktop clients resolve glXSwapIntervalEXT / glXSwapIntervalMESA through
 * glXGetProcAddress (proc.c exports it, LWJGL picks it up automatically).
 * The signatures use X11 types on paper; on this iOS build they are never
 * dereferenced, only passed through, so opaque pointers suffice. */
void glXSwapIntervalEXT(void *dpy, unsigned long drawable, int interval) {
    if (ltw_uncap_enabled() && interval > 0) interval = 0;
    void (*host)(void *, unsigned long, int) =
        (void (*)(void *, unsigned long, int)) host_eglGetProcAddress("glXSwapIntervalEXT");
    if (host != NULL) host(dpy, drawable, interval);
}

int glXSwapIntervalMESA(unsigned int interval) {
    if (ltw_uncap_enabled() && (int)interval > 0) interval = 0;
    int (*host)(unsigned int) = (int (*)(unsigned int)) host_eglGetProcAddress("glXSwapIntervalMESA");
    if (host == NULL) return 0;
    return host(interval);
}

int glXGetSwapIntervalMESA(void) {
    int (*host)(void) = (int (*)(void)) host_eglGetProcAddress("glXGetSwapIntervalMESA");
    if (host == NULL) return 0;
    return host();
}

/* Windows clients (running through Wine/x86 translation layers on iOS
 * jailbreak setups) resolve wglSwapIntervalEXT the same way. */
int wglSwapIntervalEXT(int interval) {
    if (ltw_uncap_enabled() && interval > 0) interval = 0;
    int (*host)(int) = (int (*)(int)) host_eglGetProcAddress("wglSwapIntervalEXT");
    if (host == NULL) return 0;
    return host(interval);
}

int wglGetSwapIntervalEXT(void) {
    int (*host)(void) = (int (*)(void)) host_eglGetProcAddress("wglGetSwapIntervalEXT");
    if (host == NULL) return 0;
    return host();
}

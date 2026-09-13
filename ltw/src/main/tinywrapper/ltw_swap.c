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
 * immediate) unless LTW_UNCAP_FPS=0 is set explicitly. Uncapping is the
 * DEFAULT of this fork; set LTW_UNCAP_FPS=0 to restore vsync.
 *
 * Enforcement happens at three layers, because clients set the interval at
 * different times:
 *   1. eglSwapInterval / glXSwapInterval* / wglSwapInterval* - the standard
 *      knobs clients call; clamped to 0 before reaching the host.
 *   2. eglMakeCurrent - GLFW calls eglSwapInterval right after making the
 *      context current; re-assert 0 there in case a client set 1 before.
 *   3. eglSwapBuffers - belt and braces: if some client re-enabled vsync
 *      mid-session (settings screen, mod), re-assert 0 before presenting.
 * Re-asserting interval 0 is idempotent and cheap (no driver round-trip
 * unless the value changed).
 */

#include "proc.h"
#include "egl.h"
#include "env.h"
#include "libraryinternal.h"
#include <EGL/egl.h>
#include <string.h>

/* Cached once at library load - same pattern as init_noerror() in main.c.
 * getenv on each swap call would also be fine (it is a TLS lookup), but the
 * flag also gets logged, and logging once is nicer than logging per frame. */
static int uncap_enabled = 1;

__attribute__((constructor)) static void ltw_swap_init() {
    /* Default ON: this fork exists to remove the 60 FPS lock. opt-out=0 */
    uncap_enabled = !env_istrue("LTW_UNCAP_FPS_DISABLED");
    if(uncap_enabled) printf("LTW: FPS uncap enabled (interval 0 enforced at swap)\n");
    else printf("LTW: FPS uncap disabled by LTW_UNCAP_FPS_DISABLED, host vsync applies\n");
}

static EGLBoolean (*host_swapinterval_fn(void))(EGLDisplay, EGLint) {
    return (EGLBoolean (*)(EGLDisplay, EGLint)) host_eglGetProcAddress("eglSwapInterval");
}

/* Clamp the interval to 0 when uncapping. Returns the value to hand the host. */
static EGLint effective_interval(EGLint interval) {
    if(uncap_enabled && interval > 0) return 0;
    return interval;
}

INTERNAL void ltw_swap_enforce(EGLDisplay dpy) {
    if(!uncap_enabled) return;
    EGLBoolean (*host)(EGLDisplay, EGLint) = host_swapinterval_fn();
    if(host != NULL) host(dpy, 0);
}

/* EGL: the only standard knob. EGL declares interval 0 legal, but the
 * surface must support it; ANGLE-on-Metal accepts 0 and maps it to
 * unthrottled CAMetalLayer presents (no vsync wait). */
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    EGLBoolean (*host)(EGLDisplay, EGLint) = host_swapinterval_fn();
    if(host == NULL) return EGL_FALSE;
    return host(dpy, effective_interval(interval));
}

/* Desktop clients resolve glXSwapIntervalEXT / glXSwapIntervalMESA through
 * glXGetProcAddress (proc.c exports it, LWJGL picks it up automatically).
 * The signatures use X11 types on paper; on this iOS build they are never
 * dereferenced, only passed through, so opaque pointers suffice. */
void glXSwapIntervalEXT(void *dpy, unsigned long drawable, int interval) {
    void (*host)(void *, unsigned long, int) =
        (void (*)(void *, unsigned long, int)) host_eglGetProcAddress("glXSwapIntervalEXT");
    if(host != NULL) host(dpy, drawable, effective_interval(interval));
}

int glXSwapIntervalMESA(unsigned int interval) {
    int (*host)(unsigned int) = (int (*)(unsigned int)) host_eglGetProcAddress("glXSwapIntervalMESA");
    if(host == NULL) return 0;
    return host((unsigned int) effective_interval((EGLint) interval));
}

int glXGetSwapIntervalMESA(void) {
    int (*host)(void) = (int (*)(void)) host_eglGetProcAddress("glXGetSwapIntervalMESA");
    if(host == NULL) return 0;
    return host();
}

/* Windows clients (running through Wine/x86 translation layers on iOS
 * jailbreak setups) resolve wglSwapIntervalEXT the same way. */
int wglSwapIntervalEXT(int interval) {
    int (*host)(int) = (int (*)(int)) host_eglGetProcAddress("wglSwapIntervalEXT");
    if(host == NULL) return 0;
    return host((int) effective_interval((EGLint) interval));
}

int wglGetSwapIntervalEXT(void) {
    int (*host)(void) = (int (*)(void)) host_eglGetProcAddress("wglGetSwapIntervalEXT");
    if(host == NULL) return 0;
    return host();
}

/* Layer 2/3 enforcement hooks, called from egl.c. EGLDisplay is passed by
 * the caller; for SwapBuffers enforcement the display of the surface being
 * presented is the right target. */
static EGLDisplay last_known_display = EGL_NO_DISPLAY;

INTERNAL void ltw_swap_note_display(EGLDisplay dpy) {
    if(dpy != EGL_NO_DISPLAY) last_known_display = dpy;
}

INTERNAL void ltw_swap_enforce_last_display(void) {
    if(last_known_display != EGL_NO_DISPLAY) ltw_swap_enforce(last_known_display);
}

/* Layer 3: present-path enforcement. Clients that render through EGL call
 * eglSwapBuffers every frame; GLFW's iOS port in Pojav-family launchers
 * resolves it by name, so exporting it here intercepts the frame loop of
 * vanilla Minecraft (which uses GLFW, not raw glX). The host presents the
 * frame; then interval 0 is re-asserted so a client that re-enabled vsync
 * mid-session cannot reintroduce the lock silently. Re-asserting interval 0
 * when it is already 0 is a no-op for the driver. */
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    EGLBoolean (*host)(EGLDisplay, EGLSurface) =
        (EGLBoolean (*)(EGLDisplay, EGLSurface)) host_eglGetProcAddress("eglSwapBuffers");
    if(host == NULL) return EGL_FALSE;
    ltw_swap_note_display(dpy);
    EGLBoolean result = host(dpy, surface);
    ltw_swap_enforce(dpy);
    return result;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects) {
    EGLBoolean (*host)(EGLDisplay, EGLSurface, EGLint *, EGLint) =
        (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint *, EGLint)) host_eglGetProcAddress("eglSwapBuffersWithDamageKHR");
    if(host == NULL) return eglSwapBuffers(dpy, surface);
    ltw_swap_note_display(dpy);
    EGLBoolean result = host(dpy, surface, rects, n_rects);
    ltw_swap_enforce(dpy);
    return result;
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects) {
    EGLBoolean (*host)(EGLDisplay, EGLSurface, EGLint *, EGLint) =
        (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint *, EGLint)) host_eglGetProcAddress("eglSwapBuffersWithDamageEXT");
    if(host == NULL) return eglSwapBuffers(dpy, surface);
    ltw_swap_note_display(dpy);
    EGLBoolean result = host(dpy, surface, rects, n_rects);
    ltw_swap_enforce(dpy);
    return result;
}

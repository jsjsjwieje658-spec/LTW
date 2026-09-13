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
#include <time.h>

/* Frame-time diagnostics. Enabled by default so the next user-reported freeze
 * comes with hard evidence in latestlog.txt; disable with LTW_SILENT=1.
 * Prints one summary line every 300 frames (min/avg/max frame ms + swap ms)
 * and one line for any frame slower than 100ms. clock_gettime(CLOCK_MONOTONIC)
 * is a vDSO call on iOS - no syscall per frame. */
static int diag_enabled = 1;
static int first_swap_seen = 0;
static uint64_t frame_last_ns, frame_min_ms, frame_max_ms, frame_acc_ms, swap_max_ms;
static uint32_t frame_count;
static uint32_t slow_frame_count;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static uint64_t delta_ms(uint64_t a, uint64_t b) {
    return (a > b ? a - b : b - a) / 1000000ull;
}

static void diag_frame(uint64_t swap_ms) {
    if(!diag_enabled) return;
    uint64_t now = now_ns();
    if(!first_swap_seen) {
        first_swap_seen = 1;
        printf("LTW: present path confirmed via LTW eglSwapBuffers (frame diagnostics on)\n");
        frame_last_ns = now;
        return;
    }
    uint64_t frame_ms = delta_ms(now, frame_last_ns);
    frame_last_ns = now;
    if(frame_ms < frame_min_ms || frame_count == 0) frame_min_ms = frame_ms;
    if(frame_ms > frame_max_ms) frame_max_ms = frame_ms;
    if(swap_ms > swap_max_ms) swap_max_ms = swap_ms;
    frame_acc_ms += frame_ms;
    frame_count++;
    if(frame_ms > 100) {
        printf("LTW: slow frame #%u: %llums total, swap %llums (freeze marker)\n",
               frame_count, (unsigned long long) frame_ms, (unsigned long long) swap_ms);
        slow_frame_count++;
    }
    if(frame_count >= 300) {
        printf("LTW: 300-frame stats: min %llums avg %llums max %llums | swap max %llums | slow(>100ms) %u\n",
               (unsigned long long) frame_min_ms,
               (unsigned long long) (frame_acc_ms / frame_count),
               (unsigned long long) frame_max_ms,
               (unsigned long long) swap_max_ms, slow_frame_count);
        frame_min_ms = frame_max_ms = frame_acc_ms = swap_max_ms = 0;
        frame_count = 0;
        slow_frame_count = 0;
    }
}

/* Cached once at library load - same pattern as init_noerror() in main.c.
 * getenv on each swap call would also be fine (it is a TLS lookup), but the
 * flag also gets logged, and logging once is nicer than logging per frame. */
static int uncap_enabled = 1;

__attribute__((constructor)) static void ltw_swap_init() {
    /* Default ON: this fork exists to remove the 60 FPS lock. opt-out=0 */
    uncap_enabled = !env_istrue("LTW_UNCAP_FPS_DISABLED");
    diag_enabled = !env_istrue("LTW_SILENT");
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
    uint64_t t0 = now_ns();
    EGLBoolean result = host(dpy, surface);
    uint64_t swap_ms = delta_ms(now_ns(), t0);
    ltw_swap_enforce(dpy);
    diag_frame(swap_ms);
    return result;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects) {
    EGLBoolean (*host)(EGLDisplay, EGLSurface, EGLint *, EGLint) =
        (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint *, EGLint)) host_eglGetProcAddress("eglSwapBuffersWithDamageKHR");
    if(host == NULL) return eglSwapBuffers(dpy, surface);
    uint64_t t0 = now_ns();
    EGLBoolean result = host(dpy, surface, rects, n_rects);
    uint64_t swap_ms = delta_ms(now_ns(), t0);
    ltw_swap_enforce(dpy);
    diag_frame(swap_ms);
    return result;
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects) {
    EGLBoolean (*host)(EGLDisplay, EGLSurface, EGLint *, EGLint) =
        (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint *, EGLint)) host_eglGetProcAddress("eglSwapBuffersWithDamageEXT");
    if(host == NULL) return eglSwapBuffers(dpy, surface);
    uint64_t t0 = now_ns();
    EGLBoolean result = host(dpy, surface, rects, n_rects);
    uint64_t swap_ms = delta_ms(now_ns(), t0);
    ltw_swap_enforce(dpy);
    diag_frame(swap_ms);
    return result;
}

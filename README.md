## Large Thin Wrapper
A thin OpenGL core-to-OpenGL ES wrapper, primarily intended for running Minecraft.

# Building
`./gradlew :ltw:assembleRelease`

After completion, an AAR with native libraries will be available in `ltw/build/outputs/aar/ltw-release.aar`

iOS (from macOS with Xcode + ldid): `make build` → `build/libLTW.dylib`

# Performance & FPS-uncap options (this fork's additions)

All options are environment variables, read once at startup:

| Variable | Default | Effect |
|----------|---------|--------|
| `LTW_UNCAP_FPS` | off | Intercepts every swap-interval entry point (`eglSwapInterval`, `glXSwapIntervalEXT/MESA`, `wglSwapIntervalEXT`) and forces interval 0 — removes the 60 FPS vsync lock when the host compositor allows immediate presents. Set `LTW_UNCAP_FPS=1` to enable. |

Texture-upload optimization (always on, no flag): `glTexImage2D`/`glTexSubImage2D`
no longer issue a synchronous `glGetIntegerv` into the driver on every call —
the current texture binding is served from a per-context shadow updated by
`glBindTexture`/`glActiveTexture`. Shadow misses fall back to the live query,
so correctness never depends on the cache (see `ltw_shadow_test.c`).

`reference/mobileglues-refs/` documents port-ready mechanisms from
[MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) (LGPL-2.1) —
**not compiled into libLTW**; both build systems list sources explicitly and
never touch that directory.

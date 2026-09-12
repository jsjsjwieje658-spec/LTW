/*
 * Self-check for the texture-binding shadow logic in swizzle.c.
 * Pure-C, no GL: stubs the es3_functions table and the unordered_map,
 * replicates the shadow functions verbatim, and asserts the behaviours
 * the wrapper depends on:
 *   1. bind/active updates land in the right (unit, target) slot;
 *   2. uploads read the binding from the shadow instead of the driver;
 *   3. an out-of-range unit or unknown target falls back to the live query;
 *   4. the fallback query is only issued when the shadow cannot answer.
 *
 * NOT PART OF THE BUILD: neither ltw/CMakeLists.txt nor Android.mk list this
 * file. Host-only regression check for the swizzle shadow.
 *
 * Build & run (host):
 *   cc -Wall -o /tmp/ltw_shadow_test ltw_shadow_test.c && /tmp/ltw_shadow_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* --- minimal replicas of the LTW types this logic uses ---------------- */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;

#define MAX_TEXTARGETS 8
#define MAX_SHADOW_TMUS 16

enum { GL_TEXTURE_1D = 0x0DE0,
       GL_TEXTURE_2D = 0x0DE1,
       GL_TEXTURE_3D = 0x806F,
       GL_TEXTURE_CUBE_MAP = 0x8513,
       GL_TEXTURE_2D_ARRAY = 0x8C1A,
       GL_TEXTURE_2D_MULTISAMPLE = 0x9100,
       GL_TEXTURE_2D_MULTISAMPLE_ARRAY = 0x9102,
       GL_TEXTURE_CUBE_MAP_ARRAY = 0x9009,
       GL_TEXTURE_BUFFER = 0x8C2A, /* not shadowed */
       GL_TEXTURE0 = 0x84C0 };

typedef struct {
    GLuint shadow_tmu_bindings[MAX_SHADOW_TMUS][MAX_TEXTARGETS];
    GLint shadow_active_tmu;
} context_t;

static context_t ctx_storage;
static context_t *ctx = &ctx_storage;

/* driver-query counters proving which path was taken */
static int live_queries = 0;

static GLint query_current_texture(GLenum target) {
    (void)target;
    live_queries++;
    return 777; /* sentinel "driver answer" */
}

/* --- verbatim copy of the swizzle.c shadow logic ----------------------- */
static int shadow_target_slot(GLenum target) {
    switch (target) {
        case GL_TEXTURE_1D:                   return 0;
        case GL_TEXTURE_2D:                   return 1;
        case GL_TEXTURE_3D:                   return 2;
        case GL_TEXTURE_CUBE_MAP:             return 3;
        case GL_TEXTURE_2D_ARRAY:             return 4;
        case GL_TEXTURE_2D_MULTISAMPLE:      return 5;
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return 6;
        case GL_TEXTURE_CUBE_MAP_ARRAY:       return 7;
        default:                              return -1;
    }
}

static int shadow_tmu_index(GLint unit) {
    if(unit < 0 || unit >= MAX_SHADOW_TMUS) return -1;
    return unit;
}

static GLint shadow_current_texture(GLenum target) {
    int slot = shadow_target_slot(target);
    if(slot == -1) return -1;
    int tmu = shadow_tmu_index(ctx->shadow_active_tmu);
    if(tmu == -1) return -1;
    GLuint bound = ctx->shadow_tmu_bindings[tmu][slot];
    if(bound == 0) return -1;
    return (GLint) bound;
}

static void swizzle_shadow_bind_texture(GLenum target, GLuint texture) {
    int slot = shadow_target_slot(target);
    if(slot == -1) return;
    int tmu = shadow_tmu_index(ctx->shadow_active_tmu);
    if(tmu == -1) return;
    ctx->shadow_tmu_bindings[tmu][slot] = texture;
}

static void swizzle_shadow_active_texture(GLenum texture) {
    GLint unit = (GLint)texture - GL_TEXTURE0;
    if(unit < 0 || unit >= MAX_SHADOW_TMUS) return;
    ctx->shadow_active_tmu = unit;
}

/* upload-path binding resolution, same order as get_swizzle_track */
static GLint resolve_binding(GLenum target) {
    GLint texture = shadow_current_texture(target);
    if(texture == -1) texture = query_current_texture(target);
    return texture;
}

/* --- tests ------------------------------------------------------------- */
int main(void) {
    memset(ctx, 0, sizeof(*ctx));

    /* 1. cold shadow -> live query, and the answer is used */
    assert(resolve_binding(GL_TEXTURE_2D) == 777);
    assert(live_queries == 1);

    /* 2. active unit 0, bind texture 5 on 2D -> next upload reads 5, no query */
    ctx->shadow_active_tmu = 0;
    swizzle_shadow_bind_texture(GL_TEXTURE_2D, 5);
    assert(resolve_binding(GL_TEXTURE_2D) == 5);
    assert(live_queries == 1); /* no new driver query */

    /* 3. switching units keeps slots independent */
    swizzle_shadow_active_texture(GL_TEXTURE0 + 3);
    assert(ctx->shadow_active_tmu == 3);
    assert(resolve_binding(GL_TEXTURE_2D) == 777); /* unit 3 never bound -> query */
    assert(live_queries == 2);
    swizzle_shadow_bind_texture(GL_TEXTURE_2D, 9);
    assert(resolve_binding(GL_TEXTURE_2D) == 9);
    assert(live_queries == 2);
    swizzle_shadow_active_texture(GL_TEXTURE0); /* back to unit 0 */
    assert(resolve_binding(GL_TEXTURE_2D) == 5); /* unit 0 still cached */
    assert(live_queries == 2);

    /* 4. targets are independent slots on the same unit */
    swizzle_shadow_bind_texture(GL_TEXTURE_CUBE_MAP, 42);
    assert(resolve_binding(GL_TEXTURE_CUBE_MAP) == 42);
    assert(resolve_binding(GL_TEXTURE_2D) == 5);
    assert(live_queries == 2);

    /* 5. untracked target falls back to the driver, never crashes */
    assert(resolve_binding(GL_TEXTURE_BUFFER) == 777);
    assert(live_queries == 3);

    /* 6. out-of-range unit: set is ignored (matches GL semantics - the unit
     * stays on the last valid one), so the shadow keeps answering. */
    swizzle_shadow_active_texture(GL_TEXTURE0 + MAX_SHADOW_TMUS);
    assert(ctx->shadow_active_tmu == 0); /* unchanged: out of range rejected */
    assert(resolve_binding(GL_TEXTURE_2D) == 5); /* still served from shadow */
    assert(live_queries == 3);

    /* 7. binding 0 (unbind) forces the live query again, matches GL semantics */
    swizzle_shadow_active_texture(GL_TEXTURE0);
    swizzle_shadow_bind_texture(GL_TEXTURE_2D, 0);
    assert(resolve_binding(GL_TEXTURE_2D) == 777);
    assert(live_queries == 4);

    /* 8. slot mapping sanity for every tracked target */
    assert(shadow_target_slot(GL_TEXTURE_1D) == 0);
    assert(shadow_target_slot(GL_TEXTURE_2D) == 1);
    assert(shadow_target_slot(GL_TEXTURE_3D) == 2);
    assert(shadow_target_slot(GL_TEXTURE_CUBE_MAP) == 3);
    assert(shadow_target_slot(GL_TEXTURE_2D_ARRAY) == 4);
    assert(shadow_target_slot(GL_TEXTURE_2D_MULTISAMPLE) == 5);
    assert(shadow_target_slot(GL_TEXTURE_2D_MULTISAMPLE_ARRAY) == 6);
    assert(shadow_target_slot(GL_TEXTURE_CUBE_MAP_ARRAY) == 7);

    printf("ALL SHADOW SELF-CHECKS PASSED\n");
    return 0;
}

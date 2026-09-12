/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "proc.h"
#include "egl.h"
#include <string.h>
#include "libraryinternal.h"
#include "GL/gl.h"
//#include <GL/glext.h>

#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46

static void swizzle_process_bgra(GLenum* swizzle) {
    GLenum red_src = swizzle[0];
    GLenum blue_src = swizzle[2];
    swizzle[0] = blue_src;
    swizzle[2] = red_src;
}

static void swizzle_process_endianness(GLenum* swizzle) {
    GLenum orig_swizzle[4];
    memcpy(orig_swizzle, swizzle, 4 * sizeof(GLenum));
    swizzle[0] = orig_swizzle[3];
    swizzle[1] = orig_swizzle[2];
    swizzle[2] = orig_swizzle[1];
    swizzle[3] = orig_swizzle[0];
}

/* ---------------------------------------------------------------------------
 * Texture-binding shadow
 *
 * swizzle_process_upload used to call glGetIntegerv(GL_TEXTURE_BINDING_2D, ..)
 * on EVERY glTexImage2D/glTexSubImage2D. That is a synchronous round trip into
 * the host driver, and with ANGLE-on-Metal it can force a pipeline flush,
 * stalling the CPU while the GPU catches up. Minecraft uploads textures in
 * bursts (atlas stitching, animated textures), so this was a per-upload stall.
 *
 * The shadow keeps the last-known binding per (unit, target) and is updated
 * from the glBindTexture/glActiveTexture wrappers below. A cold start or a
 * GL_TEXTURE0 offset beyond the shadow simply falls back to the live query,
 * so correctness never depends on the cache.
 * ------------------------------------------------------------------------ */

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

/* Read the current binding for `target` from the shadow when possible,
 * otherwise fall back to the driver query. */
static GLint shadow_current_texture(GLenum target) {
    int slot = shadow_target_slot(target);
    if(slot == -1) return -1;
    context_t *ctx = current_context;
    int tmu = shadow_tmu_index(ctx->shadow_active_tmu);
    if(tmu == -1) return -1;
    GLuint bound = ctx->shadow_tmu_bindings[tmu][slot];
    if(bound == 0) return -1; /* never populated for this slot -> live query */
    return (GLint) bound;
}

static GLint query_current_texture(GLenum target) {
    GLint texture = 0;
    es3_functions.glGetIntegerv(get_textarget_query_param(target), &texture);
    return texture;
}

INTERNAL void swizzle_shadow_bind_texture(GLenum target, GLuint texture) {
    context_t *ctx = current_context;
    if(ctx == NULL) return;
    int slot = shadow_target_slot(target);
    if(slot == -1) return;
    int tmu = shadow_tmu_index(ctx->shadow_active_tmu);
    if(tmu == -1) return;
    ctx->shadow_tmu_bindings[tmu][slot] = texture;
}

INTERNAL void swizzle_shadow_active_texture(GLenum texture) {
    context_t *ctx = current_context;
    if(ctx == NULL) return;
    GLint unit = (GLint)texture - GL_TEXTURE0;
    if(unit < 0 || unit >= MAX_SHADOW_TMUS) return;
    ctx->shadow_active_tmu = unit;
}

static texture_swizzle_track_t* get_swizzle_track(GLenum target) {
    GLenum getter = get_textarget_query_param(target);
    if(getter == 0) return NULL;
    /* Fast path: read the binding from the shadow instead of a synchronous
     * glGetIntegerv into the host driver (see the shadow comment above). */
    GLint texture = shadow_current_texture(target);
    if(texture == -1) texture = query_current_texture(target);
    texture_swizzle_track_t* track = unordered_map_get(current_context->texture_swztrack_map, (void*)texture);
    if(track == NULL) {
        track = malloc(sizeof(texture_swizzle_track_t));
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_R, (GLint*)&track->original_swizzle[0]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_G, (GLint*)&track->original_swizzle[1]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_B, (GLint*)&track->original_swizzle[2]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_A, (GLint*)&track->original_swizzle[3]);
        unordered_map_put(current_context->texture_swztrack_map, (void*)texture, track);
    }
    return track;
}

static void apply_swizzles(GLenum target, texture_swizzle_track_t* track) {
    GLenum new_swizzle[4];
    memcpy(new_swizzle, track->original_swizzle, 4 * sizeof(GLenum));
    if(track->goofy_byte_order) swizzle_process_endianness(new_swizzle);
    if(track->upload_bgra) swizzle_process_bgra(new_swizzle);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, new_swizzle[0]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, new_swizzle[1]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, new_swizzle[2]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, new_swizzle[3]);
}

INTERNAL void swizzle_process_upload(GLenum target, GLenum* format, GLenum* type) {
    texture_swizzle_track_t* track = get_swizzle_track(target);
    if(track == NULL) return;
    bool apply_upload_bgra = false;
    bool apply_goofy_order = false;
    if((*format) == GL_BGRA_EXT) {
        apply_upload_bgra = true;
        *format = GL_RGBA;
    }
    if((*type) == 0x8035) {
        apply_goofy_order = true;
        *type = GL_UNSIGNED_BYTE;
    }
    if((*type) == 0x8367) {
        *type = GL_UNSIGNED_BYTE;
    }
    if(apply_goofy_order != track->goofy_byte_order || apply_upload_bgra != track->upload_bgra) {
        track->goofy_byte_order = apply_goofy_order;
        track->upload_bgra = apply_upload_bgra;
        apply_swizzles(target, track);
    }
}

INTERNAL void swizzle_process_swizzle_param(GLenum target, GLenum swizzle_param, const GLenum* swizzle) {
    switch (swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
        case GL_TEXTURE_SWIZZLE_RGBA:
            break;
        default:
            return;
    }
    texture_swizzle_track_t* track = get_swizzle_track(target);
    if(track == NULL) return;
    switch(swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
            track->original_swizzle[swizzle_param - GL_TEXTURE_SWIZZLE_R] = *swizzle;
            apply_swizzles(target, track);
            break;
        case GL_TEXTURE_SWIZZLE_RGBA:
            memcpy(track->original_swizzle, swizzle, 4 * sizeof(GLenum));
            apply_swizzles(target, track);
            break;
    }
}

/* ---------------------------------------------------------------------------
 * Binding wrappers feeding the shadow (see the shadow comment near the top).
 * They do nothing but forward the call and update the cache, so behaviour is
 * identical to the previous pass-through; the win is that texture uploads no
 * longer pay a synchronous glGetIntegerv.
 * ------------------------------------------------------------------------ */

void glBindTexture(GLenum target, GLuint texture) {
    if(!current_context) return;
    es3_functions.glBindTexture(target, texture);
    swizzle_shadow_bind_texture(target, texture);
}

void glActiveTexture(GLenum texture) {
    if(!current_context) return;
    es3_functions.glActiveTexture(texture);
    swizzle_shadow_active_texture(texture);
}
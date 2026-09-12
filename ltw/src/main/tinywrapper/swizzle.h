/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#ifndef GL4ES_WRAPPER_SWIZZLE_H
#define GL4ES_WRAPPER_SWIZZLE_H

#include "egl.h"

void swizzle_process_upload(GLenum target, GLenum *format, GLenum *type);
void swizzle_process_swizzle_param(GLenum target, GLenum swizzle_param, const GLenum* swizzle);
/* Texture-binding shadow updates, called from the glBindTexture /
 * glActiveTexture wrappers. */
void swizzle_shadow_bind_texture(GLenum target, GLuint texture);
void swizzle_shadow_active_texture(GLenum texture);

#endif //GL4ES_WRAPPER_SWIZZLE_H

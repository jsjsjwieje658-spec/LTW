/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include <GLES3/gl31.h>
#include <stdlib.h>
#include "proc.h"
#include "egl.h"
#include "main.h"

typedef struct {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint baseVertex;
    GLuint reservedMustBeZero;
} indirect_pass_t;

void basevertex_init(context_t* context) {
    basevertex_renderer_t *renderer = &context->basevertex;
    if(context->drawelementsbasevertex != NULL) {
        printf("LTW: BaseVertex render calls will use the host driver implementation\n");
        return;
    }
    if(!context->es31) {
        printf("LTW: BaseVertex render calls not available: requires OpenGL ES 3.1\n");
        return;
    }
    es3_functions.glGenBuffers(1, &renderer->indirectRenderBuffer);
    GLenum error = es3_functions.glGetError();
    if(error != GL_NO_ERROR) {
        printf("LTW: Failed to initialize indirect buffers: %x\n", error);
        return;
    }
    renderer->ready = true;
}

GLint type_bytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: return 1;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_UNSIGNED_INT: return 4;
        default: return -1;
    }
}

static void restore_state(GLuint element_buffer) {
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, current_context->bound_buffers[get_buffer_index(GL_DRAW_INDIRECT_BUFFER)]);
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    if(!current_context) return;
    if(current_context->drawelementsbasevertex != NULL) {
        current_context->drawelementsbasevertex(mode, count, type, indices, basevertex);
        return;
    }
    basevertex_renderer_t *renderer = &current_context->basevertex;
    if(!renderer->ready) return;
    GLint elementbuffer;
    es3_functions.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementbuffer);
    if(elementbuffer == 0) {
        // I am not bothered enough to implement this.
        static bool nobuffer_trigger = false;
        if(!nobuffer_trigger) {
            printf("LTW: Base vertex draws without element buffer are not supported\n");
            nobuffer_trigger = true; // once: per-call printf is a frame-time killer on iOS
        }
        return;
    }
    GLint typeBytes = type_bytes(type);
    uintptr_t indicesPointer = (uintptr_t)indices;
    if(indicesPointer % typeBytes != 0) {
        static bool misaligned_trigger = false;
        if(!misaligned_trigger) {
            printf("LTW: misaligned base vertex draw not supported\n");
            misaligned_trigger = true;
        }
    }
    indirect_pass_t indirect_pass;
    indirect_pass.count = count;
    indirect_pass.firstIndex = indicesPointer / typeBytes;
    indirect_pass.baseVertex = basevertex;
    indirect_pass.instanceCount = 1;
    indirect_pass.reservedMustBeZero = 0;
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, renderer->indirectRenderBuffer);
    es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(indirect_pass_t), &indirect_pass, GL_STREAM_DRAW);
    es3_functions.glDrawElementsIndirect(mode, type, 0);
    restore_state(elementbuffer);
}



void glMultiDrawElementsBaseVertex(GLenum mode,
                                   const GLsizei *count,
                                   GLenum type,
                                   const void * const *indices,
                                   GLsizei drawcount,
                                   const GLint *basevertex) {
    if(!current_context) return;
    if(current_context->drawelementsbasevertex != NULL) {
        for(GLsizei i = 0; i < drawcount; i++) {
            current_context->drawelementsbasevertex(mode, count[i], type, indices[i], basevertex[i]);
        }
        return;
    }
    basevertex_renderer_t *renderer = &current_context->basevertex;
    if(!renderer->ready) return;
    GLint elementbuffer;
    es3_functions.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementbuffer);
    if(elementbuffer == 0) {
        // I am not bothered enough to implement this.
        static bool nobuffer_trigger_md = false;
        if(!nobuffer_trigger_md) {
            printf("LTW: Base vertex draws without element buffer are not supported\n");
            nobuffer_trigger_md = true; // once, not per call
        }
        return;
    }
    GLint typeBytes = type_bytes(type);
    // Scratch buffer instead of a VLA: drawcount is unbounded client input, a
    // large burst would blow the stack. Grown geometrically, never freed per
    // frame - same lifetime as the context.
    static indirect_pass_t* scratch = NULL;
    static GLsizei scratch_cap = 0;
    if(drawcount > scratch_cap) {
        GLsizei newcap = scratch_cap ? scratch_cap : 64;
        while(newcap < drawcount) newcap *= 2;
        indirect_pass_t* grown = realloc(scratch, newcap * sizeof(indirect_pass_t));
        if(grown == NULL) return; // OOM: skip the draw, better than crashing
        scratch = grown;
        scratch_cap = newcap;
    }
    indirect_pass_t* indirect_passes = scratch;
    for(GLsizei i = 0; i < drawcount; i++) {
        uintptr_t indicesPointer = (uintptr_t)indices[i];
        if(indicesPointer % typeBytes != 0) {
            static bool misaligned_trigger_md = false;
            if(!misaligned_trigger_md) {
                printf("LTW: misaligned base vertex draw not supported (draw %i)\n", i);
                misaligned_trigger_md = true;
            }
            return;
        }
        indirect_pass_t* pass = &indirect_passes[i];
        pass->count = count[i];
        pass->firstIndex = indicesPointer / typeBytes;
        pass->baseVertex = basevertex[i];
        pass->instanceCount = 1;
        pass->reservedMustBeZero = 0;
    }
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, renderer->indirectRenderBuffer);
    // Orphan-then-fill is the canonical STREAM_DRAW upload: naming a full
    // new size each call lets the driver hand out fresh storage instead of
    // synchronizing with the GPU still reading the previous frame's data.
    es3_functions.glBufferData(GL_DRAW_INDIRECT_BUFFER, (long)sizeof(indirect_pass_t) * drawcount, NULL, GL_STREAM_DRAW);
    es3_functions.glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, (long)sizeof(indirect_pass_t) * drawcount, indirect_passes);
    if(current_context->multidraw_indirect) {
        es3_functions.glMultiDrawElementsIndirectEXT(mode, type, 0, drawcount, 0);
    } else for(GLsizei i = 0; i < drawcount; i++) {
        es3_functions.glDrawElementsIndirect(mode, type, (void*)(sizeof(indirect_pass_t) * i));
    }
    restore_state(elementbuffer);
}
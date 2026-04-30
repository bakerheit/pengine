#pragma once

#include <glad/gl.h>

// Thin cache to avoid redundant GL state changes on the hot path.
// Call these instead of the raw GL functions for anything that changes per-draw.
namespace pengine::gl_state {

inline void use_program(GLuint program) {
    static GLuint current = 0;
    if (current != program) {
        glUseProgram(program);
        current = program;
    }
}

inline void bind_vao(GLuint vao) {
    static GLuint current = 0;
    if (current != vao) {
        glBindVertexArray(vao);
        current = vao;
    }
}

inline void bind_texture_2d(GLuint unit, GLuint tex) {
    // Per-unit tracking; enough units for Phase 1.
    static GLuint current[16] = {};
    if (unit < 16 && current[unit] != tex) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
        current[unit] = tex;
    }
}

// Call after any direct glUseProgram/glBindVertexArray to keep cache coherent.
inline void invalidate() {
    use_program(0);
    bind_vao(0);
}

} // namespace pengine::gl_state

#include "gfx/gl_state.h"

#include <array>

namespace apricot::gl_state {
namespace {

// GL 3.3 core guarantees at least 16 combined texture image units; anything
// above that is an error in the caller, not something to grow an array for.
constexpr std::size_t kMaxTextureUnits = 16;

// Buffer targets the cache tracks. Others fall through and always bind.
constexpr std::size_t kArrayBufferSlot = 0;
constexpr std::size_t kElementBufferSlot = 1;
constexpr std::size_t kBufferSlots = 2;

struct Cache {
    std::array<GLuint, kMaxTextureUnits> textures{};
    std::array<GLuint, kBufferSlots> buffers{};
    GLuint active_unit = 0xFFFFFFFFu;
    GLuint program = 0;
    GLuint vao = 0;
    unsigned int skipped = 0;
};

Cache& cache() {
    static Cache c;
    return c;
}

bool buffer_slot(GLenum target, std::size_t& out) {
    switch (target) {
        case GL_ARRAY_BUFFER: out = kArrayBufferSlot; return true;
        case GL_ELEMENT_ARRAY_BUFFER: out = kElementBufferSlot; return true;
        default: return false;
    }
}

}  // namespace

void bind_texture(GLuint unit, GLuint texture) {
    if (unit >= kMaxTextureUnits) return;
    Cache& c = cache();

    if (c.textures[unit] == texture && c.active_unit == unit) {
        ++c.skipped;
        return;
    }
    if (c.active_unit != unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        c.active_unit = unit;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    c.textures[unit] = texture;
}

void use_program(GLuint program) {
    Cache& c = cache();
    if (c.program == program) {
        ++c.skipped;
        return;
    }
    glUseProgram(program);
    c.program = program;
}

void bind_vertex_array(GLuint vao) {
    Cache& c = cache();
    if (c.vao == vao) {
        ++c.skipped;
        return;
    }
    glBindVertexArray(vao);
    c.vao = vao;

    // A VAO carries its own element-buffer binding, so binding one silently
    // changes what GL_ELEMENT_ARRAY_BUFFER points at. Caching through it would
    // skip a genuinely needed rebind.
    c.buffers[kElementBufferSlot] = 0xFFFFFFFFu;
}

void bind_buffer(GLenum target, GLuint buffer) {
    std::size_t slot = 0;
    if (!buffer_slot(target, slot)) {
        glBindBuffer(target, buffer);
        return;
    }
    Cache& c = cache();
    if (c.buffers[slot] == buffer) {
        ++c.skipped;
        return;
    }
    glBindBuffer(target, buffer);
    c.buffers[slot] = buffer;
}

void on_texture_deleted(GLuint texture) {
    Cache& c = cache();
    for (GLuint& t : c.textures) {
        if (t == texture) t = 0xFFFFFFFFu;
    }
}

void on_program_deleted(GLuint program) {
    Cache& c = cache();
    if (c.program == program) c.program = 0xFFFFFFFFu;
}

void on_vertex_array_deleted(GLuint vao) {
    Cache& c = cache();
    if (c.vao == vao) c.vao = 0xFFFFFFFFu;
}

void on_buffer_deleted(GLuint buffer) {
    Cache& c = cache();
    for (GLuint& b : c.buffers) {
        if (b == buffer) b = 0xFFFFFFFFu;
    }
}

void invalidate_all() {
    Cache& c = cache();
    c.textures.fill(0xFFFFFFFFu);
    c.buffers.fill(0xFFFFFFFFu);
    c.active_unit = 0xFFFFFFFFu;
    c.program = 0xFFFFFFFFu;
    c.vao = 0xFFFFFFFFu;
}

unsigned int skipped_binds() { return cache().skipped; }
void reset_counters() { cache().skipped = 0; }

}  // namespace apricot::gl_state

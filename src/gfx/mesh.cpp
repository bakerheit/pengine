#include "gfx/mesh.h"

#include <utility>

#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {

// ============================================================================
//  STUB — see mesh.h. Move and destroy are real, because getting GL handle
//  ownership wrong leaks or double-frees and that is worth having correct from
//  the start. Upload and draw are inert.
//  TODO(renderer ticket): implement upload/draw/draw_instanced.
// ============================================================================

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_),
      index_count_(other.index_count_) {
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.index_count_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        index_count_ = other.index_count_;
        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.index_count_ = 0;
    }
    return *this;
}

bool Mesh::upload(const ChunkMesh&) {
    AP_ERROR("Mesh::upload is not implemented yet");
    return false;
}

void Mesh::destroy() {
    // Every delete pairs with its gl_state hook. See the warning in gl_state.h.
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        gl_state::on_buffer_deleted(ebo_);
        ebo_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        gl_state::on_buffer_deleted(vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
    index_count_ = 0;
}

void Mesh::draw() const {}
void Mesh::draw_instanced(GLsizei) const {}

}  // namespace apricot

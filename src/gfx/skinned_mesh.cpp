#include "gfx/skinned_mesh.h"

#include <cstddef>
#include <utility>

#include "core/emesh_format.h"
#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {
namespace {

const void* attribute_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

}  // namespace

SkinnedMesh::~SkinnedMesh() { destroy(); }

SkinnedMesh::SkinnedMesh(SkinnedMesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_),
      index_count_(other.index_count_), bounds_(other.bounds_) {
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.index_count_ = 0;
    other.bounds_ = AABB{};
}

SkinnedMesh& SkinnedMesh::operator=(SkinnedMesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        index_count_ = other.index_count_;
        bounds_ = other.bounds_;
        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.index_count_ = 0;
        other.bounds_ = AABB{};
    }
    return *this;
}

bool SkinnedMesh::upload(const SkinnedEmesh& source) {
    if (source.vertices.empty() || source.indices.empty()) {
        AP_ERROR("skinned mesh: refusing empty geometry");
        return false;
    }
    destroy();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    if (!vao_ || !vbo_ || !ebo_) {
        AP_ERROR("skinned mesh: GL refused buffers (vao=%u vbo=%u ebo=%u)",
                 vao_, vbo_, ebo_);
        destroy();
        return false;
    }

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(source.vertices.size() *
                                         sizeof(EmeshSkinnedVertex)),
                 source.vertices.data(), GL_STATIC_DRAW);
    gl_state::bind_buffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(source.indices.size() *
                                         sizeof(uint32_t)),
                 source.indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(
        sizeof(EmeshSkinnedVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(EmeshSkinnedVertex, px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(EmeshSkinnedVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(EmeshSkinnedVertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(EmeshSkinnedVertex, tx)));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(
        4, 4, GL_UNSIGNED_BYTE, stride,
        attribute_offset(offsetof(EmeshSkinnedVertex, bone_idx)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
        5, 4, GL_FLOAT, GL_FALSE, stride,
        attribute_offset(offsetof(EmeshSkinnedVertex, bone_weight)));
    gl_state::bind_vertex_array(0);

    index_count_ = static_cast<GLsizei>(source.indices.size());
    bounds_ = source.bounds;
    return true;
}

void SkinnedMesh::draw() const {
    if (!vao_ || index_count_ <= 0) return;
    gl_state::bind_vertex_array(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void SkinnedMesh::destroy() {
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
    bounds_ = AABB{};
}

}  // namespace apricot

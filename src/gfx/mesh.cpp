#include "gfx/mesh.h"

#include <cstddef>
#include <utility>

#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {
namespace {

// Turn a struct offset into the pointer-shaped argument the pre-DSA vertex
// attribute API insists on. Isolated here so the ugly cast appears once.
const void* attrib_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

}  // namespace

Mesh::~Mesh() { destroy(); }

Mesh::Mesh(Mesh&& other) noexcept
    : vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_),
      index_count_(other.index_count_), bounds_(other.bounds_),
      instance_vbo_(other.instance_vbo_),
      instance_capacity_(other.instance_capacity_),
      instance_staged_(other.instance_staged_) {
    other.vao_ = 0;
    other.vbo_ = 0;
    other.ebo_ = 0;
    other.index_count_ = 0;
    other.bounds_ = AABB{};
    other.instance_vbo_ = 0;
    other.instance_capacity_ = 0;
    other.instance_staged_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        index_count_ = other.index_count_;
        bounds_ = other.bounds_;
        instance_vbo_ = other.instance_vbo_;
        instance_capacity_ = other.instance_capacity_;
        instance_staged_ = other.instance_staged_;
        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.index_count_ = 0;
        other.bounds_ = AABB{};
        other.instance_vbo_ = 0;
        other.instance_capacity_ = 0;
        other.instance_staged_ = 0;
    }
    return *this;
}

bool Mesh::upload(const ChunkMesh& src) {
    return upload_geometry(src.vertices, src.indices, src.bounds);
}

bool Mesh::upload(const MeshData& src) {
    return upload_geometry(src.vertices, src.indices, src.bounds);
}

bool Mesh::upload_geometry(const std::vector<MeshVertex>& vertices,
                           const std::vector<uint32_t>& indices,
                           const AABB& bounds) {
    if (vertices.empty() || indices.empty()) {
        AP_ERROR("mesh: refusing to upload empty geometry (%zu verts, %zu indices)",
                 vertices.size(), indices.size());
        return false;
    }
    if (indices.size() % 3u != 0u) {
        AP_ERROR("mesh: %zu indices is not a whole number of triangles",
                 indices.size());
        return false;
    }

    destroy();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    if (!vao_ || !vbo_ || !ebo_) {
        AP_ERROR("mesh: GL refused to create buffers (vao=%u vbo=%u ebo=%u)",
                 vao_, vbo_, ebo_);
        destroy();
        return false;
    }

    gl_state::bind_vertex_array(vao_);

    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)),
                 vertices.data(), GL_STATIC_DRAW);

    // The element buffer binding is part of VAO state, so this must happen
    // while the VAO is bound and must NOT be unbound before the VAO is.
    gl_state::bind_buffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(MeshVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(MeshVertex, uv)));
    // Location 3 is reserved for a tangent and deliberately left disabled; the
    // instance block starts at 4 so adding normal mapping later does not
    // renumber every shader in the engine.

    gl_state::bind_vertex_array(0);

    index_count_ = static_cast<GLsizei>(indices.size());
    bounds_ = bounds;
    return true;
}

bool Mesh::upload_instances(const InstanceData* data, GLsizei count) {
    if (!vao_) {
        AP_ERROR("mesh: upload_instances on a mesh with no geometry");
        return false;
    }
    if (count <= 0 || data == nullptr) {
        instance_staged_ = 0;
        return false;
    }

    gl_state::bind_vertex_array(vao_);

    const bool first_time = (instance_vbo_ == 0);
    if (first_time) {
        glGenBuffers(1, &instance_vbo_);
        if (!instance_vbo_) {
            AP_ERROR("mesh: GL refused to create an instance buffer");
            instance_staged_ = 0;
            return false;
        }
    }
    gl_state::bind_buffer(GL_ARRAY_BUFFER, instance_vbo_);

    if (first_time) {
        // Wire the divisor-1 attributes exactly once, into this mesh's VAO.
        // The layout contract lives in gfx/instance.h; it is mirrored here and
        // in lit_instanced.vert and all three must move together.
        constexpr GLsizei stride = static_cast<GLsizei>(sizeof(InstanceData));
        const auto wire = [](GLuint loc, GLint components, std::size_t offset) {
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, components, GL_FLOAT, GL_FALSE, stride,
                                  attrib_offset(offset));
            glVertexAttribDivisor(loc, 1);
        };
        // A mat4 attribute is four consecutive vec4 locations; GL has no way to
        // describe it as one.
        for (GLuint col = 0; col < 4; ++col) {
            wire(4u + col, 4,
                 offsetof(InstanceData, model) +
                     static_cast<std::size_t>(col) * sizeof(glm::vec4));
        }
        wire(8, 4, offsetof(InstanceData, normal_c0));
        wire(9, 4, offsetof(InstanceData, normal_c1));
        wire(10, 4, offsetof(InstanceData, normal_c2));
        wire(11, 4, offsetof(InstanceData, tint));
        wire(12, 2, offsetof(InstanceData, uv_scale));
    }

    const GLsizeiptr bytes =
        static_cast<GLsizeiptr>(count) *
        static_cast<GLsizeiptr>(sizeof(InstanceData));

    if (count > instance_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
        instance_capacity_ = count;
    } else {
        // Orphan, then fill. One shared mesh feeds several batches per frame
        // (every box in the field is the same cube), so a plain glBufferSubData
        // over a store the previous draw is still reading stalls the pipeline
        // until that draw retires. Handing GL a fresh store lets it rename the
        // allocation instead of waiting.
        const GLsizeiptr capacity_bytes =
            static_cast<GLsizeiptr>(instance_capacity_) *
            static_cast<GLsizeiptr>(sizeof(InstanceData));
        glBufferData(GL_ARRAY_BUFFER, capacity_bytes, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, data);
    }

    instance_staged_ = count;
    return true;
}

void Mesh::draw() const {
    if (!vao_ || index_count_ == 0) return;
    gl_state::bind_vertex_array(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

void Mesh::draw_instanced(GLsizei instance_count) const {
    if (!vao_ || index_count_ == 0 || instance_count <= 0) return;
    if (instance_count > instance_staged_) {
        // Reading past the staged records draws garbage transforms, which
        // scatters geometry to infinity and reads as "the batching is broken".
        // Say so and draw only what is really there.
        AP_ERROR("mesh: asked for %d instances but only %d are staged; "
                 "drawing %d",
                 instance_count, instance_staged_, instance_staged_);
        instance_count = instance_staged_;
        if (instance_count <= 0) return;
    }
    gl_state::bind_vertex_array(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr,
                            instance_count);
}

void Mesh::destroy() {
    // Every delete pairs with its gl_state hook. See the warning in gl_state.h.
    if (instance_vbo_) {
        glDeleteBuffers(1, &instance_vbo_);
        gl_state::on_buffer_deleted(instance_vbo_);
        instance_vbo_ = 0;
    }
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
    instance_capacity_ = 0;
    instance_staged_ = 0;
    bounds_ = AABB{};
}

}  // namespace apricot

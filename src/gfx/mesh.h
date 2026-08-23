#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include "core/aabb.h"
#include "gfx/instance.h"
#include "gfx/primitives.h"  // MeshVertex, MeshData
#include "terrain/chunk.h"

namespace apricot {

// A VAO/VBO/EBO triple owning GPU geometry, plus a lazily-created per-instance
// attribute buffer.
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Move-only. Copying would duplicate GL handles and the second destructor
    // would delete geometry the first copy is still drawing.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    // Upload terrain chunk geometry. ChunkMesh is sim-side plain data; this is
    // the boundary where it becomes a GPU resource.
    bool upload(const ChunkMesh& src);

    // Upload procedurally generated geometry (gfx/primitives.h).
    bool upload(const MeshData& src);

    void destroy();
    void draw() const;

    // Stage `count` per-instance records into this mesh's instance buffer and
    // wire the divisor-1 attributes (once, on first call). Separate from
    // draw_instanced so the caller can decide how many draws one upload feeds.
    // Returns false if the upload could not happen, in which case DO NOT draw:
    // drawing anyway reads whatever was in the buffer last frame.
    bool upload_instances(const InstanceData* data, GLsizei count);

    // One glDrawElementsInstanced over the records staged by the last
    // upload_instances(). Drawing more instances than were staged reads past
    // the buffer, so the count is checked and clamped, loudly.
    void draw_instanced(GLsizei instance_count) const;

    bool valid() const { return vao_ != 0; }
    GLsizei index_count() const { return index_count_; }
    const AABB& bounds() const { return bounds_; }

private:
    bool upload_geometry(const std::vector<MeshVertex>& vertices,
                         const std::vector<uint32_t>& indices,
                         const AABB& bounds);

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei index_count_ = 0;
    AABB bounds_;

    // Lazily created on the first upload_instances(). Mutable is not needed —
    // upload_instances() is non-const on purpose, so "this draw grew a buffer"
    // is visible in the signature rather than hidden behind a const method.
    GLuint instance_vbo_ = 0;
    GLsizei instance_capacity_ = 0;
    GLsizei instance_staged_ = 0;
};

}  // namespace apricot

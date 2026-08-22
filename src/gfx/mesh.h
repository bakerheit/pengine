#pragma once

#include <glad/gl.h>

#include "terrain/chunk.h"

namespace apricot {

// A VAO/VBO/EBO triple owning GPU geometry.
//
// NOT IMPLEMENTED YET — contract only; mesh.cpp is a stub.
// TODO(renderer ticket): upload, draw, and instanced draw with a per-instance
// attribute buffer. Note the ownership rule below before implementing.
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

    void destroy();
    void draw() const;
    void draw_instanced(GLsizei instance_count) const;

    bool valid() const { return vao_ != 0; }
    GLsizei index_count() const { return index_count_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei index_count_ = 0;
};

}  // namespace apricot

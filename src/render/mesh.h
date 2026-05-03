#pragma once

#include <vector>
#include <cstdint>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace pengine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent; // xyz = tangent, w = bitangent sign
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() { destroy(); }

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& o) noexcept
        : vao_(o.vao_), vbo_(o.vbo_), ebo_(o.ebo_),
          index_count_(o.index_count_),
          bounds_min_(o.bounds_min_), bounds_max_(o.bounds_max_),
          instance_vbo_(o.instance_vbo_), instance_capacity_(o.instance_capacity_)
    { o.vao_ = o.vbo_ = o.ebo_ = o.instance_vbo_ = 0;
      o.instance_capacity_ = 0; o.index_count_ = 0; }
    Mesh& operator=(Mesh&& o) noexcept {
        if (this != &o) { destroy();
            vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_;
            instance_vbo_ = o.instance_vbo_;
            instance_capacity_ = o.instance_capacity_;
            index_count_ = o.index_count_;
            bounds_min_ = o.bounds_min_; bounds_max_ = o.bounds_max_;
            o.vao_ = o.vbo_ = o.ebo_ = o.instance_vbo_ = 0;
            o.instance_capacity_ = 0; o.index_count_ = 0; }
        return *this;
    }

    void upload(const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices);
    void destroy();
    void draw() const;

    // Instanced draw. Uploads `count` per-instance model matrices to an
    // internal VBO and emits a single glDrawElementsInstanced. The first call
    // wires up vertex attribs 4..7 (one mat4 column each) on this mesh's VAO
    // with divisor = 1; pair with a shader that consumes them at those
    // locations (see assets/shaders/lit_instanced.vert).
    void draw_instanced(const glm::mat4* matrices, int count) const;

    int       index_count() const { return index_count_; }
    glm::vec3 bounds_min()  const { return bounds_min_; }
    glm::vec3 bounds_max()  const { return bounds_max_; }

private:
    GLuint    vao_         = 0;
    GLuint    vbo_         = 0;
    GLuint    ebo_         = 0;
    int       index_count_ = 0;
    glm::vec3 bounds_min_  = {0.f, 0.f, 0.f};
    glm::vec3 bounds_max_  = {0.f, 0.f, 0.f};

    // Lazy-initialised on first draw_instanced() call. Mutable so a
    // logically-const draw can grow the buffer.
    mutable GLuint instance_vbo_      = 0;
    mutable int    instance_capacity_ = 0;
};

// Procedural geometry helpers.
void make_cube(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float half = 0.5f);
void make_plane(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float half = 10.f);

// Load a single-submesh static .emesh into one Mesh (combines all submeshes
// into one buffer pair). For multi-material loads, use Model instead.
bool load_static_emesh(const std::string& path, Mesh& out);

} // namespace pengine

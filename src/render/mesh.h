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

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices);
    void destroy();
    void draw() const;

    int index_count() const { return index_count_; }

private:
    GLuint vao_         = 0;
    GLuint vbo_         = 0;
    GLuint ebo_         = 0;
    int    index_count_ = 0;
};

// Procedural geometry helpers.
void make_cube(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float half = 0.5f);
void make_plane(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float half = 10.f);

} // namespace pengine

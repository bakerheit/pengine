#include "render/mesh.h"

#include "render/gl_state.h"

namespace pengine {

void Mesh::upload(const std::vector<Vertex>& vertices,
                  const std::vector<uint32_t>& indices) {
    destroy();
    index_count_ = static_cast<int>(indices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    gl_state::bind_vao(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    // layout 0: position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    // layout 1: normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    // layout 2: uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    // layout 3: tangent
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, tangent)));

    gl_state::bind_vao(0);
}

void Mesh::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);       vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);       ebo_ = 0; }
    index_count_ = 0;
}

void Mesh::draw() const {
    gl_state::bind_vao(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

// ---------------------------------------------------------------------------
// Procedural geometry
// ---------------------------------------------------------------------------

// Each face of the cube has its own 4 vertices for hard (flat-shaded) normals.
void make_cube(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float h) {
    verts.clear(); indices.clear();

    // face: normal, tangent, 4 corner positions, 4 UVs
    struct Face {
        glm::vec3 n, t;
        glm::vec3 p[4];
        glm::vec2 uv[4];
    };

    const Face faces[] = {
        // +X
        {{ 1,0,0},{ 0,0,-1}, {{ h,-h,-h},{ h,-h, h},{ h, h, h},{ h, h,-h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -X
        {{-1,0,0},{ 0,0, 1}, {{-h,-h, h},{-h,-h,-h},{-h, h,-h},{-h, h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // +Y
        {{ 0,1,0},{ 1,0, 0}, {{-h, h,-h},{ h, h,-h},{ h, h, h},{-h, h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -Y
        {{ 0,-1,0},{ 1,0, 0},{{-h,-h, h},{ h,-h, h},{ h,-h,-h},{-h,-h,-h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // +Z
        {{ 0,0,1},{ 1,0, 0}, {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -Z
        {{ 0,0,-1},{-1,0, 0}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
         {{0,0},{1,0},{1,1},{0,1}}},
    };

    for (const auto& f : faces) {
        auto base = static_cast<uint32_t>(verts.size());
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = f.p[i];
            v.normal   = f.n;
            v.uv       = f.uv[i];
            v.tangent  = glm::vec4(f.t, 1.f);
            verts.push_back(v);
        }
        indices.insert(indices.end(), {base,base+1,base+2, base,base+2,base+3});
    }
}

void make_plane(std::vector<Vertex>& verts, std::vector<uint32_t>& indices, float h) {
    verts.clear(); indices.clear();
    const glm::vec3 n{0, 1, 0};
    const glm::vec4 t{1, 0, 0, 1};
    verts = {
        {{-h, 0,-h}, n, {0,   0  }, t},
        {{ h, 0,-h}, n, {h*2, 0  }, t},
        {{ h, 0, h}, n, {h*2, h*2}, t},
        {{-h, 0, h}, n, {0,   h*2}, t},
    };
    indices = {0,1,2, 0,2,3};
}

} // namespace pengine

#include "render/mesh.h"

#include <cfloat>
#include <cstdio>
#include <vector>

#include "core/emesh_format.h"
#include "core/log.h"
#include "render/gl_state.h"

namespace pengine {

void Mesh::upload(const std::vector<Vertex>& vertices,
                  const std::vector<uint32_t>& indices) {
    destroy();
    index_count_ = static_cast<int>(indices.size());

    bounds_min_ = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    bounds_max_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const auto& v : vertices) {
        bounds_min_ = glm::min(bounds_min_, v.position);
        bounds_max_ = glm::max(bounds_max_, v.position);
    }

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

    // Per face: outward normal, tangent, 4 corners in CCW order viewed from
    // outside (BL, BR, TR, TL with respect to that face's outside view), and
    // matching UVs. Originally +X / -X / +Y / -Y were wound CW from outside,
    // so back-face culling hid the visible side and the player saw straight
    // through to the inside of the far wall. Fixed.
    const Face faces[] = {
        // +X (outside = +X looking -X with +Y up; -Z is camera right)
        {{ 1,0,0},{ 0,0,-1}, {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -X (outside = -X looking +X with +Y up; +Z is camera right)
        {{-1,0,0},{ 0,0, 1}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // +Y (outside = above looking down; +X right, +Z toward viewer)
        {{ 0,1,0},{ 1,0, 0}, {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -Y (outside = below looking up; +X right, +Z up on screen)
        {{ 0,-1,0},{ 1,0, 0},{{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // +Z (outside = +Z looking -Z with +Y up; +X right)
        {{ 0,0,1},{ 1,0, 0}, {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
         {{0,0},{1,0},{1,1},{0,1}}},
        // -Z (outside = -Z looking +Z with +Y up; -X right)
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

bool load_static_emesh(const std::string& path, Mesh& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { PE_ERROR("Mesh: cannot open %s", path.c_str()); return false; }

    EmeshHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) { std::fclose(f); return false; }
    if (hdr.magic != EMESH_MAGIC || hdr.version != EMESH_VERSION) {
        PE_ERROR("Mesh: bad magic/version in %s", path.c_str());
        std::fclose(f); return false;
    }
    if (hdr.flags & EMESH_FLAG_SKINNED) {
        PE_ERROR("Mesh: %s is skinned, use load_skinned_emesh", path.c_str());
        std::fclose(f); return false;
    }

    std::vector<EmeshVertex> raw_verts(hdr.vertex_count);
    std::vector<uint32_t>    raw_idx(hdr.index_count);
    bool ok =
        std::fread(raw_verts.data(), sizeof(EmeshVertex), raw_verts.size(), f)
            == raw_verts.size() &&
        std::fread(raw_idx.data(),   sizeof(uint32_t),    raw_idx.size(),   f)
            == raw_idx.size();
    std::fclose(f);
    if (!ok) { PE_ERROR("Mesh: truncated %s", path.c_str()); return false; }

    std::vector<Vertex> verts(hdr.vertex_count);
    for (uint32_t i = 0; i < hdr.vertex_count; ++i) {
        const auto& s = raw_verts[i];
        Vertex& d = verts[i];
        d.position = {s.px, s.py, s.pz};
        d.normal   = {s.nx, s.ny, s.nz};
        d.uv       = {s.u,  s.v};
        d.tangent  = {s.tx, s.ty, s.tz, s.tw};
    }
    out.upload(verts, raw_idx);
    PE_INFO("Mesh loaded: %s  (%u verts, %u idx)",
            path.c_str(), hdr.vertex_count, hdr.index_count);
    return true;
}

} // namespace pengine

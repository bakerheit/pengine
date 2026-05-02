#include "render/skinned_mesh.h"

#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "core/emesh_format.h"
#include "core/log.h"
#include "render/gl_state.h"

namespace pengine {

void SkinnedMesh::upload(const std::vector<SkinnedVertex>& verts,
                          const std::vector<uint32_t>&     indices) {
    destroy();
    if (verts.empty() || indices.empty()) return;

    bounds_min_ = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    bounds_max_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const SkinnedVertex& v : verts) {
        bounds_min_ = glm::min(bounds_min_, v.position);
        bounds_max_ = glm::max(bounds_max_, v.position);
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers     (1, &vbo_);
    glGenBuffers     (1, &ebo_);

    gl_state::bind_vao(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(verts.size() * sizeof(SkinnedVertex)),
                  verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                  indices.data(), GL_STATIC_DRAW);

    auto stride = static_cast<GLsizei>(sizeof(SkinnedVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, tangent)));
    // Bone indices as integers (NOT normalised) using IPointer.
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, bone_idx)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SkinnedVertex, bone_weight)));

    gl_state::bind_vao(0);
    index_count_ = static_cast<int>(indices.size());
}

void SkinnedMesh::destroy() {
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    index_count_ = 0;
}

void SkinnedMesh::draw() const {
    if (!vao_) return;
    gl_state::bind_vao(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
}

bool load_skinned_emesh(const std::string& path, SkinnedMesh& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { PE_ERROR("SkinnedMesh: cannot open %s", path.c_str()); return false; }

    EmeshHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) { std::fclose(f); return false; }
    if (hdr.magic != EMESH_MAGIC || hdr.version != EMESH_VERSION) {
        PE_ERROR("SkinnedMesh: bad magic/version in %s (need v%u)",
                 path.c_str(), EMESH_VERSION);
        std::fclose(f); return false;
    }
    if (!(hdr.flags & EMESH_FLAG_SKINNED)) {
        PE_ERROR("SkinnedMesh: %s is not flagged as skinned", path.c_str());
        std::fclose(f); return false;
    }

    std::vector<EmeshSkinnedVertex> raw_verts(hdr.vertex_count);
    std::vector<uint32_t>           raw_idx(hdr.index_count);
    bool ok =
        std::fread(raw_verts.data(), sizeof(EmeshSkinnedVertex),
                    raw_verts.size(), f) == raw_verts.size() &&
        std::fread(raw_idx.data(),   sizeof(uint32_t),
                    raw_idx.size(), f) == raw_idx.size();
    std::fclose(f);
    if (!ok) { PE_ERROR("SkinnedMesh: truncated %s", path.c_str()); return false; }

    std::vector<SkinnedVertex> verts(hdr.vertex_count);
    for (uint32_t i = 0; i < hdr.vertex_count; ++i) {
        const auto& s = raw_verts[i];
        SkinnedVertex& d = verts[i];
        d.position = {s.px, s.py, s.pz};
        d.normal   = {s.nx, s.ny, s.nz};
        d.uv       = {s.u,  s.v};
        d.tangent  = {s.tx, s.ty, s.tz, s.tw};
        for (int k = 0; k < 4; ++k) {
            d.bone_idx[k]    = s.bone_idx[k];
            d.bone_weight[k] = s.bone_weight[k];
        }
    }

    out.upload(verts, raw_idx);
    PE_INFO("SkinnedMesh loaded: %s  (%u verts, %u indices)",
            path.c_str(), hdr.vertex_count, hdr.index_count);
    return true;
}

} // namespace pengine

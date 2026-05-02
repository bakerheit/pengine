#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace pengine {

// Per-vertex skinning data: 4 bone indices + 4 weights (sum to 1).
struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    uint8_t   bone_idx[4];
    float     bone_weight[4];
};

class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh() { destroy(); }

    SkinnedMesh(const SkinnedMesh&)            = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    void upload(const std::vector<SkinnedVertex>& verts,
                const std::vector<uint32_t>&      indices);
    void destroy();
    void draw() const;

    bool      valid()       const { return vao_ != 0; }
    int       index_count() const { return index_count_; }
    glm::vec3 bounds_min()  const { return bounds_min_; }
    glm::vec3 bounds_max()  const { return bounds_max_; }

private:
    GLuint    vao_         = 0;
    GLuint    vbo_         = 0;
    GLuint    ebo_         = 0;
    int       index_count_ = 0;
    glm::vec3 bounds_min_  {0.f};
    glm::vec3 bounds_max_  {0.f};
};

// Load a skinned .emesh file (single submesh assumption — uses the whole
// index buffer). Returns true on success.
bool load_skinned_emesh(const std::string& path, SkinnedMesh& out);

} // namespace pengine

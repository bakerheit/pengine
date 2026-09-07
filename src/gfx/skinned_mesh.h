#pragma once

#include <glad/gl.h>

#include "core/aabb.h"
#include "core/emesh_reader.h"

namespace apricot {

// One bind-pose mesh with four bone influences per vertex. Poses stay in a
// small uniform palette, so every character can share its model geometry while
// sampling a different point in the clip.
class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh();

    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;
    SkinnedMesh(SkinnedMesh&& other) noexcept;
    SkinnedMesh& operator=(SkinnedMesh&& other) noexcept;

    bool upload(const SkinnedEmesh& source);
    void destroy();
    void draw() const;

    bool valid() const { return vao_ != 0; }
    const AABB& bounds() const { return bounds_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei index_count_ = 0;
    AABB bounds_;
};

}  // namespace apricot

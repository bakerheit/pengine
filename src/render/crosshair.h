#pragma once

#include <string>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// Static circular crosshair drawn at screen centre. Geometry is a thin
// annulus baked once at init — no per-frame upload. Reuses the minimap
// shader (with clipping disabled) so no new shader file is needed.
class Crosshair {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        glm::vec2 viewport_size_px;
    };

    void draw(const DrawState& state);

    struct Vertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

private:
    Shader  shader_;
    GLuint  vao_          = 0;
    GLuint  vbo_          = 0;
    GLsizei vertex_count_ = 0;
};

} // namespace pengine

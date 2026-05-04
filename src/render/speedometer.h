#pragma once

#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// Lightweight 2D vehicle HUD. Geometry is generated per frame from the
// current vehicle speed and drawn in screen space after the 3D scene.
class Speedometer {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        float     speed_kmh = 0.f;
        glm::vec2 viewport_size_px;
    };

    void draw(const DrawState& state);

    struct Vertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

private:
    void emit_disk(std::vector<Vertex>& v, glm::vec2 c, float r,
                   int sides, glm::vec3 col);
    void emit_quad(std::vector<Vertex>& v,
                   glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d,
                   glm::vec3 col);

    Shader      shader_;
    GLuint      vao_          = 0;
    GLuint      vbo_          = 0;
    std::size_t vbo_capacity_ = 0;
};

} // namespace pengine

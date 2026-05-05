#pragma once

#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// GTA-style status HUD drawn as lightweight 2D geometry: clock, money,
// health, armour, and wanted stars. Time/money/armour can be placeholder
// values until those systems exist.
class WantedStars {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        int       wanted_level = 0; // 0..5
        float     health = 100.f;   // 0..100
        float     armor  = 0.f;     // 0..100
        int       money  = 0;
        int       hour   = 12;
        int       minute = 0;
        glm::vec2 viewport_size_px;
    };

    void draw(const DrawState& state);

    struct Vertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

private:
    Shader      shader_;
    GLuint      vao_          = 0;
    GLuint      vbo_          = 0;
    std::size_t vbo_capacity_ = 0;
};

} // namespace pengine

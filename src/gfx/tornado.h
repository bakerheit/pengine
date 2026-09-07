#pragma once

#include <cstddef>

#include <glad/gl.h>

#include <glm/glm.hpp>

#include "gfx/camera.h"
#include "gfx/shader.h"

namespace apricot {

// Depth-tested, translucent world-space tornado funnel.
//
// App owns placement and time. Call render after opaque world geometry and
// before the HUD. The funnel has fixed metre dimensions; intensity only fades
// and agitates the smoke.
class TornadoRenderer {
public:
    TornadoRenderer() = default;
    ~TornadoRenderer();

    TornadoRenderer(const TornadoRenderer&) = delete;
    TornadoRenderer& operator=(const TornadoRenderer&) = delete;

    bool init();
    void destroy();

    bool valid() const {
        return shader_.valid() && vao_ != 0 && vbo_ != 0 && vertex_count_ > 0;
    }

    // `center_xz_m` is the funnel axis in world metres and `ground_y_m` is the
    // terrain height directly under it. Inputs are read only for this draw.
    void render(const Camera& camera, const glm::vec2& center_xz_m,
                float ground_y_m, float intensity, float time_seconds);

    std::size_t vertex_count() const {
        return static_cast<std::size_t>(vertex_count_);
    }

private:
    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertex_count_ = 0;
};

}  // namespace apricot

#pragma once

#include <random>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// Cheap CPU-side point-sprite particle system. One pool, one draw call per
// frame. Particles integrate with simple Euler + gravity + air drag, fade
// alpha over their lifetime, and are uploaded to a single VBO each frame.
//
// Tuned for short-lived sparky / debris effects (chassis scrapes, etc.) —
// not meant for thousands of long-lived particles.
class Particles {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    void update(float dt);
    void render(const glm::mat4& view_proj, const glm::vec3& cam_pos,
                float viewport_height_px);

    // Spawn `count` sparks at `pos`. The base velocity drives the average
    // direction (typically the tangential velocity of the scraping corner,
    // reflected back); each particle adds randomized spread on top so they
    // fan out in a cone instead of all flying the same direction.
    void emit_sparks(const glm::vec3& pos,
                     const glm::vec3& base_vel,
                     int count);

    int live_count() const { return static_cast<int>(particles_.size()); }

private:
    struct Particle {
        glm::vec3 pos{0.f};
        glm::vec3 vel{0.f};
        float     age          = 0.f;
        float     max_age      = 0.5f;
        glm::vec3 color_start{1.f}; // rgb at age 0
        glm::vec3 color_end  {1.f}; // rgb at age = max_age
        float     size_px    = 6.f; // base point size at 1 m from camera
    };

    // Per-vertex GPU layout = (vec3 pos, vec4 rgba, float size) = 8 floats.
    struct GpuVertex {
        glm::vec3 pos;
        glm::vec4 color;
        float     size;
    };

    std::vector<Particle>  particles_;
    std::vector<GpuVertex> upload_buf_;
    GLuint                 vao_          = 0;
    GLuint                 vbo_          = 0;
    std::size_t            vbo_capacity_ = 0;
    Shader                 shader_;
    std::mt19937           rng_{0xC0FFEE};
};

} // namespace pengine

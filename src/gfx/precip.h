#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include <glm/glm.hpp>

#include "gfx/camera.h"
#include "gfx/rain_field.h"
#include "gfx/shader.h"
#include "gfx/sky_env.h"

namespace apricot {

// Camera-locked rain.
//
// A fixed set of drops in a box that follows the camera, wrapped rather than
// respawned, drawn as camera-facing streak quads in ONE alpha-blended call. The
// simulation and the wrap live in gfx/rain_field.h, which is pure and tested;
// this class is the buffer and the draw.
//
// At zero intensity it does no work at all: no simulation, no buffer upload, no
// draw. That is the same rule the weather layering obeys — an effect that is
// "nearly free" when off is an effect that shows up in a profile forever.
class Precipitation {
public:
    Precipitation() = default;
    ~Precipitation();

    Precipitation(const Precipitation&) = delete;
    Precipitation& operator=(const Precipitation&) = delete;

    bool init(uint64_t seed);
    void destroy();

    bool valid() const { return shader_.valid() && vao_ != 0; }

    // Advance the field. `dt` is sim time, handed down from App — nothing below
    // App reads a clock. Safe for any dt, including a lag spike.
    void update(const Camera& camera, float intensity, float dt);

    // Draw. Call AFTER opaque geometry (rain is translucent and must blend over
    // the world) and before the HUD.
    void render(const Camera& camera, const SkyEnv& env);

    RainTuning& tuning() { return tuning_; }
    const RainTuning& tuning() const { return tuning_; }

    // Drops actually simulated last update, and quads actually drawn last
    // render. Surfaced in the debug overlay: a rain field that silently drew
    // nothing looks exactly like a dry day.
    int live_drops() const { return live_drops_; }
    int drawn_quads() const { return drawn_quads_; }

private:
    struct StreakVertex {
        glm::vec3 pos;
        glm::vec2 uv;
        float alpha;
    };

    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::size_t vbo_capacity_bytes_ = 0;

    RainTuning tuning_;
    uint64_t seed_ = 0;

    std::vector<glm::vec3> drops_;
    std::vector<float> drop_alpha_;
    std::vector<StreakVertex> verts_;

    float intensity_ = 0.0f;
    int live_drops_ = 0;
    int drawn_quads_ = 0;
};

}  // namespace apricot

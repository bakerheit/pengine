#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gfx/lighting.h"
#include "gfx/shader.h"
#include "gfx/sky_env.h"
#include "gfx/tire_track_field.h"

namespace apricot {

struct Camera;
// Fixed-capacity tire-track decals. The physics stays authoritative; this class
// only samples wheel contacts, keeps a bounded visual trail, and draws one batch.
class TireTracks {
public:
    TireTracks() = default;
    ~TireTracks();

    TireTracks(const TireTracks&) = delete;
    TireTracks& operator=(const TireTracks&) = delete;

    bool init();
    void destroy();

    void step(const VehicleState& car, float handbrake, float snow_cover,
              uint64_t step);
    void render(const Camera& camera, const SkyEnv& env,
                const HeadlightRig& headlights,
                const CanopyLightRig& canopy_lights);

    std::size_t live_count() const;
    std::size_t live_count(TireTrackSurface surface) const;
    int drawn_quads() const { return drawn_quads_; }
    bool recent_bounds(glm::vec3& minimum, glm::vec3& maximum,
                       std::size_t max_stamps = 420u) const;
    bool valid() const { return shader_.valid() && vao_ != 0 && vbo_ != 0; }

private:
    struct Stamp {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        TireTrackSurface surface = TireTrackSurface::Rubber;
        float intensity = 0.0f;
        uint64_t born_step = 0;
        uint64_t lifetime_steps = 0;
        bool live = false;
    };

    struct Trail {
        glm::vec3 position{0.0f};
        TireTrackSurface surface = TireTrackSurface::Rubber;
        bool active = false;
    };

    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec3 color;
        float alpha;
        float surface;
    };

    void add(const TireTrackEmission& emission, glm::vec3 position,
             glm::vec3 direction, uint64_t step);

    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::size_t vbo_capacity_bytes_ = 0;
    std::vector<Stamp> stamps_;
    std::vector<Vertex> vertices_;
    std::array<Trail, kWheelCount> trails_{};
    std::size_t head_ = 0;
    uint64_t latest_step_ = 0;
    int drawn_quads_ = 0;
};

}  // namespace apricot

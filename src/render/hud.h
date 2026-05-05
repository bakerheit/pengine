#pragma once

#include <string>

#include <glm/glm.hpp>

#include "render/crosshair.h"
#include "render/minimap.h"
#include "render/speedometer.h"
#include "render/wanted_stars.h"

namespace pengine {

// Bundle of 2D HUD widgets drawn on top of the 3D scene. Owns its own
// minimap / speedometer / crosshair / wanted-stars instances and dispatches
// each draw based on a single State struct supplied by the caller.
class Hud {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct State {
        bool      show_speedometer = false; // in vehicle
        bool      show_crosshair   = false; // armed + on-foot
        float     speed_kmh        = 0.f;
        int       wanted_level     = 0;
        float     health           = 100.f;
        glm::vec3 player_pos_world {0.f};
        float     player_yaw_deg   = 0.f;   // 0 = facing -Z (north)
        glm::vec2 viewport_size_px {0.f};
    };

    void render(const State& s);

private:
    Minimap     minimap_;
    Speedometer speedometer_;
    Crosshair   crosshair_;
    WantedStars wanted_stars_;
};

} // namespace pengine

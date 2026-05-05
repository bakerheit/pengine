#pragma once

#include <glm/glm.hpp>

namespace pengine {

class Camera;
class DebugDraw;
class TrafficSystem;
class WorldCollision;

// Per-frame debug-draw lines/markers and one-shot world snapshots. Stateless.
namespace debug_overlay {

struct RenderState {
    bool      in_debug_fly        = false;
    bool      show_enter_prompt   = false;
    glm::vec3 enter_prompt_base   {0.f};
    float     enter_prompt_radius = 0.f;
};

// Calls debug_draw.clear() at start, draws diagnostic markers (forward ray
// in DebugFly, wheel contact crosses on the player car, enter-vehicle ring,
// traffic debug overlay), then debug_draw.flush().
void render(const RenderState& s, DebugDraw& debug_draw,
            const Camera& camera, const glm::mat4& view_proj,
            const WorldCollision& world_col,
            const TrafficSystem& traffic);

// Heightmap / road-grid snapshot around a position. Pure stdout via PE_INFO;
// no rendering. Counter is bumped per call so log entries are sequence-
// numbered for correlation across debug presses.
void log_world_area(const glm::vec3& pos, const glm::vec3& fwd,
                    const char* mode_label);

} // namespace debug_overlay
} // namespace pengine

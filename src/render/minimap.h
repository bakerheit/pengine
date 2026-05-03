#pragma once

#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// GTA-style circular minimap. Draws a top-down view of the world's road grid
// rotated so the player's facing direction is always "up" on the map. A
// compass triangle on the rim points to world-North so the player can keep
// orient themselves while turning.
//
// Geometry is regenerated each frame from the road grid constants
// (ROAD_PITCH / STREET_WIDTH) — no need to query the streamer or scene
// graph; the grid is regular and fully defined by world_size + constants.
class Minimap {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        glm::vec3 player_pos_world;   // centred here
        float     player_yaw_deg;     // 0 = facing -Z (north)
        glm::vec2 viewport_size_px;
    };

    // Draw the minimap to the bottom-left of the viewport. Should be called
    // after the main scene render and after debug overlay; assumes the
    // colour buffer is current and depth-test-on is OK to disable.
    void draw(const DrawState& state);

    struct Vertex {
        glm::vec2 pos;     // minimap-local, radius 1 = rim
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

}  // namespace pengine

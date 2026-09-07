#pragma once

#include <array>

#include <glm/glm.hpp>

namespace apricot {

inline constexpr int kHeadlightCount = 2;
inline constexpr int kCanopyLightCount = 6;

struct TrafficSpotLight {
    glm::vec4 position_range{0.0f};
    // xyz is a unit world-space direction; w is nonnegative lamp power.
    glm::vec4 direction_power{0.0f};
    // Linear RGB and outer cone cosine. Defaults preserve ordinary headlights.
    glm::vec4 color_outer{1.0f, 0.86f, 0.66f, 0.94f};
};

// GPU texture contents are owned by TiledLighting. This is the shared shader
// view, also passed to skinned people; zero columns disables the whole path.
struct TrafficLightView {
    int columns = 0;
    int rows = 0;
    glm::vec4 depth_plane{0.0f};
};

// Two forward spot lights owned by the player vehicle. Plain data so the host
// can build it from an interpolated pose and the renderer only has to upload
// it. Intensity zero is a strict off switch during daylight.
struct HeadlightRig {
    TrafficLightView traffic;
    std::array<glm::vec3, kHeadlightCount> position{};
    std::array<glm::vec3, kHeadlightCount> direction{
        glm::vec3{0.0f, -0.06f, -1.0f},
        glm::vec3{0.0f, -0.06f, -1.0f}};
    glm::vec3 color{1.0f, 0.86f, 0.66f};
    // Per-lamp so a front-left hit kills only the front-left beam.
    glm::vec2 intensity{0.0f};
    float range = 42.0f;
    float inner_cos = 0.965f;
    float outer_cos = 0.86f;
};

// Six authored downlights under Halloway Gas's canopy. These are spotlights,
// not shadowless ambient fill: the bright pools and falloff are what make the
// canopy feel like it is actually over the forecourt at dusk and in rain.
struct CanopyLightRig {
    std::array<glm::vec3, kCanopyLightCount> position{};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f, 0.80f, 0.52f};
    float intensity = 2.6f;
    float range = 11.5f;
    float inner_cos = 0.86f;
    float outer_cos = 0.48f;
};

}  // namespace apricot

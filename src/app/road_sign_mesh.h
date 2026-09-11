#pragma once

#include <array>

#include "app/road_sign_font.h"
#include "app/traffic_signal_mesh.h"

namespace apricot {
namespace road_sign_detail {
inline void face(MeshData& mesh, int sides, float radius, float height, float z,
                 float phase) {
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float angle = phase + 6.28318530718f * static_cast<float>(i) /
                                   static_cast<float>(sides);
        const glm::vec3 point{radius * std::cos(angle),
                              height + radius * std::sin(angle), z};
        mesh.vertices.push_back({point, {0, 0, 1}, {0, 0}, glm::vec4{0}});
        mesh.bounds.expand(point);
    }
    for (uint32_t i = 1; i + 1 < static_cast<uint32_t>(sides); ++i)
        mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
}

template <std::size_t N>
inline void lettering(MeshData& mesh, const std::array<glm::vec2, N>& letters,
                      float aspect, float width,
                      float height, float centre_y, float z) {
    // Fit the font's natural proportions instead of stretching its glyphs.
    const float scale = std::min(height, width / aspect);
    const auto base = static_cast<uint32_t>(mesh.vertices.size());
    for (const auto& p : letters) {
        const glm::vec3 point{p.x * scale, centre_y + p.y * scale, z};
        mesh.vertices.push_back({point, {0, 0, 1}, {0, 0}, glm::vec4{0}});
        mesh.bounds.expand(point);
        mesh.indices.push_back(base + static_cast<uint32_t>(&p - letters.data()));
    }
}
} // namespace road_sign_detail

inline constexpr std::size_t kRoadSignPolePart = 0u;
inline constexpr std::size_t kRoadSignBackingPart = 1u;
inline constexpr std::size_t kRoadSignWhitePart = 2u;
inline constexpr std::size_t kRoadSignRedPart = 3u;
inline constexpr std::size_t kRoadSignPartCount = 4u;

// Local +Z faces the driver. The pole and plate backing are separate metal
// meshes so a struck stop sign can fracture into two believable rigid chunks.
// The colored plate layers are welded to the backing by TrafficVisual; the
// lettering therefore stays on the plate instead of becoming loose debris.
inline std::array<MeshData, kRoadSignPartCount> make_road_sign_mesh(
    bool yield, bool all_way) {
    std::array<MeshData, kRoadSignPartCount> out;
    auto& pole = out[kRoadSignPolePart];
    auto& backing = out[kRoadSignBackingPart];
    auto& white = out[kRoadSignWhitePart];
    auto& red = out[kRoadSignRedPart];
    signal_mesh_detail::append(
        pole, make_cylinder(0.04f, 1.30f, 8), {0, 1.30f, 0});
    const int sides = yield ? 3 : 8;
    const float radius = yield ? 0.80f : 0.65f;
    const float height = yield ? 2.65f : 2.50f;
    const float phase = glm::radians(yield ? -90.0f : 22.5f);
    road_sign_detail::face(backing, sides, radius, height, -0.05f, phase);
    // A back face with reversed winding keeps the bare metal visible to cars
    // leaving the junction, without mirrored STOP lettering on the reverse.
    for (std::size_t i = 0; i < backing.indices.size(); i += 3) {
        if (backing.vertices[backing.indices[i]].normal.z != 1.0f) continue;
        // The pole already has proper closed geometry; only reverse the plate.
        if (backing.vertices[backing.indices[i]].position.z != -0.05f) continue;
        std::swap(backing.indices[i + 1], backing.indices[i + 2]);
    }
    for (auto& vertex : backing.vertices)
        if (vertex.position.z == -0.05f && vertex.normal.z == 1.0f)
            vertex.normal = {0, 0, -1};
    road_sign_detail::face(yield ? red : white, sides, radius, height, 0.055f, phase);
    road_sign_detail::face(yield ? white : red, sides,
                          yield ? 0.62f : 0.585f, height, 0.06f, phase);
    if (yield) {
        road_sign_detail::lettering(red, road_sign_detail::kYieldLetters,
            road_sign_detail::kYieldAspect, 0.61f, 0.17f, height + 0.10f, 0.067f);
    } else {
        road_sign_detail::lettering(white, road_sign_detail::kStopLetters,
            road_sign_detail::kStopAspect, 0.92f, 0.30f, height, 0.067f);
    }
    if (all_way) {
        signal_mesh_detail::box(white, {0, 1.65f, 0.025f}, {0.84f, 0.24f, 0.06f});
        road_sign_detail::lettering(
            backing, road_sign_detail::kAllWayLetters,
            road_sign_detail::kAllWayAspect, 0.72f, 0.13f, 1.65f, 0.06f);
    }
    return out;
}

// A row of give-way teeth, pointing toward approaching traffic (local +Z).
// The renderer scales X to the inbound carriageway, and aligns it to the road.
inline MeshData make_yield_marking() {
    MeshData out;
    for (int i = 0; i < 5; ++i) {
        const float x = -0.5f + static_cast<float>(i) * 0.2f;
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (glm::vec3 p : {glm::vec3{x, 0, -0.3f},
                            glm::vec3{x + 0.085f, 0, 0.3f},
                            glm::vec3{x + 0.17f, 0, -0.3f}}) {
            out.vertices.push_back({p, {0, 1, 0}, {0, 0}, glm::vec4{0}});
            out.bounds.expand(p);
        }
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
    }
    return out;
}
} // namespace apricot

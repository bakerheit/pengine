#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

namespace apricot {

// Authored world-space dimensions. These never depend on camera distance,
// viewport size, or event intensity: perspective is the camera's job.
inline constexpr float kTornadoHeightMeters = 56.0f;
inline constexpr float kTornadoTopRadiusMeters = 15.0f;
inline constexpr float kTornadoTipRadiusMeters = 1.4f;
inline constexpr int kTornadoRadialSegments = 64;
inline constexpr int kTornadoVerticalSegments = 32;
inline constexpr std::array<float, 3> kTornadoLayerRadiusScales{
    1.0f, 0.78f, 0.58f,
};

// Static funnel surface consumed by TornadoRenderer. The shader bends and
// churns these metre-space shells but does not scale them toward a HUD size.
struct TornadoVertex {
    glm::vec3 local_position_m{0.0f};
    glm::vec2 funnel_uv{0.0f};
    float layer = 0.0f;
};

static_assert(std::is_standard_layout<TornadoVertex>::value,
              "tornado vertex attributes require a standard-layout type");

struct TornadoMeshData {
    std::vector<TornadoVertex> vertices;
};

inline constexpr std::size_t tornado_mesh_vertex_count() {
    return kTornadoLayerRadiusScales.size() *
           static_cast<std::size_t>(kTornadoRadialSegments) *
           static_cast<std::size_t>(kTornadoVerticalSegments) * 6u;
}

inline float tornado_radius_at_height(float height_fraction,
                                      float layer_radius_scale = 1.0f) {
    const float h = std::clamp(height_fraction, 0.0f, 1.0f);
    // A sub-linear taper gets broad early while preserving a tight ground
    // contact. It reads as a storm funnel instead of a straight traffic cone.
    const float taper = std::pow(h, 0.72f);
    return (kTornadoTipRadiusMeters +
            (kTornadoTopRadiusMeters - kTornadoTipRadiusMeters) * taper) *
           std::max(layer_radius_scale, 0.0f);
}

inline TornadoVertex make_tornado_vertex(float u, float v,
                                         float layer_radius_scale,
                                         float layer_fraction) {
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float angle = u * kTwoPi;
    const float radius = tornado_radius_at_height(v, layer_radius_scale);
    return {
        {std::cos(angle) * radius, v * kTornadoHeightMeters,
         std::sin(angle) * radius},
        {u, v},
        layer_fraction,
    };
}

// Three open, nested shells. Vertices are deliberately duplicated at each
// triangle and seam so UVs stay continuous while the smoke bands revolve.
// This generator is GL-free and deterministic, which keeps its dimensions and
// topology testable without a window or graphics context.
inline TornadoMeshData build_tornado_mesh() {
    TornadoMeshData mesh;
    mesh.vertices.reserve(tornado_mesh_vertex_count());

    const float radial_denominator =
        static_cast<float>(kTornadoRadialSegments);
    const float vertical_denominator =
        static_cast<float>(kTornadoVerticalSegments);
    const float layer_denominator =
        static_cast<float>(kTornadoLayerRadiusScales.size() - 1u);

    for (std::size_t layer_index = 0;
         layer_index < kTornadoLayerRadiusScales.size(); ++layer_index) {
        const float layer_fraction =
            static_cast<float>(layer_index) / layer_denominator;
        const float layer_scale = kTornadoLayerRadiusScales[layer_index];

        for (int vertical = 0; vertical < kTornadoVerticalSegments; ++vertical) {
            const float v0 = static_cast<float>(vertical) / vertical_denominator;
            const float v1 = static_cast<float>(vertical + 1) /
                             vertical_denominator;

            for (int radial = 0; radial < kTornadoRadialSegments; ++radial) {
                const float u0 = static_cast<float>(radial) / radial_denominator;
                const float u1 = static_cast<float>(radial + 1) /
                                 radial_denominator;

                const TornadoVertex low0 = make_tornado_vertex(
                    u0, v0, layer_scale, layer_fraction);
                const TornadoVertex low1 = make_tornado_vertex(
                    u1, v0, layer_scale, layer_fraction);
                const TornadoVertex high0 = make_tornado_vertex(
                    u0, v1, layer_scale, layer_fraction);
                const TornadoVertex high1 = make_tornado_vertex(
                    u1, v1, layer_scale, layer_fraction);

                mesh.vertices.push_back(low0);
                mesh.vertices.push_back(high1);
                mesh.vertices.push_back(low1);
                mesh.vertices.push_back(low0);
                mesh.vertices.push_back(high0);
                mesh.vertices.push_back(high1);
            }
        }
    }
    return mesh;
}

}  // namespace apricot

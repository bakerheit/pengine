#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <glm/glm.hpp>

#include "gfx/tornado_geometry.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void dimensions_are_fixed_world_metres() {
    REQUIRE_NEAR(kTornadoHeightMeters, 56.0, 1e-6);
    REQUIRE_NEAR(kTornadoTopRadiusMeters, 15.0, 1e-6);
    REQUIRE_NEAR(kTornadoTipRadiusMeters, 1.4, 1e-6);
    REQUIRE_NEAR(tornado_radius_at_height(0.0f),
                 kTornadoTipRadiusMeters, 1e-6);
    REQUIRE_NEAR(tornado_radius_at_height(1.0f),
                 kTornadoTopRadiusMeters, 1e-6);
    apricot_test::pass("tornado dimensions are fixed authored metres");
}

void mesh_has_complete_nested_shells() {
    const TornadoMeshData mesh = build_tornado_mesh();
    REQUIRE(mesh.vertices.size() == tornado_mesh_vertex_count());
    REQUIRE(mesh.vertices.size() % 3u == 0u);

    float min_y = kTornadoHeightMeters;
    float max_y = 0.0f;
    std::array<float, 3> max_radius{0.0f, 0.0f, 0.0f};
    for (const TornadoVertex& vertex : mesh.vertices) {
        REQUIRE(std::isfinite(vertex.local_position_m.x));
        REQUIRE(std::isfinite(vertex.local_position_m.y));
        REQUIRE(std::isfinite(vertex.local_position_m.z));
        REQUIRE(vertex.funnel_uv.x >= 0.0f && vertex.funnel_uv.x <= 1.0f);
        REQUIRE(vertex.funnel_uv.y >= 0.0f && vertex.funnel_uv.y <= 1.0f);
        REQUIRE(vertex.layer >= 0.0f && vertex.layer <= 1.0f);

        min_y = std::min(min_y, vertex.local_position_m.y);
        max_y = std::max(max_y, vertex.local_position_m.y);
        const float radius = glm::length(glm::vec2{
            vertex.local_position_m.x,
            vertex.local_position_m.z,
        });
        const std::size_t layer_index = static_cast<std::size_t>(
            std::lround(vertex.layer * 2.0f));
        REQUIRE(layer_index < max_radius.size());
        max_radius[layer_index] = std::max(max_radius[layer_index], radius);
    }

    REQUIRE_NEAR(min_y, 0.0, 1e-6);
    REQUIRE_NEAR(max_y, kTornadoHeightMeters, 1e-5);
    for (std::size_t layer_index = 0;
         layer_index < max_radius.size(); ++layer_index) {
        const float expected = kTornadoTopRadiusMeters *
                               kTornadoLayerRadiusScales[layer_index];
        REQUIRE_NEAR(max_radius[layer_index], expected, 1e-4);
    }
    apricot_test::pass("tornado mesh has three complete nested shells");
}

void every_emitted_triangle_has_area() {
    const TornadoMeshData mesh = build_tornado_mesh();
    for (std::size_t i = 0; i < mesh.vertices.size(); i += 3u) {
        const glm::vec3 a = mesh.vertices[i].local_position_m;
        const glm::vec3 b = mesh.vertices[i + 1u].local_position_m;
        const glm::vec3 c = mesh.vertices[i + 2u].local_position_m;
        const float doubled_area = glm::length(glm::cross(b - a, c - a));
        REQUIRE(doubled_area > 1e-5f);
    }
    apricot_test::pass("tornado triangle stream is non-degenerate");
}

}  // namespace

int main() {
    dimensions_are_fixed_world_metres();
    mesh_has_complete_nested_shells();
    every_emitted_triangle_has_area();
    return apricot_test::done("tornado_geometry_tests");
}

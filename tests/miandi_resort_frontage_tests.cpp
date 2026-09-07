#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/miandi_ocean_drive.h"
#include "city/miandi_presentation.h"
#include "city/miandi_resort_frontage.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kPi = 3.14159265358979323846f;

float extent_x(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * kPi / 180.0f;
    return (std::fabs(std::cos(yaw)) * p.width_m +
            std::fabs(std::sin(yaw)) * p.depth_m) * .5f;
}

float extent_z(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * kPi / 180.0f;
    return (std::fabs(std::sin(yaw)) * p.width_m +
            std::fabs(std::cos(yaw)) * p.depth_m) * .5f;
}

bool blocks_disc(const city::BuildingPiece& p, city::Vec2 point,
                 float radius = .55f) {
    return p.solid && p.bottom_m < 2.1f && p.bottom_m + p.height_m > .2f &&
           point.x + radius > p.centre.x - extent_x(p) &&
           point.x - radius < p.centre.x + extent_x(p) &&
           point.z + radius > p.centre.z - extent_z(p) &&
           point.z - radius < p.centre.z + extent_z(p);
}

bool route_clear(const std::vector<city::BuildingPiece>& parts, city::Vec2 from,
                 city::Vec2 to) {
    constexpr int kSamples = 120;
    for (int i = 0; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const city::Vec2 point{from.x + (to.x - from.x) * t,
                                from.z + (to.z - from.z) * t};
        if (std::any_of(parts.begin(), parts.end(),
                        [&](const auto& p) { return blocks_disc(p, point); }))
            return false;
    }
    return true;
}

bool overlaps_rect(const city::BuildingPiece& p, float x0, float x1, float z0,
                   float z1) {
    return p.centre.x - extent_x(p) < x1 && p.centre.x + extent_x(p) > x0 &&
           p.centre.z - extent_z(p) < z1 && p.centre.z + extent_z(p) > z0;
}

float distance_to_rect(city::Vec2 p, float x0, float x1, float z0, float z1) {
    const float dx = std::max({x0 - p.x, 0.0f, p.x - x1});
    const float dz = std::max({z0 - p.z, 0.0f, p.z - z1});
    return std::sqrt(dx * dx + dz * dz);
}

glm::vec3 world_point(city::Vec2 local, float height = 0.0f) {
    const auto& site = city::kMiandiOceanDriveSite;
    return {site.origin.x + local.x, site.ground_m + height,
            site.origin.z + local.z};
}

TerrainCollider frontage_collider(const std::vector<city::BuildingPiece>& parts) {
    TerrainCollider collider{city::kMapSeed};
    for (const auto& p : parts) {
        const glm::vec3 centre = world_point(
            p.centre, p.bottom_m + p.height_m * .5f);
        if (p.solid) {
            REQUIRE(p.pitch_deg == 0.0f && p.roll_deg == 0.0f);
            collider.add_static_oriented_box(
                centre, {p.width_m * .5f, p.height_m * .5f, p.depth_m * .5f},
                p.yaw_deg * kPi / 180.0f);
        }
        if (city::miandi_ground_piece(p))
            collider.add_static_ground_rect(
                {centre.x, centre.z}, city::kMiandiOceanDriveSite.ground_m +
                    p.bottom_m + p.height_m,
                {p.width_m * .5f, p.depth_m * .5f}, p.yaw_deg * kPi / 180.0f,
                Surface::Rock);
    }
    // Conservative trunk envelopes also exercise the moved palm anchors.
    // World uses narrower tapered/bent segments, not crown-sized obstacles.
    for (const auto& palm : city::kMiandiResortPalms)
        collider.add_static_oriented_box(
            world_point(palm.centre, .2f + palm.height_m * .41f),
            {.6f, palm.height_m * .41f, .6f}, 0.0f);
    return collider;
}

PlayerCharacterState walk_to(PlayerCharacterState state,
                             const TerrainCollider& collider, city::Vec2 goal,
                             int steps = 10000) {
    constexpr float kDt = 1.0f / 120.0f;
    const CharacterTuning tuning;
    const glm::vec3 target = world_point(goal);
    for (int i = 0; i < steps; ++i) {
        const glm::vec2 delta{target.x - state.position.x,
                              target.z - state.position.z};
        const float distance = glm::length(delta);
        if (distance < .03f) break;
        InputFrame input;
        input.look_dx = std::atan2(delta.x, -delta.y) - state.view_yaw;
        input.throttle = std::min(1.0f, distance / (tuning.walk_speed_mps * kDt));
        state = step_character(state, tuning, input, collider, kDt);
        REQUIRE(character_position_clear(collider, state.position, tuning));
    }
    return state;
}

city::Vec2 local_point(glm::vec3 world) {
    return {world.x - city::kMiandiOceanDriveSite.origin.x,
            world.z - city::kMiandiOceanDriveSite.origin.z};
}

void frontage_bake_is_deterministic_bounded_and_layered() {
    const auto a = city::bake_miandi_resort_frontage();
    const auto b = city::bake_miandi_resort_frontage();
    REQUIRE(a.size() >= 30u && a.size() < 300u);
    REQUIRE(a.size() == b.size());
    int paving = 0;
    int stripes = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-5f);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-5f);
        REQUIRE(a[i].solid == b[i].solid);
        REQUIRE(a[i].centre.x - extent_x(a[i]) >= -80.01f);
        REQUIRE(a[i].centre.x + extent_x(a[i]) <= 86.01f);
        REQUIRE(a[i].centre.z - extent_z(a[i]) >= -75.01f);
        REQUIRE(a[i].centre.z + extent_z(a[i]) <= 75.01f);
        if (std::strstr(a[i].name, "terrace")) {
            ++paving;
            REQUIRE_NEAR(a[i].bottom_m, .12f, 1e-5f);
            REQUIRE_NEAR(a[i].bottom_m + a[i].height_m, .20f, 1e-5f);
            REQUIRE(!a[i].solid);
            REQUIRE_NEAR(a[i].centre.x - extent_x(a[i]), 78.15f, 1e-4f);
            REQUIRE_NEAR(a[i].centre.x + extent_x(a[i]), 86.0f, 1e-4f);
        }
        if (std::strstr(a[i].name, "shade stripe")) {
            ++stripes;
            REQUIRE(!a[i].solid);
        }
    }
    REQUIRE(paving == 5);
    REQUIRE(stripes == 4);
    apricot_test::pass("OD-1 forecourt bake is deterministic, bounded, and has separated paving layers");
}

void moved_shell_and_compact_forecourt_keep_real_routes_clear() {
    auto parts = city::bake_miandi_ocean_drive();
    const auto frontage = city::bake_miandi_resort_frontage();
    parts.insert(parts.end(), frontage.begin(), frontage.end());

    for (const auto& p : frontage) {
        // Allow 1 cm of float roundoff where pavement abuts the wall skin.
        REQUIRE_MSG(!overlaps_rect(p, 28.0f, 78.14f, -54.0f, -4.0f),
                    "frontage intersects moved Bellmar", p.name);
        REQUIRE_MSG(!overlaps_rect(p, 36.0f, 78.14f, 4.0f, 52.0f),
                    "frontage intersects moved Maravelle", p.name);
        if (!p.solid) continue;
        REQUIRE_MSG(!overlaps_rect(p, 78.0f, 86.0f, -31.4f, -26.6f),
                    "forecourt blocks Bellmar lobby corridor", p.name);
        REQUIRE_MSG(!overlaps_rect(p, 78.0f, 86.0f, 25.6f, 30.4f),
                    "forecourt blocks Maravelle lobby corridor", p.name);
        REQUIRE_MSG(!overlaps_rect(p, -90.0f, -54.0f, -75.0f, 75.0f),
                    "forecourt blocks west service lane", p.name);
    }
    REQUIRE(route_clear(parts, {78.7f, -29.0f}, {85.4f, -29.0f}));
    REQUIRE(route_clear(parts, {78.7f, 28.0f}, {85.4f, 28.0f}));
    REQUIRE(route_clear(parts, {-89.4f, 0.0f}, {-54.6f, 0.0f}));
    REQUIRE(route_clear(parts, {85.0f, -72.0f}, {85.0f, 64.0f}));

    // The former lawn is now continuous supported paving at the road edge,
    // including the corner cafe and the seam between the two hotels.
    for (float z = -72.0f; z <= 64.0f; z += .25f) {
        REQUIRE(std::any_of(parts.begin(), parts.end(), [&](const auto& p) {
            return city::miandi_ground_piece(p) &&
                   overlaps_rect(p, 84.9f, 85.1f, z - .01f, z + .01f);
        }));
    }

    // A route through the moved Coral shell away from its true lobby opening is
    // expected to fail.  This keeps the sampling test from being name-only.
    REQUIRE(!route_clear(parts, {60.0f, -10.0f}, {85.4f, -10.0f}));

    // Run the real on-foot controller through the same registration style
    // World uses: visible solid boxes plus independently supported ground.
    const auto collider = frontage_collider(parts);
    const auto travel = [&](city::Vec2 from, city::Vec2 to) {
        const auto start = world_point(from);
        auto state = spawn_character(collider, start.x, start.z);
        state = walk_to(state, collider, to);
        const auto local = local_point(state.position);
        REQUIRE_NEAR(local.x, to.x, .10f);
        REQUIRE_NEAR(local.z, to.z, .10f);
    };
    travel({78.7f, -29.0f}, {85.4f, -29.0f});
    travel({78.7f, 28.0f}, {85.4f, 28.0f});
    travel({85.4f, -29.0f}, {69.0f, -29.0f});
    travel({69.0f, -29.0f}, {85.4f, -29.0f});
    travel({85.4f, 28.0f}, {69.0f, 28.0f});
    travel({69.0f, 28.0f}, {85.4f, 28.0f});
    travel({-89.4f, 0.0f}, {-54.6f, 0.0f});
    travel({85.0f, -72.0f}, {85.0f, 64.0f});
    travel({85.0f, 64.0f}, {85.0f, -72.0f});
    travel({66.0f, -72.0f}, {66.0f, -58.0f});
    travel({66.0f, -58.0f}, {85.0f, -58.0f});

    // This gate is deliberately added after the clear route check.  The same
    // real controller must stop short rather than passing a test-only AABB.
    auto blocked = frontage_collider(parts);
    blocked.add_static_oriented_box(world_point({82.0f, -29.0f}, 1.6f),
                                    {.18f, 1.6f, 80.0f}, 0.0f);
    const auto start = world_point({78.7f, -29.0f});
    auto state = spawn_character(blocked, start.x, start.z);
    state = walk_to(state, blocked, {85.4f, -29.0f});
    REQUIRE(local_point(state.position).x < 81.55f);
    apricot_test::pass("street-front hotels, cafe and palms preserve real character routes and reject a blocked control");
}

void palms_are_real_mesh_anchors_with_clearance() {
    REQUIRE(city::kMiandiResortPalms.size() >= 6u);
    REQUIRE(city::kMiandiResortPalms.size() <= 8u);
    for (const auto& palm : city::kMiandiResortPalms) {
        REQUIRE(palm.height_m >= 7.0f && palm.height_m <= 11.0f);
        REQUIRE(palm.centre.x >= -80.0f && palm.centre.x <= 86.0f);
        REQUIRE(palm.centre.z >= -75.0f && palm.centre.z <= 75.0f);
        REQUIRE(distance_to_rect(palm.centre, 28.0f, 78.0f, -54.0f, -4.0f) >= 3.0f);
        REQUIRE(distance_to_rect(palm.centre, 36.0f, 78.0f, 4.0f, 52.0f) >= 3.0f);
        REQUIRE(distance_to_rect(palm.centre, 78.0f, 86.0f, -31.4f, -26.6f) >= 2.0f);
        REQUIRE(distance_to_rect(palm.centre, 78.0f, 86.0f, 25.6f, 30.4f) >= 2.0f);
        REQUIRE(distance_to_rect(palm.centre, -90.0f, -54.0f, -75.0f, 75.0f) >= 2.0f);
    }
    const auto parts = city::bake_miandi_resort_frontage();
    for (const auto& p : parts)
        REQUIRE(std::strstr(p.name, "palm") == nullptr);
    apricot_test::pass("six palm mesh anchors clear hotel, lobby, and service circulation");
}

}  // namespace

int main() {
    frontage_bake_is_deterministic_bounded_and_layered();
    moved_shell_and_compact_forecourt_keep_real_routes_clear();
    palms_are_real_mesh_anchors_with_clearance();
    return apricot_test::done("miandi_resort_frontage_tests");
}

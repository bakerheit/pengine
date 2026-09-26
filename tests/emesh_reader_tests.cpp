// The first authored vehicle assets, read through the real static .emesh
// parser. These counts also catch the easy doubled-wheel mistake: the Car 5
// body must be the 716-vertex wheel-less cook, not the source OBJ with its four
// wheel components still baked in.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "app/traffic_visual_layout.h"
#include "app/player_car_catalog.h"
#include "app/vehicle_lamp_mesh.h"
#include "app/vehicle_headlight_profile.h"
#include "city/airport_aircraft.h"
#include "city/marina.h"
#include "city/map.h"
#include "terrain/chunk.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"
#include "traffic/crowd.h"

using namespace apricot;

namespace {

void marlin_speedboat_has_a_real_hull_and_water_mooring() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path(city::kMarlinBody),body));
    REQUIRE(body.indices.size()/3==912u);
    REQUIRE_NEAR(body.bounds.min.y,-.48f,1e-4f);
    REQUIRE(body.bounds.max.y>1.42f && body.bounds.max.y<1.47f);
    REQUIRE_NEAR(body.bounds.max.x,-body.bounds.min.x,1e-4f);
    REQUIRE(body.bounds.size().z>7.14f && body.bounds.size().z<7.18f);
    std::size_t upward_bow=0;
    for (const auto& v:body.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
        // Dedicated hull chart cannot have inward-facing outer skin.
        if (v.v>1-66.f/256 && v.v<1-2.f/256 && std::fabs(v.nx)>.5f && std::fabs(v.px)>.3f)
            REQUIRE(v.px*v.nx>0);
        if (v.pz>1 && v.py>.7f && std::fabs(v.px)<.6f && std::fabs(v.ny)>.8f) {
            REQUIRE(v.ny>0);++upward_bow;
        }
    }
    REQUIRE(upward_bow>0);
    // Test the full water footprint, not just the centre: the stern must not
    // run aground on the harbour's sloping bank at the authored mooring.
    for(float x:{-3.5f,0.f,3.65f}) for(float z:{-1.21f,0.f,1.21f})
        REQUIRE(mesh_height_at(city::kMapSeed,city::kMarlinMooring.x+x,city::kMarlinMooring.z+z)<-.60f);
    REQUIRE(mesh_height_at(city::kMapSeed,city::kMarlinDockSite.origin.x+8,
        city::kMarlinDockSite.origin.z)>.64f);
    apricot_test::pass("Marlin Sprint has outward hull/deck faces and a clear water mooring connected to shore");
}

void aster_aircraft_is_a_separate_airframe_and_landing_gear() {
    StaticEmesh body, gear;
    REQUIRE(read_static_emesh(asset_path(city::kAirportAircraftBody), body));
    REQUIRE(read_static_emesh(asset_path(city::kAirportAircraftGear), gear));
    REQUIRE(body.indices.size()/3 >= 600u && body.indices.size()/3 <= 1500u);
    REQUIRE_NEAR(body.bounds.size().x, 28.0f, 1e-4f);
    REQUIRE_NEAR(body.bounds.size().z, 32.0f, 1e-4f);
    REQUIRE_NEAR(body.bounds.max.y, 9.20f, 1e-4f);
    REQUIRE(body.bounds.min.y > .40f);
    REQUIRE_NEAR(gear.bounds.min.y, 0.0f, 1e-5f);
    REQUIRE_NEAR(gear.bounds.max.y, 1.16f, 1e-5f);
    for (const auto* mesh : {&body, &gear}) {
        for (const auto& v : mesh->vertices) {
            REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
            REQUIRE(v.u >= 0.0f && v.u <= 1.0f && v.v >= 0.0f && v.v <= 1.0f);
        }
        for (const auto index : mesh->indices) REQUIRE(index < mesh->vertices.size());
    }
    // Compound collision preserves empty space below wings, rather than
    // surrounding the entire airframe with one enormous invisible wall.
    for (const auto& box : city::kAirportAircraftCollision) {
        REQUIRE(box.half.x > 0 && box.half.y > 0 && box.half.z > 0);
        if (std::fabs(box.centre.x) > 6.0f) REQUIRE(box.centre.y-box.half.y > 2.6f);
    }
    REQUIRE_NEAR(city::kAirportAircraftEntry.z, 10.3f, 1e-5f);
    apricot_test::pass("Aster airliner has a wheel-free body, separate gear and clear wing undersides");
}

void player_body_is_the_wheelless_alpha_cook() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path("models/vehicles/car5/body.emesh"), body));
    REQUIRE(body.vertices.size() == 716u);
    REQUIRE(body.indices.size() == 1026u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.781204f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().y, 1.667547f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 7.365836f, 1e-5f);
    apricot_test::pass("Car 5 is the wheel-less alpha body cook");
}

void shared_wheel_has_the_expected_axle_and_radius() {
    StaticEmesh wheel;
    REQUIRE(read_static_emesh(asset_path("models/vehicles/common/wheel.emesh"),
                              wheel));
    REQUIRE(wheel.vertices.size() == 48u);
    REQUIRE(wheel.indices.size() == 84u);
    REQUIRE(wheel.bounds.valid());
    REQUIRE_NEAR(wheel.bounds.size().x, 0.273632f, 1e-5f);
    REQUIRE_NEAR(wheel.bounds.size().y, 0.917154f, 1e-5f);
    REQUIRE_NEAR(wheel.bounds.size().z, 0.917154f, 1e-5f);
    for (const uint32_t index : wheel.indices) {
        REQUIRE(index < wheel.vertices.size());
    }
    apricot_test::pass("the shared wheel model is centred on an X axle");
}

void halcyon_six_is_a_wheelless_traffic_ready_body() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(
        asset_path("models/vehicles/halcyon_six/body.emesh"), body));
    REQUIRE(body.vertices.size() == 1368u);
    REQUIRE(body.indices.size() == 1974u);
    REQUIRE(body.indices.size() / 3u == 658u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.50f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().y, 2.09f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 6.60f, 1e-5f);

    constexpr float kArchY = 0.68f;
    constexpr float kWheelX = 1.06f;
    constexpr float kFrontWheelZ = 2.18f;
    constexpr float kRearWheelZ = 1.86f;
    const TrafficVisualLayout layout = make_traffic_visual_layout(
        body.bounds, kArchY, kWheelX, kFrontWheelZ, kRearWheelZ);
    REQUIRE_NEAR(layout.placed_body_bounds.size().z, 5.0f, 1e-4f);
    REQUIRE_NEAR(layout.placed_body_bounds.size().x, 1.893939f, 2e-5f);
    for (const glm::vec3 centre : layout.wheel_centres) {
        REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
        REQUIRE(centre.x >= layout.placed_body_bounds.min.x);
        REQUIRE(centre.x <= layout.placed_body_bounds.max.x);
        REQUIRE(centre.z >= layout.placed_body_bounds.min.z);
        REQUIRE(centre.z <= layout.placed_body_bounds.max.z);
    }
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const MeshData glow = make_vehicle_lamp_mesh(body, lamp);
        REQUIRE(glow.bounds.valid());
        REQUIRE(glow.vertices.size() == 4u);
        REQUIRE(glow.indices.size() == 6u);
    }
    apricot_test::pass(
        "Halcyon Six meets the body-only triangle, bounds and axle contract");
}

void montrose_regent_eight_is_a_wheelless_traffic_ready_body() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(
        asset_path("models/vehicles/montrose_regent_eight/body.emesh"), body));
    REQUIRE(body.vertices.size() == 1784u);
    REQUIRE(body.indices.size() == 2550u);
    REQUIRE(body.indices.size() / 3u == 850u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.64f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().y, 2.31f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 7.10f, 1e-5f);

    constexpr float kArchY = 0.68f;
    constexpr float kWheelX = 1.10f;
    constexpr float kFrontWheelZ = 2.30f;
    constexpr float kRearWheelZ = 2.05f;
    const TrafficVisualLayout layout = make_traffic_visual_layout(
        body.bounds, kArchY, kWheelX, kFrontWheelZ, kRearWheelZ);
    REQUIRE_NEAR(layout.placed_body_bounds.size().z, 5.0f, 1e-4f);
    REQUIRE_NEAR(layout.placed_body_bounds.size().x, 1.859155f, 2e-5f);
    const TrafficVehicleFootprint footprint = traffic_vehicle_footprint(
        TrafficVehicleKind::MontroseRegentEight);
    REQUIRE_NEAR(layout.placed_body_bounds.size().x,
                 footprint.half_width_m * 2.0f, 2e-5f);
    REQUIRE_NEAR(layout.placed_body_bounds.size().z,
                 footprint.half_length_m * 2.0f, 2e-5f);
    for (const glm::vec3 centre : layout.wheel_centres) {
        REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
        REQUIRE(centre.x >= layout.placed_body_bounds.min.x);
        REQUIRE(centre.x <= layout.placed_body_bounds.max.x);
        REQUIRE(centre.z >= layout.placed_body_bounds.min.z);
        REQUIRE(centre.z <= layout.placed_body_bounds.max.z);
    }
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const MeshData glow = make_vehicle_lamp_mesh(body, lamp);
        REQUIRE(glow.bounds.valid());
        REQUIRE(glow.vertices.size() == 4u);
        REQUIRE(glow.indices.size() == 6u);
    }
    apricot_test::pass(
        "Montrose Regent Eight meets the body-only triangle, bounds and axle contract");
}

void vesper_vx91_is_a_wheelless_traffic_ready_body() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(
        asset_path("models/vehicles/vesper_vx91/body.emesh"), body));
    REQUIRE(body.vertices.size() == 2820u);
    REQUIRE(body.indices.size() == 2820u);
    REQUIRE(body.indices.size() / 3u == 940u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.50f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().y, 1.56f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 6.44f, 1e-5f);

    constexpr float kArchY = 0.55f;
    constexpr float kWheelX = 1.08f;
    constexpr float kFrontWheelZ = 2.08f;
    constexpr float kRearWheelZ = 1.86f;
    const TrafficVisualLayout layout = make_traffic_visual_layout(
        body.bounds, kArchY, kWheelX, kFrontWheelZ, kRearWheelZ);
    REQUIRE_NEAR(layout.placed_body_bounds.size().z, 5.0f, 1e-4f);
    REQUIRE_NEAR(layout.placed_body_bounds.size().x, 1.940994f, 2e-5f);
    // The player rig fits this body to the shared 2.70 m wheelbase. Keep the
    // roof sports-car low without letting it collapse below a usable coupe.
    constexpr float kPlayerBodyScale = 2.70f /
        (kFrontWheelZ + kRearWheelZ);
    const float player_roof_y = 0.32f +
        (body.bounds.max.y - kArchY) * kPlayerBodyScale;
    REQUIRE(player_roof_y > 1.12f);
    REQUIRE(player_roof_y < 1.20f);
    const TrafficVehicleFootprint footprint = traffic_vehicle_footprint(
        TrafficVehicleKind::VesperVx91);
    REQUIRE_NEAR(layout.placed_body_bounds.size().x,
                 footprint.half_width_m * 2.0f, 2e-5f);
    REQUIRE_NEAR(layout.placed_body_bounds.size().z,
                 footprint.half_length_m * 2.0f, 2e-5f);
    for (const glm::vec3 centre : layout.wheel_centres) {
        REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
        REQUIRE(centre.x >= layout.placed_body_bounds.min.x);
        REQUIRE(centre.x <= layout.placed_body_bounds.max.x);
        REQUIRE(centre.z >= layout.placed_body_bounds.min.z);
        REQUIRE(centre.z <= layout.placed_body_bounds.max.z);
    }
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const MeshData glow = make_vehicle_lamp_mesh(body, lamp);
        REQUIRE(glow.bounds.valid());
        REQUIRE(glow.vertices.size() == 4u);
        REQUIRE(glow.indices.size() == 6u);
    }
    apricot_test::pass(
        "Vesper VX-91 meets the body-only triangle, bounds and axle contract");
}

void glm_lunge_is_a_wheelless_player_ready_body() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(
        asset_path("models/vehicles/glm_lunge/body.emesh"), body));
    REQUIRE(body.vertices.size() == 2544u);
    REQUIRE(body.indices.size() == 2544u);
    REQUIRE(body.indices.size() / 3u == 848u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.50f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().y, 1.16f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 5.508f, 1e-5f);

    constexpr float kArchY = 0.50f;
    constexpr float kWheelX = 1.08f;
    constexpr float kFrontWheelZ = 1.58f;
    constexpr float kRearWheelZ = 1.62f;
    const TrafficVisualLayout layout = make_traffic_visual_layout(
        body.bounds, kArchY, kWheelX, kFrontWheelZ, kRearWheelZ);
    for (const glm::vec3 centre : layout.wheel_centres) {
        REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
        REQUIRE(centre.x >= layout.placed_body_bounds.min.x);
        REQUIRE(centre.x <= layout.placed_body_bounds.max.x);
        REQUIRE(centre.z >= layout.placed_body_bounds.min.z);
        REQUIRE(centre.z <= layout.placed_body_bounds.max.z);
    }
    constexpr float kPlayerBodyScale = 2.70f /
        (kFrontWheelZ + kRearWheelZ);
    const float player_roof_y = 0.32f +
        (body.bounds.max.y - kArchY) * kPlayerBodyScale;
    REQUIRE(player_roof_y > 0.98f);
    REQUIRE(player_roof_y < 1.08f);
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const MeshData glow = make_vehicle_lamp_mesh(body, lamp);
        REQUIRE(glow.bounds.valid());
        REQUIRE(glow.vertices.size() == 4u);
        REQUIRE(glow.indices.size() == 6u);
    }
    apricot_test::pass(
        "GLM Lunge meets the body-only triangle, bounds and axle contract");
}

void glm_zip_is_a_wheelless_player_ready_body() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(
        asset_path("models/vehicles/glm_zip/body.emesh"), body));
    REQUIRE(body.vertices.size() >= 1950u);
    REQUIRE(body.vertices.size() <= 3900u);
    REQUIRE(body.indices.size() == body.vertices.size());
    REQUIRE(body.indices.size() / 3u >= 650u);
    REQUIRE(body.indices.size() / 3u <= 1300u);
    REQUIRE(body.bounds.valid());
    REQUIRE_NEAR(body.bounds.size().x, 2.30f, 1e-5f);
    REQUIRE(body.bounds.size().y > 1.08f);
    REQUIRE(body.bounds.size().y < 1.20f);
    REQUIRE(body.bounds.size().z > 4.95f);
    REQUIRE(body.bounds.size().z < 5.15f);

    constexpr float kArchY = 0.46f;
    constexpr float kWheelX = 0.98f;
    constexpr float kFrontWheelZ = 1.65f;
    constexpr float kRearWheelZ = 1.60f;
    const TrafficVisualLayout layout = make_traffic_visual_layout(
        body.bounds, kArchY, kWheelX, kFrontWheelZ, kRearWheelZ);
    for (const glm::vec3 centre : layout.wheel_centres) {
        REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
        REQUIRE(centre.x >= layout.placed_body_bounds.min.x);
        REQUIRE(centre.x <= layout.placed_body_bounds.max.x);
        REQUIRE(centre.z >= layout.placed_body_bounds.min.z);
        REQUIRE(centre.z <= layout.placed_body_bounds.max.z);
    }
    constexpr float kPlayerBodyScale = 2.70f /
        (kFrontWheelZ + kRearWheelZ);
    const float player_roof_y = 0.32f +
        (body.bounds.max.y - kArchY) * kPlayerBodyScale;
    REQUIRE(player_roof_y > 1.00f);
    REQUIRE(player_roof_y < 1.06f);

    const auto signed_area = [](float px, float py, float ax, float ay,
                                float bx, float by) {
        return (px - bx) * (ay - by) - (ax - bx) * (py - by);
    };
    const auto covers_sample = [&](float sample_z, float sample_y,
                                   float side) {
        for (std::size_t start = 0; start < body.indices.size(); start += 3u) {
            const EmeshVertex& a = body.vertices[body.indices[start]];
            const EmeshVertex& b = body.vertices[body.indices[start + 1u]];
            const EmeshVertex& c = body.vertices[body.indices[start + 2u]];
            if (side * a.px <= 0.75f || side * b.px <= 0.75f ||
                side * c.px <= 0.75f) {
                continue;
            }
            const float d0 = signed_area(sample_z, sample_y,
                                         a.pz, a.py, b.pz, b.py);
            const float d1 = signed_area(sample_z, sample_y,
                                         b.pz, b.py, c.pz, c.py);
            const float d2 = signed_area(sample_z, sample_y,
                                         c.pz, c.py, a.pz, a.py);
            const bool positive =
                d0 > 1e-6f && d1 > 1e-6f && d2 > 1e-6f;
            const bool negative =
                d0 < -1e-6f && d1 < -1e-6f && d2 < -1e-6f;
            if (positive || negative) return true;
        }
        return false;
    };
    constexpr std::array<glm::vec2, 5> kWellSamples = {{
        {0.0f, 0.0f}, {-0.05f, 0.0f}, {0.05f, 0.0f},
        {0.0f, -0.05f}, {0.0f, 0.05f},
    }};
    for (const float wheel_z : {kFrontWheelZ, -kRearWheelZ}) {
        for (const glm::vec2 offset : kWellSamples) {
            REQUIRE(!covers_sample(wheel_z + offset.x,
                                   kArchY + offset.y, -1.0f));
            REQUIRE(!covers_sample(wheel_z + offset.x,
                                   kArchY + offset.y, 1.0f));
        }
    }
    const auto covers_hood = [&](float sample_x, float sample_z) {
        for (std::size_t start = 0; start < body.indices.size(); start += 3u) {
            const EmeshVertex& a = body.vertices[body.indices[start]];
            const EmeshVertex& b = body.vertices[body.indices[start + 1u]];
            const EmeshVertex& c = body.vertices[body.indices[start + 2u]];
            if (a.py <= 0.65f || b.py <= 0.65f || c.py <= 0.65f) continue;
            const float d0 = signed_area(sample_x, sample_z,
                                         a.px, a.pz, b.px, b.pz);
            const float d1 = signed_area(sample_x, sample_z,
                                         b.px, b.pz, c.px, c.pz);
            const float d2 = signed_area(sample_x, sample_z,
                                         c.px, c.pz, a.px, a.pz);
            const bool positive =
                d0 > 1e-6f && d1 > 1e-6f && d2 > 1e-6f;
            const bool negative =
                d0 < -1e-6f && d1 < -1e-6f && d2 < -1e-6f;
            if (positive || negative) return true;
        }
        return false;
    };
    for (const float hood_x : {-0.60f, 0.0f, 0.60f}) {
        REQUIRE(covers_hood(hood_x, kFrontWheelZ - 0.05f));
        REQUIRE(covers_hood(hood_x, kFrontWheelZ + 0.05f));
    }
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const MeshData glow = make_vehicle_lamp_mesh(body, lamp);
        REQUIRE(glow.bounds.valid());
        REQUIRE(glow.vertices.size() == 4u);
        REQUIRE(glow.indices.size() == 6u);
    }
    apricot_test::pass(
        "GLM ZIP meets the body-only triangle, bounds and axle contract");
}

void harrow_workman_has_open_wells_hood_and_cargo_bed() {
    const auto& car = player_car_definition(PlayerCarId::HarrowWorkman);
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path(car.mesh_path), body));
    REQUIRE(body.bounds.valid());
    REQUIRE(body.indices.size() / 3u >= 650u);
    REQUIRE(body.indices.size() / 3u <= 1300u);
    // Mirror paint is now on its housing, not 2 mm outside each side.
    REQUIRE_NEAR(body.bounds.size().x, 2.220f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 5.40f, 1e-5f);
    const float scale = 2.70f / (car.wheel_front_z + car.wheel_rear_z);
    const float roof = 0.32f + (body.bounds.max.y - car.arch_centre_y) * scale;
    REQUIRE(roof > 1.55f && roof < 1.70f);
    const auto contains = [](glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        const auto cross = [](glm::vec2 u, glm::vec2 v) {
            return u.x * v.y - u.y * v.x;
        };
        const float d0 = cross(b - a, p - a);
        const float d1 = cross(c - b, p - b);
        const float d2 = cross(a - c, p - c);
        return (d0 > 1e-6f && d1 > 1e-6f && d2 > 1e-6f) ||
               (d0 < -1e-6f && d1 < -1e-6f && d2 < -1e-6f);
    };
    const auto covers = [&](glm::vec2 sample, bool top, float threshold) {
        for (std::size_t i = 0; i < body.indices.size(); i += 3u) {
            const auto& a = body.vertices[body.indices[i]];
            const auto& b = body.vertices[body.indices[i + 1u]];
            const auto& c = body.vertices[body.indices[i + 2u]];
            if (top) {
                if (a.py <= threshold || b.py <= threshold || c.py <= threshold) continue;
                if (contains(sample, {a.px, a.pz}, {b.px, b.pz}, {c.px, c.pz})) return true;
            } else {
                const float side = threshold;
                if (side * a.px <= .85f || side * b.px <= .85f || side * c.px <= .85f) continue;
                if (contains(sample, {a.pz, a.py}, {b.pz, b.py}, {c.pz, c.py})) return true;
            }
        }
        return false;
    };
    for (float axle : {car.wheel_front_z, -car.wheel_rear_z}) {
        for (float side : {-1.0f, 1.0f}) {
            for (float offset : {-.12f, 0.0f, .12f}) {
                REQUIRE(!covers({axle + offset, car.arch_centre_y}, false, side));
            }
        }
    }
    for (float x : {-.75f, 0.0f, .75f}) {
        REQUIRE(covers({x, 1.60f}, true, 1.10f));
        REQUIRE(covers({x, 1.70f}, true, 1.10f));
    }
    for (float x : {-.3f, .3f}) {
        for (float z : {-2.3f, -1.8f, -1.2f, -.8f}) {
            REQUIRE(!covers({x, z}, true, .70f));
            REQUIRE(covers({x, z}, true, .59f));
        }
    }
    for (const auto& v : body.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
    }
    apricot_test::pass("Harrow Workman keeps real wheel openings, a wide hood and an open bed");
}

void alder_wayfarer_has_open_wells_and_a_complete_wagon_roof() {
    const auto& car = player_car_definition(PlayerCarId::AlderWayfarer);
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path(car.mesh_path), body));
    REQUIRE(body.bounds.valid());
    REQUIRE(body.indices.size() / 3u >= 650u);
    REQUIRE(body.indices.size() / 3u <= 1300u);
    REQUIRE_NEAR(body.bounds.size().x, 2.300f, 1e-5f);
    REQUIRE_NEAR(body.bounds.size().z, 5.500f, 1e-5f);
    REQUIRE_NEAR(body.bounds.max.y, 1.88f, 1e-5f);
    const float scale = 2.70f / (car.wheel_front_z + car.wheel_rear_z);
    REQUIRE_NEAR(0.32f + (1.77f - car.arch_centre_y) * scale, 1.43323f, 1e-5f);
    const auto contains = [](glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        const auto cross = [](glm::vec2 u, glm::vec2 v) {
            return u.x * v.y - u.y * v.x;
        };
        const float d0 = cross(b - a, p - a);
        const float d1 = cross(c - b, p - b);
        const float d2 = cross(a - c, p - c);
        return (d0 > 1e-6f && d1 > 1e-6f && d2 > 1e-6f) ||
               (d0 < -1e-6f && d1 < -1e-6f && d2 < -1e-6f);
    };
    const auto covers = [&](glm::vec2 sample, bool top, float threshold) {
        for (std::size_t i = 0; i < body.indices.size(); i += 3u) {
            const auto& a = body.vertices[body.indices[i]];
            const auto& b = body.vertices[body.indices[i + 1u]];
            const auto& c = body.vertices[body.indices[i + 2u]];
            if (top) {
                if (a.py <= threshold || b.py <= threshold || c.py <= threshold) continue;
                if (contains(sample, {a.px, a.pz}, {b.px, b.pz}, {c.px, c.pz})) return true;
            } else {
                if (threshold * a.px <= .66f || threshold * b.px <= .66f ||
                    threshold * c.px <= .66f) continue;
                if (contains(sample, {a.pz, a.py}, {b.pz, b.py}, {c.pz, c.py})) return true;
            }
        }
        return false;
    };
    for (float axle : {car.wheel_front_z, -car.wheel_rear_z}) {
        for (float side : {-1.0f, 1.0f}) {
            for (float offset : {-.12f, 0.0f, .12f}) {
                REQUIRE(!covers({axle + offset, car.arch_centre_y}, false, side));
            }
        }
    }
    for (float x : {-.65f, 0.0f, .65f}) {
        REQUIRE(covers({x, 1.65f}, true, 1.02f));
        REQUIRE(covers({x, 1.75f}, true, 1.02f));
    }
    for (float x : {-.55f, .13f, .55f}) {
        for (float z : {-1.9f, -1.3f, -.6f, .1f}) {
            REQUIRE(covers({x, z}, true, 1.70f));
        }
    }
    for (const auto& v : body.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u >= 0.0f && v.u <= 1.0f);
        REQUIRE(v.v >= 0.0f && v.v <= 1.0f);
    }
    apricot_test::pass("Alder Wayfarer keeps open wheel wells, a wide hood and a long roof");
}

void fleet_uses_body_surface_cooks() {
    constexpr std::array<PlayerCarId, 7> baked{{
        PlayerCarId::GlmLunge, PlayerCarId::GlmZip, PlayerCarId::HalcyonSix,
        PlayerCarId::HarrowWorkman, PlayerCarId::MontroseRegentEight,
        PlayerCarId::MunicipalFiretruck, PlayerCarId::VesperVx91,
    }};
    for (const auto id : baked) {
        const auto& car = player_car_definition(id);
        const std::string path = car.mesh_path;
        REQUIRE(path.find("body_surface.emesh") != std::string::npos);
        REQUIRE(std::string(car.texture_path).find("body_surface.png") != std::string::npos);
        StaticEmesh body;
        REQUIRE(read_static_emesh(asset_path(car.mesh_path), body));
        REQUIRE(body.bounds.valid());
        REQUIRE(body.indices.size() >= 200u * 3u);
        REQUIRE(body.indices.size() <= 1300u * 3u);
        REQUIRE_NEAR(body.bounds.min.x, -body.bounds.max.x, 1e-4f);
        for (const auto& v : body.vertices) {
            REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
            REQUIRE(v.u >= 0.0f && v.u <= 1.0f);
            REQUIRE(v.v >= 0.0f && v.v <= 1.0f);
        }
        for (const auto index : body.indices) REQUIRE(index < body.vertices.size());
    }
    for (std::size_t lamp = 0; lamp < 4u; ++lamp) {
        const auto flag = vehicle_lamp_surface_uv(lamp);
        REQUIRE_NEAR(-flag.x - 2.0f, static_cast<float>(lamp), 1e-6f);
        REQUIRE_NEAR(flag.y, 1.0f, 1e-6f);
    }
    apricot_test::pass("fleet loads baked shells and uses body-surface glow selectors");
}

void legacy_traffic_bodies_are_wheelless_cooks() {
    const CrowdTuning collision;
    struct ExpectedBody {
        const char* path;
        const char* name;
        std::size_t vertices;
        std::size_t indices;
        float arch_y;
        float wheel_x;
        float wheel_front_z;
        float wheel_rear_z;
        TrafficVehicleKind kind;
    };
    const ExpectedBody expected[] = {
        {"models/vehicles/car5/body.emesh", "Car 5", 716u, 1026u,
         0.45f, 1.038f, 2.254f, 1.813f, TrafficVehicleKind::Sedan},
        {"models/vehicles/car8/body.emesh", "Car 8", 480u, 792u,
         0.525f, 1.10f, 1.65f, 2.25f, TrafficVehicleKind::BoxTruck},
        {"models/vehicles/ambulance/body.emesh", "ambulance", 3888u, 3888u,
         .47f, .98f, 1.78f, 1.58f, TrafficVehicleKind::Ambulance},
        {"models/vehicles/firetruck/body.emesh", "firetruck", 1048u, 1572u,
         0.72f, 1.13f, 2.18f, 2.12f, TrafficVehicleKind::Firetruck},
    };
    for (const ExpectedBody& item : expected) {
        StaticEmesh body;
        REQUIRE(read_static_emesh(asset_path(item.path), body));
        REQUIRE(body.vertices.size() == item.vertices);
        REQUIRE(body.indices.size() == item.indices);
        REQUIRE(body.bounds.valid());
        for (const uint32_t index : body.indices) {
            REQUIRE(index < body.vertices.size());
        }
        const TrafficVisualLayout layout = make_traffic_visual_layout(
            body.bounds, item.arch_y, item.wheel_x, item.wheel_front_z,
            item.wheel_rear_z);
        const AABB placed = body.bounds.transformed(layout.body.matrix());
        const VehicleLampLayout lamps = make_vehicle_lamp_layout(placed);
        const TrafficVehicleFootprint footprint =
            traffic_vehicle_footprint(item.kind);
        REQUIRE_NEAR(placed.size().z, 5.0f, 1e-4f);
        REQUIRE_NEAR(placed.size().x, footprint.half_width_m * 2.0f,
                     2e-5f);
        REQUIRE_NEAR(placed.size().z, footprint.half_length_m * 2.0f,
                     2e-5f);
        REQUIRE_MSG(placed.size().x <=
                        collision.traffic_half_width_m * 2.0f + 1e-4f,
                    "traffic collider is narrower than its visible body",
                    item.name);
        REQUIRE_MSG(placed.size().z <=
                        collision.traffic_half_length_m * 2.0f + 1e-4f,
                    "traffic collider is shorter than its visible body",
                    item.name);
        REQUIRE_NEAR((placed.min.x + placed.max.x) * 0.5f, 0.0f, 1e-4f);
        REQUIRE_NEAR((placed.min.z + placed.max.z) * 0.5f, 0.0f, 1e-4f);
        for (const glm::vec3 centre : layout.wheel_centres) {
            REQUIRE_NEAR(centre.y, layout.wheel_radius, 1e-5f);
            REQUIRE(centre.x >= placed.min.x && centre.x <= placed.max.x);
            REQUIRE(centre.z >= placed.min.z && centre.z <= placed.max.z);
        }
        REQUIRE(layout.wheel_centres[0].x < layout.wheel_centres[1].x);
        REQUIRE(layout.wheel_centres[2].x < layout.wheel_centres[3].x);
        REQUIRE(layout.wheel_centres[0].z < layout.wheel_centres[2].z);
        REQUIRE(layout.wheel_centres[1].z < layout.wheel_centres[3].z);
        REQUIRE_NEAR(layout.wheel_centres[0].z,
                     layout.wheel_centres[1].z, 1e-5f);
        REQUIRE_NEAR(layout.wheel_centres[2].z,
                     layout.wheel_centres[3].z, 1e-5f);
        // Lamp overlays are nearly flush with the fitted render body. They
        // must never use a generic physics length that can leave a dark box
        // floating out in front of a shorter or deformed model.
        for (std::size_t lamp_index = 0; lamp_index < lamps.lamps.size();
             ++lamp_index) {
            const Transform& lamp = lamps.lamps[lamp_index];
            const bool front = lamp_index < 2u;
            REQUIRE(lamp.position.x >= placed.min.x &&
                    lamp.position.x <= placed.max.x);
            REQUIRE(lamp.position.y >= placed.min.y &&
                    lamp.position.y <= placed.max.y);
            REQUIRE_NEAR(lamp.position.z,
                         front ? placed.min.z - 0.0036f
                               : placed.max.z + 0.0036f,
                         1e-5f);
            REQUIRE(lamp.scale.z <= 0.02f);

            // The rendered glow is authored directly in the legacy body's
            // source coordinate frame and receives this exact body transform.
            // It must cross the fitted body skin slightly: enough overlap to
            // prevent a deformation gap, without sinking into the body.
            const MeshData lamp_mesh =
                make_vehicle_lamp_mesh(body, lamp_index);
            REQUIRE(lamp_mesh.bounds.valid());
            REQUIRE(lamp_mesh.vertices.size() == 4u);
            REQUIRE(lamp_mesh.indices.size() == 6u);
            for (const MeshVertex& vertex : lamp_mesh.vertices) {
                float body_z = 0.0f;
                REQUIRE(vehicle_body_surface_z(
                    body, vertex.position.x, vertex.position.y, front,
                    body_z));
                REQUIRE_NEAR(std::fabs(vertex.position.z - body_z),
                             kVehicleLampSurfaceOffset, 2e-5f);
            }
            const AABB placed_lamp =
                lamp_mesh.bounds.transformed(layout.body.matrix());
            REQUIRE(placed_lamp.min.x >= placed.min.x - 1e-4f);
            REQUIRE(placed_lamp.max.x <= placed.max.x + 1e-4f);
            REQUIRE(placed_lamp.min.y >= placed.min.y - 1e-4f);
            REQUIRE(placed_lamp.max.y <= placed.max.y + 1e-4f);
            REQUIRE(placed_lamp.min.z >= placed.min.z - 0.01f);
            REQUIRE(placed_lamp.max.z <= placed.max.z + 0.01f);
        }
        std::printf("      %s: %zu vertices, %zu indices\n", item.name,
                    body.vertices.size(), body.indices.size());
    }
    apricot_test::pass(
        "legacy traffic bodies derive wheel nodes from their native arches");
}

void headlights_follow_authored_lens_regions() {
    for (const auto& car : kPlayerCars) {
        StaticEmesh body;
        REQUIRE(read_static_emesh(asset_path(car.mesh_path), body));
        const auto profile = vehicle_headlight_profile(car.mesh_path);
        REQUIRE(profile.id >= 0);
        REQUIRE(profile.exposed() == (car.id != PlayerCarId::VesperVx91));
        std::array<glm::vec3, 2> origins{};
        for (std::size_t side = 0; side < 2; ++side) {
            const bool found = vehicle_headlight_origin(body, profile, side, origins[side]);
            if (found != profile.exposed()) std::printf("headlight fit failed: %s side %zu\n", car.mesh_path, side);
            REQUIRE(found == profile.exposed());
            const auto selector = vehicle_lamp_surface_uv(side, profile.id);
            REQUIRE_NEAR(selector.y - 1, static_cast<float>(profile.id), 1e-6f);
        }
        if (!profile.exposed()) continue;
        REQUIRE(origins[0].x > 0);
        REQUIRE_NEAR(origins[0].x, -origins[1].x, 1e-4f);
        REQUIRE_NEAR(origins[0].y, origins[1].y, 1e-4f);
        // x and y above are mirrored by construction, so this line is the
        // only one that reads the mesh: it asserts the cooked body is
        // symmetric where the lens sits. It is, to about a millimetre. The
        // Shu's nose comes back at 2.20301 m on one side and 2.20413 m on the
        // other -- 1.12 mm apart, because the source model was never mirrored
        // before it was cooked. At that depth the difference is invisible: the
        // lamp quad it positions is centimetres across. So the tolerance is a
        // cook's worth of drift and not a millimetre more; widen it again and
        // a genuinely crooked nose walks in behind it.
        REQUIRE_NEAR(origins[0].z, origins[1].z, 2.5e-3f);
        for (const auto& r : profile.regions) {
            if (!r.valid()) continue;
            glm::vec3 p{(r.x0+r.x1)*0.5f, (r.y0+r.y1)*0.5f, (r.z0+r.z1)*0.5f};
            REQUIRE(r.contains(p));
            p.x = -p.x;
            REQUIRE(r.contains(p));
            p.x = r.x1 + 0.01f;
            REQUIRE(!r.contains(p));
            if (r.round) {
                p.x = r.x1; p.y = r.y1;
                REQUIRE(!r.contains(p)); // No square corners around round bulbs.
            }
        }
    }
    const auto split = vehicle_headlight_profile("models/vehicles/glm_zip/body_surface.emesh");
    for (const auto& r : split.regions) REQUIRE(!r.contains({0.61f,0.48f,2.50f}));
    REQUIRE(!vehicle_headlight_profile("models/unknown/body.emesh").exposed());
    apricot_test::pass("headlight profiles preserve lens shape, bulb gaps and model-specific beam origins");
}

void brake_lights_follow_rear_red_cells() {
    for (const auto& car : kPlayerCars) {
        const auto brake = vehicle_brakelight_profile(car.mesh_path);
        REQUIRE(brake.id == vehicle_headlight_profile(car.mesh_path).id);
        REQUIRE(brake.regions[0].valid());
        if (car.id == PlayerCarId::PizazConstant) {
            // The white reverse inset sits between the two red brake cells.
            // Keep both mirrored samples out of the brake-light profile.
            for (const auto& r : brake.regions) {
                REQUIRE(!r.contains({.47f,.69f,-2.342f}));
                REQUIRE(!r.contains({-.47f,.69f,-2.342f}));
            }
        }
        StaticEmesh body;
        REQUIRE(read_static_emesh(asset_path(car.mesh_path),body));
        if (car.id == PlayerCarId::MunicipalAmbulance) {
            std::size_t rear_red_vertices = 0;
            for (const auto& v : body.vertices) {
                if (v.pz >= -3.07f || v.nz >= -.9f ||
                    std::fabs(v.px) < .83f || std::fabs(v.px) > 1.03f ||
                    v.py < .65f || v.py > 1.14f ||
                    (v.py > .82f && v.py < .96f)) continue;
                REQUIRE(v.u >= 150.f / 256.f && v.u <= 174.f / 256.f);
                REQUIRE(v.v >= 54.f / 256.f && v.v <= 86.f / 256.f);
                ++rear_red_vertices;
            }
            REQUIRE(rear_red_vertices >= 12u);
        }
        for (const auto& r : brake.regions) {
            if (!r.valid()) continue;
            for (const float side : {-1.0f,1.0f}) {
                glm::vec3 p{side*(r.x0+r.x1)*.5f,(r.y0+r.y1)*.5f,0};
                const bool hit = vehicle_body_surface_z(body,p.x,p.y,false,p.z);
                if (!hit || !r.contains(p))
                    std::printf("rear fit: %s x %.3f y %.3f z %.3f\n",car.mesh_path,p.x,p.y,p.z);
                REQUIRE(hit);
                REQUIRE(r.contains(p));
            }
        }
    }
    const auto zip = vehicle_brakelight_profile("models/vehicles/glm_zip/body_surface.emesh");
    for (const auto& r : zip.regions) REQUIRE(!r.contains({.70f,.53f,-2.52f}));
    const auto harrow = vehicle_brakelight_profile("models/vehicles/harrow_workman/body_surface.emesh");
    for (const auto& r : harrow.regions) REQUIRE(!r.contains({.95f,.745f,-2.54f}));
    apricot_test::pass("rear lamp profiles follow real faces and exclude amber/reverse cells");
}

void cinder_lights_use_fixed_lenses_and_preserve_reverse_insets() {
    const char* path="models/vehicles/orison_cinder/body.emesh";
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path(path),body));
    const auto head=vehicle_headlight_profile(path);
    const auto brake=vehicle_brakelight_profile(path);
    REQUIRE(head.exposed() && head.id==21 && brake.id==head.id);
    for (std::size_t side=0;side<2;++side) {
        const float sign=side==0 ? 1.f : -1.f;
        glm::vec3 origin;
        REQUIRE(vehicle_headlight_origin(body,head,side,origin));
        REQUIRE_NEAR(origin.x,sign*.5f,1e-5f);
        REQUIRE_NEAR(origin.y,.3575f,1e-5f);
        REQUIRE_NEAR(origin.z,2.214f,1e-5f);
        for (const auto& region:head.regions) {
            REQUIRE(!region.contains({sign*.55f,.68f,1.70f})); // Shut pop-up cover.
            REQUIRE(!region.contains({sign*.72f,.48f,2.214f})); // Amber marker.
        }
        for (const auto& region:brake.regions) {
            REQUIRE(!region.contains({sign*.385f,.5415f,-2.218f})); // Reverse inset.
            REQUIRE(!region.contains({sign*.81f,.59f,-2.214f})); // Amber outer cell.
            REQUIRE(!region.contains({sign*.15f,.59f,-2.214f})); // Centre plate.
            glm::vec3 lens{sign*(region.x0+region.x1)*.5f,(region.y0+region.y1)*.5f,0};
            REQUIRE(vehicle_body_surface_z(body,lens.x,lens.y,false,lens.z));
            REQUIRE(region.contains(lens));
        }
    }
    apricot_test::pass("Cinder lights fit the cooked lenses and exclude pop-up lids, amber and reverse cells");
}

void cinder_windows_face_outward_and_plate_reads_from_the_rear() {
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path("models/vehicles/orison_cinder/body.emesh"),body));
    std::size_t front_glass=0,rear_glass=0,side_glass=0,roof=0;
    const auto glass_uv=[](const EmeshVertex& v) {
        return v.u>=5.f/256.f && v.u<=107.f/256.f &&
               v.v>=119.f/256.f && v.v<=171.f/256.f;
    };
    for (std::size_t i=0;i<body.indices.size();i+=3) {
        const auto& a=body.vertices[body.indices[i]];
        const auto& b=body.vertices[body.indices[i+1]];
        const auto& c=body.vertices[body.indices[i+2]];
        const glm::vec3 pa{a.px,a.py,a.pz},pb{b.px,b.py,b.pz},pc{c.px,c.py,c.pz};
        const glm::vec3 center=(pa+pb+pc)/3.f;
        const glm::vec3 face=glm::normalize(glm::cross(pb-pa,pc-pa));
        if (center.y>1.22f && center.z>-.84f && center.z<-.045f) {
            REQUIRE(face.y>.8f);
            ++roof;
        }
        if (!glass_uv(a) || !glass_uv(b) || !glass_uv(c)) continue;
        const glm::vec3 normal=glm::normalize(glm::vec3{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz});
        // Both winding (backface culling) and shading normals must point out.
        REQUIRE(glm::dot(face,normal)>.999f);
        if (std::abs(center.x)>.55f && std::abs(face.x)>.5f) {
            REQUIRE(face.x*center.x>.3f);
            REQUIRE(face.y>0.f);
            ++side_glass;
        } else if (center.z>.1f) {
            REQUIRE(face.y>.4f && face.z>.2f);
            ++front_glass;
        } else {
            REQUIRE(face.y>.4f && face.z<-.2f);
            ++rear_glass;
        }
    }
    REQUIRE(front_glass>=2u && rear_glass>=2u && side_glass>=8u && roof>=12u);

    const auto plate_uv=[&](float x,float y) {
        for (std::size_t i=0;i<body.indices.size();i+=3) {
            const auto& a=body.vertices[body.indices[i]];
            const auto& b=body.vertices[body.indices[i+1]];
            const auto& c=body.vertices[body.indices[i+2]];
            if (std::abs(a.pz+2.214f)>.0001f || std::abs(b.pz+2.214f)>.0001f ||
                std::abs(c.pz+2.214f)>.0001f || a.nz>-.9f) continue;
            const float denominator=(b.py-c.py)*(a.px-c.px)+(c.px-b.px)*(a.py-c.py);
            if (std::abs(denominator)<1e-7f) continue;
            const float u=((b.py-c.py)*(x-c.px)+(c.px-b.px)*(y-c.py))/denominator;
            const float v=((c.py-a.py)*(x-c.px)+(a.px-c.px)*(y-c.py))/denominator;
            const float w=1.f-u-v;
            if (u<-.0001f || v<-.0001f || w<-.0001f) continue;
            const glm::vec2 uv=u*glm::vec2{a.u,a.v}+v*glm::vec2{b.u,b.v}+w*glm::vec2{c.u,c.v};
            REQUIRE(uv.x>88.f/256.f && uv.x<168.f/256.f);
            REQUIRE(uv.y>50.f/256.f && uv.y<79.f/256.f);
            return uv;
        }
        REQUIRE(false);
        return glm::vec2{0};
    };
    // Engine +X points to the rear viewer's left. Text runs toward -X;
    // V still increases upward, matching the PNG loader's row flip.
    REQUIRE(plate_uv(-.15f,.59f).x>plate_uv(.15f,.59f).x+.1f);
    REQUIRE(plate_uv(0.f,.62f).y>plate_uv(0.f,.54f).y+.03f);
    apricot_test::pass("Cinder canopy survives backface culling and its plate reads upright from behind");
}

void signal_mast_clears_the_widest_crossing_road() {
    const glm::vec3 junction{0.0f, 0.0f, 0.0f};
    const glm::vec3 dir{1.0f, 0.0f, 0.0f};
    const glm::vec3 right{0.0f, 0.0f, 1.0f};
    const TrafficSignalLayout layout = make_traffic_signal_layout(
        junction, dir,
        5.0f,   // narrow approach is 10 m wide
        12.0f,  // crossing arterial is 24 m wide
        2.5f);  // centre of the inbound half

    // The old layout used the 5 m approach half-width here and planted this
    // mast at -8 m, four metres inside the crossing arterial. Its head also
    // stopped at the road centreline instead of above the inbound lanes.
    REQUIRE_NEAR(glm::dot(layout.pole_ground - junction, dir), -15.0f, 1e-5f);
    REQUIRE_NEAR(glm::dot(layout.pole_ground - junction, right), 6.2f, 1e-5f);
    REQUIRE_NEAR(glm::dot(layout.arm_end - junction, right), 2.5f, 1e-5f);
    REQUIRE_NEAR(layout.arm_length_m, 3.7f, 1e-5f);
    apricot_test::pass(
        "signal masts clear the widest road and heads sit over inbound lanes");
}

}  // namespace

int main() {
    std::printf("emesh_reader_tests\n");
    player_body_is_the_wheelless_alpha_cook();
    shared_wheel_has_the_expected_axle_and_radius();
    halcyon_six_is_a_wheelless_traffic_ready_body();
    montrose_regent_eight_is_a_wheelless_traffic_ready_body();
    vesper_vx91_is_a_wheelless_traffic_ready_body();
    glm_lunge_is_a_wheelless_player_ready_body();
    glm_zip_is_a_wheelless_player_ready_body();
    harrow_workman_has_open_wells_hood_and_cargo_bed();
    alder_wayfarer_has_open_wells_and_a_complete_wagon_roof();
    fleet_uses_body_surface_cooks();
    headlights_follow_authored_lens_regions();
    brake_lights_follow_rear_red_cells();
    cinder_lights_use_fixed_lenses_and_preserve_reverse_insets();
    cinder_windows_face_outward_and_plate_reads_from_the_rear();
    aster_aircraft_is_a_separate_airframe_and_landing_gear();
    marlin_speedboat_has_a_real_hull_and_water_mooring();
    legacy_traffic_bodies_are_wheelless_cooks();
    signal_mast_clears_the_widest_crossing_road();
    return apricot_test::done("emesh_reader_tests");
}

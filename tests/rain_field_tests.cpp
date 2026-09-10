// The camera-locked rain field.
//
// One function does both jobs — recycling a drop that fell out of the bottom,
// and keeping the field locked to a camera that moved — and its postcondition
// is a HALF-OPEN interval. Everything here exists to hammer that postcondition
// with the inputs you cannot produce by looking at the game: a twenty-second
// debugger pause, a camera that teleported across the world, a degenerate box.
//
// A drop that escapes the box does not crash. It streaks off to somewhere odd
// once every few minutes, gets written off as a fluke, and lives forever.

#include <cmath>
#include <cstdio>

#include "gfx/rain_field.h"
#include "gfx/precip_shelter.h"
#include "city/precipitation_cover.h"
#include "test_assert.h"

using namespace apricot;

namespace {

std::array<glm::vec3, 4> streak(glm::vec3 head,
                                glm::vec3 tail_offset = {0, 0.85f, 0}) {
    const glm::vec3 half{0.02f, 0, 0};
    return {head - half, head + half, head + tail_offset + half,
            head + tail_offset - half};
}

void roofs_block_indoor_particles_but_keep_outdoor_rain() {
    TerrainCollider collider{1};
    collider.add_static_box({{-5, 4, -5}, {5, 4.3f, 5}});
    PrecipitationShelter shelter;
    shelter.rebuild(collider.static_boxes(), {{-35, -10, -35}, {35, 35, 35}});
    REQUIRE(shelter.sheltered(streak({0, 1, 0})));
    REQUIRE(shelter.sheltered(streak({0, 4.2f, 0})));
    REQUIRE(!shelter.sheltered(streak({0, 4.4f, 0})));
    REQUIRE(!shelter.sheltered(streak({6, 1, 0})));
    // A wind-slanted tail crosses cover even though the head is outside.
    REQUIRE(shelter.sheltered(streak({5.2f, 1, 0}, {-0.4f, .85f, 0})));
    // Width matters too: both rain ribbons and snowflake edges stay outside.
    REQUIRE(shelter.sheltered(streak({5.01f, 1, 0})));
    apricot_test::pass("roof blocks full indoor streaks while roof-top and outdoor rain remain");
}

void rotated_cover_does_not_fill_its_empty_aabb_corners() {
    TerrainCollider collider{1};
    collider.add_static_oriented_box({0, 5, 0}, {5, .2f, 1}, glm::radians(45.f));
    PrecipitationShelter shelter;
    shelter.rebuild(collider.static_boxes(), {{-20, -10, -20}, {20, 20, 20}});
    REQUIRE(shelter.sheltered(streak({0, 1, 0})));
    REQUIRE(!shelter.sheltered(streak({3.5f, 1, 3.5f})));
    apricot_test::pass("rotated roofs preserve rain in uncovered AABB corners");
}

void shelter_refresh_handles_tall_roofs_disabled_props_and_teleports() {
    TerrainCollider collider{1};
    const auto roof = collider.add_kinematic_box({{-5, 100, -5}, {5, 101, 5}});
    PrecipitationShelter shelter;
    const AABB field{{-35, -10, -35}, {35, 35, 35}};
    shelter.rebuild(collider.static_boxes(), field);
    REQUIRE(shelter.sheltered(streak({0, 1, 0})));
    REQUIRE(collider.set_kinematic_enabled(roof, false));
    shelter.rebuild(collider.static_boxes(), field);
    REQUIRE(!shelter.sheltered(streak({0, 1, 0})));
    REQUIRE(collider.set_kinematic_enabled(roof, true));
    REQUIRE(collider.set_kinematic_vehicle(roof, true));
    shelter.rebuild(collider.static_boxes(), field);
    REQUIRE(!shelter.sheltered(streak({0, 1, 0})));
    REQUIRE(collider.set_kinematic_vehicle(roof, false));
    REQUIRE(collider.set_kinematic_box(roof, {{995, 100, 995}, {1005, 101, 1005}}));
    shelter.rebuild(collider.static_boxes(), {{965, -10, 965}, {1035, 35, 1035}});
    REQUIRE(shelter.sheltered(streak({1000, 1, 1000})));
    REQUIRE(!shelter.sheltered(streak({0, 1, 0})));
    apricot_test::pass("cover refresh follows tall roofs and teleports and excludes disabled/vehicle boxes");
}

void authored_cloggers_roof_shelters_the_whole_room() {
    TerrainCollider collider{city::kMapSeed};
    const auto& site = city::kFastFoodSite;
    const auto world = [&](glm::vec3 p) {
        return glm::vec3{site.origin.x + site.cos_yaw * p.x + site.sin_yaw * p.z,
                         site.ground_m + p.y,
                         site.origin.z - site.sin_yaw * p.x + site.cos_yaw * p.z};
    };
    std::vector<StaticBox> roofs;
    for (const auto& part : city::bake_building(city::kFastFoodPlan)) {
        Transform transform;
        transform.position = world({part.centre.x, part.bottom_m + part.height_m * .5f, part.centre.z});
        transform.rotation = glm::angleAxis(std::atan2(site.sin_yaw, site.cos_yaw), glm::vec3{0, 1, 0}) *
            glm::quat(glm::radians(glm::vec3{part.pitch_deg, part.yaw_deg, part.roll_deg}));
        transform.scale = {part.width_m, part.height_m, part.depth_m};
        city::append_precipitation_cover(part, transform, roofs);
        if (!part.solid) continue;
        REQUIRE(part.pitch_deg == 0 && part.roll_deg == 0);
        collider.add_static_oriented_box(
            world({part.centre.x, part.bottom_m + part.height_m * .5f, part.centre.z}),
            {part.width_m * .5f, part.height_m * .5f, part.depth_m * .5f},
            std::atan2(site.sin_yaw, site.cos_yaw) + glm::radians(part.yaw_deg));
    }
    PrecipitationShelter shelter;
    const glm::vec3 centre = world({-5, 2, 3});
    shelter.rebuild(collider.static_boxes(), {centre - glm::vec3{40}, centre + glm::vec3{40}}, roofs);
    for (float x = -19; x <= 9; x += 1.f) {
        for (float z = -4; z <= 10; z += 1.f) {
            for (float y : {1.f, 2.f, 3.5f})
                REQUIRE(shelter.sheltered(streak(world({x, y, z}))));
        }
    }
    REQUIRE(!shelter.sheltered(streak(world({-5, 7, 3}))));
    REQUIRE(!shelter.sheltered(streak(world({-5, 2, -12}))));
    apricot_test::pass("actual Cloggers roof pieces shelter the room and leave rain above/outside visible");
}

bool inside_half_open(float v, float centre, float span) {
    const float lo = centre - span * 0.5f;
    return v >= lo && v < lo + span;
}

void wrapping_always_lands_inside_the_half_open_window() {
    const float centre = 12.5f;
    const float span = 40.0f;

    const float probes[] = {
        12.5f,      // already centred
        -7.5f,      // exactly on the low edge
        32.5f,      // exactly on the high edge (must wrap to the low one)
        32.4999f,
        -1000.0f,   // twenty-five spans below
        1.0e6f,     // twenty-five THOUSAND spans above: a lag spike, or a
        -1.0e6f,    // camera that teleported
        0.0f,
    };

    for (const float p : probes) {
        const float w = wrap_into_span(p, centre, span);
        REQUIRE_MSG(inside_half_open(w, centre, span),
                    "wrap must land in [centre-span/2, centre+span/2)", "probe");
        // And it must differ from the input only by whole spans, or the drop
        // has been teleported rather than recycled.
        const float k = (p - w) / span;
        REQUIRE_MSG(std::fabs(k - std::round(k)) < 1e-2f,
                    "wrap must shift by a whole number of spans", "probe");
    }

    // The high edge specifically wraps to the low one, which is what makes the
    // interval half-open rather than closed-and-hoping.
    REQUIRE(wrap_into_span(32.5f, centre, span) < 32.5f);
    apricot_test::pass("wrap holds its half-open postcondition on every input");
}

void a_degenerate_span_is_returned_untouched() {
    // A zero or negative box has no inside to wrap into. Returning the value
    // beats dividing by zero and filling the field with NaN, which propagates
    // into the vertex buffer and takes the whole pass down with it.
    REQUIRE(wrap_into_span(7.0f, 0.0f, 0.0f) == 7.0f);
    REQUIRE(wrap_into_span(7.0f, 0.0f, -3.0f) == 7.0f);
    apricot_test::pass("a degenerate span cannot produce NaN");
}

void drops_stay_in_the_box_however_long_the_frame_was() {
    RainTuning t;
    const glm::vec3 cam{100.0f, 25.0f, -40.0f};
    const glm::vec3 centre = cam + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};

    // Every dt from a 240 Hz frame to a debugger held for twenty seconds.
    const float deltas[] = {1.0f / 240.0f, 1.0f / 60.0f, 0.1f, 1.0f, 20.0f, 600.0f};

    for (const float dt : deltas) {
        glm::vec3 p = rain_seed_position(t, cam, 0xFEEDu, 7);
        for (int step = 0; step < 40; ++step) {
            p = rain_advance(p, t, cam, dt);
            REQUIRE_MSG(inside_half_open(p.x, centre.x, t.span.x),
                        "drop escaped the box on X", "advance");
            REQUIRE_MSG(inside_half_open(p.y, centre.y, t.span.y),
                        "drop escaped the box on Y", "advance");
            REQUIRE_MSG(inside_half_open(p.z, centre.z, t.span.z),
                        "drop escaped the box on Z", "advance");
        }
    }
    apricot_test::pass("drops survive a 600-second frame without escaping");
}

void the_field_follows_a_camera_that_teleported() {
    RainTuning t;
    glm::vec3 p = rain_seed_position(t, glm::vec3{0.0f}, 0xABCDu, 3);

    // Cross the world in one frame. The next advance must pull the drop into
    // the new box; a field that only wraps on Y would leave it 5 km behind and
    // the player would drive out of the rain.
    const glm::vec3 far_away{5000.0f, -300.0f, -8000.0f};
    p = rain_advance(p, t, far_away, 1.0f / 60.0f);

    const glm::vec3 centre = far_away + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};
    REQUIRE(inside_half_open(p.x, centre.x, t.span.x));
    REQUIRE(inside_half_open(p.y, centre.y, t.span.y));
    REQUIRE(inside_half_open(p.z, centre.z, t.span.z));
    apricot_test::pass("the field catches up with a teleported camera in one step");
}

void a_dry_sky_costs_exactly_nothing() {
    RainTuning t;
    REQUIRE_MSG(rain_drop_count(t, 0.0f) == 0, "zero intensity means zero drops",
                "dry");
    REQUIRE_MSG(rain_drop_count(t, -1.0f) == 0, "negative clamps to dry", "dry");

    // And once it is raining at all, there is something to see.
    REQUIRE(rain_drop_count(t, 0.001f) >= 1);
    REQUIRE(rain_drop_count(t, 1.0f) == t.drop_count);
    REQUIRE(rain_drop_count(t, 2.0f) == t.drop_count);   // clamped, not scaled
    REQUIRE(rain_drop_count(t, 0.5f) < t.drop_count);
    apricot_test::pass("dry is free; intensity scales the count and clamps");
}

void the_field_is_deterministic() {
    RainTuning t;
    const glm::vec3 cam{3.0f, 9.0f, -2.0f};
    for (int i = 0; i < 50; ++i) {
        const glm::vec3 a = rain_seed_position(t, cam, 0x1234u, i);
        const glm::vec3 b = rain_seed_position(t, cam, 0x1234u, i);
        REQUIRE_MSG(a == b, "seeding must be pure in (seed, index)", "determinism");
    }
    // Different indices must not all land in the same place, which is what a
    // hash used wrong looks like.
    REQUIRE(rain_seed_position(t, cam, 0x1234u, 0) !=
            rain_seed_position(t, cam, 0x1234u, 1));
    apricot_test::pass("drop seeding is pure and actually varies");
}

void the_fall_direction_is_down_and_normalised() {
    RainTuning t;
    const glm::vec3 d = rain_fall_dir(t);
    REQUIRE_NEAR(static_cast<double>(glm::length(d)), 1.0, 1e-5);
    REQUIRE_MSG(d.y < -0.9f, "rain falls down", "fall");
    REQUIRE_MSG(d.x > 0.0f, "and slants with the wind", "fall");
    apricot_test::pass("rain falls down, on a slant, at unit speed");
}

void snow_and_blizzard_are_explicitly_different() {
    const SnowTuning snow = default_snow_tuning(PrecipitationType::Snow);
    const SnowTuning blizzard = default_snow_tuning(PrecipitationType::Blizzard);
    REQUIRE(blizzard.flake_count > snow.flake_count);
    REQUIRE(blizzard.fall_speed > snow.fall_speed);
    REQUIRE(glm::length(blizzard.wind) > glm::length(snow.wind));
    REQUIRE(snow_flake_count(snow, 0.0f) == 0);
    REQUIRE(snow_flake_count(snow, -2.0f) == 0);
    REQUIRE(snow_flake_count(snow, 1.0f) == snow.flake_count);
    REQUIRE(snow_flake_count(snow, 2.0f) == snow.flake_count);
    apricot_test::pass("snow and blizzard have explicit tuning; dry still costs zero");
}

void snow_motion_is_deterministic_and_drifts() {
    const SnowTuning t = default_snow_tuning(PrecipitationType::Snow);
    const glm::vec3 a = snow_displacement(t, 0x51A0u, 17, 3.25f, 0.5f);
    const glm::vec3 b = snow_displacement(t, 0x51A0u, 17, 3.25f, 0.5f);
    REQUIRE(a == b);
    REQUIRE(a.y < 0.0f);
    REQUIRE(std::fabs(a.x) > 1e-5f || std::fabs(a.z) > 1e-5f);
    REQUIRE(snow_displacement(t, 0x51A0u, 17, 3.25f, 0.0f) == glm::vec3{0.0f});
    apricot_test::pass("snow movement is deterministic, downward, and laterally alive");
}

void snow_step_partition_preserves_analytic_motion() {
    const SnowTuning t = default_snow_tuning(PrecipitationType::Snow);
    const glm::vec3 whole = snow_displacement(t, 0xF1A4u, 8, 2.0f, 1.0f);
    const glm::vec3 split = snow_displacement(t, 0xF1A4u, 8, 2.0f, 0.4f) +
                            snow_displacement(t, 0xF1A4u, 8, 2.4f, 0.6f);
    REQUIRE_NEAR(static_cast<double>(glm::length(whole - split)), 0.0, 1e-5);
    apricot_test::pass("snow drift does not change when a frame is partitioned");
}

void snow_stays_camera_locked_after_large_steps() {
    const SnowTuning t = default_snow_tuning(PrecipitationType::Blizzard);
    const glm::vec3 cam{4200.0f, 85.0f, -9100.0f};
    const glm::vec3 centre = snow_field_centre(t, cam);
    glm::vec3 p = snow_seed_position(t, glm::vec3{0.0f}, 0xB112u, 4,
                                     PrecipitationType::Blizzard);
    p = snow_advance(p, t, cam, 0xB112u, 4, 0.0f, 600.0f,
                     PrecipitationType::Blizzard);
    REQUIRE(inside_half_open(p.x, centre.x, t.span.x));
    REQUIRE(inside_half_open(p.y, centre.y, t.span.y));
    REQUIRE(inside_half_open(p.z, centre.z, t.span.z));
    apricot_test::pass("blizzard flakes survive lag and camera teleport together");
}

}  // namespace

int main() {
    roofs_block_indoor_particles_but_keep_outdoor_rain();
    rotated_cover_does_not_fill_its_empty_aabb_corners();
    shelter_refresh_handles_tall_roofs_disabled_props_and_teleports();
    authored_cloggers_roof_shelters_the_whole_room();
    wrapping_always_lands_inside_the_half_open_window();
    a_degenerate_span_is_returned_untouched();
    drops_stay_in_the_box_however_long_the_frame_was();
    the_field_follows_a_camera_that_teleported();
    a_dry_sky_costs_exactly_nothing();
    the_field_is_deterministic();
    the_fall_direction_is_down_and_normalised();
    snow_and_blizzard_are_explicitly_different();
    snow_motion_is_deterministic_and_drifts();
    snow_step_partition_preserves_analytic_motion();
    snow_stays_camera_locked_after_large_steps();
    return apricot_test::done("rain_field_tests");
}

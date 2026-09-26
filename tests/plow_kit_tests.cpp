// The plow trucks: a front plow and a roof service light bar fitted to the
// real cooked Rodeo Grazer and Harrow Workman bodies, the blade numbers the
// sim reads, and a real clearance field under a real driven plow.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/plow_kit.h"
#include "app/player_car_catalog.h"
#include "app/vehicle_driver_door.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_paint_catalog.h"
#include "app/vehicle_registration.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "game/plow_blade.h"
#include "game/snow_clearance.h"
#include "physics/terrain_collider.h"
#include "road/lane_graph.h"
#include "traffic/crowd.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

constexpr PlayerCarId kPlows[] = {PlayerCarId::RodeoGrazerPlow, PlayerCarId::HarrowWorkmanPlow};

struct Fitted {
    StaticEmesh body;
    VehicleTuning tuning;
    Transform body_local;
    PlowKitMeshes kit;
    std::vector<glm::vec3> samples;
};

Fitted fit(PlayerCarId id) {
    Fitted out;
    const auto& definition = player_car_definition(id);
    REQUIRE_MSG(read_static_emesh(asset_path(definition.mesh_path), out.body),
                "the base truck's cooked body is missing", definition.mesh_path);
    out.tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    out.body_local = player_car_body_transform(definition, out.tuning);
    out.kit = make_plow_kit(out.body, player_car_source_ground_y(definition, out.tuning),
                            1.0f / out.body_local.scale, plow_kit_spec(id));
    out.samples = plow_kit_detail::surface_samples(out.body);
    return out;
}

float min_y(const MeshData& mesh) {
    float y = 1e9f;
    for (const auto& v : mesh.vertices) y = std::min(y, v.position.y);
    return y;
}

void variants_drive_their_base_trucks() {
    REQUIRE(static_cast<unsigned>(PlayerCarId::RodeoGrazerPlow) == 38u);  // append-only ids
    REQUIRE(static_cast<unsigned>(PlayerCarId::HarrowWorkmanPlow) == 39u);
    for (const auto id : kPlows) {
        const PlayerCarId base = player_car_body_id(id);
        REQUIRE(base != id && !has_plow_kit(base) && has_plow_kit(id));
        const auto& row = player_car_definition(id);
        const auto& base_row = player_car_definition(base);
        REQUIRE(row.id == id);
        REQUIRE(std::strcmp(row.brand, base_row.brand) == 0);
        REQUIRE(std::strcmp(row.mesh_path, base_row.mesh_path) == 0);
        REQUIRE(std::strcmp(player_car_body_texture_path(id), player_car_body_texture_path(base)) == 0);
        REQUIRE(row.arch_centre_y == base_row.arch_centre_y && row.wheel_x == base_row.wheel_x &&
                row.wheel_front_z == base_row.wheel_front_z && row.wheel_rear_z == base_row.wheel_rear_z);
        REQUIRE(has_animated_driver(id) == has_animated_driver(base));
        REQUIRE(has_passenger_door(id) == has_passenger_door(base));
        REQUIRE(&vehicle_driver_layout(id) == &vehicle_driver_layout(base));
        REQUIRE(player_plate_use(id) == PlateUse::Commercial);
        REQUIRE(player_car_variant_key(id) != nullptr);
        REQUIRE(player_car_brand_index(id) == player_car_brand_index(base));
        // Same body, same chassis box; the profile is the variant's own.
        const auto a = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
        const auto b = player_model_tuning(DrivingMechanicsStyle::ClassicGta, base);
        REQUIRE(a.chassis_half_width == b.chassis_half_width);
        REQUIRE(a.car_collision_half_length == b.car_collision_half_length);
        REQUIRE(a.mass_kg > b.mass_kg + 250.0f);
        REQUIRE(a.car_collision_front_extension > 0.5f && b.car_collision_front_extension == 0.0f);
        REQUIRE(a.car_collision_half_width >= plow_kit_numbers(id).blade.half_width_m);
    }
    REQUIRE(std::string(player_car_variant_key(PlayerCarId::RodeoGrazerPlow)) == "rodeo_grazer_plow");
    REQUIRE(std::string(player_car_variant_key(PlayerCarId::HarrowWorkmanPlow)) == "harrow_workman_plow");
    pass("plow variants drive their base truck's body, seat, doors and paint, with their own tune");
}

void kit_fits_the_real_cooked_body() {
    for (const auto id : kPlows) {
        const Fitted f = fit(id);
        const char* name = player_car_definition(id).model;
        const PlowKitFit& fit_ = f.kit.fit;
        REQUIRE_MSG(fit_.valid, "the kit could not measure the cooked body", name);
        std::printf("      %s: bumper z %.3f y %.3f..%.3f, hood %.3f, roof %.3f (z %.3f..%.3f, half x %.3f), ground %.3f\n",
                    name, static_cast<double>(fit_.bumper_front_z), static_cast<double>(fit_.bumper_bottom_y),
                    static_cast<double>(fit_.bumper_top_y), static_cast<double>(fit_.hood_front_y),
                    static_cast<double>(fit_.roof_y), static_cast<double>(fit_.roof_rear_z),
                    static_cast<double>(fit_.roof_front_z), static_cast<double>(fit_.roof_half_x),
                    static_cast<double>(fit_.ground_y));
        for (std::size_t p = 0; p < kPlowPartCount; ++p) {
            REQUIRE_MSG(!f.kit.parts[p].vertices.empty(), "a kit part is empty", name);
            for (const auto& v : f.kit.parts[p].vertices) {
                REQUIRE(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
                REQUIRE_NEAR(glm::length(v.normal), 1.0f, 1e-3f);
                REQUIRE(v.material_weights == glm::vec4{0.0f});
                REQUIRE(v.uv.x >= 0.0f && v.uv.x <= 1.0f && v.uv.y >= 0.0f && v.uv.y <= 1.0f);
            }
            REQUIRE(f.kit.parts[p].indices.size() % 3u == 0u);
        }

        // THE CUTTING EDGE IS ON THE ROAD, not hovering and not buried.
        const float edge = min_y(f.kit.parts[plow_part(PlowPart::BladeSteel)]);
        REQUIRE_MSG(edge >= fit_.ground_y && edge <= fit_.ground_y + 0.015f,
                    "the cutting edge is not at road level", name);

        // NOTHING IN FRONT OF THE TRUCK CLIPS INTO IT. Every kit vertex that
        // stands at the nose must be ahead of the body surface at its own
        // height and offset: the headgear clears the grille and bumper, and
        // the A-frame and blade are wholly ahead of it.
        const float nose_top = fit_.hood_front_y + 0.6f;
        std::size_t checked = 0;
        for (std::size_t p = 0; p < kPlowPartCount; ++p) {
            if (p >= plow_part(PlowPart::BarLens0)) continue;  // the roof bar is checked below
            for (const auto& v : f.kit.parts[p].vertices) {
                if (v.position.y < fit_.bumper_bottom_y || v.position.y > nose_top) continue;
                const float body_front = plow_kit_front_z(f.samples, v.position.x - 0.02f, v.position.x + 0.02f,
                                                          v.position.y - 0.02f, v.position.y + 0.02f);
                if (body_front < -1e8f) continue;
                if (p == plow_part(PlowPart::MountFrame) && v.position.y > fit_.roof_y - 0.3f) continue;
                ++checked;
                if (!(v.position.z > body_front + 0.004f))
                    std::printf("      clip: %s part %zu at (%.3f %.3f %.3f), body front %.3f\n", name, p,
                                static_cast<double>(v.position.x), static_cast<double>(v.position.y),
                                static_cast<double>(v.position.z), static_cast<double>(body_front));
                REQUIRE_MSG(v.position.z > body_front + 0.004f, "a kit part clips into the nose", name);
            }
        }
        REQUIRE(checked > 500u);

        // THE MOUNT IS BOLTED TO THE TRUCK: each push beam's rear end either
        // butts against the body behind it or runs on under it.
        const auto& mount = f.kit.parts[plow_part(PlowPart::MountFrame)];
        for (const float side : {-1.0f, 1.0f}) {
            float rearmost = 1e9f;
            glm::vec3 rear_point{0.0f};
            for (const auto& v : mount.vertices) {
                if (v.position.y < fit_.bumper_bottom_y && v.position.x * side > 0.3f &&
                    v.position.z < rearmost) {
                    rearmost = v.position.z;
                    rear_point = v.position;
                }
            }
            REQUIRE_MSG(rearmost < fit_.bumper_front_z, "a push beam stops short of the bumper", name);
            bool butts = false;
            float above = 1e9f;
            for (const auto& q : f.samples) {
                if (std::fabs(q.x - rear_point.x) > 0.06f) continue;
                if (std::fabs(q.y - rear_point.y) < 0.08f && q.z > rear_point.z - 0.04f &&
                    q.z < rear_point.z + 0.01f) butts = true;
                if (std::fabs(q.z - rear_point.z) < 0.08f && q.y > rear_point.y)
                    above = std::min(above, q.y);
            }
            std::printf("      %s: push beam ends at z %.3f (bumper %.3f), %s\n", name,
                        static_cast<double>(rearmost), static_cast<double>(fit_.bumper_front_z),
                        butts ? "against the body" : "under it");
            REQUIRE_MSG(butts || above < rear_point.y + 0.25f, "a push beam floats clear of the truck", name);
        }

        // THE LIGHT BAR SITS ON THE ROOF: each foot pad rests on the roof
        // skin under it, and the bar clears the roof everywhere.
        for (const auto& foot : f.kit.bar_feet_xz) {
            const float roof = plow_kit_surface_y(f.samples, foot.x, foot.y, 0.07f);
            float pad = 1e9f;
            for (const auto& v : mount.vertices)
                if (std::fabs(v.position.x - foot.x) < 0.06f && std::fabs(v.position.z - foot.y) < 0.09f &&
                    v.position.y > fit_.roof_y - 0.25f)
                    pad = std::min(pad, v.position.y);
            REQUIRE_MSG(std::fabs(pad - roof) < 0.012f, "a light-bar foot is not on the roof", name);
        }
        for (std::size_t g = 0; g < kPlowBarLensGroups; ++g) {
            for (const auto& v : f.kit.parts[plow_part(PlowPart::BarLens0) + g].vertices) {
                if (v.position.y < fit_.roof_y - 0.25f) continue;  // the amber strips in the plow lamps
                const float roof = plow_kit_surface_y(f.samples, v.position.x, v.position.z, 0.05f);
                REQUIRE_MSG(v.position.y > roof + 0.03f, "a light-bar lens sits in the roof", name);
            }
        }
        REQUIRE(f.kit.bar_centre.y > fit_.roof_y);
        // The plow lamps stand above the hood so they shine over a raised blade.
        for (const auto& lamp : f.kit.lamp_centres) REQUIRE(lamp.y > fit_.hood_front_y + 0.1f);

        // THE BLADE IS THE SPECIFIED WIDTH ON THE ROAD, and wider than the
        // truck's tyres, so the wheels run in the path it cleared.
        const PlowKitSpec spec = plow_kit_spec(id);
        const PlowBladeMount mount_numbers = plow_blade_mount(f.kit, f.body_local);
        REQUIRE_NEAR(mount_numbers.half_width_m * 2.0f, spec.blade_width_m, 0.002f);
        REQUIRE(mount_numbers.half_width_m > f.tuning.half_track + 0.14f);
        REQUIRE(mount_numbers.lift_m > 0.2f && mount_numbers.lift_m < 0.3f);
        std::printf("      %s: edge %.3f ahead, %.3f down, half width %.3f, lift %.3f; reach %.3f (body half length %.3f)\n",
                    name, static_cast<double>(mount_numbers.edge_forward_m),
                    static_cast<double>(mount_numbers.edge_drop_m),
                    static_cast<double>(mount_numbers.half_width_m), static_cast<double>(mount_numbers.lift_m),
                    static_cast<double>(plow_kit_reach_m(f.kit, f.body_local)),
                    static_cast<double>(f.tuning.car_collision_half_length));
        std::size_t triangles = 0;
        for (const auto& part : f.kit.parts) triangles += part.indices.size() / 3u;
        std::printf("      %s: %zu kit triangles\n", name, triangles);
        REQUIRE(triangles < 60000u);
    }
    pass("the kit fits the real cooked Grazer and Workman: edge on the road, nothing clipping, bar on the roof");
}

// THE SIM'S BLADE IS THE BLADE THAT DRAWS. plow_kit_numbers() is what the sim
// reads without a mesh; hold it to the kit fitted on the real body.
void pinned_numbers_match_the_fitted_kit() {
    for (const auto id : kPlows) {
        const Fitted f = fit(id);
        const char* name = player_car_definition(id).model;
        const PlowBladeMount measured = plow_blade_mount(f.kit, f.body_local);
        const PlowKitNumbers pinned = plow_kit_numbers(id);
        REQUIRE_MSG(std::fabs(measured.edge_forward_m - pinned.blade.edge_forward_m) < 0.01f,
                    "plow_kit_numbers edge_forward_m no longer matches the fitted kit", name);
        // The drop follows the ride height of every driving style.
        for (std::size_t s = 0; s < kDrivingMechanicsStyleCount; ++s) {
            const auto style = static_cast<DrivingMechanicsStyle>(s);
            const VehicleTuning t = player_model_tuning(style, id);
            const Transform local = player_car_body_transform(player_car_definition(id), t);
            const PlowBladeMount styled = plow_blade_mount(f.kit, local);
            REQUIRE_MSG(std::fabs(styled.edge_drop_m - plow_blade_mount_for(id, t).edge_drop_m) < 0.01f,
                        "the blade edge is not on the road under this style's ride height", name);
            REQUIRE_NEAR(styled.edge_forward_m, measured.edge_forward_m, 1e-4f);
        }
        REQUIRE_MSG(std::fabs(measured.half_width_m - pinned.blade.half_width_m) < 0.01f,
                    "plow_kit_numbers half_width_m no longer matches the fitted kit", name);
        REQUIRE_MSG(std::fabs(measured.lift_m - pinned.blade.lift_m) < 0.01f,
                    "plow_kit_numbers lift_m no longer matches the fitted kit", name);
        const float reach = plow_kit_reach_m(f.kit, f.body_local);
        REQUIRE_MSG(std::fabs(reach - pinned.reach_m) < 0.01f,
                    "plow_kit_numbers reach_m no longer matches the fitted kit", name);
        // The car-car footprint's front face is the blade's front face.
        REQUIRE_NEAR(f.tuning.car_collision_half_length + f.tuning.car_collision_front_extension,
                     pinned.reach_m, 1e-4f);
    }
    pass("the sim's pinned blade mount and collision reach are the fitted blade's, to 1 cm");
}

// A REAL PLOW PASS: the real plow tune stepped by the real vehicle physics on
// real ground, its real blade edge fed through PlowSweep into the real
// clearance field. A straight push is one strip; the ground either side of
// the blade keeps its snow; a raised blade clears nothing.
void a_driven_plow_clears_its_blade_width() {
    const PlayerCarId id = PlayerCarId::RodeoGrazerPlow;
    const VehicleTuning tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    const PlowBladeMount blade = plow_blade_mount_for(id, tuning);
    TerrainCollider ground(0x5EEDull);
    ground.add_static_ground_rect({0.0f, 0.0f}, 50.0f, {400.0f, 400.0f}, 0.0f, Surface::Rock);
    VehicleState car = spawn_vehicle(tuning, ground, 0.0f, 0.0f, 0.0f);
    car.position.y = 50.0f + static_ride_height(tuning);
    SnowClearanceField field;
    PlowSweep sweep;
    PlowBladeState state;
    constexpr float kDepth = 0.18f;
    constexpr float kDt = 1.0f / 120.0f;
    const auto edge_world = [&](const VehicleState& s) {
        return s.position + s.orientation * blade.edge_local(state.raised);
    };
    InputFrame input;
    std::vector<glm::vec3> path;
    for (int step = 0; step < 120 * 6; ++step) {
        // Push, then stop on the handbrake: the service brake at a standstill
        // is reverse, and this pass is about going forward.
        input.throttle = step < 120 * 4 ? 0.45f : 0.0f;
        input.handbrake = step < 120 * 4 ? 0.0f : 1.0f;
        car = step_vehicle(car, tuning, input, ground, kDt);
        state.step(kDt);
        const glm::vec3 edge = edge_world(car);
        sweep.step(edge, vehicle_speed(car), state.scraping(),
                   blade.half_width_m, field, kDepth);
        if (step % 30 == 0) path.push_back(edge);
    }
    const glm::vec3 start = path.front(), end = path.back();
    const float travelled = glm::length(glm::vec2{end.x - start.x, end.z - start.z});
    std::printf("      straight push: %.1f m, %zu strips, %.1f m swept\n", static_cast<double>(travelled),
                field.strips().size(), static_cast<double>(sweep.cleared_distance_m()));
    REQUIRE(travelled > 15.0f);
    REQUIRE(field.strips().size() <= 2u);
    REQUIRE(sweep.cleared_distance_m() > travelled - 1.0f);
    const glm::vec3 fwd = glm::normalize(end - start);
    const glm::vec3 right{-fwd.z, 0.0f, fwd.x};
    for (float t : {0.15f, 0.5f, 0.85f}) {
        const glm::vec3 p = glm::mix(start, end, t);
        REQUIRE(field.depth_at(p.x, p.y, p.z, kDepth) < 0.01f);
        for (float side : {-1.0f, 1.0f}) {
            const glm::vec3 inside = p + right * (side * (blade.half_width_m - 0.05f));
            const glm::vec3 outside = p + right * (side * (blade.half_width_m + 0.10f));
            REQUIRE(field.depth_at(inside.x, inside.y, inside.z, kDepth) < 0.01f);
            REQUIRE(field.depth_at(outside.x, outside.y, outside.z, kDepth) == kDepth);
        }
    }

    // Turning with the blade down: chords, not a strip per step.
    const std::size_t before = field.strips().size();
    for (int step = 0; step < 120 * 8; ++step) {
        input.throttle = 0.35f;
        input.handbrake = 0.0f;
        input.steer = 0.8f;
        car = step_vehicle(car, tuning, input, ground, kDt);
        sweep.step(edge_world(car), vehicle_speed(car), state.scraping(),
                   blade.half_width_m, field, kDepth);
    }
    const std::size_t turn_strips = field.strips().size() - before;
    std::printf("      turning push: %zu strips for %.1f m swept in total\n", turn_strips,
                static_cast<double>(sweep.cleared_distance_m()));
    REQUIRE(turn_strips > 3u && turn_strips < 60u);

    // Raise the blade and drive on: nothing more is cleared.
    state.lowered = false;
    const float swept = sweep.cleared_distance_m();
    for (int step = 0; step < 120 * 3; ++step) {
        input.steer = 0.0f;
        car = step_vehicle(car, tuning, input, ground, kDt);
        state.step(kDt);
        sweep.step(edge_world(car), vehicle_speed(car), state.scraping(),
                   blade.half_width_m, field, kDepth);
    }
    REQUIRE(state.raised == 1.0f && !state.scraping());
    // The strip being laid ended the moment the blade left the road.
    REQUIRE(sweep.cleared_distance_m() - swept < 0.05f);
    REQUIRE(!sweep.anchored());
    // And a blade that is still travelling down does not scrape yet.
    state.lowered = true;
    state.step(kDt);
    REQUIRE(!state.scraping());
    pass("a real plow pass clears exactly its blade width, in chords, and a raised blade clears nothing");
}

// The blade reaches cars the bare truck would not: traffic stopped just past
// the bumper but inside the blade is hit by the plow truck and not by the
// same truck without its blade.
void the_blade_is_solid_to_traffic() {
    RoadSpine road;
    road.id = 1;
    road.cls = RoadClass::Street;
    road.points = {{0.f, -500.f}, {0.f, 500.f}};
    RoadGraph graph;
    graph.build({road}, {}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(graph, GroundSampler{});
    const auto tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, PlayerCarId::HarrowWorkmanPlow);
    const auto hit = [&](float extension) {
        CrowdTuning ct;
        ct.max_peds = 0;
        Crowd crowd;
        crowd.build(lanes, 0xB1ADEull, {}, ct);
        crowd.refresh(0, {0.f, 0.f});
        REQUIRE(!crowd.vehicles().empty());
        const auto& victim = crowd.vehicles().front();
        const glm::vec3 forward = glm::normalize(glm::vec3{victim.fwd.x, 0.f, victim.fwd.z});
        const auto footprint = traffic_vehicle_footprint(traffic_vehicle_kind(victim));
        VehicleState player;
        // Nose-to-tail behind it: the truck's bumper 0.3 m short of the car.
        player.position = victim.pos - forward * (footprint.half_length_m + tuning.car_collision_half_length + 0.3f);
        player.orientation = glm::angleAxis(std::atan2(-forward.x, -forward.z), glm::vec3{0, 1, 0});
        player.velocity = forward * victim.speed_mps;
        return crowd.resolve_player_collision(player, tuning.car_collision_half_width,
                                              tuning.car_collision_half_length, tuning.mass_kg,
                                              tuning.body_damage_gain, extension);
    };
    REQUIRE(!hit(0.0f));
    REQUIRE(hit(tuning.car_collision_front_extension));
    pass("the blade's footprint is solid to traffic the bare truck would miss");
}

// Pushing snow up against a wall is the job. The blade must stop at the wall,
// not sink into it behind the body's own collision circle.
void the_blade_stops_at_a_wall() {
    for (const auto id : kPlows) {
        const VehicleTuning tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
        const PlowBladeMount blade = plow_blade_mount_for(id, tuning);
        TerrainCollider ground(0x5EEDull);
        ground.add_static_ground_rect({0.0f, 0.0f}, 50.0f, {400.0f, 400.0f}, 0.0f, Surface::Rock);
        // A wall across the path 14 m ahead (the truck faces -Z).
        constexpr float kWallZ = -14.0f;
        ground.add_static_box({{-10.0f, 49.0f, kWallZ - 1.0f}, {10.0f, 53.0f, kWallZ}}, Surface::Rock);
        VehicleState car = spawn_vehicle(tuning, ground, 0.0f, 0.0f, 0.0f);
        car.position.y = 50.0f + static_ride_height(tuning);
        InputFrame input;
        input.throttle = 0.5f;
        float deepest = -1e9f;
        for (int step = 0; step < 120 * 8; ++step) {
            car = step_vehicle(car, tuning, input, ground, 1.0f / 120.0f);
            const glm::vec3 edge = car.position + car.orientation * blade.edge_local(0.0f);
            deepest = std::max(deepest, kWallZ - edge.z);  // positive once past the face
        }
        std::printf("      %s: blade edge ends %.2f m %s the wall face\n", player_car_definition(id).model,
                    static_cast<double>(std::fabs(deepest)), deepest > 0.0f ? "into" : "short of");
        REQUIRE_MSG(deepest < 0.15f, "the blade sank into a wall", player_car_definition(id).model);
        REQUIRE(car.position.z < -5.0f);  // it did drive up to the wall
    }
    pass("a plow truck driven into a wall stops with its blade at the face");
}

void light_bar_alternates_on_the_sim_step() {
    std::size_t lit_left = 0, lit_right = 0, lit_any = 0;
    for (uint64_t step = 0; step < 60; ++step) {
        const bool left = plow_light_bar_power(step, 0) > 0.5f || plow_light_bar_power(step, 1) > 0.5f;
        const bool right = plow_light_bar_power(step, 2) > 0.5f || plow_light_bar_power(step, 3) > 0.5f;
        REQUIRE(!(left && right));  // the halves alternate, never burn together
        lit_left += left ? 1u : 0u;
        lit_right += right ? 1u : 0u;
        lit_any += left || right ? 1u : 0u;
        for (std::size_t g = 0; g < kPlowBarLensGroups; ++g)
            REQUIRE(plow_light_bar_power(step, g) == plow_light_bar_power(step + 60u, g));
    }
    REQUIRE(lit_left == lit_right);
    // Lit often enough to read in a still frame, dark often enough to flash.
    REQUIRE(lit_any >= 24u && lit_any <= 36u);
    pass("the service light bar double-flashes left then right on the sim step, twice a second");
}

}  // namespace

int main() {
    std::printf("plow_kit_tests\n");
    variants_drive_their_base_trucks();
    kit_fits_the_real_cooked_body();
    pinned_numbers_match_the_fitted_kit();
    a_driven_plow_clears_its_blade_width();
    the_blade_is_solid_to_traffic();
    the_blade_stops_at_a_wall();
    light_bar_alternates_on_the_sim_step();
    return apricot_test::done("plow_kit_tests");
}

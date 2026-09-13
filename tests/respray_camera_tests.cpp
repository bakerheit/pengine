// The respray booth's bay camera, scored against Rook's real baked geometry.
//
// The collider is a real TerrainCollider(kMapSeed) with every solid piece of
// bake_auto_repair() added as an oriented box, the call World makes for
// Rook's (and the bank_vault_tests pattern). The cars are real spawn_vehicle
// states with real player_model_tuning rows. The camera the checks project
// through is retyped from gfx/camera.cpp and App::update_camera rather than
// borrowed from the header under test.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/respray_camera.h"
#include "app/vehicle_model_tuning.h"
#include "city/map.h"
#include "city/neighborhood_shops.h"
#include "game/repair_shop.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {

const city::StartSite& rooks() { return city::kAutoRepairSite; }

glm::vec3 site_world(float x, float y, float z) {
    const auto& s = rooks();
    return {s.origin.x + s.cos_yaw * x + s.sin_yaw * z, s.ground_m + y,
            s.origin.z - s.sin_yaw * x + s.cos_yaw * z};
}
glm::vec3 site_local(glm::vec3 w) {
    const auto& s = rooks();
    const float x = w.x - s.origin.x;
    const float z = w.z - s.origin.z;
    return {s.cos_yaw * x - s.sin_yaw * z, w.y - s.ground_m, s.sin_yaw * x + s.cos_yaw * z};
}

void add_rooks(TerrainCollider& collider) {
    int solids = 0;
    for (const auto& p : city::bake_auto_repair()) {
        if (!p.solid) continue;
        REQUIRE(p.pitch_deg == 0.0f && p.roll_deg == 0.0f);
        collider.add_static_oriented_box(
            site_world(p.centre.x, p.bottom_m + p.height_m * 0.5f, p.centre.z),
            glm::vec3{p.width_m, p.height_m, p.depth_m} * 0.5f,
            std::atan2(rooks().sin_yaw, rooks().cos_yaw) + glm::radians(p.yaw_deg));
        ++solids;
    }
    // Walls, lintels, divider, four lift columns, fixtures, fences, tyres.
    REQUIRE(solids >= 25);
}

const city::BuildingPiece& rooks_piece(const std::vector<city::BuildingPiece>& parts,
                                       const char* name) {
    for (const auto& p : parts)
        if (std::strcmp(p.name, name) == 0) return p;
    REQUIRE_MSG(false, "Rook's lost a piece this suite measures against", name);
    return parts.front();
}

// A baked piece's site-local bounds. Rook's solids are yawed by whole quarter
// turns at most, so the rotated footprint is exact.
struct LocalBox {
    float x0, x1, y0, y1, z0, z1;
};
LocalBox piece_box(const city::BuildingPiece& p) {
    const float c = std::fabs(std::cos(glm::radians(p.yaw_deg)));
    const float s = std::fabs(std::sin(glm::radians(p.yaw_deg)));
    const float hx = 0.5f * (c * p.width_m + s * p.depth_m);
    const float hz = 0.5f * (s * p.width_m + c * p.depth_m);
    return {p.centre.x - hx, p.centre.x + hx, p.bottom_m, p.bottom_m + p.height_m,
            p.centre.z - hz, p.centre.z + hz};
}

// Where a straight-parked car centred at site-local `lane_x` can stand: just
// inside the front of repair_shop_ready's zone, halfway, and as deep as its
// lane lets a car go. The zone runs to local z -14.5, but Rook's workbench and
// tool chest stand across the back of the lanes, and a pose inside one of them
// is a pose no player can reach. An earlier draft of this suite tested exactly
// that pose and read the workbench hiding the car's own rear corners as a
// camera bug.
std::array<float, 3> bay_poses_z(const std::vector<city::BuildingPiece>& parts,
                                 const VehicleTuning& tuning, float lane_x) {
    const float hw = tuning.car_collision_half_width;
    const float hl = tuning.car_collision_half_length;
    const float front_z = 3.5f - hl - 0.25f;
    float back = -14.5f;
    for (const auto& p : parts) {
        if (!p.solid) continue;
        const LocalBox b = piece_box(p);
        if (b.x1 > lane_x - hw && b.x0 < lane_x + hw && b.z1 <= front_z - hl)
            back = std::max(back, b.z1);
    }
    const float deep_z = back + 0.25f + hl;
    REQUIRE(deep_z <= front_z);
    return {front_z, 0.5f * (front_z + deep_z), deep_z};
}

void require_clear_of_fixtures(const std::vector<city::BuildingPiece>& parts,
                               const VehicleState& car, const VehicleTuning& tuning) {
    const glm::vec3 c = site_local(car.position);
    const float hw = tuning.car_collision_half_width;
    const float hl = tuning.car_collision_half_length;
    for (const auto& p : parts) {
        if (!p.solid) continue;
        const LocalBox b = piece_box(p);
        const bool overlaps = b.x1 > c.x - hw && b.x0 < c.x + hw && b.z1 > c.z - hl &&
                              b.z0 < c.z + hl && b.y1 > c.y + tuning.chassis_floor &&
                              b.y0 < c.y + tuning.chassis_roof;
        REQUIRE_MSG(!overlaps, "the test car stands inside a solid piece of Rook's", p.name);
    }
}

VehicleState spawn_in_rooks(const VehicleTuning& tuning, const TerrainCollider& ground,
                            float x, float z) {
    const glm::vec3 w = site_world(x, 0.0f, z);
    return spawn_vehicle(tuning, ground, w.x, w.z,
                         std::atan2(rooks().sin_yaw, rooks().cos_yaw));
}

// The body box, restated: car footprint between chassis floor and roof.
std::array<glm::vec3, 8> body_box(const VehicleState& car, const VehicleTuning& t) {
    std::array<glm::vec3, 8> out{};
    std::size_t n = 0;
    for (float x : {-t.car_collision_half_width, t.car_collision_half_width})
        for (float y : {t.chassis_floor, t.chassis_roof})
            for (float z : {-t.car_collision_half_length, t.car_collision_half_length})
                out[n++] = car.position + car.orientation * glm::vec3{x, y, z};
    return out;
}

// gfx/camera.cpp Camera::forward/view/projection, fed the way
// App::update_camera turns a pose into yaw and pitch.
glm::mat4 drawn_view_projection(glm::vec3 eye, glm::vec3 target, float fov_deg,
                                float aspect) {
    const glm::vec3 dir = target - eye;
    const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    const float yaw = std::atan2(dir.x, -dir.z);
    const float pitch = std::atan2(dir.y, flat > 1e-4f ? flat : 1e-4f);
    const float cp = std::cos(pitch);
    const glm::vec3 forward{cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)};
    const glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3{0.0f, 1.0f, 0.0f});
    return glm::perspective(glm::radians(fov_deg), aspect, 0.15f, 2000.0f) * view;
}

glm::vec2 ndc(const glm::mat4& vp, glm::vec3 p, float& w) {
    const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    w = clip.w;
    return {clip.x / clip.w, clip.y / clip.w};
}

struct Framing {
    const char* name;
    float aspect;
    float free_fraction;
};
// 16:9 at the picker's reference layout (free region 1780 of 2560 units), and
// the square window the in-game check cannot capture.
constexpr Framing kFramings[] = {
    {"16:9", 16.0f / 9.0f, 1780.0f / 2560.0f},
    {"1:1", 1.0f, 0.46f},
};

struct CarCase {
    const char* name;
    PlayerCarId id;
};
constexpr CarCase kCars[] = {
    {"Vesper Mistral", PlayerCarId::VesperMistral},
    {"Harrow Cityliner", PlayerCarId::HarrowCityliner},
};

bool same_sight(const RespraySight& a, const RespraySight& b) {
    return a.eye == b.eye && a.target == b.target && a.pivot == b.pivot &&
           a.fov_deg == b.fov_deg && a.candidate == b.candidate && a.clear == b.clear;
}

// Everything the plan asks of a chosen sight, checked the way the app will
// consume it.
void check_sight(const TerrainCollider& collider, const VehicleState& car,
                 const VehicleTuning& tuning, float bay, const Framing& framing,
                 const RespraySight& sight, const std::vector<city::BuildingPiece>& parts,
                 const char* label) {
    REQUIRE_MSG(sight.clear, "no bay camera candidate cleared", label);
    REQUIRE(sight.candidate >= 0 &&
            sight.candidate < static_cast<int>(std::size(kRespraySightCandidates)));
    REQUIRE_MSG(sight.fov_deg >= 48.0f - 1e-4f && sight.fov_deg <= 60.0f + 1e-4f,
                "bay camera fov left 48..60 degrees", label);

    const auto corners = body_box(car, tuning);
    glm::vec3 centre{0.0f};
    for (const auto& c : corners) centre += c * 0.125f;
    REQUIRE_NEAR(sight.pivot.x, centre.x, 1e-3);
    REQUIRE_NEAR(sight.pivot.y, centre.y, 1e-3);
    REQUIRE_NEAR(sight.pivot.z, centre.z, 1e-3);
    const auto header_corners = respray_body_corners(car, tuning);
    for (std::size_t i = 0; i < corners.size(); ++i)
        REQUIRE(glm::length(header_corners[i] - corners[i]) < 1e-4f);

    // The obstruction pass, as update_camera runs it: no pull-in at all, and
    // 0.35 m to spare beyond the eye.
    const glm::vec3 ray = sight.eye - sight.pivot;
    const float distance = glm::length(ray);
    REQUIRE(distance > 1.0f);
    const auto app_hit = collider.raycast(sight.pivot, ray, distance);
    REQUIRE_MSG(!(app_hit.hit && app_hit.distance < distance),
                "the obstruction pass would pull the bay camera in", label);
    const auto margin_hit =
        collider.raycast(sight.pivot, ray, distance + kRespraySightRayMarginM);
    REQUIRE_MSG(!margin_hit.hit, "bay camera eye is within 0.35 m of a wall", label);
    REQUIRE_MSG(sight.eye.y >= collider.height(sight.eye.x, sight.eye.z) + 1.2f,
                "update_camera's ground clamp would move the bay camera", label);

    for (const auto& corner : corners)
        REQUIRE_MSG(!collider.line_of_sight_blocked(sight.eye, corner),
                    "a body-box corner is hidden from the bay camera", label);

    const glm::vec3 eye = site_local(sight.eye);
    const auto& divider = rooks_piece(parts, "repair office divider");
    const auto& front = rooks_piece(parts, "repair front");
    const auto& roof = rooks_piece(parts, "Rook's garage roof");
    if (bay > -5.0f)
        REQUIRE_MSG(eye.x < divider.centre.x,
                    "bay -1 camera is behind the office divider", label);
    if (eye.z < front.centre.z)
        REQUIRE_MSG(eye.y < roof.bottom_m, "bay camera is above Rook's roof", label);

    // The picture as it will draw: the whole body box inside the free region,
    // in front of the near plane, and the car centred in that region.
    const glm::mat4 vp =
        drawn_view_projection(sight.eye, sight.target, sight.fov_deg, framing.aspect);
    const float right = 2.0f * framing.free_fraction - 1.0f;
    for (const auto& corner : corners) {
        float w = 0.0f;
        const glm::vec2 p = ndc(vp, corner, w);
        REQUIRE_MSG(w > 0.15f, "a body-box corner is behind the near plane", label);
        REQUIRE_MSG(p.x >= -1.0f && p.x <= right,
                    "the car spills out of the free region horizontally", label);
        REQUIRE_MSG(p.y >= -1.0f && p.y <= 1.0f,
                    "the car spills off the top or bottom of the frame", label);
    }
    float w = 0.0f;
    const glm::vec2 middle = ndc(vp, sight.pivot, w);
    REQUIRE_NEAR(middle.x, framing.free_fraction - 1.0f, 0.02);
}

void every_bay_car_and_framing_clears() {
    TerrainCollider collider(city::kMapSeed);
    add_rooks(collider);
    const auto parts = city::bake_auto_repair();
    for (const CarCase& c : kCars) {
        const VehicleTuning tuning =
            player_model_tuning(DrivingMechanicsStyle::Balanced, c.id);
        if (c.id == PlayerCarId::HarrowCityliner) {
            // The plan sizes the long case on these two numbers.
            REQUIRE_NEAR(tuning.car_collision_half_width, 1.25, 1e-6);
            REQUIRE_NEAR(tuning.car_collision_half_length, 5.5, 1e-6);
        }
        for (float bay : {-9.0f, -1.0f}) {
            // Just pulled in, where a player stops as the prompt appears,
            // halfway, and as deep as the lane lets a car go.
            for (float z : bay_poses_z(parts, tuning, bay)) {
                const VehicleState car = spawn_in_rooks(tuning, collider, bay, z);
                REQUIRE_MSG(repair_shop_ready(car, tuning),
                            "the test car is not in a bay repair_shop_ready accepts",
                            c.name);
                require_clear_of_fixtures(parts, car, tuning);
                for (const Framing& framing : kFramings) {
                    char label[160];
                    std::snprintf(label, sizeof label, "%s, bay %.0f, z %.2f, %s", c.name,
                                  static_cast<double>(bay), static_cast<double>(z),
                                  framing.name);
                    const RespraySight sight = choose_respray_camera(
                        collider, car, tuning, bay, framing.free_fraction, framing.aspect);
                    check_sight(collider, car, tuning, bay, framing, sight, parts, label);
                    REQUIRE(same_sight(sight, choose_respray_camera(collider, car, tuning, bay,
                                                                    framing.free_fraction,
                                                                    framing.aspect)));
                    std::printf("       %s -> %s, fov %.0f, %.2f m\n", label,
                                kRespraySightCandidates[sight.candidate].name,
                                static_cast<double>(sight.fov_deg),
                                static_cast<double>(glm::length(sight.eye - sight.pivot)));
                }
            }
        }
    }
    apricot_test::pass("both bays, both cars, front to deep, 16:9 and square: clear, "
                       "unclipped, every corner seen, framed left of the panel");
}

void the_current_vehicle_box_must_be_disabled_by_the_caller() {
    TerrainCollider collider(city::kMapSeed);
    add_rooks(collider);
    const VehicleTuning tuning =
        player_model_tuning(DrivingMechanicsStyle::Balanced, PlayerCarId::VesperMistral);
    const VehicleState car = spawn_in_rooks(tuning, collider, -9.0f, 0.5f);
    const RespraySight without_box =
        choose_respray_camera(collider, car, tuning, -9.0f, 0.46f, 1.0f);
    REQUIRE(without_box.clear);

    // A stand-in for the box sync_current_vehicle_obstacle keeps for the
    // player's car: the same kind of kinematic oriented box, sized here to the
    // body box because the drawn body's bounds need the host layer to load.
    const glm::vec3 axis = car.orientation * glm::vec3{0.0f, 0.0f, 1.0f};
    const std::size_t box = collider.add_kinematic_oriented_box(
        respray_body_centre(car, tuning),
        {tuning.car_collision_half_width, 0.5f * (tuning.chassis_roof - tuning.chassis_floor),
         tuning.car_collision_half_length},
        std::atan2(axis.x, axis.z));
    REQUIRE(box != static_cast<std::size_t>(-1));
    REQUIRE(collider.set_kinematic_enabled(box, true));
    const RespraySight blocked =
        choose_respray_camera(collider, car, tuning, -9.0f, 0.46f, 1.0f);
    REQUIRE_MSG(!blocked.clear,
                "a sight cleared from inside the car's own enabled box", "contract");
    REQUIRE(blocked.candidate ==
            static_cast<int>(std::size(kRespraySightCandidates)) - 1);

    REQUIRE(collider.set_kinematic_enabled(box, false));
    REQUIRE(same_sight(without_box,
                       choose_respray_camera(collider, car, tuning, -9.0f, 0.46f, 1.0f)));
    apricot_test::pass("the car's own kinematic box blocks every candidate until the "
                       "caller disables it, as update_camera does");
}

void hall_keep_out_catches_what_the_collider_cannot() {
    // Rook's roof and the air over the office divider are not solid, so no ray
    // sees them. The hall check has to.
    TerrainCollider collider(city::kMapSeed);
    add_rooks(collider);
    const glm::vec3 pivot = site_world(-1.0f, 1.0f, -5.0f);
    const glm::vec3 through_roof = site_world(-1.0f, 7.5f, -6.0f);
    REQUIRE(!collider.raycast(pivot, through_roof - pivot, glm::length(through_roof - pivot))
                 .hit);
    REQUIRE(!respray_sight_stays_in_hall(pivot, through_roof));
    // From a car-height pivot in bay -1 to an eye just inside the office,
    // crossing the divider plane at about 5.1 m: over its 4.8 m top and under
    // the 5.8 m roof, so neither query sees anything in the way.
    const glm::vec3 car_pivot = site_world(-1.0f, 1.2f, -5.0f);
    const glm::vec3 over_divider = site_world(5.5f, 5.4f, -6.0f);
    REQUIRE(!collider.line_of_sight_blocked(car_pivot, over_divider));
    REQUIRE(!collider
                 .raycast(car_pivot, over_divider - car_pivot,
                          glm::length(over_divider - car_pivot))
                 .hit);
    REQUIRE(!respray_sight_stays_in_hall(car_pivot, over_divider));
    REQUIRE(respray_sight_stays_in_hall(pivot, site_world(-3.0f, 3.0f, 9.0f)));
    REQUIRE(respray_sight_stays_in_hall(pivot, site_world(-4.0f, 3.5f, -12.0f)));
    apricot_test::pass("the non-solid roof and the open top of the divider are kept out");
}

// Half the gap between a bay's two lift columns, read from the bake.
float lift_column_clearance(const std::vector<city::BuildingPiece>& parts, float bay) {
    float inner = 1e9f;
    for (const auto& p : parts)
        if (std::strcmp(p.name, "repair lift column") == 0 && std::fabs(p.centre.x - bay) < 3.0f)
            inner = std::min(inner, std::fabs(p.centre.x - bay) - 0.5f * p.width_m);
    REQUIRE_MSG(inner < 1e8f, "Rook's lost its lift columns", "lift_column_clearance");
    return inner;
}

void every_drivable_car_clears_in_both_bays() {
    // Every drivable car can be resprayed, emergency vehicles included, so the
    // shot has to hold for the whole roster and not only the two cars above:
    // the motorbike is the case where a close fit puts the eye under the
    // ground clamp, and the coach the one that needs the forecourt. Each car
    // stands front, middle and deep, centred and pushed against either lift
    // column, wherever that pose does not put it inside a fixture.
    TerrainCollider collider(city::kMapSeed);
    add_rooks(collider);
    const auto parts = city::bake_auto_repair();
    int sights = 0;
    for (std::size_t i = 0; i < kPlayerCarCount; ++i) {
        const auto id = static_cast<PlayerCarId>(i);
        // A save-compatibility alias that loads as the 91-C, not a car.
        if (id == PlayerCarId::LegacyCruiser91CSlot) continue;
        const VehicleTuning tuning = player_model_tuning(DrivingMechanicsStyle::Balanced, id);
        for (float bay : {-9.0f, -1.0f}) {
            const float room = std::max(
                0.0f, lift_column_clearance(parts, bay) - tuning.car_collision_half_width - 0.05f);
            for (float dx : {-room, 0.0f, room}) {
                for (float z : bay_poses_z(parts, tuning, bay + dx)) {
                    const VehicleState car = spawn_in_rooks(tuning, collider, bay + dx, z);
                    char label[160];
                    std::snprintf(label, sizeof label, "%s, bay %.0f, dx %.2f, z %.2f",
                                  player_car_definition(id).model, static_cast<double>(bay),
                                  static_cast<double>(dx), static_cast<double>(z));
                    REQUIRE_MSG(repair_shop_ready(car, tuning),
                                "the test car is not in a bay repair_shop_ready accepts", label);
                    require_clear_of_fixtures(parts, car, tuning);
                    for (const Framing& framing : kFramings) {
                        const RespraySight sight = choose_respray_camera(
                            collider, car, tuning, bay, framing.free_fraction, framing.aspect);
                        check_sight(collider, car, tuning, bay, framing, sight, parts, label);
                        ++sights;
                    }
                }
            }
        }
    }
    // kSelectablePlayerCarCount is the roster less that alias: 2 bays, 3
    // lateral positions, 3 depths and 2 framings each.
    REQUIRE(sights == static_cast<int>(kSelectablePlayerCarCount) * 2 * 3 * 3 * 2);
    apricot_test::pass("every drivable car, both bays, front to deep, centred and against "
                       "either lift column, 16:9 and square: every sight clears");
}

}  // namespace

int main() {
    every_bay_car_and_framing_clears();
    every_drivable_car_clears_in_both_bays();
    the_current_vehicle_box_must_be_disabled_by_the_caller();
    hall_keep_out_catches_what_the_collider_cannot();
    return apricot_test::done("respray_camera_tests");
}

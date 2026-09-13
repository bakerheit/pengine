#pragma once

// Rook's Auto Repair bay camera: the shot of the car the respray booth holds
// while the paint picker is open and while the spray runs.
//
// Header-only, and free of the host layer on purpose. The chooser is a pure
// function of the collider, the car and three framing numbers, so
// respray_camera_tests runs it against Rook's real baked geometry with no
// window and no GL context. Nothing here may include a gfx header: the suite
// links apricot_sim only.
//
// WHY A CHOOSER AND NOT A FIXED POSE. The chase camera centres the car, and
// the picker panel covers the right of the screen, so the car has to sit left
// of centre, and no one pose does that for Rook's two bays and the whole
// roster. Measured in respray_camera_tests: a Vesper Mistral frames from 5 to
// 6.5 m at 16:9, while a Harrow Cityliner in a square window needs 17 m and
// the eye out on the forecourt; the same coach pulled to the back of a bay, in
// that window, clears only from straight out of the bay door; and a Fang Venom
// at the back of bay -9 leaves under the 2.5 m minimum of clear ray behind it.
// So each candidate below is scored against the collider and the first one
// that clears wins.
//
// WHAT "CLEARS" MEANS, and why it is the obstruction pass's own test:
//   (a) raycast(pivot, eye - pivot) finds nothing within the eye distance plus
//       kRespraySightRayMarginM. App::update_camera pulls the eye in to
//       (hit - 0.35 m) whenever that ray is blocked, so an eye scored with a
//       looser test gets dragged into the car on the first frame and the shot
//       the chooser framed is not the shot that draws.
//   (b) line_of_sight_blocked(eye, corner) is false for all 8 body-box
//       corners, so no column, wall or parked car hides part of the paint.
//   (c) the eye already sits at least kRespraySightGroundLiftM above terrain,
//       so update_camera's ground clamp does not move it either. This is part
//       of the distance search, not only a final check; see first_fit.
//   (d) the sight stays inside the hall the car is in. Rook's roof and the air
//       above the 4.8 m office divider are not solid (bake_building emits flat
//       roofs non-solid), so (a) and (b) cannot see them. Without this check a
//       high candidate looks straight through the roof or over the divider
//       into the office, and both rays report clear.
//
// CALLER CONTRACT: disable the current vehicle's kinematic box (and an
// attached trailer's) before calling, exactly as update_camera does around its
// obstruction ray. Left enabled, every ray starts inside the car's own box, so
// nothing clears and the fallback is returned. respray_camera_tests pins that.
// Then use RespraySight::pivot as the obstruction pass's collision pivot.
//
// COST: the constants below restate update_camera's 0.35 m and 1.2 m, and
// nothing ties them to its literals; change one there and this keeps scoring
// the old one. And it is a search, a raycast and a distance scan per candidate
// and eight sight tests per framed eye, each sight test a scan of every static
// box. Against Rook's boxes alone, over 19,664 reachable poses of every
// drivable car, it took 0.23 ms mean and 2.6 ms worst, and it grows with the
// world's box count. Run it once when the picker opens, never per frame.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "city/neighborhood_shops.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"

namespace apricot {

// Must equal the pull-in margin of the obstruction pass in App::update_camera.
inline constexpr float kRespraySightRayMarginM = 0.35f;
// Must equal update_camera's ground clamp: terrain height + 1.2 m.
inline constexpr float kRespraySightGroundLiftM = 1.2f;
// Must equal Camera's default near plane; a corner nearer than this is cut.
inline constexpr float kRespraySightNearPlaneM = 0.15f;
inline constexpr float kRespraySightFarPlaneM = 2000.0f;
// The booth shot starts at 48 degrees and widens to 60 only when the car does
// not fit in the free region from any eye the ray allows.
inline constexpr float kRespraySightFovMinDeg = 48.0f;
inline constexpr float kRespraySightFovMaxDeg = 60.0f;
inline constexpr float kRespraySightFovStepDeg = 4.0f;
inline constexpr float kRespraySightMinDistanceM = 2.5f;
inline constexpr float kRespraySightMaxDistanceM = 18.0f;
inline constexpr float kRespraySightDistanceStepM = 0.125f;
// NDC inset kept around the body box inside the free region.
inline constexpr float kRespraySightNdcInset = 0.04f;
// Keep-out below the non-solid roof and above the divider's open top.
inline constexpr float kRespraySightHallMarginM = 0.35f;

struct RespraySight {
    glm::vec3 eye{0.0f};
    // The look-at point. NOT the car: the camera is yawed so the car lands in
    // the free region left of the picker panel.
    glm::vec3 target{0.0f};
    // The body-box centre every ray was scored from. The integrator must use
    // this as the obstruction pass's collision pivot; scoring from one point
    // and pulling in from another is how a clear sight gets clipped anyway.
    glm::vec3 pivot{0.0f};
    float fov_deg = kRespraySightFovMaxDeg;
    // Index into kRespraySightCandidates.
    int candidate = -1;
    // False only for the fallback when no candidate cleared.
    bool clear = false;
};

// Candidates live in Rook's site-local frame (x along the front wall, +z out
// to the forecourt), not the car's frame. The obstacles are the building's,
// and a car reversed into the bay must not get a different set of them.
// Azimuth is measured from +z, positive toward the middle of the garage hall;
// elevation is up from horizontal. Ordered best shot first. The LAST entry is
// the fallback, used even when nothing clears.
struct RespraySightCandidate {
    const char* name;
    float azimuth_deg;
    float elevation_deg;
};
inline constexpr RespraySightCandidate kRespraySightCandidates[] = {
    {"forecourt three-quarter", 24.0f, 11.0f},
    {"forecourt three-quarter, tight", 12.0f, 9.0f},
    {"rear three-quarter", 150.0f, 16.0f},
    {"high interior corner", 125.0f, 34.0f},
    {"rear three-quarter, wall side", -150.0f, 16.0f},
    {"straight out of the bay door", 0.0f, 8.0f},
};

namespace respray_camera_detail {

constexpr bool same_name(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
constexpr const city::BuildingWall* repair_wall(const char* name) {
    for (const city::BuildingWall& wall : city::kRepairWalls)
        if (same_name(wall.name, name)) return &wall;
    return nullptr;
}
constexpr const city::BuildingRoof* repair_roof(const char* name) {
    for (const city::BuildingRoof& roof : city::kRepairRoofs)
        if (same_name(roof.name, name)) return &roof;
    return nullptr;
}

// Read from the authored plan by name, so moving the divider or raising the
// roof moves the camera's keep-out with it instead of silently disagreeing.
inline constexpr const city::BuildingWall* kWest = repair_wall("repair west");
inline constexpr const city::BuildingWall* kDivider =
    repair_wall("repair office divider");
inline constexpr const city::BuildingRoof* kRoof =
    repair_roof("Rook's garage roof");
static_assert(kWest != nullptr && kDivider != nullptr && kRoof != nullptr,
              "respray_camera.h reads Rook's walls and roof by name; "
              "rename them here too");
static_assert(kWest->a.x == kWest->b.x && kDivider->a.x == kDivider->b.x,
              "the hall keep-out assumes the west wall and the divider run "
              "along site z");
// Middle of the garage hall both bays share, between the west wall and the
// office divider.
inline constexpr float kHallCentreX = (kWest->a.x + kDivider->a.x) * 0.5f;

}  // namespace respray_camera_detail

inline glm::vec3 respray_site_direction(float x, float y, float z) {
    const city::StartSite& s = city::kAutoRepairSite;
    return {s.cos_yaw * x + s.sin_yaw * z, y, -s.sin_yaw * x + s.cos_yaw * z};
}
// World -> Rook's site-local, height above the site ground. Same basis as
// repair_shop_ready.
inline glm::vec3 respray_site_local(glm::vec3 world) {
    const city::StartSite& s = city::kAutoRepairSite;
    const float x = world.x - s.origin.x;
    const float z = world.z - s.origin.z;
    return {s.cos_yaw * x - s.sin_yaw * z, world.y - s.ground_m,
            s.sin_yaw * x + s.cos_yaw * z};
}

// The body box the shot must contain: the road-plane car footprint
// (car_collision_half_width/length, which are fitted to the drawn body)
// between the chassis floor and roof, in the body frame whose origin is the
// centre of mass.
inline std::array<glm::vec3, 8> respray_body_corners(const VehicleState& car,
                                                     const VehicleTuning& tuning) {
    std::array<glm::vec3, 8> out{};
    std::size_t n = 0;
    for (float x : {-tuning.car_collision_half_width, tuning.car_collision_half_width})
        for (float y : {tuning.chassis_floor, tuning.chassis_roof})
            for (float z : {-tuning.car_collision_half_length,
                            tuning.car_collision_half_length})
                out[n++] = car.position + car.orientation * glm::vec3{x, y, z};
    return out;
}
inline glm::vec3 respray_body_centre(const VehicleState& car,
                                     const VehicleTuning& tuning) {
    return car.position +
           car.orientation *
               glm::vec3{0.0f, 0.5f * (tuning.chassis_floor + tuning.chassis_roof), 0.0f};
}

// Camera::forward() for a yaw and pitch. Mirrors gfx/camera.cpp, which this
// header cannot include.
inline glm::vec3 respray_camera_forward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)};
}
// Camera::projection() * Camera::view() for a pose, built the way
// update_camera turns (eye, target) into yaw and pitch.
inline glm::mat4 respray_view_projection(glm::vec3 eye, float yaw, float pitch,
                                         float fov_deg, float aspect) {
    const glm::mat4 view = glm::lookAt(eye, eye + respray_camera_forward(yaw, pitch),
                                       glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 projection =
        glm::perspective(glm::radians(fov_deg), aspect > 0.0f ? aspect : 1.0f,
                         kRespraySightNearPlaneM, kRespraySightFarPlaneM);
    return projection * view;
}

// Every corner in front of the near plane and inside the free region: the
// left `free_fraction` of the viewport width, full height, less the inset.
inline bool respray_box_in_free_region(const glm::mat4& view_projection,
                                       const std::array<glm::vec3, 8>& corners,
                                       float free_fraction) {
    const float left = -1.0f + kRespraySightNdcInset;
    const float right = 2.0f * free_fraction - 1.0f - kRespraySightNdcInset;
    const float bottom = -1.0f + kRespraySightNdcInset;
    const float top = 1.0f - kRespraySightNdcInset;
    for (const glm::vec3& corner : corners) {
        const glm::vec4 clip = view_projection * glm::vec4(corner, 1.0f);
        if (!(clip.w > kRespraySightNearPlaneM)) return false;
        const float x = clip.x / clip.w;
        const float y = clip.y / clip.w;
        if (!(x >= left && x <= right && y >= bottom && y <= top)) return false;
    }
    return true;
}

// Check (d): the segment from pivot to eye stays under Rook's roof and does
// not cross the office divider's plane inside the building, above or below its
// top. Below the top the raycast would also catch it; above, nothing else can.
inline bool respray_sight_stays_in_hall(glm::vec3 pivot_world, glm::vec3 eye_world) {
    namespace d = respray_camera_detail;
    const glm::vec3 p = respray_site_local(pivot_world);
    const glm::vec3 e = respray_site_local(eye_world);
    const float m = kRespraySightHallMarginM;

    const float roof_y = d::kRoof->bottom_m - m;
    if (!(p.y < roof_y)) return false;
    if (e.y > roof_y) {
        const float t = (roof_y - p.y) / (e.y - p.y);
        const glm::vec3 q = p + (e - p) * t;
        const float half_x = d::kRoof->width_m * 0.5f + d::kRoof->overhang_m + m;
        const float half_z = d::kRoof->depth_m * 0.5f + d::kRoof->overhang_m + m;
        if (std::fabs(q.x - d::kRoof->centre.x) <= half_x &&
            std::fabs(q.z - d::kRoof->centre.z) <= half_z) return false;
    }

    const float divider_x = d::kDivider->a.x;
    const float from_pivot = p.x - divider_x;
    const float from_eye = e.x - divider_x;
    if (from_pivot * from_eye <= 0.0f || std::fabs(from_eye) < m) {
        const float run = e.x - p.x;
        const float t = std::fabs(run) > 1e-6f
            ? std::clamp((divider_x - p.x) / run, 0.0f, 1.0f) : 1.0f;
        const float z = p.z + (e.z - p.z) * t;
        const float z0 = std::min(d::kDivider->a.z, d::kDivider->b.z) - m;
        const float z1 = std::max(d::kDivider->a.z, d::kDivider->b.z) + m;
        if (z >= z0 && z <= z1) return false;
    }
    return true;
}

namespace respray_camera_detail {

struct Aim {
    glm::vec3 dir{0.0f};   // unit, pivot -> eye
    float yaw = 0.0f;      // camera yaw, already turned to frame the free region
    float pitch = 0.0f;
    glm::vec3 look{0.0f};  // respray_camera_forward(yaw, pitch)
};

inline glm::vec3 candidate_direction(const RespraySightCandidate& c, float side) {
    const float azimuth = glm::radians(c.azimuth_deg);
    const float elevation = glm::radians(c.elevation_deg);
    return respray_site_direction(side * std::sin(azimuth) * std::cos(elevation),
                                  std::sin(elevation),
                                  std::cos(azimuth) * std::cos(elevation));
}

// Point the camera back along `dir` at the pivot, then turn it right until the
// pivot projects onto the free region's horizontal centre. The turn depends on
// the direction only, never the distance, so it is solved once per candidate
// and field of view. Bisection: the pivot's NDC x falls monotonically as the
// camera turns right, for any turn short of a right angle.
inline Aim aim(glm::vec3 pivot, glm::vec3 dir, float fov_deg, float aspect,
               float free_fraction) {
    Aim out;
    out.dir = dir;
    const glm::vec3 to_pivot = -dir;
    const float flat = std::sqrt(to_pivot.x * to_pivot.x + to_pivot.z * to_pivot.z);
    const float yaw = std::atan2(to_pivot.x, -to_pivot.z);
    out.pitch = std::atan2(to_pivot.y, flat > 1e-4f ? flat : 1e-4f);
    const float want = free_fraction - 1.0f;
    float turn = 0.0f;
    if (want < 0.0f) {
        const glm::vec3 eye = pivot + dir;
        float lo = 0.0f;
        float hi = 1.4f;
        for (int i = 0; i < 32; ++i) {
            const float mid = 0.5f * (lo + hi);
            const glm::vec4 clip =
                respray_view_projection(eye, yaw + mid, out.pitch, fov_deg, aspect) *
                glm::vec4(pivot, 1.0f);
            if (clip.x / clip.w > want) lo = mid;
            else hi = mid;
        }
        turn = 0.5f * (lo + hi);
    }
    out.yaw = yaw + turn;
    out.look = respray_camera_forward(out.yaw, out.pitch);
    return out;
}

inline RespraySight place(const Aim& a, glm::vec3 pivot, float distance,
                          float fov_deg) {
    RespraySight s;
    s.pivot = pivot;
    s.eye = pivot + a.dir * distance;
    s.target = s.eye + a.look * distance;
    s.fov_deg = fov_deg;
    return s;
}

// Nearest eye along the ray, no further than `max_distance`, whose frame holds
// the whole body box inside the free region AND that update_camera's ground
// clamp leaves where it is. Negative when none does.
//
// The ground condition lives here rather than only in sight_is_clear because
// both constraints are minimum distances along a rising ray. Scored
// separately, a small car fits so close that its eye is under the clamp, the
// candidate is rejected, and a shot a metre further out that satisfied both
// is never tried: the Fang Venom wedged into the back of bay -9 fell through
// to the fallback that way.
inline float first_fit(const TerrainCollider& collider, const Aim& a, glm::vec3 pivot,
                       const std::array<glm::vec3, 8>& corners, float fov_deg,
                       float aspect, float free_fraction, float max_distance) {
    for (int k = 0;; ++k) {
        const float distance =
            kRespraySightMinDistanceM + kRespraySightDistanceStepM * static_cast<float>(k);
        if (distance > max_distance) return -1.0f;
        const glm::vec3 eye = pivot + a.dir * distance;
        if (eye.y < collider.height(eye.x, eye.z) + kRespraySightGroundLiftM + 0.01f)
            continue;
        const glm::mat4 vp =
            respray_view_projection(eye, a.yaw, a.pitch, fov_deg, aspect);
        if (respray_box_in_free_region(vp, corners, free_fraction)) return distance;
    }
}

inline bool sight_is_clear(const TerrainCollider& collider, const RespraySight& s,
                           const std::array<glm::vec3, 8>& corners) {
    if (s.eye.y < collider.height(s.eye.x, s.eye.z) + kRespraySightGroundLiftM + 0.01f)
        return false;
    if (!respray_sight_stays_in_hall(s.pivot, s.eye)) return false;
    for (const glm::vec3& corner : corners)
        if (collider.line_of_sight_blocked(s.eye, corner)) return false;
    return true;
}

// Distance along `dir` the obstruction pass will leave alone: the first hit
// less the pull-in margin, capped at the search limit.
inline float clear_reach(const TerrainCollider& collider, glm::vec3 pivot, glm::vec3 dir) {
    const float reach = kRespraySightMaxDistanceM + kRespraySightRayMarginM;
    const TerrainCollider::GroundHit hit = collider.raycast(pivot, dir, reach);
    if (!hit.hit) return kRespraySightMaxDistanceM;
    return std::min(kRespraySightMaxDistanceM, hit.distance - kRespraySightRayMarginM);
}

}  // namespace respray_camera_detail

// `bay_x` is the bay's site-local x (-9 or -1, as repair_shop_ready tests).
// `free_fraction` is the share of the viewport width, from the LEFT edge, that
// the picker panel leaves uncovered. `aspect` is the viewport's width/height.
inline RespraySight choose_respray_camera(const TerrainCollider& collider,
                                          const VehicleState& car,
                                          const VehicleTuning& tuning, float bay_x,
                                          float free_fraction, float aspect) {
    namespace d = respray_camera_detail;
    const std::array<glm::vec3, 8> corners = respray_body_corners(car, tuning);
    const glm::vec3 pivot = respray_body_centre(car, tuning);
    const float fraction =
        std::isfinite(free_fraction) ? std::clamp(free_fraction, 0.1f, 1.0f) : 1.0f;
    const float view_aspect = std::isfinite(aspect) && aspect > 0.0f ? aspect : 1.0f;
    const float side = bay_x < d::kHallCentreX ? 1.0f : -1.0f;
    const int fov_steps = static_cast<int>(std::lround(
        (kRespraySightFovMaxDeg - kRespraySightFovMinDeg) / kRespraySightFovStepDeg));
    const std::size_t count = std::size(kRespraySightCandidates);

    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 dir = d::candidate_direction(kRespraySightCandidates[i], side);
        const float reach = d::clear_reach(collider, pivot, dir);
        if (reach < kRespraySightMinDistanceM) continue;
        for (int k = 0; k <= fov_steps; ++k) {
            const float fov =
                kRespraySightFovMinDeg + kRespraySightFovStepDeg * static_cast<float>(k);
            const d::Aim a = d::aim(pivot, dir, fov, view_aspect, fraction);
            const float distance = d::first_fit(collider, a, pivot, corners, fov,
                                                view_aspect, fraction, reach);
            if (distance < 0.0f) continue;
            RespraySight sight = d::place(a, pivot, distance, fov);
            if (!d::sight_is_clear(collider, sight, corners)) continue;
            sight.candidate = static_cast<int>(i);
            sight.clear = true;
            return sight;
        }
    }

    // Nothing cleared. Take the last candidate at the widest angle, framed as
    // well as the search allows, but never further out than its ray is clear:
    // the obstruction pass would only pull a further eye back in.
    const std::size_t last = count - 1;
    const glm::vec3 dir = d::candidate_direction(kRespraySightCandidates[last], side);
    const float reach = std::max(d::clear_reach(collider, pivot, dir), 0.15f);
    const d::Aim a = d::aim(pivot, dir, kRespraySightFovMaxDeg, view_aspect, fraction);
    const float fit = d::first_fit(collider, a, pivot, corners, kRespraySightFovMaxDeg,
                                   view_aspect, fraction, kRespraySightMaxDistanceM);
    const float distance = std::min(fit > 0.0f ? fit : kRespraySightMaxDistanceM, reach);
    RespraySight sight = d::place(a, pivot, distance, kRespraySightFovMaxDeg);
    sight.candidate = static_cast<int>(last);
    sight.clear = false;
    return sight;
}

}  // namespace apricot

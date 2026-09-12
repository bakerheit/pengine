#pragma once

// Presentation-only seated pose. No GL, scene, animation clock or sim writes.
#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/player_car_catalog.h"
#include "core/aabb.h"
#include "core/skeletal_animation.h"
#include "core/transform.h"

namespace apricot {

// Car 5-NEXT seats a full-size rig under a body the catalog squashes to .664
// of its authored height. Reclining buys the last few centimetres of
// headroom that dropping the hip alone cannot, without burying the seat in
// the floor. Pinned by the headroom probe in vehicle_driver_pose_tests.
inline constexpr float kCar5NextRecline = 28.f;

inline bool shows_mistral_driver(PlayerCarId car, bool occupied) {
    return occupied && car == PlayerCarId::VesperMistral;
}

inline bool has_animated_driver(PlayerCarId car) {
    return car == PlayerCarId::VesperMistral || car == PlayerCarId::HarrowWorkman ||
        car==PlayerCarId::AlderPip || car==PlayerCarId::VesperScythe ||
        car==PlayerCarId::FangVenom ||
        car==PlayerCarId::HalcyonSovereign ||
        car==PlayerCarId::LegacyCar5Next ||
        car==PlayerCarId::LegacyCar5NextPolice ||
        is_municipal_cruiser_91(car);
}

inline bool has_contact_driver_entry(PlayerCarId car) {
    return car==PlayerCarId::AlderPip || car==PlayerCarId::VesperScythe ||
        car==PlayerCarId::HalcyonSovereign;
}

inline bool shows_vehicle_driver(PlayerCarId car, bool occupied) {
    return occupied && has_animated_driver(car);
}

// Source car X/up-Y/forward-Z. Hip is above the seat cushion; wrists sit just
// behind the steering rim so the fingers reach it. Feet stay in the footwell.
// Facing the +Z nose, left is +X (forward cross up points to the right).
inline const glm::vec3 kMistralDriverHip{.43f, .74f, -.37f};
inline const glm::vec3 kMistralDriverWrists[2]{
    {.325f, 1.015f, .075f}, {.535f, 1.015f, .075f}};
inline const glm::vec3 kMistralDriverAnkles[2]{
    {.33f, .50f, .29f}, {.53f, .50f, .29f}};

struct VehicleDriverLayout {
    glm::vec3 hip;
    glm::vec3 wrists[2];
    glm::vec3 ankles[2];
    glm::vec3 knees[2];
    glm::vec3 elbows[2];
    glm::vec3 handle_elbow;
    float approach_x;
};

inline const VehicleDriverLayout& vehicle_driver_layout(PlayerCarId car) {
    car=canonical_player_car_id(car);
    static const VehicleDriverLayout mistral{
        kMistralDriverHip, {kMistralDriverWrists[0], kMistralDriverWrists[1]},
        {kMistralDriverAnkles[0], kMistralDriverAnkles[1]},
        {{.33f, 1.f, 0.f}, {.53f, 1.f, 0.f}},
        {{.09f, .75f, -.27f}, {.77f, .75f, -.27f}}, {1.40f, .80f, -.65f}, 1.05f};
    static const VehicleDriverLayout workman{
        {.43f, .94f, -.12f}, {{.32f, 1.20f, .44f}, {.54f, 1.20f, .44f}},
        {{.33f, .635f, .60f}, {.53f, .635f, .60f}},
        {{.33f, 1.22f, .30f}, {.53f, 1.22f, .30f}},
        {{.09f, .96f, -.02f}, {.77f, .96f, -.02f}}, {1.40f, 1.10f, -.40f}, 1.02f};
    static const VehicleDriverLayout pip{
        {.38f,.56f,-.25f},{{.27f,.92f,.21f},{.49f,.92f,.21f}},
        // The supplied shoe extends 9.1cm below the ankle; the .29m floor
        // needs an ankle at .39m, rather than the pedal's surface height.
        {{.29f,.39f,.47f},{.47f,.39f,.47f}},
        {{.29f,.83f,.10f},{.47f,.83f,.10f}},
        {{.05f,.63f,-.18f},{.70f,.63f,-.18f}},{1.2f,.75f,-.60f},.96f};
    static const VehicleDriverLayout scythe{
        {.43f,.42f,-.24f},{{.32f,.72f,.10f},{.54f,.72f,.10f}},
        {{.33f,.35f,.55f},{.53f,.35f,.55f}},
        {{.33f,.75f,.28f},{.53f,.75f,.28f}},
        {{.08f,.48f,-.22f},{.78f,.48f,-.22f}},{1.4f,.70f,-.50f},1.13f};
    static const VehicleDriverLayout sovereign{
        {.46f,.65f,.67f},{{.35f,1.06f,1.15f},{.57f,1.06f,1.15f}},
        {{.36f,.45f,1.47f},{.56f,.45f,1.47f}},
        {{.36f,.95f,1.10f},{.56f,.95f,1.10f}},
        {{.10f,.75f,.72f},{.80f,.75f,.72f}},{1.5f,.9f,.25f},1.17f};
    static const VehicleDriverLayout cruiser91a{
        {.43f,.73f,.17f},{{.32f,1.08f,.58f},{.54f,1.08f,.58f}},
        {{.33f,.47f,.66f},{.53f,.47f,.66f}},
        {{.33f,.92f,.46f},{.53f,.92f,.46f}},
        {{.22f,.94f,.30f},{.62f,.94f,.30f}},{1.42f,.96f,-.01f},1.06f};
    static const VehicleDriverLayout cruiser91b{
        {.43f,.72f,-.12f},{{.32f,.96f,.39f},{.54f,.96f,.39f}},
        {{.33f,.44f,.52f},{.53f,.44f,.52f}},
        {{.33f,.90f,.24f},{.53f,.90f,.24f}},
        {{.09f,.75f,-.04f},{.77f,.75f,-.04f}},{1.42f,.92f,-.02f},1.06f};
    static const VehicleDriverLayout cruiser91c{
        {.42f,.73f,.18f},{{.31f,1.04f,.56f},{.53f,1.04f,.56f}},
        {{.34f,.43f,.72f},{.54f,.43f,.72f}},
        {{.34f,.90f,.38f},{.54f,.90f,.38f}},
        {{.10f,.78f,.16f},{.76f,.78f,.16f}},{1.42f,.94f,.02f},1.08f};
    static const VehicleDriverLayout cruiser91d{
        {.42f,.76f,.06f},{{.31f,1.10f,.50f},{.53f,1.10f,.50f}},
        {{.33f,.47f,.60f},{.53f,.47f,.60f}},
        {{.33f,.92f,.35f},{.53f,.92f,.35f}},
        {{.17f,.94f,.16f},{.67f,.94f,.16f}},{1.38f,.99f,-.13f},1.04f};
    static const VehicleDriverLayout cruiser91e{
        {.44f,.70f,.14f},{{.32f,1.04f,.56f},{.55f,1.04f,.56f}},
        {{.34f,.45f,.68f},{.54f,.45f,.68f}},
        {{.34f,.91f,.43f},{.54f,.91f,.43f}},
        {{.19f,.90f,.27f},{.67f,.90f,.27f}},{1.42f,.94f,-.05f},1.05f};
    // Car 5-NEXT is an imported body, so its source units are neither metres
    // nor any other car's. This rig is the 91-C's, converted: X by the body's
    // own fit, Y measured up from the ground rather than the mesh origin, Z as
    // a fraction along the driver door. The extra 8 cm of drop is real -- this
    // roof is 1.32 m over the ground where the 91-C's is 1.37 m, and without it
    // the driver's head sits in the headliner. Feet are exempt: both cabin
    // floors are 0.30 m off the ground. See tools/car5_next_spec.py.
    static const VehicleDriverLayout car5Next{
        {0.559f,0.796f,0.264f},{{0.413f,1.263f,0.827f},{0.705f,1.263f,0.827f}},
        {{0.452f,0.465f,1.064f},{0.719f,0.465f,1.064f}},
        {{0.452f,1.052f,0.560f},{0.719f,1.052f,0.560f}},
        {{0.133f,0.871f,0.235f},{1.011f,0.871f,0.235f}},{1.890f,1.112f,0.027f},1.437f};
    static const VehicleDriverLayout fangVenom{
        {0.f,.90f,-.20f},{{-.25f,1.04f,.34f},{.25f,1.04f,.34f}},
        {{-.225f,.52f,-.08f},{.225f,.52f,-.08f}},
        {{-.245f,.80f,.17f},{.245f,.80f,.17f}},
        {{-.36f,1.10f,.12f},{.36f,1.10f,.12f}},{.72f,1.17f,.19f},.58f};
    if(car==PlayerCarId::AlderPip) return pip;
    if(car==PlayerCarId::VesperScythe) return scythe;
    if(car==PlayerCarId::HalcyonSovereign) return sovereign;
    if(car==PlayerCarId::MunicipalCruiser91A) return cruiser91a;
    if(car==PlayerCarId::MunicipalCruiser91B) return cruiser91b;
    if(car==PlayerCarId::MunicipalCruiser91C) return cruiser91c;
    if(car==PlayerCarId::MunicipalCruiser91D) return cruiser91d;
    if(car==PlayerCarId::MunicipalCruiser91E) return cruiser91e;
    if(car==PlayerCarId::FangVenom) return fangVenom;
    if(car==PlayerCarId::LegacyCar5Next ||
       car==PlayerCarId::LegacyCar5NextPolice) return car5Next;
    return car == PlayerCarId::HarrowWorkman ? workman : mistral;
}

struct VehicleDriverPose {
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;
    std::vector<glm::mat4> joints;
    std::vector<glm::vec4> dual_real;
    std::vector<glm::vec4> dual_part;
    Transform world;
};

namespace driver_pose_detail {

inline bool finite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}

inline glm::quat aim(glm::vec3 from, glm::vec3 to) {
    const float a = glm::length(from), b = glm::length(to);
    if (a < 1e-6f || b < 1e-6f) return {1.f, 0.f, 0.f, 0.f};
    from /= a; to /= b;
    const float dot = glm::clamp(glm::dot(from, to), -1.f, 1.f);
    if (dot < -.9999f) {
        glm::vec3 axis = glm::cross(from, glm::vec3{0, 1, 0});
        if (glm::length(axis) < .01f) axis = glm::cross(from, glm::vec3{0, 0, 1});
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 cross = glm::cross(from, to);
    return glm::normalize(glm::quat{1.f + dot, cross.x, cross.y, cross.z});
}

inline void globals(const Skeleton& s, VehicleDriverPose& pose) {
    s.compute_skin_matrices(pose.local, pose.skin);
    pose.joints.resize(pose.skin.size());
    for (std::size_t i = 0; i < pose.skin.size(); ++i)
        pose.joints[i] = pose.skin[i] * glm::inverse(s.bone(static_cast<int>(i)).inverse_bind);
}

inline void set_rotation(const Skeleton& s, VehicleDriverPose& p, int bone,
                          const glm::quat& rotation) {
    glm::mat4 world = glm::mat4_cast(glm::normalize(rotation));
    world[3] = p.joints[static_cast<std::size_t>(bone)][3];
    const int parent = s.bone(bone).parent;
    p.local[static_cast<std::size_t>(bone)] = parent < 0 ? world
        : glm::inverse(p.joints[static_cast<std::size_t>(parent)]) * world;
    globals(s, p);
}

inline void aim_child(const Skeleton& s, VehicleDriverPose& p,
                       int bone, int child, const glm::vec3& target) {
    const glm::mat4& world = p.joints[static_cast<std::size_t>(bone)];
    const glm::vec3 position{world[3]};
    const glm::vec3 old = glm::vec3{p.joints[static_cast<std::size_t>(child)][3]} - position;
    set_rotation(s, p, bone, aim(old, target - position) * glm::quat_cast(glm::mat3{world}));
}

// Analytic two-bone solve preserves measured limb lengths. The pole selects
// knees forward/up and elbows out/down; no scale or vertex translation hacks.
inline void limb(const Skeleton& s, VehicleDriverPose& p, int upper, int lower,
                  int end, const glm::vec3& target, const glm::vec3& pole,
                  float extension_margin=.001f) {
    const glm::vec3 a{p.joints[static_cast<std::size_t>(upper)][3]};
    const glm::vec3 b{p.joints[static_cast<std::size_t>(lower)][3]};
    const glm::vec3 c{p.joints[static_cast<std::size_t>(end)][3]};
    const float l1 = glm::length(b-a), l2 = glm::length(c-b);
    const float raw = glm::length(target-a);
    if (raw < 1e-5f || l1 < 1e-5f || l2 < 1e-5f) return;
    const glm::vec3 forward = (target-a)/raw;
    const float distance = std::clamp(raw, std::fabs(l1-l2)+.001f, l1+l2-extension_margin);
    glm::vec3 bend = pole-a-forward*glm::dot(pole-a, forward);
    if (glm::length(bend) < 1e-5f) bend = glm::cross(forward, glm::vec3{0,0,1});
    if (glm::length(bend) < 1e-5f) bend = glm::cross(forward, glm::vec3{0,1,0});
    bend = glm::normalize(bend);
    const float along = (l1*l1-l2*l2+distance*distance)/(2.f*distance);
    const float height = std::sqrt(std::max(0.f, l1*l1-along*along));
    aim_child(s, p, upper, lower, a + forward*along + bend*height);
    aim_child(s, p, lower, end, a + forward*distance);
}

}  // namespace driver_pose_detail

inline bool make_seated_driver_pose(const VehicleDriverLayout& layout, const Skeleton& skeleton,
                                     const AABB& bind_bounds,
                                     const Transform& rendered_body,
                                     VehicleDriverPose& out, float recline_degrees=-12.f) {
    using namespace driver_pose_detail;
    out = VehicleDriverPose{};
    const float height = bind_bounds.size().y;
    if (!(height > 1e-4f) || !std::isfinite(height) ||
        !finite(rendered_body.matrix()) ||
        !(glm::length(rendered_body.rotation) > 1e-5f) ||
        glm::any(glm::lessThanEqual(rendered_body.scale, glm::vec3{0.f}))) return false;
    // Supplied pack's native forward is +X and lateral is Z, as measured from
    // its feet, shoulders and inverse bind matrices. Map +X to the car's +Z.
    const glm::quat facing = glm::angleAxis(-1.57079632679f, glm::vec3{0,1,0});
    const float human_scale = 1.76f / height;
    const int hips = skeleton.find_bone("mixamorig:Hips");
    const int head = skeleton.find_bone("mixamorig:Head");
    int chains[2][6]{};
    const char* sides[]{"Right", "Left"};
    const char* names[]{"UpLeg", "Leg", "Foot", "Arm", "ForeArm", "Hand"};
    for (int side = 0; side < 2; ++side)
        for (int part = 0; part < 6; ++part) {
            chains[side][part] = skeleton.find_bone(std::string{"mixamorig:"}+sides[side]+names[part]);
            if (chains[side][part] < 0) return false;
        }
    if (hips < 0 || head < 0) return false;
    for (int i = 0; i < skeleton.bone_count(); ++i) out.local.push_back(skeleton.bone(i).bind_local);
    globals(skeleton, out);
    if (out.joints.size() != out.local.size()) return false;
    const glm::vec3 bind_hip{out.joints[static_cast<std::size_t>(hips)][3]};
    const glm::quat head_bind = glm::quat_cast(glm::mat3{out.joints[static_cast<std::size_t>(head)]});
    const auto native_target = [&](const glm::vec3& car_point) {
        return bind_hip + glm::inverse(facing) *
            (rendered_body.scale * (car_point-layout.hip)) / human_scale;
    };
    out.world.rotation = glm::normalize(rendered_body.rotation * facing);
    out.world.scale = glm::vec3{human_scale};  // never squeeze people to car width
    out.world.position = rendered_body.transform_point(layout.hip) -
        out.world.rotation * (out.world.scale * bind_hip);
    set_rotation(skeleton, out, hips,
        glm::angleAxis(glm::radians(recline_degrees), glm::vec3{0,0,1}) *
        glm::quat_cast(glm::mat3{out.joints[static_cast<std::size_t>(hips)]}));
    for (int side = 0; side < 2; ++side) {
        const auto& chain = chains[side];
        const glm::quat foot_bind = glm::quat_cast(glm::mat3{
            glm::inverse(skeleton.bone(chain[2]).inverse_bind)});
        limb(skeleton, out, chain[0], chain[1], chain[2],
             native_target(layout.ankles[side]), native_target(layout.knees[side]));
        set_rotation(skeleton, out, chain[2], foot_bind); // soles level, toes forward
        limb(skeleton, out, chain[3], chain[4], chain[5],
             native_target(layout.wrists[side]), native_target(layout.elbows[side]));
        // Point the pack's simple fingers forward into the rim. The wrist
        // stays at the IK endpoint; descendants keep their authored lengths.
        const int finger = skeleton.find_bone(std::string{"mixamorig:"}+sides[side]+"HandIndex1");
        if (finger >= 0) {
            const glm::vec3 wrist{out.joints[static_cast<std::size_t>(chain[5])][3]};
            aim_child(skeleton, out, chain[5], finger,
                      wrist + glm::inverse(facing)*glm::vec3{0,0,1});
        }
    }
    set_rotation(skeleton, out, head, head_bind);
    for (const glm::mat4& m : out.skin) if (!finite(m)) return false;
    skin_matrices_to_dual_quaternions(out.skin, out.dual_real, out.dual_part);
    return !out.skin.empty();
}

inline bool make_vehicle_driver_pose(PlayerCarId car,const Skeleton& skeleton,
        const AABB& bounds,const Transform& body,VehicleDriverPose& out) {
    if(!has_animated_driver(car)) {out={};return false;}
    // Positive angles recline this supplied rig toward the seat back. The
    // Scythe's low canopy needs the measured rearward sports-car posture.
    return make_seated_driver_pose(vehicle_driver_layout(car),skeleton,bounds,body,out,
        car==PlayerCarId::VesperScythe?20.f:
        car==PlayerCarId::LegacyCar5Next ||
            car==PlayerCarId::LegacyCar5NextPolice?kCar5NextRecline:
        car==PlayerCarId::FangVenom?-18.f:-12.f);
}

inline bool make_mistral_driver_pose(const Skeleton& skeleton, const AABB& bounds,
                                     const Transform& body, VehicleDriverPose& out) {
    return make_vehicle_driver_pose(PlayerCarId::VesperMistral, skeleton, bounds, body, out);
}

}  // namespace apricot

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// Twelve body regions let a scrape walk down one side and let bumper damage
// stay centred or favour a corner. Coordinates use the physics chassis frame:
// -Z is front and -X is left.
enum VehicleDamageZone : std::size_t {
    kDamageFrontLeft = 0,
    kDamageFrontCenter,
    kDamageFrontRight,
    kDamageRearLeft,
    kDamageRearCenter,
    kDamageRearRight,
    kDamageSideLeftFront,
    kDamageSideLeftMiddle,
    kDamageSideLeftRear,
    kDamageSideRightFront,
    kDamageSideRightMiddle,
    kDamageSideRightRear,
    kVehicleDamageZoneCount,
};

// Compatibility names for callers that only care about the middle door.
inline constexpr VehicleDamageZone kDamageSideLeft = kDamageSideLeftMiddle;
inline constexpr VehicleDamageZone kDamageSideRight = kDamageSideRightMiddle;

struct VehicleDamageState {
    std::array<float, kVehicleDamageZoneCount> zones{};
    struct DentStamp {
        // Normalised physics-body coordinates. X: -left/+right, Z:
        // -front/+rear. A stamp is inactive when severity is zero.
        glm::vec2 contact_xz{0.0f};
        float severity = 0.0f;
        float motion_angle = 0.0f;  // radians in local XZ
        float radius = 0.25f;       // narrow object 0 .. broad object 1
        float height = 0.45f;       // floor 0 .. roof 1
        float glancing = 0.0f;      // direct 0 .. side-swipe 1
    };
    std::array<DentStamp, 2> stamps{};
};

using VehicleDentStamp = VehicleDamageState::DentStamp;

// A GL 3.3 implementation only guarantees 16 vertex attributes and Apricot
// already uses all of them. Four exact 24-bit float integers carry twelve
// 8-bit zone strengths; the remaining two carry two persistent impact stamps.
// This keeps the existing 192-byte instance and traffic batching contract.
inline float pack_vehicle_damage_triple(float first, float second,
                                        float third) {
    const auto quantize = [](float value) {
        return static_cast<uint32_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return static_cast<float>(quantize(first) + quantize(second) * 256u +
                              quantize(third) * 65536u);
}

inline glm::vec3 unpack_vehicle_damage_triple(float packed) {
    constexpr float kBase = 256.0f;
    constexpr float kLevels = 255.0f;
    const float value = std::clamp(packed, 0.0f, 16777215.0f);
    const float high = std::floor(value / 65536.0f);
    const float remainder = value - high * 65536.0f;
    const float middle = std::floor(remainder / kBase);
    const float low = remainder - middle * kBase;
    return {low / kLevels, middle / kLevels, high / kLevels};
}

inline glm::vec4 pack_vehicle_damage0(const VehicleDamageState& state) {
    const auto damage = [&](VehicleDamageZone zone) {
        return state.zones[static_cast<std::size_t>(zone)];
    };
    return {
        pack_vehicle_damage_triple(damage(kDamageFrontLeft),
                                   damage(kDamageFrontCenter),
                                   damage(kDamageFrontRight)),
        pack_vehicle_damage_triple(damage(kDamageRearLeft),
                                   damage(kDamageRearCenter),
                                   damage(kDamageRearRight)),
        pack_vehicle_damage_triple(damage(kDamageSideLeftFront),
                                   damage(kDamageSideLeftMiddle),
                                   damage(kDamageSideLeftRear)),
        pack_vehicle_damage_triple(damage(kDamageSideRightFront),
                                   damage(kDamageSideRightMiddle),
                                   damage(kDamageSideRightRear)),
    };
}

inline float pack_vehicle_dent_stamp(const VehicleDentStamp& stamp) {
    constexpr float kPi = 3.14159265358979323846f;
    const auto quantize = [](float value, uint32_t levels) {
        return static_cast<uint32_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * static_cast<float>(levels)));
    };
    const uint32_t x = quantize(stamp.contact_xz.x * 0.5f + 0.5f, 31u);
    const uint32_t z = quantize(stamp.contact_xz.y * 0.5f + 0.5f, 31u);
    const uint32_t severity = quantize(stamp.severity, 31u);
    float wrapped = std::fmod(stamp.motion_angle + kPi, 2.0f * kPi);
    if (wrapped < 0.0f) wrapped += 2.0f * kPi;
    const uint32_t angle = quantize(wrapped / (2.0f * kPi), 15u);
    const uint32_t radius = quantize(stamp.radius, 3u);
    const uint32_t height = quantize(stamp.height, 3u);
    const uint32_t glancing = stamp.glancing >= 0.5f ? 1u : 0u;
    const uint32_t packed = x | (z << 5u) | (severity << 10u) |
                            (angle << 15u) | (radius << 19u) |
                            (height << 21u) | (glancing << 23u);
    return static_cast<float>(packed);
}

inline VehicleDentStamp unpack_vehicle_dent_stamp(float packed_float) {
    constexpr float kPi = 3.14159265358979323846f;
    const uint32_t packed = static_cast<uint32_t>(std::lround(
        std::clamp(packed_float, 0.0f, 16777215.0f)));
    VehicleDentStamp stamp;
    stamp.contact_xz.x =
        static_cast<float>(packed & 31u) / 31.0f * 2.0f - 1.0f;
    stamp.contact_xz.y =
        static_cast<float>((packed >> 5u) & 31u) / 31.0f * 2.0f - 1.0f;
    stamp.severity = static_cast<float>((packed >> 10u) & 31u) / 31.0f;
    stamp.motion_angle =
        static_cast<float>((packed >> 15u) & 15u) / 15.0f *
            (2.0f * kPi) -
        kPi;
    stamp.radius = static_cast<float>((packed >> 19u) & 3u) / 3.0f;
    stamp.height = static_cast<float>((packed >> 21u) & 3u) / 3.0f;
    stamp.glancing = static_cast<float>((packed >> 23u) & 1u);
    return stamp;
}

inline glm::vec2 pack_vehicle_damage1(const VehicleDamageState& state) {
    return {pack_vehicle_dent_stamp(state.stamps[0]),
            pack_vehicle_dent_stamp(state.stamps[1])};
}

inline float vehicle_damage_total(const VehicleDamageState& state) {
    float total = 0.0f;
    for (float damage : state.zones) total += damage;
    return total;
}

// The body shader pushes damaged panels inward, so collision must not keep the
// pristine rectangle after the visible bumper or door has moved. This is the
// broad panel displacement from lit_instanced.vert expressed as four body
// extents. `contact_xz01` selects the part of the body facing the other object;
// that preserves an intact corner beside a crushed centre instead of shrinking
// the whole vehicle to its most damaged point.
struct VehicleDamageCollisionFootprint {
    float half_width_m = 0.0f;
    float half_length_m = 0.0f;
    float centre_right_m = 0.0f;
    float centre_forward_m = 0.0f;
    float left_extent_m = 0.0f;
    float right_extent_m = 0.0f;
    float front_extent_m = 0.0f;
    float rear_extent_m = 0.0f;
};

inline VehicleDamageCollisionFootprint vehicle_damage_collision_footprint(
    const VehicleDamageState& state, float base_half_width_m,
    float base_half_length_m, glm::vec2 contact_xz01) {
    const float half_width = std::max(base_half_width_m, 0.1f);
    const float half_length = std::max(base_half_length_m, 0.1f);
    const float x = std::clamp(contact_xz01.x, -1.0f, 1.0f);
    const float z = std::clamp(contact_xz01.y, -1.0f, 1.0f);
    const auto damage = [&](VehicleDamageZone zone) {
        return std::clamp(state.zones[static_cast<std::size_t>(zone)],
                          0.0f, 1.0f);
    };
    const auto smoothstep = [](float edge0, float edge1, float value) {
        const float t = std::clamp((value - edge0) / (edge1 - edge0),
                                   0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    // Physics uses -X left and -Z front. The legacy source meshes are rotated
    // 180 degrees for rendering, hence the sign changes compared with the
    // shader's source-space nx/nz values.
    const float left_band = smoothstep(-0.12f, 0.82f, -x);
    const float right_band = smoothstep(-0.12f, 0.82f, x);
    const float center_band =
        1.0f - smoothstep(0.02f, 0.68f, std::fabs(x));
    const float longitudinal_front = smoothstep(-0.12f, 0.82f, -z);
    const float longitudinal_rear = smoothstep(-0.12f, 0.82f, z);
    const float longitudinal_middle =
        1.0f - smoothstep(0.02f, 0.68f, std::fabs(z));
    const float front_surface = smoothstep(0.05f, 0.92f, -z);
    const float rear_surface = smoothstep(0.05f, 0.92f, z);
    const float middle =
        1.0f - smoothstep(0.18f, 0.72f, std::fabs(z));

    const float front_damage = std::max(
        damage(kDamageFrontLeft) * left_band,
        std::max(damage(kDamageFrontCenter) * center_band,
                 damage(kDamageFrontRight) * right_band));
    const float rear_damage = std::max(
        damage(kDamageRearLeft) * left_band,
        std::max(damage(kDamageRearCenter) * center_band,
                 damage(kDamageRearRight) * right_band));
    const float left_side_damage = std::max(
        damage(kDamageSideLeftFront) * longitudinal_front,
        std::max(damage(kDamageSideLeftMiddle) * longitudinal_middle,
                 damage(kDamageSideLeftRear) * longitudinal_rear));
    const float right_side_damage = std::max(
        damage(kDamageSideRightFront) * longitudinal_front,
        std::max(damage(kDamageSideRightMiddle) * longitudinal_middle,
                 damage(kDamageSideRightRear) * longitudinal_rear));
    const float left_damage = std::max(
        left_side_damage * middle,
        std::max(damage(kDamageFrontLeft) * front_surface,
                 damage(kDamageRearLeft) * rear_surface));
    const float right_damage = std::max(
        right_side_damage * middle,
        std::max(damage(kDamageFrontRight) * front_surface,
                 damage(kDamageRearRight) * rear_surface));

    VehicleDamageCollisionFootprint out;
    out.front_extent_m = half_length * (1.0f - front_damage * 0.22f);
    out.rear_extent_m = half_length * (1.0f - rear_damage * 0.20f);
    out.left_extent_m = half_width * (1.0f - left_damage * 0.18f);
    out.right_extent_m = half_width * (1.0f - right_damage * 0.18f);
    out.half_length_m =
        (out.front_extent_m + out.rear_extent_m) * 0.5f;
    out.half_width_m =
        (out.left_extent_m + out.right_extent_m) * 0.5f;
    out.centre_forward_m =
        (out.front_extent_m - out.rear_extent_m) * 0.5f;
    out.centre_right_m =
        (out.right_extent_m - out.left_extent_m) * 0.5f;
    return out;
}

enum class VehicleLamp : uint8_t {
    FrontLeft = 0,
    FrontRight,
    RearLeft,
    RearRight,
};

// Local panel damage at one lens. Kept separate from the failure curve so the
// presentation can also move a still-working lamp with its crushed panel.
inline float vehicle_lamp_damage(const VehicleDamageState& state,
                                 VehicleLamp lamp) {
    const auto damage = [&](VehicleDamageZone zone) {
        return state.zones[static_cast<std::size_t>(zone)];
    };
    switch (lamp) {
        case VehicleLamp::FrontLeft:
            return std::max({damage(kDamageFrontLeft),
                             damage(kDamageSideLeftFront) * 0.72f,
                             damage(kDamageFrontCenter) * 0.32f});
        case VehicleLamp::FrontRight:
            return std::max({damage(kDamageFrontRight),
                             damage(kDamageSideRightFront) * 0.72f,
                             damage(kDamageFrontCenter) * 0.32f});
        case VehicleLamp::RearLeft:
            return std::max({damage(kDamageRearLeft),
                             damage(kDamageSideLeftRear) * 0.72f,
                             damage(kDamageRearCenter) * 0.32f});
        case VehicleLamp::RearRight:
            return std::max({damage(kDamageRearRight),
                             damage(kDamageSideRightRear) * 0.72f,
                             damage(kDamageRearCenter) * 0.32f});
    }
    return 0.0f;
}

// Remaining output from one lamp. A nearby corner strike starts dimming and
// flickering the lens before a hard repeat kills it. Centre-bumper damage can
// reach both lamps, but at a much lower weight than a hit on that corner.
inline float vehicle_lamp_health(const VehicleDamageState& state,
                                 VehicleLamp lamp) {
    const float local = vehicle_lamp_damage(state, lamp);
    const float t = std::clamp((local - 0.34f) / (0.82f - 0.34f),
                               0.0f, 1.0f);
    const float failure = t * t * (3.0f - 2.0f * t);
    return 1.0f - failure;
}

enum class VehicleFluidKind : uint8_t {
    Coolant = 0,
    Oil,
    Fuel,
};

struct VehicleFluidLeak {
    VehicleFluidKind kind = VehicleFluidKind::Coolant;
    // Normalised physics body coordinates: -X left/+X right and
    // -Z front/+Z rear. Severity zero means this leak is inactive.
    glm::vec2 local_xz{0.0f};
    float severity = 0.0f;
};

// Damage-driven leak sources. These are body-relative rather than world
// effects so the same regional state works for the player, sedans, trucks and
// ambulances. Fluids come from plausible systems: cooling and lubrication up
// front, fuel around the rear tank/lines.
inline std::array<VehicleFluidLeak, 3> vehicle_fluid_leaks(
    const VehicleDamageState& state) {
    const auto damage = [&](VehicleDamageZone zone) {
        return std::clamp(state.zones[static_cast<std::size_t>(zone)],
                          0.0f, 1.0f);
    };
    const auto severity = [](float value, float start, float full) {
        const float t = std::clamp((value - start) / (full - start),
                                   0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const auto source_x = [](float left, float center, float right) {
        if (left > center && left >= right) return -0.58f;
        if (right > center && right > left) return 0.58f;
        return 0.0f;
    };

    const float front_left = std::max(
        damage(kDamageFrontLeft), damage(kDamageSideLeftFront) * 0.72f);
    const float front_center = damage(kDamageFrontCenter);
    const float front_right = std::max(
        damage(kDamageFrontRight), damage(kDamageSideRightFront) * 0.72f);
    const float front_peak = std::max({front_left, front_center, front_right});

    const float rear_left = std::max(
        damage(kDamageRearLeft), damage(kDamageSideLeftRear) * 0.78f);
    const float rear_center = damage(kDamageRearCenter);
    const float rear_right = std::max(
        damage(kDamageRearRight), damage(kDamageSideRightRear) * 0.78f);
    const float rear_peak = std::max({rear_left, rear_center, rear_right});

    const float underbody = std::max(
        front_center,
        std::max(damage(kDamageSideLeftMiddle),
                 damage(kDamageSideRightMiddle)) * 0.70f);

    return {{
        {VehicleFluidKind::Coolant,
         {source_x(front_left, front_center, front_right), -0.72f},
         severity(front_peak, 0.38f, 0.90f)},
        {VehicleFluidKind::Oil,
         {source_x(front_left, underbody, front_right) * 0.45f, -0.22f},
         severity(std::max(front_peak * 0.74f, underbody), 0.70f, 0.96f)},
        {VehicleFluidKind::Fuel,
         {source_x(rear_left, rear_center, rear_right), 0.70f},
         severity(rear_peak, 0.50f, 0.90f)},
    }};
}

// Damage reaching one wheel from the two body panels that surround its arch.
// `wheel_index` follows VehicleState's stable FL, FR, RL, RR order, but this
// header deliberately does not include vehicle.h (vehicle.h owns this state).
// Centre-bumper hits can bend both wheels on an axle, at a reduced weight.
inline float vehicle_wheel_damage(const VehicleDamageState& state,
                                  int wheel_index) {
    const auto damage = [&](VehicleDamageZone zone) {
        return state.zones[static_cast<std::size_t>(zone)];
    };
    switch (wheel_index) {
        case 0:
            return std::clamp(std::max(
                std::max(damage(kDamageFrontLeft),
                         damage(kDamageSideLeftFront)),
                damage(kDamageFrontCenter) * 0.38f), 0.0f, 1.0f);
        case 1:
            return std::clamp(std::max(
                std::max(damage(kDamageFrontRight),
                         damage(kDamageSideRightFront)),
                damage(kDamageFrontCenter) * 0.38f), 0.0f, 1.0f);
        case 2:
            return std::clamp(std::max(
                std::max(damage(kDamageRearLeft),
                         damage(kDamageSideLeftRear)),
                damage(kDamageRearCenter) * 0.38f), 0.0f, 1.0f);
        case 3:
            return std::clamp(std::max(
                std::max(damage(kDamageRearRight),
                         damage(kDamageSideRightRear)),
                damage(kDamageRearCenter) * 0.38f), 0.0f, 1.0f);
        default:
            return 0.0f;
    }
}

// Accumulate a hit at a body-local contact point. Damage is expressed in the
// same points used by VehicleState health. Centre hits blend across both halves
// instead of choosing an arbitrary side; repeated hits deepen the existing dent.
inline void apply_vehicle_impact(VehicleDamageState& state,
                                 const glm::vec3& local_contact,
                                 float damage_points, float half_width_m,
                                 float half_length_m,
                                 glm::vec2 local_motion_xz = {0.0f, -1.0f},
                                 float contact_height01 = 0.45f,
                                 float contact_radius01 = 0.25f,
                                 float glancing01 = 0.0f,
                                 float body_damage_gain = 1.0f) {
    if (!(damage_points > 0.0f) || !(body_damage_gain > 0.0f)) return;

    const float half_w = std::max(half_width_m, 0.01f);
    const float half_l = std::max(half_length_m, 0.01f);
    const float x = std::clamp(local_contact.x / half_w, -1.0f, 1.0f);
    const float z = std::clamp(local_contact.z / half_l, -1.0f, 1.0f);
    const float amount = std::min(damage_points / 35.0f, 0.55f) *
                         std::clamp(body_damage_gain, 0.0f, 1.0f);

    // Adjacent areas blend, so moving a contact along the bumper or doors
    // moves the dent continuously instead of crossing a hard region seam.
    const float left_weight = std::max(0.0f, -x);
    const float right_weight = std::max(0.0f, x);
    const float center_weight = 1.0f - std::fabs(x);
    const float front_weight = std::max(0.0f, -z);
    const float rear_weight = std::max(0.0f, z);
    const float middle_weight = 1.0f - std::fabs(z);
    auto smoothstep = [](float edge0, float edge1, float value) {
        const float t = std::clamp((value - edge0) / (edge1 - edge0),
                                   0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    float end_presence = smoothstep(0.25f, 0.78f, std::fabs(z));
    float side_presence = smoothstep(0.25f, 0.78f, std::fabs(x));
    const float presence_sum = end_presence + side_presence;
    if (presence_sum > 1.0f) {
        end_presence /= presence_sum;
        side_presence /= presence_sum;
    }
    auto add = [&](VehicleDamageZone zone, float weight) {
        float& value = state.zones[static_cast<std::size_t>(zone)];
        value = std::clamp(value + amount * weight, 0.0f, 1.0f);
    };

    const float front_presence = end_presence * (z <= 0.0f ? 1.0f : 0.0f);
    const float rear_presence = end_presence * (z >= 0.0f ? 1.0f : 0.0f);
    add(kDamageFrontLeft, front_presence * left_weight);
    add(kDamageFrontCenter, front_presence * center_weight);
    add(kDamageFrontRight, front_presence * right_weight);
    add(kDamageRearLeft, rear_presence * left_weight);
    add(kDamageRearCenter, rear_presence * center_weight);
    add(kDamageRearRight, rear_presence * right_weight);

    if (x <= 0.0f) {
        add(kDamageSideLeftFront, side_presence * front_weight);
        add(kDamageSideLeftMiddle, side_presence * middle_weight);
        add(kDamageSideLeftRear, side_presence * rear_weight);
    } else {
        add(kDamageSideRightFront, side_presence * front_weight);
        add(kDamageSideRightMiddle, side_presence * middle_weight);
        add(kDamageSideRightRear, side_presence * rear_weight);
    }

    // Keep two bounded, persistent dent stamps. Nearby repeats deepen and
    // refine one stamp; a new location takes an empty slot or replaces the
    // weaker stamp. No allocation, clock or random choice enters sim state.
    const glm::vec2 contact{x, z};
    const float motion_length = glm::length(local_motion_xz);
    const glm::vec2 motion = motion_length > 1e-5f
                                 ? local_motion_xz / motion_length
                                 : glm::vec2{0.0f, -1.0f};
    const float new_severity = std::clamp(amount, 0.0f, 1.0f);
    std::size_t slot = state.stamps.size();
    float nearest = 999.0f;
    for (std::size_t i = 0; i < state.stamps.size(); ++i) {
        const VehicleDentStamp& stamp = state.stamps[i];
        if (!(stamp.severity > 0.0f)) {
            if (slot == state.stamps.size()) slot = i;
            continue;
        }
        const float distance = glm::distance(stamp.contact_xz, contact);
        const float merge_radius =
            0.28f + 0.24f * std::max(stamp.radius, contact_radius01);
        if (distance <= merge_radius && distance < nearest) {
            slot = i;
            nearest = distance;
        }
    }
    if (slot == state.stamps.size()) {
        slot = state.stamps[1].severity < state.stamps[0].severity ? 1u : 0u;
    }

    VehicleDentStamp& stamp = state.stamps[slot];
    const float old_weight = stamp.severity;
    const float new_weight = std::max(new_severity, 0.01f);
    const float weight_sum = old_weight + new_weight;
    if (old_weight > 0.0f) {
        stamp.contact_xz =
            (stamp.contact_xz * old_weight + contact * new_weight) / weight_sum;
        const glm::vec2 old_motion{std::cos(stamp.motion_angle),
                                   std::sin(stamp.motion_angle)};
        const glm::vec2 blended =
            old_motion * old_weight + motion * new_weight;
        if (glm::length(blended) > 1e-5f)
            stamp.motion_angle = std::atan2(blended.y, blended.x);
        stamp.radius = (stamp.radius * old_weight +
                        std::clamp(contact_radius01, 0.0f, 1.0f) * new_weight) /
                       weight_sum;
        stamp.height = (stamp.height * old_weight +
                        std::clamp(contact_height01, 0.0f, 1.0f) * new_weight) /
                       weight_sum;
        stamp.glancing = (stamp.glancing * old_weight +
                          std::clamp(glancing01, 0.0f, 1.0f) * new_weight) /
                         weight_sum;
    } else {
        stamp.contact_xz = contact;
        stamp.motion_angle = std::atan2(motion.y, motion.x);
        stamp.radius = std::clamp(contact_radius01, 0.0f, 1.0f);
        stamp.height = std::clamp(contact_height01, 0.0f, 1.0f);
        stamp.glancing = std::clamp(glancing01, 0.0f, 1.0f);
    }
    stamp.severity = std::clamp(old_weight + new_severity * 0.82f,
                                0.0f, 1.0f);
}

}  // namespace apricot

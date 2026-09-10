#pragma once

#include <array>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/presentation_budgets.h"
#include "core/aabb.h"
#include "core/transform.h"

namespace apricot {

// Pure model fitting for the legacy wheel-less traffic bodies. Keep this
// renderer-free so the relationship between body arches and wheel nodes can be
// pinned by a headless test.
struct TrafficVisualLayout {
    Transform body;
    std::array<glm::vec3, 4> wheel_centres{};
    AABB placed_body_bounds;
    float wheel_radius = 0.34375f;
};

// A lamp overlay belongs to the fitted render body, not the physics box. The
// legacy paints already contain the unlit lenses; these four transforms are
// deliberately paper-thin emissive overlays that only become visible while a
// lamp is powered. Keeping this renderer-free lets every vehicle model prove
// its lamps are on the body instead of floating at a generic chassis extent.
struct VehicleLampLayout {
    std::array<Transform, 4> lamps{};
};

inline VehicleLampLayout make_vehicle_lamp_layout(const AABB& body_bounds) {
    VehicleLampLayout out;
    if (!body_bounds.valid()) return out;

    const glm::vec3 centre = body_bounds.center();
    const glm::vec3 half = body_bounds.extents();
    const glm::vec3 size = body_bounds.size();
    const float front_lamp_y = centre.y - size.y * 0.28f;
    const float rear_lamp_y = centre.y - size.y * 0.15f;
    constexpr float kLensDepth = 0.018f;

    for (std::size_t i = 0; i < out.lamps.size(); ++i) {
        const bool front = i < 2u;
        const bool left = i == 0u || i == 2u;
        Transform& lamp = out.lamps[i];
        lamp.position = {
            centre.x + (left ? -1.0f : 1.0f) * half.x * 0.60f,
            front ? front_lamp_y : rear_lamp_y,
            front ? body_bounds.min.z - kLensDepth * 0.20f
                  : body_bounds.max.z + kLensDepth * 0.20f};
        lamp.scale = {
            size.x * (front ? 0.17f : 0.15f),
            std::max(size.y * (front ? 0.085f : 0.072f), 0.055f),
            kLensDepth};
    }
    return out;
}

// Follow the broad crush used by the body shader closely enough that a lit
// lens stays attached to a bent bumper. Broken lamps disappear entirely, but
// this matters for the useful partial-damage range before failure.
inline Transform deform_vehicle_lamp(const Transform& pristine,
                                     const AABB& body_bounds,
                                     std::size_t lamp_index,
                                     float local_damage) {
    Transform out = pristine;
    const float damage = std::clamp(local_damage, 0.0f, 1.0f);
    const bool front = lamp_index < 2u;
    const bool left = lamp_index == 0u || lamp_index == 2u;
    const glm::vec3 half = body_bounds.extents();
    out.position.z += (front ? 1.0f : -1.0f) * damage * half.z *
                      (front ? 0.22f : 0.20f);
    out.position.x += (left ? 1.0f : -1.0f) * damage * half.x * 0.12f;
    out.position.y -= damage * half.z * 0.035f;
    return out;
}

// Renderer-free placement for one cantilever signal. The pole's longitudinal
// setback is based on the WIDEST road crossing the junction, while its lateral
// position and head target belong to this approach. Using the approach width
// for both puts a narrow-road signal inside the edge of a wider cross street.
struct TrafficSignalLayout {
    glm::vec3 pole_ground{0.0f};
    glm::vec3 pole_top{0.0f};
    glm::vec3 arm_end{0.0f};
    float arm_length_m = 0.0f;
    // Local +X is road-right, +Y up, +Z faces approaching drivers.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 facing{0.0f};
};

inline TrafficSignalLayout make_traffic_signal_layout(
    glm::vec3 junction, glm::vec3 approach_dir,
    float approach_half_width_m, float junction_half_width_m,
    float inbound_lane_centre_m, float corner_back_m = 3.0f,
    float corner_side_m = 1.2f, float pole_height_m = 6.0f) {
    TrafficSignalLayout out;
    approach_dir.y = 0.0f;
    const float dir_length = glm::length(approach_dir);
    if (!(dir_length > 1e-5f)) return out;
    approach_dir /= dir_length;
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 right = glm::normalize(glm::cross(approach_dir, up));
    out.facing = -approach_dir;
    // {right, up, approach_dir} is a reflection (determinant -1), not a
    // rotation. quat_cast silently loses the intended heading in that case.
    out.rotation = glm::normalize(glm::quat_cast(
        glm::mat3{right, up, out.facing}));
    const float approach_half = std::max(0.0f, approach_half_width_m);
    const float junction_half = std::max(
        approach_half, std::max(0.0f, junction_half_width_m));
    const float side_sign = inbound_lane_centre_m < 0.0f ? -1.0f : 1.0f;
    const float pole_lateral = side_sign *
        (approach_half + std::max(0.0f, corner_side_m));
    const float head_lateral = std::clamp(
        inbound_lane_centre_m, -approach_half, approach_half);

    out.pole_ground = junction - approach_dir *
        (junction_half + std::max(0.0f, corner_back_m)) +
        right * pole_lateral;
    out.pole_top = out.pole_ground + up * std::max(0.0f, pole_height_m);
    out.arm_end = out.pole_top + right * (head_lateral - pole_lateral);
    out.arm_length_m = std::fabs(head_lateral - pole_lateral);
    return out;
}

inline TrafficVisualLayout make_traffic_visual_layout(
    const AABB& bounds, float arch_centre_y_native, float wheel_x_native,
    float wheel_front_z_native, float wheel_rear_z_native) {
    // Probable Cause fitted every vehicle into its 4 m chassis, then applied
    // the global 1.25 vehicle scale. Preserve that 5 m visual length and its
    // shared 0.275 * 1.25 m visible wheel radius.
    constexpr float kLegacyVisualLength = 5.0f;
    constexpr float kLegacyWheelRadius = 0.34375f;
    constexpr float kPi = 3.14159265358979323846f;

    TrafficVisualLayout out;
    out.wheel_radius = kLegacyWheelRadius;
    const float scale = kLegacyVisualLength /
                        std::max(bounds.size().z, 0.001f);
    out.body.scale = glm::vec3{scale};
    out.body.rotation =
        glm::angleAxis(kPi, glm::vec3{0.0f, 1.0f, 0.0f});

    // Centre the cooked body's XZ bounds after the 180-degree axis fix. Y is
    // authored from the arch centre, not the mesh bottom: bumpers and exhausts
    // may hang below the tyre centre and are not a ride-height reference.
    const glm::vec3 native_centre = (bounds.min + bounds.max) * 0.5f;
    const glm::vec3 rotated_centre =
        out.body.rotation * (native_centre * scale);
    out.body.position.x = -rotated_centre.x;
    out.body.position.z = -rotated_centre.z;
    out.body.position.y =
        kLegacyWheelRadius - arch_centre_y_native * scale;

    // Native models face +Z. After the body yaw fix, +Z is Apricot's -Z
    // front and native +X lands on Apricot's left. Derive the actual wheel
    // nodes through the SAME body transform so they cannot drift away from the
    // arches when a differently proportioned model is added.
    const std::array<glm::vec3, 4> native_arch_centres{
        glm::vec3{+wheel_x_native, arch_centre_y_native,
                  +wheel_front_z_native},
        glm::vec3{-wheel_x_native, arch_centre_y_native,
                  +wheel_front_z_native},
        glm::vec3{+wheel_x_native, arch_centre_y_native,
                  -wheel_rear_z_native},
        glm::vec3{-wheel_x_native, arch_centre_y_native,
                  -wheel_rear_z_native},
    };
    for (std::size_t i = 0; i < native_arch_centres.size(); ++i) {
        out.wheel_centres[i] = out.body.transform_point(native_arch_centres[i]);
    }
    out.placed_body_bounds = bounds.transformed(out.body.matrix());
    return out;
}

}  // namespace apricot

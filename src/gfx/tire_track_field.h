#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "physics/vehicle.h"
#include "terrain/surface.h"

namespace apricot {

enum class TireTrackSurface : uint8_t {
    Rubber = 0,
    Dirt = 1,
    Snow = 2,
};

struct TireTrackEmission {
    bool emit = false;
    bool sliding = false;
    TireTrackSurface surface = TireTrackSurface::Rubber;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float intensity = 0.0f;
};

inline TireTrackSurface tire_track_surface(Surface contact_material,
                                           float snow_cover) {
    if (std::isfinite(snow_cover) && snow_cover >= 0.15f) {
        return TireTrackSurface::Snow;
    }
    return contact_material == Surface::Rock ? TireTrackSurface::Rubber
                                             : TireTrackSurface::Dirt;
}

// Convert the authoritative wheel contact into a presentation-only track event.
// Ordinary driving marks snow because the tyre compresses it. Dry ground needs
// real lateral slip, so a normal corner does not graffiti every street.
inline TireTrackEmission tire_track_emission(
    const VehicleState& car, int wheel_index, float handbrake,
    float snow_cover) {
    TireTrackEmission out;
    if (wheel_index < 0 || wheel_index >= kWheelCount) return out;

    const WheelState& wheel =
        car.wheels[static_cast<std::size_t>(wheel_index)];
    if (!wheel.grounded || !std::isfinite(wheel.slip)) return out;

    const Surface ground = wheel.contact_material;

    const glm::vec3 point_velocity =
        car.velocity + glm::cross(car.angular_velocity,
                                  wheel.contact_point - car.position);
    const glm::vec3 horizontal_velocity{
        point_velocity.x, 0.0f, point_velocity.z};
    const float speed = glm::length(horizontal_velocity);
    if (!(speed > 0.75f) || !std::isfinite(speed)) return out;

    glm::vec3 right = car.orientation * glm::vec3{1.0f, 0.0f, 0.0f};
    right.y = 0.0f;
    const float right_length = glm::length(right);
    right = right_length > 1e-5f ? right / right_length
                                  : glm::vec3{1.0f, 0.0f, 0.0f};
    const float lateral_speed = std::fabs(glm::dot(horizontal_velocity, right));
    const bool rear = wheel_index == kWheelRearLeft ||
                      wheel_index == kWheelRearRight;
    const bool handbrake_slide = rear && handbrake > 0.20f &&
                                 speed > 2.5f && wheel.slip > 0.75f;
    const float lateral_threshold = ground == Surface::Rock ? 1.8f : 1.1f;
    const float slip_threshold = ground == Surface::Rock ? 1.05f : 0.85f;
    const bool lateral_slide = speed > 2.5f &&
                               lateral_speed > lateral_threshold &&
                               wheel.slip > slip_threshold;

    out.surface = tire_track_surface(ground, snow_cover);
    const bool snow_roll = out.surface == TireTrackSurface::Snow;
    out.sliding = handbrake_slide || lateral_slide;
    out.emit = snow_roll || out.sliding;
    if (!out.emit) return out;

    out.position = wheel.contact_point;
    const float normal_length = glm::length(wheel.contact_normal);
    out.normal = normal_length > 1e-5f
        ? wheel.contact_normal / normal_length
        : glm::vec3{0.0f, 1.0f, 0.0f};
    out.direction = horizontal_velocity / speed;

    const float slip_amount = std::clamp((wheel.slip - 0.70f) / 1.80f,
                                         0.0f, 1.0f);
    const float lateral_amount = std::clamp(
        (lateral_speed - 0.5f) / 6.0f, 0.0f, 1.0f);
    if (snow_roll) {
        const float cover = std::clamp(snow_cover, 0.0f, 1.0f);
        out.intensity = std::clamp(0.32f + cover * 0.36f +
                                   std::max(slip_amount, lateral_amount) * 0.30f,
                                   0.0f, 1.0f);
    } else {
        out.intensity = std::clamp(
            std::max(slip_amount, lateral_amount) +
                (handbrake_slide ? 0.22f : 0.0f),
            0.18f, 1.0f);
    }
    return out;
}

}  // namespace apricot

#pragma once

#include <algorithm>
#include <cmath>

#include "app/player_car_catalog.h"
#include "core/transform.h"

namespace apricot {

// The Mistral's folding canvas top. Cooked car coordinates: +X driver side,
// +Y up, +Z nose. Keep these in sync with TOP in tools/vesper_mistral_spec.py.
//
// Two bows, cooked as soft_top_front.emesh and soft_top_rear.emesh. The rear
// bow swings about a hinge under the tonneau; the front bow folds back over it
// as it goes, so the pair stacks behind the seats instead of sweeping a single
// slab across the rear deck.
//
// Rotation is about the LATERAL axis on purpose. The fitted chassis scale is
// {half_track fit, wheelbase fit, wheelbase fit} — Y and Z carry the same
// factor, so a rotation about X after that scale is still rigid. A roof hinged
// about any other axis would shear as the body stretched to the chassis.
inline constexpr float kMistralTopDegrees = 3.14159265358979323846f / 180.f;
inline const glm::vec3 kMistralTopRearHinge{0.f, .91f, -.90f};
inline const glm::vec3 kMistralTopJoint{0.f, 1.40f, -.48f};
inline constexpr float kMistralTopStowRadians = -123.f * kMistralTopDegrees;
inline constexpr float kMistralTopFoldRadians = 130.f * kMistralTopDegrees;
// Long enough to read as a mechanism rather than a pop, short enough that you
// are not waiting at a green light for it.
inline constexpr float kMistralTopSeconds = 2.4f;

inline constexpr bool is_convertible(PlayerCarId car) {
    return canonical_player_car_id(car) == PlayerCarId::VesperMistral;
}

// `stowed` runs 0 (latched to the windshield header) to 1 (folded away).
inline constexpr float clamp_soft_top(float stowed) {
    return std::isfinite(stowed) ? std::clamp(stowed, 0.f, 1.f) : 0.f;
}

// A rigid rotation about the lateral axis through `pivot`, with `pivot` given
// in the parent's own source coordinates. Same shape as the driver door's
// hinge: rotate the already-fitted panel, never the source before the fit.
inline Transform mistral_top_pivot(const Transform& parent,
                                   const glm::vec3& pivot, float radians) {
    Transform out = parent;
    out.rotation = parent.rotation * glm::angleAxis(radians, glm::vec3{1, 0, 0});
    out.position =
        parent.transform_point(pivot) - out.rotation * (parent.scale * pivot);
    return out;
}

inline Transform mistral_top_rear_transform(const Transform& body, float stowed) {
    const float f = clamp_soft_top(stowed);
    if (f == 0.f) return body;
    return mistral_top_pivot(body, kMistralTopRearHinge, kMistralTopStowRadians * f);
}

// The front bow rides the rear bow, so its hinge is the joint carried by the
// rear bow's own motion — not a second independent swing from the body.
inline Transform mistral_top_front_transform(const Transform& body, float stowed) {
    const float f = clamp_soft_top(stowed);
    if (f == 0.f) return body;
    return mistral_top_pivot(mistral_top_rear_transform(body, f), kMistralTopJoint,
                             kMistralTopFoldRadians * f);
}

// Where the canvas is and where it is heading. Presentation state: it changes
// no dimension the physics uses, so it stays out of InputFrame and out of the
// replay tape. It is still advanced on the sim cadence rather than the render
// cadence, so the fold takes the same 2.4 seconds at any frame rate.
struct MistralSoftTop {
    float stowed = 1.f;   // roadsters live with the top down
    float target = 1.f;
};

inline MistralSoftTop step_mistral_soft_top(MistralSoftTop top, bool toggle,
                                            float dt) {
    if (toggle) top.target = top.target > .5f ? 0.f : 1.f;
    if (!std::isfinite(dt) || dt <= 0.f) return top;
    const float step = dt / kMistralTopSeconds;
    top.stowed = top.target > top.stowed
        ? std::min(top.target, top.stowed + step)
        : std::max(top.target, top.stowed - step);
    return top;
}

// True while the canvas is between its two latched positions. Exact, not
// epsilon'd: the step above clamps onto the target rather than easing toward
// it, so "arrived" is a real equality and not a tolerance to pick.
inline bool mistral_soft_top_moving(const MistralSoftTop& top) {
    return top.target != top.stowed;
}

}  // namespace apricot

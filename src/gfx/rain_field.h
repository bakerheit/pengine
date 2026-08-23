#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/rng.h"

namespace apricot {

// The pure maths behind the camera-locked rain field. GL-free and header-only
// so the headless suite can hammer it with the inputs that actually break this
// kind of code — a twenty-second lag spike, a camera teleport, a zero-size box
// — none of which you can produce by looking at the game.

struct RainTuning {
    int drop_count = 3000;      // drops in the field at full intensity
    float fall_speed = 20.0f;   // m/s
    float streak_len = 0.85f;   // metres
    float half_width = 0.02f;   // metres
    float slant_x = 0.14f;      // constant wind slant of the fall direction
    float slant_z = 0.05f;
    float opacity = 0.45f;      // alpha at full intensity

    // The box the field occupies, centred on the camera. Wide enough that the
    // edge is never in shot, shallow enough that the drop count is not spread
    // so thin the rain looks sparse.
    glm::vec3 span{70.0f, 45.0f, 70.0f};

    glm::vec3 color{0.62f, 0.68f, 0.78f};
};

// Shift `value` by whole multiples of `span` until it lies in the half-open
// window [centre - span/2, centre + span/2).
//
// This ONE function does both jobs the field needs: recycling a drop that has
// fallen out of the bottom, and keeping the field locked to a camera that has
// moved. Doing them separately means two places that can disagree about where
// the box is.
//
// O(1) for any input, including a delta of several hundred spans — a loop here
// turns a twenty-second debugger pause into a visible hang. A non-positive span
// is returned untouched rather than dividing by zero.
inline float wrap_into_span(float value, float centre, float span) {
    if (!(span > 0.0f)) return value;
    const float lo = centre - span * 0.5f;
    const float offset = value - lo;
    const float wrapped = offset - std::floor(offset / span) * span;
    return lo + wrapped;
}

inline glm::vec3 wrap_into_box(glm::vec3 p, glm::vec3 centre, glm::vec3 span) {
    return glm::vec3{wrap_into_span(p.x, centre.x, span.x),
                     wrap_into_span(p.y, centre.y, span.y),
                     wrap_into_span(p.z, centre.z, span.z)};
}

// Unit vector a drop travels along. Down, plus a constant wind slant, so rain
// does not fall in a perfectly vertical grid.
inline glm::vec3 rain_fall_dir(const RainTuning& t) {
    return glm::normalize(glm::vec3{t.slant_x, -1.0f, t.slant_z});
}

// How many drops to actually simulate and draw at a given intensity. Zero
// intensity means ZERO drops, not "a few faint ones": a dry sky must cost
// nothing, and a field that always draws something is a field that always
// costs something.
inline int rain_drop_count(const RainTuning& t, float intensity) {
    const float i = std::clamp(intensity, 0.0f, 1.0f);
    if (i <= 0.0f) return 0;
    const float n = static_cast<float>(t.drop_count) * i;
    return std::max(1, static_cast<int>(n));
}

// A drop's position after `dt` seconds, wrapped back into the field around
// `camera_pos`. Pure in its arguments — the caller owns the drop array.
inline glm::vec3 rain_advance(glm::vec3 pos, const RainTuning& t,
                              glm::vec3 camera_pos, float dt) {
    // The field centre sits ABOVE the camera by a third of its height, so most
    // of the box is overhead where rain is visible against the sky rather than
    // below the ground where it is not.
    const glm::vec3 centre = camera_pos + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};
    const glm::vec3 moved = pos + rain_fall_dir(t) * (t.fall_speed * dt);
    return wrap_into_box(moved, centre, t.span);
}

// Deterministic initial position for drop `index`. Derived from hash_coord, not
// from a sequential stream, so drop 500 lands in the same place whether the
// field was built all at once or grown as the intensity ramped up.
inline glm::vec3 rain_seed_position(const RainTuning& t, glm::vec3 camera_pos,
                                    uint64_t seed, int index) {
    Rng r = rng_at(seed, index, 0, 0x7A14u);  // channel tag: "rain"
    const glm::vec3 centre = camera_pos + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};
    return glm::vec3{centre.x + (r.next_float() - 0.5f) * t.span.x,
                     centre.y + (r.next_float() - 0.5f) * t.span.y,
                     centre.z + (r.next_float() - 0.5f) * t.span.z};
}

}  // namespace apricot

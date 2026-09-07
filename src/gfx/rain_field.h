#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/rng.h"

namespace apricot {

// The pure maths behind the camera-locked precipitation field. GL-free and header-only
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

enum class PrecipitationType : uint8_t {
    Rain,
    Snow,
    Blizzard,
};

// Snow needs a much slower fall, broad readable flakes, and deterministic
// lateral wander. Blizzard is the same particle shape with denser, faster,
// wind-driven tuning; keeping it as a type makes the renderer choice explicit.
struct SnowTuning {
    int flake_count = 2400;
    float fall_speed = 2.4f;
    float half_size = 0.048f;
    glm::vec2 wind{0.45f, 0.12f};  // world X/Z metres per second
    float sway_amplitude = 0.42f;  // metres from the wind-centred path
    float sway_rate = 1.35f;       // radians per second
    float opacity = 0.82f;
    glm::vec3 span{62.0f, 38.0f, 62.0f};
    glm::vec3 color{0.94f, 0.97f, 1.0f};
};

inline SnowTuning default_snow_tuning(PrecipitationType type) {
    SnowTuning t;
    if (type == PrecipitationType::Blizzard) {
        t.flake_count = 4200;
        t.fall_speed = 5.8f;
        t.half_size = 0.042f;
        t.wind = {5.5f, 1.7f};
        t.sway_amplitude = 0.72f;
        t.sway_rate = 2.6f;
        t.opacity = 0.9f;
        t.span = {72.0f, 42.0f, 72.0f};
    }
    return t;
}

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
    float wrapped = offset - std::floor(offset / span) * span;

    // The postcondition is HALF-OPEN and it has to actually hold. For a large
    // offset, `offset / span` rounds up just enough that the subtraction lands
    // exactly on span (or a hair past it), and the drop escapes the box by one
    // span — one drop out of thousands, once in a while, which is precisely the
    // kind of thing that gets written off as a fluke for a month.
    if (!(wrapped < span)) wrapped = 0.0f;
    if (wrapped < 0.0f) wrapped = 0.0f;
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

inline int snow_flake_count(const SnowTuning& t, float intensity) {
    const float i = std::clamp(intensity, 0.0f, 1.0f);
    if (i <= 0.0f) return 0;
    return std::max(1, static_cast<int>(static_cast<float>(t.flake_count) * i));
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

inline glm::vec3 snow_field_centre(const SnowTuning& t, glm::vec3 camera_pos) {
    return camera_pos + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};
}

inline uint32_t snow_channel(PrecipitationType type) {
    return type == PrecipitationType::Blizzard ? 0xB112u : 0x5A0Fu;
}

// Snow placement and motion are random-looking but pure in their arguments.
// An analytic sine delta makes a flake arrive at the same point whether a
// second was simulated as one update or sixty, apart from wrapping the box.
inline glm::vec3 snow_seed_position(const SnowTuning& t, glm::vec3 camera_pos,
                                    uint64_t seed, int index,
                                    PrecipitationType type = PrecipitationType::Snow) {
    Rng r = rng_at(seed, index, 0, snow_channel(type));
    const glm::vec3 centre = snow_field_centre(t, camera_pos);
    return glm::vec3{centre.x + (r.next_float() - 0.5f) * t.span.x,
                     centre.y + (r.next_float() - 0.5f) * t.span.y,
                     centre.z + (r.next_float() - 0.5f) * t.span.z};
}

inline glm::vec3 snow_displacement(const SnowTuning& t, uint64_t seed, int index,
                                   float elapsed, float dt,
                                   PrecipitationType type = PrecipitationType::Snow) {
    const float step = std::max(dt, 0.0f);
    if (step <= 0.0f) return glm::vec3{0.0f};

    Rng r = rng_at(seed, index, 1, snow_channel(type));
    const float phase_x = r.range(0.0f, 6.28318530718f);
    const float phase_z = r.range(0.0f, 6.28318530718f);
    const float rate_x = t.sway_rate * r.range(0.72f, 1.28f);
    const float rate_z = t.sway_rate * r.range(0.72f, 1.28f);
    const float begin = std::max(elapsed, 0.0f);
    const float end = begin + step;

    const float sway_x = t.sway_amplitude *
        (std::sin(phase_x + rate_x * end) - std::sin(phase_x + rate_x * begin));
    const float sway_z = t.sway_amplitude *
        (std::cos(phase_z + rate_z * end) - std::cos(phase_z + rate_z * begin));
    return glm::vec3{t.wind.x * step + sway_x, -t.fall_speed * step,
                     t.wind.y * step + sway_z};
}

inline glm::vec3 snow_advance(glm::vec3 pos, const SnowTuning& t,
                              glm::vec3 camera_pos, uint64_t seed, int index,
                              float elapsed, float dt,
                              PrecipitationType type = PrecipitationType::Snow) {
    return wrap_into_box(pos + snow_displacement(t, seed, index, elapsed, dt, type),
                         snow_field_centre(t, camera_pos), t.span);
}

}  // namespace apricot

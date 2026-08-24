#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>

namespace apricot {

// Crowd separation — the pure steering maths for one pedestrian's per-frame
// sidestep. Header-only and glm-only, nothing from the host layer, so the
// headless gate exercises the real decision rather than a copy of it.
//
// The split: the crowd system (not in this tree yet) does the spatial-hash
// neighbour gather and the temporal smoothing; this decides one ped's lateral
// target from the neighbours it is handed.

// Neighbour search radius (m). Doubles as the spatial-hash cell size in the
// caller's neighbour gather: cell == radius means a ped only scans its own
// cell plus the eight around it.
inline constexpr float PED_SEPARATION_RADIUS = 1.3f;
// Clamp for the total sidestep (m): keeps a ped inside the 3 m sidewalk strip
// (half-width 1.5 m) clear of the kerb and the building wall.
inline constexpr float PED_SEPARATION_MAX_OFFSET = 1.1f;

// ---- Per-ped variation, rolled once at spawn (drives the "dynamic spread") --
// preferred_offset: where in the strip this ped likes to walk. Without it every
// uncrowded ped targets the exact centreline and the crowd forms a single file;
// spreading the baseline fans them across the pavement width even when no-one is
// near. space_scale: how hard this ped defends its personal space (how strongly
// it reacts to neighbours) — some bustle through, some give a wide berth.
inline constexpr float PED_PREFERRED_OFFSET_MAX = 0.75f;  // m, ± across the strip
inline constexpr float PED_SPACE_SCALE_MIN      = 0.6f;   // timid pusher
inline constexpr float PED_SPACE_SCALE_MAX      = 1.6f;   // demands more room

struct PedSeparation {
    float lateral_target = 0.f;   // clamped sidestep (m) along the lane right axis
    bool  blocked        = false; // a neighbour sits close within the forward cone
};

// `self` and `neighbours` are planar XZ positions; `fwd` is the unit lane-
// forward (XZ). The lane right vector is cross(up, fwd) = {fwd.y, -fwd.x} — the
// same axis LaneGraph::pose() offsets along, so a positive target moves the ped
// the way the separation push points. `neighbours` must EXCLUDE self.
// `preferred_offset` is this ped's baseline position in the strip; `space_scale`
// scales how strongly it reacts to neighbours. The result is the baseline plus
// the (scaled) neighbour push, clamped to the strip.
inline PedSeparation ped_separation(glm::vec2 self, glm::vec2 fwd,
                                    const glm::vec2* neighbours,
                                    std::size_t count,
                                    float preferred_offset = 0.f,
                                    float space_scale = 1.f) {
    constexpr float GAIN       = 0.85f;  // push magnitude → target offset
    constexpr float BLOCK_DIST = 0.9f;   // m — "right in front of me"
    constexpr float BLOCK_COS  = 0.5f;   // ~60° forward cone counts as blocked
    constexpr float TIE_BREAK  = 0.5f;   // head-on with no lateral → pass on right
    constexpr float R          = PED_SEPARATION_RADIUS;
    constexpr float MAX_OFF    = PED_SEPARATION_MAX_OFFSET;
    const glm::vec2 right{fwd.y, -fwd.x};

    glm::vec2 push{0.f};
    bool      blocked = false;
    for (std::size_t k = 0; k < count; ++k) {
        glm::vec2 d = self - neighbours[k];
        float dist2 = d.x * d.x + d.y * d.y;
        if (dist2 <= 1e-8f || dist2 >= R * R) continue;
        float dist = std::sqrt(dist2);
        float w    = (R - dist) / R;          // closer → stronger
        push += (d / dist) * w;
        glm::vec2 to = -d;                     // toward the neighbour
        if (dist < BLOCK_DIST && (to.x * fwd.x + to.y * fwd.y) > BLOCK_COS * dist)
            blocked = true;
    }

    // Baseline (where this ped likes to walk) + the scaled neighbour push.
    float steer  = (push.x * right.x + push.y * right.y) * GAIN * space_scale;
    float target = std::clamp(preferred_offset + steer, -MAX_OFF, MAX_OFF);
    // Head-on with no lateral push: bias off the baseline so deadlocked peds
    // pass on the right instead of standing nose-to-nose.
    if (blocked && std::abs(steer) < 0.2f)
        target = std::clamp(preferred_offset + TIE_BREAK, -MAX_OFF, MAX_OFF);
    return {target, blocked};
}

}  // namespace apricot

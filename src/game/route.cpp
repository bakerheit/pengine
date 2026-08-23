#include "game/route.h"

#include <cmath>
#include <cstddef>

#include "core/rng.h"

namespace apricot {
namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};

// --- the candidate lattice ---------------------------------------------------
// 7 x 7 centres x 4 radii = 196 candidate rings, walked in a fixed order. Most
// die on their first bad gate, so the search is far cheaper than the worst case
// suggests.
constexpr int kCentreHalfSteps = 3;       // -3..3 on each axis
constexpr float kCentreStep = 260.0f;     // metres
constexpr int kRadiusSteps = 4;
constexpr float kRadiusMin = 190.0f;
constexpr float kRadiusStep = 55.0f;

// Per-gate local search. Radius is the outer loop, so a gate prefers to keep
// the ring's shape and swing sideways before it gives up and moves in or out.
constexpr int kRadialProbes = 9;
constexpr float kRadialStep = 0.075f;   // fraction of the ring radius
constexpr int kAngularProbes = 5;
constexpr float kAngularSpan = 0.22f;   // fraction of the gate spacing

// Interior samples taken along each link when scoring a ring.
constexpr int kLinkSamples = 12;

// Fold a probe index into the sequence 0, +1, -1, +2, -2, ... so the search
// spirals out from the intended spot instead of sweeping from one edge.
int probe_offset(int i) {
    const int magnitude = (i + 1) / 2;
    return (i % 2 == 1) ? magnitude : -magnitude;
}

// The seed's own taste in corners: two harmonics of radius wobble, so the loop
// is a lobed shape rather than a circle, and still closes exactly because both
// harmonics are whole numbers of cycles per lap.
struct RouteShape {
    float start_angle = 0.0f;
    int harm_a = 2;
    int harm_b = 4;
    float amp_a = 0.0f;
    float amp_b = 0.0f;
    float phase_a = 0.0f;
    float phase_b = 0.0f;
};

RouteShape shape_from_seed(uint64_t seed) {
    // Salted so the route's shape is uncorrelated with the terrain's lattice
    // at the same seed — otherwise the wobble lines up with the hills and every
    // world gets the same relationship between corner and crest.
    Rng rng{splitmix64_mix(seed ^ 0x524F55544530ull)};  // "ROUTE0"

    RouteShape s;
    s.start_angle = rng.range(0.0f, kTwoPi);
    s.harm_a = rng.next_int(2, 3);
    s.harm_b = rng.next_int(4, 5);
    s.amp_a = rng.range(0.10f, 0.26f);
    s.amp_b = rng.range(0.03f, 0.11f);
    s.phase_a = rng.range(0.0f, kTwoPi);
    s.phase_b = rng.range(0.0f, kTwoPi);
    return s;
}

float shape_radius_scale(const RouteShape& s, float angle) {
    return 1.0f +
           s.amp_a * std::sin(static_cast<float>(s.harm_a) * angle + s.phase_a) +
           s.amp_b * std::sin(static_cast<float>(s.harm_b) * angle + s.phase_b);
}

struct Ring {
    bool ok = false;
    int link_score = 0;
    std::vector<glm::vec2> points;
};

bool far_enough(glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = a - b;
    return glm::dot(d, d) >= kGateMinSpacing * kGateMinSpacing;
}

// One candidate ring: `count` gates around `centre`, each nudged to the nearest
// drivable spot that also keeps its distance from the gate before it.
Ring build_ring(const TerrainCollider& collider, const RouteShape& shape,
                glm::vec2 centre, float radius, int count) {
    Ring ring;
    ring.points.reserve(static_cast<std::size_t>(count));

    const float spacing = kTwoPi / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        const float base_angle =
            shape.start_angle + spacing * static_cast<float>(i);
        const float base_radius = radius * shape_radius_scale(shape, base_angle);

        bool placed = false;
        for (int probe = 0; probe < kRadialProbes * kAngularProbes && !placed;
             ++probe) {
            const int radial = probe_offset(probe / kAngularProbes);
            const int angular = probe_offset(probe % kAngularProbes);

            const float r =
                base_radius * (1.0f + kRadialStep * static_cast<float>(radial));
            const float a = base_angle + spacing * kAngularSpan *
                                             static_cast<float>(angular) * 0.5f;
            const glm::vec2 p{centre.x + std::cos(a) * r,
                              centre.y + std::sin(a) * r};

            if (!is_gate_ground(collider, p.x, p.y)) continue;
            if (!ring.points.empty() && !far_enough(p, ring.points.back())) {
                continue;
            }
            // The loop closes, so the last gate answers to the first as well.
            if (i == count - 1 && !far_enough(p, ring.points.front())) continue;

            ring.points.push_back(p);
            placed = true;
        }

        // A ring that cannot place a gate is dead; do not keep probing the rest
        // of it just to produce a more detailed failure.
        if (!placed) return ring;
    }

    ring.ok = true;

    // Score by how much of the ground BETWEEN the gates is passable. Gates on
    // drivable ground with a lake across every link is a valid route and a
    // miserable one.
    for (int i = 0; i < count; ++i) {
        const glm::vec2 a = ring.points[static_cast<std::size_t>(i)];
        const glm::vec2 b =
            ring.points[static_cast<std::size_t>((i + 1) % count)];
        for (int s = 1; s <= kLinkSamples; ++s) {
            const float u =
                static_cast<float>(s) / static_cast<float>(kLinkSamples + 1);
            const glm::vec2 p = a + (b - a) * u;
            if (is_link_ground(collider, p.x, p.y)) ++ring.link_score;
        }
    }
    return ring;
}

}  // namespace

bool is_gate_ground(const TerrainCollider& collider, float x, float z) {
    if (collider.height(x, z) < kGateMinHeight) return false;
    return collider.normal(x, z).y >= kGateMinNormalY;
}

bool is_link_ground(const TerrainCollider& collider, float x, float z) {
    if (collider.height(x, z) < kLinkMinHeight) return false;
    return collider.normal(x, z).y >= kLinkMinNormalY;
}

glm::vec3 gate_post_left(const Checkpoint& gate) {
    return gate.position - gate.right * gate.half_width;
}

glm::vec3 gate_post_right(const Checkpoint& gate) {
    return gate.position + gate.right * gate.half_width;
}

GateCrossing sweep_gate(const Checkpoint& gate, glm::vec3 p0, glm::vec3 p1) {
    GateCrossing out;

    // Signed distance from the gate plane at each end of the step. Negative is
    // the approach side.
    const float s0 = glm::dot(p0 - gate.position, gate.forward);
    const float s1 = glm::dot(p1 - gate.position, gate.forward);

    // Front-to-back only. A car going the other way through the line gets
    // s0 >= 0 and s1 < 0 and is ignored, which is why there is no separate
    // "is the velocity pointing the right way" test — that one is fooled by a
    // sideways slide, this one cannot be.
    if (!(s0 < 0.0f && s1 >= 0.0f)) return out;

    const float denom = s0 - s1;
    if (!(denom < 0.0f || denom > 0.0f)) return out;  // exactly parallel

    const float t = glm::clamp(s0 / denom, 0.0f, 1.0f);
    const glm::vec3 hit = p0 + (p1 - p0) * t;
    const glm::vec3 d = hit - gate.position;

    if (std::fabs(glm::dot(d, gate.right)) > gate.half_width) return out;
    if (std::fabs(glm::dot(d, kWorldUp)) > gate.half_height) return out;

    out.crossed = true;
    out.t = t;
    out.point = hit;
    return out;
}

Route build_route(uint64_t seed, const TerrainCollider& collider, int count) {
    Route route;
    route.seed = seed;
    route.closed = true;

    // Two gates cannot describe a loop, and one cannot describe anything.
    if (count < 3) return route;

    const RouteShape shape = shape_from_seed(seed);

    Ring best;
    for (int cz = -kCentreHalfSteps; cz <= kCentreHalfSteps; ++cz) {
        for (int cx = -kCentreHalfSteps; cx <= kCentreHalfSteps; ++cx) {
            const glm::vec2 centre{static_cast<float>(cx) * kCentreStep,
                                   static_cast<float>(cz) * kCentreStep};
            for (int ri = 0; ri < kRadiusSteps; ++ri) {
                const float radius =
                    kRadiusMin + kRadiusStep * static_cast<float>(ri);
                Ring ring = build_ring(collider, shape, centre, radius, count);
                if (!ring.ok) continue;
                // Strictly greater, so a tie keeps the earlier candidate and the
                // iteration order alone decides. No float comparison anywhere
                // near the choice.
                if (!best.ok || ring.link_score > best.link_score) {
                    best = ring;
                }
            }
        }
    }

    if (!best.ok) return route;  // no gate in the sea; no route at all.

    route.checkpoints.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const glm::vec2 here = best.points[idx];
        const glm::vec2 prev =
            best.points[static_cast<std::size_t>((i + count - 1) % count)];
        const glm::vec2 next =
            best.points[static_cast<std::size_t>((i + 1) % count)];

        Checkpoint& cp = route.checkpoints[idx];
        cp.position = glm::vec3{here.x, collider.height(here.x, here.y), here.y};

        // Central difference around the loop, not the direction to the next
        // gate: on a hairpin the two differ by most of the corner, and a gate
        // square to the exit is a gate you cannot cut on the entry.
        const glm::vec2 tangent = next - prev;
        const float len = std::sqrt(glm::dot(tangent, tangent));
        cp.forward = (len > 1e-4f)
                         ? glm::vec3{tangent.x / len, 0.0f, tangent.y / len}
                         : glm::vec3{0.0f, 0.0f, -1.0f};
        cp.right = glm::cross(cp.forward, kWorldUp);
        cp.half_width = kGateHalfWidth;
        cp.half_height = kGateHalfHeight;
    }
    return route;
}

}  // namespace apricot

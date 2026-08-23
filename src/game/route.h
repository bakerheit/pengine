#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "physics/terrain_collider.h"

namespace apricot {

// The checkpoint route: a closed loop of timing gates laid over the procedural
// island, derived from the SAME seed as the terrain.
//
// A gate is a PLANE SEGMENT, not a trigger sphere. That is the whole reason
// this file exists. A sphere is entered when the car is near a point, which
// means a car launched off a crest can skip it entirely, a car idling on the
// line can trigger it twice, and the recorded split is "the step that noticed"
// rather than "the moment the line was cut". A plane segment is cut exactly
// once per pass, at a known fraction of the step, no matter how far the car
// moved during it.
//
// Everything here is a pure function of the seed and the collider. No clock,
// no globals, no allocation beyond the returned Route.

// --- what counts as drivable ------------------------------------------------
// Sea level is y = 0 by construction (terrain/heightmap.h centres the field on
// zero), so a gate's height above the water is just its y.

// Gates carry the timing, so they get the strict bar: well clear of the water
// and on ground a car can actually sit on.
inline constexpr float kGateMinHeight = 2.5f;

// Steepest ground a gate may stand on, as the minimum surface-normal Y — that
// is, cos(max slope). Kept in that form because it is exactly what the
// collider hands back; converting to degrees on every probe just to compare it
// back to a threshold is work for nothing.
inline constexpr float kGateMinNormalY = 0.87f;  // ~29.5 degrees

// The straight line between two gates is sampled and held to a looser bar:
// gates have to be parkable, the ground between them only has to be passable.
inline constexpr float kLinkMinHeight = 0.25f;
inline constexpr float kLinkMinNormalY = 0.70f;  // ~45.5 degrees

inline constexpr float kGateHalfWidth = 9.0f;

// The vertical band is deliberately far more generous than the posts a
// renderer will draw. It exists to reject something passing high overhead, not
// to be a second width check: the crossing point is INTERPOLATED across a
// step, so a car covering ground quickly on a slope legitimately produces a
// crossing point several metres off the gate's centre height. A tight band
// there turns "you were going fast over a crest" into "your lap did not
// count", which is the worst bug this file could have.
inline constexpr float kGateHalfHeight = 12.0f;

// Two gates closer together than this are the same corner counted twice.
inline constexpr float kGateMinSpacing = 70.0f;

// Ground tests, exposed because the route tests assert against the same
// predicate the generator used — a private threshold and a test's own copy of
// it drift apart, and then the test is checking last month's rule.
bool is_gate_ground(const TerrainCollider& collider, float x, float z);
bool is_link_ground(const TerrainCollider& collider, float x, float z);

struct Checkpoint {
    // Gate centre, ON the terrain surface: position.y == collider.height(x, z)
    // exactly, so a renderer can plant the posts from this without re-probing
    // and getting a subtly different answer.
    glm::vec3 position{0.0f};

    // Route direction through the gate: horizontal, unit length, pointing the
    // way a car is meant to be travelling as it passes. It doubles as the gate
    // plane's normal, so the sign of the car's distance along it is what
    // "before the line" and "past the line" mean.
    glm::vec3 forward{0.0f, 0.0f, -1.0f};

    // The gate line itself: horizontal, unit length, forward x up. The posts
    // stand at position +/- right * half_width.
    glm::vec3 right{1.0f, 0.0f, 0.0f};

    float half_width = kGateHalfWidth;

    // Vertical half-extent. The car has to cut the plane INSIDE this band, so
    // a gate on a hillside is not triggered by something sailing over the top
    // of it with thirty metres of air underneath.
    float half_height = kGateHalfHeight;
};

struct Route {
    uint64_t seed = 0;
    std::vector<Checkpoint> checkpoints;

    // True when the last checkpoint feeds back into the first (a circuit);
    // false for a point-to-point stage.
    bool closed = true;
};

// The two posts, for a renderer. Left is -right: the gate's own frame, not the
// viewer's, so it does not flip when the camera swings round behind it.
glm::vec3 gate_post_left(const Checkpoint& gate);
glm::vec3 gate_post_right(const Checkpoint& gate);

// Where the swept car path cut a gate plane, if it did.
struct GateCrossing {
    bool crossed = false;

    // Fraction of the swept segment at which the plane was cut, in [0, 1]. The
    // lap clock is advanced by exactly this fraction, so a split is the moment
    // the car crossed the line rather than the end of the step that noticed.
    float t = 0.0f;

    glm::vec3 point{0.0f};
};

// Sweep the segment p0 -> p1 against one gate.
//
// Counts ONLY a front-to-back crossing — from the -forward side of the plane
// to the +forward side — inside the gate's width and height. Reversing back
// over a line is therefore worth nothing, without needing a separate velocity
// check that a sideways slide can fool.
GateCrossing sweep_gate(const Checkpoint& gate, glm::vec3 p0, glm::vec3 p1);

// Lay out a checkpoint route over the terrain.
//
// The island's drivable land is wherever the seed put it, so the loop's centre
// and radius are SEARCHED rather than assumed: a fixed lattice of candidate
// rings is scored against the terrain and the best one wins. The lattice is
// fixed on purpose — a stream of random guesses would make the answer depend
// on how many times the stream had been pulled, which is the one thing
// core/rng.h tells you not to build.
//
// Every returned gate is guaranteed to satisfy is_gate_ground() and to sit at
// least kGateMinSpacing from its neighbours, and route.seed == seed.
//
// Returns a route with NO checkpoints when it cannot place all `count` gates
// on drivable ground — the caller gets a refusal it can see rather than a
// route with a gate in the sea. Check with route_ok().
Route build_route(uint64_t seed, const TerrainCollider& collider, int count);

// True when `route` holds the gate count that was asked for.
inline bool route_ok(const Route& route, int count) {
    return count > 0 && route.checkpoints.size() == static_cast<std::size_t>(count);
}

}  // namespace apricot

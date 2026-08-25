#pragma once

#include <cmath>
#include <cstdint>

#include "city/map.h"
#include "city/roads.h"

namespace apricot {
namespace city {

// TERRAIN OPERATORS — how authored intent reaches the height field.
//
// Retuning the noise constants gets you a plausible island. It does not get
// you a flat downtown, a harbour deep enough for a ship, or terraces on a
// hillside, because there is no constant you can turn that means "this
// particular square kilometre is a city". Without operators the map has to beg
// the noise for a flat downtown and never gets one: measured over nineteen
// candidate seeds, the fraction of land under 5 degrees swings from 32% to
// 76% on the seed alone, and none of them puts the flat part where the city
// wants it.
//
// So: a small authored table of shapes, each with a rule, evaluated
// ANALYTICALLY inside height_at().
//
//     h = ops( metres( noise_shape(x, z) ), x, z )
//
// FIVE KINDS, AND ONLY FIVE.
//
//   Flatten  pulls the field toward a target, feathered over a margin
//   Bench    flattens to the nearest of N terrace levels
//   Carve    pushes the field DOWN to a target, below sea level if asked
//   Mound    raises the field UP to a target
//   Grade    flattens along a swept corridor to that corridor's own profile
//
// THE RULES THAT KEEP THEM HONEST
//
// * PURE, like everything else the height field touches. No state, no cache,
//   no clock, no allocation. Two constexpr tables and arithmetic.
// * C1 AT THE EDGES. Every op feathers with a Hermite smoothstep, never with a
//   max() against a floor. A hard floor draws a visible contour line around
//   the flattened area on every single seed — the spawn-lift dome that used to
//   live in heightmap.cpp learned that and left a comment saying so, and then
//   was deleted for putting a 380 m bulge under the financial district.
// * OPS COMPOSE IN TABLE ORDER, AND THE ORDER IS THE MAP. A carve after a
//   flatten is a dry dock; a flatten after a carve is a filled-in one. The
//   table below is grouped by kind for exactly this reason and the grouping is
//   the authored decision, not tidiness.
// * EVERY OP IS A BLEND TOWARD A TARGET, so the result always lies between the
//   field and the target. That is what lets a compile-time assertion on the
//   TARGETS guarantee the height field's analytic bounds still hold — see
//   kOpTargetFloorMetres.
//
// WHAT IS NOT HERE, AND WHY
//
// The design document describes op shapes as "a polygon or a swept corridor".
// Polygon distance is the expensive one — you cannot get a feathered edge
// without a distance to the boundary, and that is a loop over edges with a
// sqrt each, inside the innermost terrain loop. An axis-aligned rectangle, a
// circle and a swept corridor turned out to cover every operator the map
// actually needed, and a corridor with two points IS an oriented rectangle.
// There is no trigonometry anywhere in this file as a result, which is not
// only cheaper: std::cos and std::sin carry no correct-rounding requirement,
// exactly like the std::pow that heightmap.cpp refuses to call, and a
// last-bit difference in a rotation is a different city from the same seed.

// Floating-point contraction is switched off here for the same reason
// terrain/noise.h switches it off: this file is nothing but multiply-adds, and
// one machine fusing where another does not is a world that differs in the
// last bit. Clang only, because clang's pragma is the only one that reliably
// does what it says; on another toolchain pass -ffp-contract=off at the build
// level and re-pin the golden heights rather than assuming.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

// ---------------------------------------------------------------------------
//  Kernel
// ---------------------------------------------------------------------------

constexpr float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Hermite smoothstep, clamped outside the edges.
//
// DELIBERATELY SPELLED AGAIN rather than included from terrain/noise.h. The
// module dependency runs terrain -> city and must not run back, or evaluating
// an operator can reach into the field the operator is modifying. It is the
// identical polynomial, and tests/city_map_tests.cpp asserts the two agree
// BIT FOR BIT across a sweep, so "they drifted apart" is a test failure rather
// than a subtle seam at the edge of every flattened district.
constexpr float smoothstep01(float edge0, float edge1, float v) {
    const float span = edge1 - edge0;
    if (span <= 0.0f) return v < edge0 ? 0.0f : 1.0f;
    const float t = clamp01((v - edge0) / span);
    return t * t * (3.0f - 2.0f * t);
}

// 1 inside `inner`, 0 beyond `inner + feather`, C1 at both ends.
constexpr float falloff(float inner, float feather, float d) {
    return 1.0f - smoothstep01(inner, inner + feather, d);
}

enum class OpKind : uint8_t {
    Flatten,  // toward target_m
    Bench,    // toward the nearest terrace of (target_m + k * step_m)
    Carve,    // toward target_m, but never raises the ground
    Mound,    // toward target_m, but never lowers it
    Grade     // toward the corridor's own vertical profile
};

enum class OpShape : uint8_t {
    Circle,    // centre + radius_m
    Rect,      // centre + half_m, axis aligned
    Corridor   // polyline + half_width_m, carrying a vertical profile
};

// Longest corridor the tables may author.
//
// It is exactly kMaxRoadPoints (roads.h), and that is load-bearing rather than
// tidy: ONE ROAD MUST BECOME EXACTLY ONE CORRIDOR.
//
// It used to be 8, and roads longer than that were split into several
// corridors sharing an endpoint. That is wrong in a way that is invisible
// until you measure it. A corridor's nearest-point search CLAMPS to its own
// polyline, so past its last control point it keeps applying that point's
// height as a flat cap for a whole half width. Two corridors meeting end to
// end therefore each flatten the other's approach, and the later one in the
// table wins -- which turns the last twenty metres of a climbing road into a
// level shelf and then a lump, on ground that is supposed to be a constant
// gradient. Splitting a road is the one thing that manufactures that, so a
// road is never split.
inline constexpr int kMaxCorridorPoints = kMaxRoadPoints;

// A corridor control point: position in the XZ plane and the height the road
// or channel is at THERE. `y` is authored, not draped — that is the whole
// point of Grade. A road draped onto whatever the noise did is how the
// reference implementation ended up reconciling two disagreeing ground heights
// after the fact.
struct CorridorPoint {
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.0f;
};

// Legal range for any op target, in metres about sea level.
//
// This is the bridge between two modules that must not include each other.
// heightmap.cpp static_asserts that this window sits inside the height field's
// own analytic bounds; the table below static_asserts every target sits inside
// this window. Since every op is a blend toward its target, the composition of
// the two assertions is a compile-time proof that operators cannot push a
// height outside the bounds culling and streaming already rely on.
inline constexpr float kOpTargetFloorMetres = -40.0f;
inline constexpr float kOpTargetCeilingMetres = 140.0f;

struct TerrainOp {
    OpKind kind = OpKind::Flatten;
    OpShape shape = OpShape::Circle;

    // WHY THIS OP EXISTS, in a few words. It costs a pointer in a table that
    // is thirty entries long and it is the difference between a code review
    // and a wall of numbers. Also printed by the measurement suite, so a
    // failing district measurement names the op that shaped it.
    const char* note = nullptr;

    Vec2 centre{};        // Circle, Rect
    float radius_m = 0.0f;  // Circle
    Vec2 half_m{};        // Rect, half extents

    CorridorPoint path[kMaxCorridorPoints]{};  // Corridor
    int path_count = 0;
    float half_width_m = 0.0f;

    // Width of the feathered margin outside the shape. Never zero: a zero
    // feather is the hard floor this whole design exists to avoid.
    float feather_m = 0.0f;

    float target_m = 0.0f;  // Flatten / Carve / Mound target, Bench base
    float step_m = 0.0f;    // Bench terrace height
    float riser = 0.0f;     // Bench: fraction of a step spent on the riser

    // Peak weight, in [0, 1]. Below 1 the op leaves some of the noise showing,
    // which is how a suburb gets to be flat-ish without being a billiard
    // table. 1 means "this is exactly the height, everywhere inside".
    float strength = 1.0f;

    // --- derived, all constexpr, none of it authored -------------------------

    constexpr Vec2 bounds_min() const {
        if (shape == OpShape::Circle) {
            return Vec2{centre.x - radius_m - feather_m,
                        centre.z - radius_m - feather_m};
        }
        if (shape == OpShape::Rect) {
            return Vec2{centre.x - half_m.x - feather_m,
                        centre.z - half_m.z - feather_m};
        }
        float mx = path[0].x, mz = path[0].z;
        for (int i = 1; i < path_count; ++i) {
            if (path[i].x < mx) mx = path[i].x;
            if (path[i].z < mz) mz = path[i].z;
        }
        return Vec2{mx - half_width_m - feather_m, mz - half_width_m - feather_m};
    }

    constexpr Vec2 bounds_max() const {
        if (shape == OpShape::Circle) {
            return Vec2{centre.x + radius_m + feather_m,
                        centre.z + radius_m + feather_m};
        }
        if (shape == OpShape::Rect) {
            return Vec2{centre.x + half_m.x + feather_m,
                        centre.z + half_m.z + feather_m};
        }
        float mx = path[0].x, mz = path[0].z;
        for (int i = 1; i < path_count; ++i) {
            if (path[i].x > mx) mx = path[i].x;
            if (path[i].z > mz) mz = path[i].z;
        }
        return Vec2{mx + half_width_m + feather_m, mz + half_width_m + feather_m};
    }

    // Every target this op can produce must sit inside the legal window. For a
    // Grade that is every control point's authored height; for a Bench it is
    // the base and the top of the terraces it can reach.
    constexpr bool targets_in_range() const {
        if (kind == OpKind::Grade) {
            for (int i = 0; i < path_count; ++i) {
                if (path[i].y < kOpTargetFloorMetres) return false;
                if (path[i].y > kOpTargetCeilingMetres) return false;
            }
            return true;
        }
        if (kind == OpKind::Bench) {
            // A bench only ever moves a height to the terrace below or above
            // it, so it cannot leave the field's own range by more than one
            // step. Check both ends of that.
            return target_m - step_m >= kOpTargetFloorMetres &&
                   target_m + step_m <= kOpTargetCeilingMetres;
        }
        return target_m >= kOpTargetFloorMetres &&
               target_m <= kOpTargetCeilingMetres;
    }

    constexpr bool well_formed() const {
        if (feather_m <= 0.0f) return false;            // no hard floors
        if (strength <= 0.0f || strength > 1.0f) return false;
        if (shape == OpShape::Circle && radius_m <= 0.0f) return false;
        if (shape == OpShape::Rect &&
            (half_m.x <= 0.0f || half_m.z <= 0.0f)) {
            return false;
        }
        if (shape == OpShape::Corridor) {
            if (path_count < 2 || path_count > kMaxCorridorPoints) return false;
            if (half_width_m <= 0.0f) return false;
        }
        if (kind == OpKind::Bench && (step_m <= 0.0f || riser <= 0.0f ||
                                      riser >= 1.0f)) {
            return false;
        }
        if (kind == OpKind::Grade && shape != OpShape::Corridor) return false;
        const Vec2 lo = bounds_min();
        const Vec2 hi = bounds_max();
        if (lo.x < -kWorldHalfMetres || lo.z < -kWorldHalfMetres) return false;
        if (hi.x > kWorldHalfMetres || hi.z > kWorldHalfMetres) return false;
        return targets_in_range();
    }
};

// ---------------------------------------------------------------------------
//  Evaluation
// ---------------------------------------------------------------------------

// Coverage weight of one op at a point, plus — for a corridor — the profile
// height at the nearest point on it. Zero outside the shape's feathered edge.
inline float op_weight(const TerrainOp& op, float x, float z, float& profile_y) {
    switch (op.shape) {
        case OpShape::Circle: {
            const float dx = x - op.centre.x;
            const float dz = z - op.centre.z;
            const float r2 = dx * dx + dz * dz;
            const float outer = op.radius_m + op.feather_m;
            if (r2 >= outer * outer) return 0.0f;
            return falloff(op.radius_m, op.feather_m, std::sqrt(r2));
        }
        case OpShape::Rect: {
            const float dx = std::fabs(x - op.centre.x);
            const float dz = std::fabs(z - op.centre.z);
            if (dx >= op.half_m.x + op.feather_m) return 0.0f;
            if (dz >= op.half_m.z + op.feather_m) return 0.0f;
            // The PRODUCT of the two axis falloffs, not the falloff of a
            // corner distance. The product is separable, needs no sqrt, and
            // rounds the corners slightly — which is what a real graded plate
            // looks like anyway.
            return falloff(op.half_m.x, op.feather_m, dx) *
                   falloff(op.half_m.z, op.feather_m, dz);
        }
        case OpShape::Corridor: {
            float best_d2 = -1.0f;
            float best_y = 0.0f;
            for (int i = 0; i + 1 < op.path_count; ++i) {
                const CorridorPoint a = op.path[i];
                const CorridorPoint b = op.path[i + 1];
                const float ex = b.x - a.x;
                const float ez = b.z - a.z;
                const float len2 = ex * ex + ez * ez;
                float t = 0.0f;
                if (len2 > 0.0f) {
                    t = clamp01(((x - a.x) * ex + (z - a.z) * ez) / len2);
                }
                const float px = a.x + ex * t;
                const float pz = a.z + ez * t;
                const float dx = x - px;
                const float dz = z - pz;
                const float d2 = dx * dx + dz * dz;
                if (best_d2 < 0.0f || d2 < best_d2) {
                    best_d2 = d2;
                    best_y = a.y + (b.y - a.y) * t;
                }
            }
            if (best_d2 < 0.0f) return 0.0f;
            const float outer = op.half_width_m + op.feather_m;
            if (best_d2 >= outer * outer) return 0.0f;
            profile_y = best_y;
            return falloff(op.half_width_m, op.feather_m, std::sqrt(best_d2));
        }
    }
    return 0.0f;
}

// Apply one op to one height.
//
// Every kind is the same two steps -- work out the target, then blend toward
// it -- and that uniformity is what makes the bounds proof in
// kOpTargetFloorMetres hold for all five.
inline float apply_op(const TerrainOp& op, float h, float x, float z) {
    float profile_y = 0.0f;
    const float w = op_weight(op, x, z, profile_y) * op.strength;
    if (w <= 0.0f) return h;

    float target = op.target_m;
    switch (op.kind) {
        case OpKind::Grade:
            // The corridor's own authored vertical profile, interpolated
            // along it. The road decides its height and the terrain conforms;
            // draping the road onto whatever the noise did is how the
            // reference implementation ended up reconciling two disagreeing
            // ground heights after the fact.
            target = profile_y;
            break;

        case OpKind::Bench: {
            // Flat treads with smooth risers. A plain round() to the nearest
            // terrace has an infinite gradient at every half-step, and a
            // height field cannot express a retaining wall -- it expresses it
            // as one sample of near-vertical slope, which the normal takes
            // personally. So: hold the tread flat over most of the step and
            // spend the last `riser` fraction climbing, through a smoothstep,
            // which leaves the result C1 in the height it is terracing.
            const float f = (h - op.target_m) / op.step_m;
            const float k = std::floor(f);
            const float frac = f - k;
            const float s = smoothstep01(1.0f - op.riser, 1.0f, frac);
            target = op.target_m + op.step_m * (k + s);
            break;
        }

        default:
            break;
    }

    // AT FULL WEIGHT THE OPERATOR *IS* THE TARGET, spelled as a branch rather
    // than left to `h + (target - h) * 1.0f`. That expression is algebraically
    // the target and is not always the target in floating point: it rounds
    // twice, so a plate authored dead level comes out varying in the last bit
    // or two across its whole area. This is continuous -- the branch takes the
    // value the blend converges to -- and it is what lets a test assert that
    // the runway, the downtown plate and the authored spawn point are at the
    // heights the table says, by equality rather than by tolerance.
    float out = (w >= 1.0f) ? target : h + (target - h) * w;

    // Carve never raises and Mound never lowers. The clamp only bites where
    // the ground is already past the target -- open water inside a harbour
    // mouth, or a hill already taller than a berm -- so the crease it leaves
    // is a shoreline rather than a contour line across a hillside.
    if (op.kind == OpKind::Carve && out > h) out = h;
    if (op.kind == OpKind::Mound && out < h) out = h;
    return out;
}

// ---------------------------------------------------------------------------
//  The table
// ---------------------------------------------------------------------------

// TABLE ORDER IS COMPOSITION ORDER, and it is authored:
//
//   1. Flatten  — the plates the districts are built on
//   2. Bench    — terraces cut into a plate or a hillside
//   3. Mound    — berms and spoil heaps raised on top
//   4. Carve    — the water, cut LAST so a basin inside an apron stays wet.
//                 Move a carve above its flatten and you fill in the harbour.
//   5. Grade    — roads, which win over everything, because a road that a
//                 district plate half-buried is how you get a 30% gradient
//                 nobody authored.
inline constexpr TerrainOp kBaseOps[] = {
    // ---- 1. Flatten: the district plates ---------------------------------
    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Vellum Row: the downtown plate. A grid on rolling ground reads "
             "as a mistake even to a player who could not say why",
     .centre = {70.0f, -40.0f},
     .half_m = {520.0f, 440.0f},
     .feather_m = 240.0f,
     .target_m = 12.0f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Circle,
     .note = "Halloway Square: one civic plaza, four radial arms, all of it "
             "level so the plaza steps are the only vertical thing in it",
     .centre = {-70.0f, 780.0f},
     .radius_m = 400.0f,
     .feather_m = 260.0f,
     .target_m = 14.0f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Saltmarsh: a low mat just above the water line. The old town is "
             "on the flood plain because that is where the fishing was",
     .centre = {-980.0f, 60.0f},
     .half_m = {430.0f, 380.0f},
     .feather_m = 220.0f,
     .target_m = 5.5f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Ostend Docks: the container apron, cut into a 15-23 m coastal "
             "bluff. Dead level, because a straddle carrier does not do "
             "gradients",
     .centre = {-1560.0f, -640.0f},
     .half_m = {450.0f, 330.0f},
     .feather_m = 210.0f,
     .target_m = 5.0f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Kepler Flats: refinery and rail yard. Rail is why it is flat - "
             "a level crossing on a slope is not a level crossing",
     .centre = {-560.0f, -1760.0f},
     .half_m = {620.0f, 330.0f},
     .feather_m = 260.0f,
     .target_m = 9.0f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Nickel Heights: suburbs. Strength 0.75 on purpose - the streets "
             "should roll a little, or every sight line is infinite",
     .centre = {1080.0f, 340.0f},
     .half_m = {420.0f, 430.0f},
     .feather_m = 300.0f,
     .target_m = 11.0f,
     .strength = 0.75f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Rect,
     .note = "Camber Point: the airfield. The flattest large surface in the "
             "world, which is where handling gets tested and stunts land",
     .centre = {150.0f, 2140.0f},
     .half_m = {520.0f, 180.0f},
     .feather_m = 200.0f,
     .target_m = 6.0f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Corridor,
     .note = "The Strand: 2.2 km of promenade behind the beach, one gentle "
             "curve, so the only decision on it is the throttle",
     .path = {{1960.0f, -420.0f, 14.0f},
              {2020.0f, 300.0f, 12.0f},
              {1860.0f, 1000.0f, 10.0f},
              {1620.0f, 1560.0f, 8.0f}},
     .path_count = 4,
     .half_width_m = 150.0f,
     .feather_m = 190.0f,
     .target_m = 11.0f,
     .strength = 0.9f},

    {.kind = OpKind::Flatten,
     .shape = OpShape::Circle,
     .note = "Marrow: the farm pad. One flat thing in the whole district, so "
             "the dirt web around it reads as rough by comparison",
     .centre = {-1080.0f, 1180.0f},
     .radius_m = 200.0f,
     .feather_m = 190.0f,
     .target_m = 36.0f,
     .strength = 0.8f},

    // ---- 2. Bench: terraces ----------------------------------------------
    {.kind = OpKind::Bench,
     .shape = OpShape::Circle,
     .note = "Ferrone Hill: mansion plots. Terraces are what make a hillside "
             "buildable, and the risers are what make the switchbacks legible",
     .centre = {860.0f, -1420.0f},
     .radius_m = 520.0f,
     .feather_m = 300.0f,
     .target_m = 40.0f,
     .step_m = 9.0f,
     .riser = 0.34f,
     .strength = 0.85f},

    // ---- 3. Mound: berms and heaps ---------------------------------------
    {.kind = OpKind::Mound,
     .shape = OpShape::Circle,
     .note = "The stadium berm in Nickel Heights. A District-tier landmark "
             "has to be visible over a suburb, so the bowl sits up on ground",
     .centre = {1250.0f, 720.0f},
     .radius_m = 150.0f,
     .feather_m = 140.0f,
     .target_m = 22.0f},

    {.kind = OpKind::Mound,
     .shape = OpShape::Circle,
     .note = "Marrow's spoil heap. Everything the quarry took out had to go "
             "somewhere, and the somewhere is a jump",
     .centre = {-1180.0f, 1560.0f},
     .radius_m = 160.0f,
     .feather_m = 150.0f,
     .target_m = 78.0f},

    // ---- 4. Carve: the water ---------------------------------------------
    {.kind = OpKind::Carve,
     .shape = OpShape::Corridor,
     .note = "The Kessel Channel. Cuts the north off from the centre, which "
             "is the entire reason there is a bridge to block",
     .path = {{-2600.0f, -1120.0f, 0.0f},
              {-1600.0f, -1180.0f, 0.0f},
              {-600.0f, -1220.0f, 0.0f},
              {180.0f, -1260.0f, 0.0f}},
     .path_count = 4,
     .half_width_m = 200.0f,
     .feather_m = 210.0f,
     .target_m = -13.0f},

    {.kind = OpKind::Carve,
     .shape = OpShape::Corridor,
     .note = "The harbour basin at Ostend. Deep enough for a ship, short "
             "enough to leave the eastern two thirds of the apron dry",
     .path = {{-2380.0f, -580.0f, 0.0f},
              {-2200.0f, -600.0f, 0.0f}},
     .path_count = 2,
     .half_width_m = 100.0f,
     .feather_m = 100.0f,
     .target_m = -11.0f},

    {.kind = OpKind::Carve,
     .shape = OpShape::Corridor,
     .note = "The Saltmarsh creek. Four narrow crossings, all passable, none "
             "fast: chases fragment here instead of stopping",
     .path = {{-1560.0f, 420.0f, 0.0f},
              {-1100.0f, 300.0f, 0.0f},
              {-700.0f, 90.0f, 0.0f},
              {-480.0f, -260.0f, 0.0f}},
     .path_count = 4,
     .half_width_m = 15.0f,
     .feather_m = 34.0f,
     .target_m = -1.5f},

    {.kind = OpKind::Carve,
     .shape = OpShape::Circle,
     .note = "Marrow's quarry pit, 60 m into the side of a 120 m hill. A ramp "
             "with a drop on the outside, and no cruiser follows you down it",
     .centre = {-1480.0f, 1300.0f},
     .radius_m = 180.0f,
     .feather_m = 150.0f,
     .target_m = 46.0f},

    {.kind = OpKind::Carve,
     .shape = OpShape::Corridor,
     .note = "The Camber channel: severs the southern peninsula end to end, "
             "so the only way to the plane is the causeway graded back across "
             "it below. This pair is why composition order is part of the map",
     .path = {{-1150.0f, 1980.0f, 0.0f},
              {-650.0f, 1820.0f, 0.0f},
              {0.0f, 1730.0f, 0.0f},
              {700.0f, 1690.0f, 0.0f}},
     .path_count = 4,
     .half_width_m = 140.0f,
     .feather_m = 160.0f,
     .target_m = -7.0f},

};

inline constexpr int kBaseOpCount =
    static_cast<int>(sizeof(kBaseOps) / sizeof(kBaseOps[0]));

// ---------------------------------------------------------------------------
//  5. Grade: roads win, and they are DERIVED FROM THE ROAD TABLE
// ---------------------------------------------------------------------------
//
// There is no hand-written Grade in kBaseOps and there must never be one
// again. Every Grade corridor below is generated from `kRoads` in roads.h --
// the same table map_spines() hands to the road module -- so a road and the
// ground it sits on cannot disagree about where the road is. They used to be
// two tables: five corridors here and, later, a spine list over there. Two
// descriptions of one road is the oldest failure in this repo wearing a new
// hat, and the symptom would have been a carriageway floating over a ridge
// with an operator note explaining, confidently, why the ridge was correct.
//
// The derivation is deliberately dull:
//
//   * only roads with `shapes_ground` (see the long note on that field -- a
//     decked road must never grade, and a road on an exactly-flat plate has
//     nothing to gain);
//   * half width from the class, via Road::corridor_half_m(), which is the
//     ribbon's own footprint plus kLodCorridorMarginM so that every LOD level
//     agrees about where the road bed is;
//   * STRENGTH EXACTLY 1.0, always. Below 1 the operator leaves some of the
//     noise showing, which is lovely for a suburb and fatal here: at less than
//     full weight the corridor is no longer planar, and a road bed that is not
//     planar is a road bed a coarse lattice cannot reproduce. The whole
//     level-of-detail argument rests on this number being one.
//
// ONE ROAD, ONE CORRIDOR. See the note on kMaxCorridorPoints for why a road is
// never split into two.

constexpr int road_corridor_count(const Road& r) { return r.shapes_ground ? 1 : 0; }

constexpr int count_road_grade_ops() {
    int n = 0;
    for (int i = 0; i < kRoadCount; ++i) n += road_corridor_count(kRoads[i]);
    return n;
}

inline constexpr int kRoadGradeOpCount = count_road_grade_ops();
inline constexpr int kTerrainOpCount = kBaseOpCount + kRoadGradeOpCount;

// The whole table, with an INT subscript.
//
// Every loop over the operators counts with an int -- kTerrainOpCount is an
// int, because the bucket index stores op numbers as uint8_t and its
// static_asserts compare against int. std::array subscripts by size_t, so
// using one directly would mean a cast at every call site in this header and
// in every test that walks the table. One cast, here, instead.
struct TerrainOpTable {
    TerrainOp ops[kTerrainOpCount]{};

    constexpr const TerrainOp& operator[](int i) const { return ops[i]; }
    constexpr TerrainOp& operator[](int i) { return ops[i]; }
};

// The authored plates, terraces, berms and water, then every road. Grade still
// composes LAST, which is what keeps the Camber Causeway on top of the Camber
// channel instead of at the bottom of it.
constexpr TerrainOpTable build_terrain_ops() {
    TerrainOpTable out{};
    int w = 0;
    for (int i = 0; i < kBaseOpCount; ++i) out[w++] = kBaseOps[i];

    for (int i = 0; i < kRoadCount; ++i) {
        const Road& r = kRoads[i];
        if (!r.shapes_ground) continue;

        TerrainOp op{};
        op.kind = OpKind::Grade;
        op.shape = OpShape::Corridor;
        // The road's own name is the operator's note, so a failing measurement
        // names the road rather than an index.
        op.note = r.name;
        for (int p = 0; p < r.count; ++p) {
            op.path[p] = CorridorPoint{r.path[p].x, r.path[p].z, r.path[p].y};
        }
        op.path_count = r.count;
        op.half_width_m = r.corridor_half_m();
        op.feather_m = r.feather_m();
        op.strength = 1.0f;
        out[w++] = op;
    }
    return out;
}

inline constexpr TerrainOpTable kTerrainOps = build_terrain_ops();

// ---------------------------------------------------------------------------
//  The bucket index
// ---------------------------------------------------------------------------
//
// height_at() is called 4225 times per chunk for the mesh, four more times per
// vertex for the normals, and again by every physics ground query. A naive
// loop over the whole op table adds that many shape tests to every one of
// them. So the table is bucketed by a 128 m grid over the world box: 48 x 48
// cells, built ONCE, AT COMPILE TIME.
//
// THIS IS DATA, NOT A CACHE, and the distinction is the whole reason it is
// allowed to exist inside a function documented as having no statics. It is
// `constexpr`, so it is computed by the compiler and lives in read-only
// memory next to the noise constants. There is no lazy initialisation, no
// first-call path, no mutation and nothing to invalidate. If it were built on
// demand it would be a cache, it would have an initialisation order, and it
// would be exactly the stale-entry desync the purity rule exists to prevent.
inline constexpr float kOpBucketMetres = 128.0f;
inline constexpr int kOpBucketsPerSide =
    static_cast<int>(2.0f * kWorldHalfMetres / kOpBucketMetres);
inline constexpr int kOpBucketCount = kOpBucketsPerSide * kOpBucketsPerSide;

// Ops touching one 128 m cell. Twelve is generous — the measured worst cell in
// the table above holds far fewer — and the static_assert below turns "one op
// too many" into a build failure rather than a silently missing harbour.
inline constexpr int kMaxOpsPerBucket = 12;

struct OpIndex {
    uint8_t count[kOpBucketCount]{};
    uint8_t op[kOpBucketCount][kMaxOpsPerBucket]{};
    bool overflowed = false;
    int max_in_bucket = 0;
    int occupied = 0;
};

constexpr int op_bucket_axis(float v) {
    const float t = (v + kWorldHalfMetres) / kOpBucketMetres;
    if (t < 0.0f) return -1;
    const int i = static_cast<int>(t);
    return i >= kOpBucketsPerSide ? -1 : i;
}

constexpr int op_bucket_axis_clamped(float v) {
    const float t = (v + kWorldHalfMetres) / kOpBucketMetres;
    if (t < 0.0f) return 0;
    const int i = static_cast<int>(t);
    return i >= kOpBucketsPerSide ? kOpBucketsPerSide - 1 : i;
}

// List op `i` in every bucket its box touches.
//
// Called once per SHAPE for a circle or a rectangle, and once per SEGMENT for a
// corridor, which is the whole reason it is a function.
constexpr void index_op_box(OpIndex& ix, int i, Vec2 lo, Vec2 hi) {
    const int x0 = op_bucket_axis_clamped(lo.x);
    const int x1 = op_bucket_axis_clamped(hi.x);
    const int z0 = op_bucket_axis_clamped(lo.z);
    const int z1 = op_bucket_axis_clamped(hi.z);
    for (int bz = z0; bz <= z1; ++bz) {
        for (int bx = x0; bx <= x1; ++bx) {
            const int b = bz * kOpBucketsPerSide + bx;
            // Already listed here for this op? Every box belonging to one op is
            // inserted consecutively, so the last entry is the only one that
            // can be a duplicate -- and skipping it keeps each bucket's list
            // both ascending AND free of repeats, which is what lets the walk
            // in apply_terrain_ops() apply each op exactly once, in table
            // order.
            if (ix.count[b] > 0 &&
                ix.op[b][ix.count[b] - 1] == static_cast<uint8_t>(i)) {
                continue;
            }
            if (ix.count[b] >= kMaxOpsPerBucket) {
                ix.overflowed = true;
                continue;
            }
            ix.op[b][ix.count[b]] = static_cast<uint8_t>(i);
            ix.count[b] = static_cast<uint8_t>(ix.count[b] + 1);
            if (ix.count[b] > ix.max_in_bucket) {
                ix.max_in_bucket = ix.count[b];
            }
            if (ix.count[b] == 1) ++ix.occupied;
        }
    }
}

// A CORRIDOR IS INDEXED SEGMENT BY SEGMENT, NOT BY ITS BOUNDING BOX.
//
// The box of a road that runs 2.2 km diagonally is 2.2 km on a side and the
// road touches almost none of it. Before the road table landed the whole map
// held five corridors and the waste did not matter; now it holds one per road,
// and a dozen of them crossing the same square kilometre would each claim
// every bucket in it and blow kMaxOpsPerBucket -- turning "the map has roads"
// into "the operator index silently dropped a harbour".
//
// It is also strictly MORE correct in the sense that matters: the union of the
// per-segment boxes still covers every point the op can reach, because any
// point within (half width + feather) of the polyline is within that distance
// of one of its segments. Nothing the op affects can fall outside the index.
constexpr OpIndex build_op_index() {
    OpIndex ix{};
    for (int i = 0; i < kTerrainOpCount; ++i) {
        const TerrainOp& op = kTerrainOps[i];
        if (op.shape != OpShape::Corridor) {
            index_op_box(ix, i, op.bounds_min(), op.bounds_max());
            continue;
        }
        const float r = op.half_width_m + op.feather_m;
        for (int k = 0; k + 1 < op.path_count; ++k) {
            const CorridorPoint a = op.path[k];
            const CorridorPoint b = op.path[k + 1];
            const float minx = (a.x < b.x ? a.x : b.x) - r;
            const float maxx = (a.x > b.x ? a.x : b.x) + r;
            const float minz = (a.z < b.z ? a.z : b.z) - r;
            const float maxz = (a.z > b.z ? a.z : b.z) + r;
            index_op_box(ix, i, Vec2{minx, minz}, Vec2{maxx, maxz});
        }
    }
    return ix;
}

inline constexpr OpIndex kOpIndex = build_op_index();

// ---------------------------------------------------------------------------
//  The entry point
// ---------------------------------------------------------------------------

// Apply the authored terrain operators to a height, at a position.
//
// PURE. A function of (height, x, z) and two constexpr tables, and of nothing
// else — no seed, because operators are AUTHORED and must not vary with the
// noise, and no state at all.
//
// Outside the world box there are no operators, so the ocean costs one compare.
inline float apply_terrain_ops(float h, float x, float z) {
    const int bx = op_bucket_axis(x);
    if (bx < 0) return h;
    const int bz = op_bucket_axis(z);
    if (bz < 0) return h;

    const int b = bz * kOpBucketsPerSide + bx;
    const int n = kOpIndex.count[b];
    for (int k = 0; k < n; ++k) {
        h = apply_op(kTerrainOps[kOpIndex.op[b][k]], h, x, z);
    }
    return h;
}

// How much of a ROAD corridor covers this point, 0 outside and 1 on the
// carriageway centreline. Non-zero means "a road shaped the ground here".
//
// This exists so scatter can stop planting trees in the carriageway. It
// deliberately asks the SAME op_weight() that graded the ground rather than
// re-deriving a corridor from the road table: a second opinion about where the
// road is would drift from the first one, and the drift would look like trees
// creeping onto the tarmac over several commits with nobody able to say when it
// started. Collision derives from the geometry that draws; so does this.
//
// Road ops are the tail of the table — everything from kBaseOpCount up is
// derived from kRoads, which is what makes the index test cheap and exact.
//
// The feathered margin is included on purpose. It is the shoulder, and a tree
// hanging over the kerb is the same complaint as a tree in the road.
inline float road_corridor_weight(float x, float z) {
    const int bx = op_bucket_axis(x);
    if (bx < 0) return 0.0f;
    const int bz = op_bucket_axis(z);
    if (bz < 0) return 0.0f;

    const int b = bz * kOpBucketsPerSide + bx;
    const int n = kOpIndex.count[b];
    float most = 0.0f;
    for (int k = 0; k < n; ++k) {
        const int idx = kOpIndex.op[b][k];
        if (idx < kBaseOpCount) continue;  // district plates, not roads
        float unused_profile = 0.0f;
        const float w = op_weight(kTerrainOps[idx], x, z, unused_profile);
        if (w > most) most = w;
    }
    return most;
}

// ---------------------------------------------------------------------------
//  Compile-time invariants
// ---------------------------------------------------------------------------

static_assert(kTerrainOpCount > 0, "the op table is empty");
static_assert(kTerrainOpCount <= 255,
              "op indices are stored as uint8_t in the bucket index");
static_assert(kOpBucketsPerSide == 48, "the world box is not 6144 m wide");
static_assert(!kOpIndex.overflowed,
              "a 128 m cell holds more than kMaxOpsPerBucket operators - raise "
              "the cap or split the op, but do NOT let the index drop one");
static_assert(kOpIndex.max_in_bucket > 0, "the op index indexed nothing");

// Every op, checked individually, at compile time. A feather of zero, a
// corridor with one point, a Grade on a circle, a target outside the height
// field's range, or a shape hanging outside the world box all fail HERE.
constexpr bool all_ops_well_formed() {
    for (int i = 0; i < kTerrainOpCount; ++i) {
        if (!kTerrainOps[i].well_formed()) return false;
        if (kTerrainOps[i].note == nullptr) return false;
    }
    return true;
}
static_assert(all_ops_well_formed(),
              "a terrain operator is malformed - see TerrainOp::well_formed()");

// Composition order is the map, so it is pinned: all Flattens, then Benches,
// then Mounds, then Carves, then Grades. Reordering the table is allowed and
// is an authoring decision; reordering it BY ACCIDENT, by pasting a new op in
// the wrong group, is what this catches.
constexpr bool ops_are_grouped_by_kind() {
    int rank = -1;
    for (int i = 0; i < kTerrainOpCount; ++i) {
        int r = 0;
        switch (kTerrainOps[i].kind) {
            case OpKind::Flatten: r = 0; break;
            case OpKind::Bench:   r = 1; break;
            case OpKind::Mound:   r = 2; break;
            case OpKind::Carve:   r = 3; break;
            case OpKind::Grade:   r = 4; break;
        }
        if (r < rank) return false;
        rank = r;
    }
    return true;
}
static_assert(ops_are_grouped_by_kind(),
              "the op table is out of composition order: flatten, bench, "
              "mound, carve, grade. A carve above its flatten fills in the "
              "harbour and nothing will tell you but the water");

}  // namespace city
}  // namespace apricot

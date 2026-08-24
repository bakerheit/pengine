#pragma once

#include <cstdint>

#include "city/map.h"

namespace apricot {
namespace city {

// PINATTY'S ROAD NETWORK — the authored spines, as compiled C++ data.
//
// This is the table `map_spines()` turns into `RoadSpine`s for src/road/, and
// it is ALSO the table the Grade terrain operators are derived from. Those are
// the same roads, so they are one table. The alternative — a spine list here
// and a hand-written corridor operator over there — is two descriptions of one
// road, and the only thing two descriptions of one thing ever do is disagree,
// slowly, in the direction of a carriageway floating over a ridge nobody
// authored. See kRoadGradeOps in terrain_ops.h for the derivation.
//
// It sits beside districts.h for the reason map.h gives at length: the map is
// a skeleton, generation cannot fail because it is arithmetic, and compiled
// data gets -Werror, designated initialisers and static_assert for free.
//
// MODULE DEPENDENCY. This header knows nothing about src/road/. It speaks in
// `city::RoadClass` (map.h), and city/spines.cpp — the ONE file in this module
// that includes road/ — translates. That keeps `terrain -> city` one-way: this
// header is pulled in by terrain_ops.h, which heightmap.cpp includes, and
// dragging src/road/ along that path would put the road module inside the
// height field's include graph for no benefit at all. spines.cpp static_asserts
// that the two modules' width and sidewalk tables agree, so the translation
// cannot drift silently.
//
// ---------------------------------------------------------------------------
//  WHAT THE ROADS ARE FOR (docs/design/pinatty.md sections 2 and 3)
// ---------------------------------------------------------------------------
//
// A district is not its palette, it is how it CHASES. Every shape below is
// chosen for that and nothing else:
//
//   Vellum Row     a true grid. Every junction is four choices, so escape is
//                  about reading the pursuit rather than out-driving it.
//   Saltmarsh      6 m alleys, no sidewalk, no through route, and cut-throughs
//                  a local knows. A cruiser cannot swing these corners.
//   Ferrone Hill   ONE paved way up (the Shoulder) plus an unmarked fire road.
//                  Block the Shoulder and the hill is sealed to anyone who has
//                  not found the dirt.
//   The Strand     2.2 km straight, zero junctions. The only decision is the
//                  throttle, and a roadblock on it is genuinely frightening.
//   Camber Point   reached by one causeway over the one land bridge in 2.5 km
//                  of water. One stopped vehicle closes the road to the plane.
//   Nickel Heights collector loops and dead ends. About a third go nowhere,
//                  and the correct play — leaving — is made to feel too slow.
//
// Route 1, the Rimway, is DELIBERATELY NOT A RING. Its two ends are cul-de-sacs
// (Kepler Flats in the north-west, Camber Point in the south-east), so at both
// ends you have to make a decision at speed instead of just continuing.
//
// ---------------------------------------------------------------------------
//  WHERE THE COORDINATES CAME FROM
// ---------------------------------------------------------------------------
//
// Measured off the terrain kMapSeed actually produces, not drawn on a napkin.
// The two facts that shaped the whole network:
//
//   * The only land connection between the north (Kepler Flats, Ferrone Hill)
//     and the rest of the island is x = 470..1705 at the foot of Ferrone Hill.
//     Everything west of that is 400-630 m of open water. That is why the
//     Kessel Bridge exists and why blocking it costs a pursuit four kilometres.
//   * z = 1420 is the only continuously dry line from the causeway head east to
//     the Strand; at z = 1500 it crosses water twice.
//
// tests/city_roads_tests.cpp re-measures both on every run and prints what it
// found, so a terrain change that drops a road in the sea is a test failure
// rather than a swim.

// ---------------------------------------------------------------------------
//  Authoring types
// ---------------------------------------------------------------------------

// Longest authored road. A road needing more than this is several roads, and
// saying so in a static_assert is cheaper than finding a truncated highway.
inline constexpr int kMaxRoadPoints = 16;

// One control point of a road centreline: position in the XZ plane, and the
// height the ROAD BED is at there.
//
// `y` is authored, never draped, and it means two different things depending on
// which side of `shapes_ground` the road is on.
//
//   * For a road that SHAPES the ground it is the Grade corridor's own vertical
//     profile, and the terrain conforms to it. The road decides.
//   * For a road that does not, it is a CLAIM ABOUT WHERE THE GROUND ALREADY
//     IS, recorded to the nearest half metre, and tests/city_roads_tests.cpp
//     holds it to that claim within a metre. An unchecked "documentation" field
//     is a field that goes stale and then gets believed.
//
// The second kind earns its keep. It caught three Saltmarsh alleys meeting on
// what turned out to be Route 1's embankment, two and a half metres above the
// marsh they are supposed to be part of, and it is why Vellum Row's southern
// streets read 12.5 and 13.5 rather than a tidy 12.0 — Halloway Square's plate
// feathers 660 m and genuinely lifts that edge of downtown.
struct RoadPoint {
    float x = 0.0f;
    float z = 0.0f;
    float y = 0.0f;
};

// How a road meets the ground it crosses. Mirrors apricot::RoadStructure in
// road/road_graph.h; spines.cpp maps one onto the other and static_asserts the
// enumerator values agree.
enum class RoadStructure : uint8_t {
    Ground = 0,  // drapes onto the terrain
    Bridge = 1,  // authored deck height, flat, NOT draped
    Tunnel = 2,  // authored deck height, flat, under the terrain
    Cut = 3,     // drapes, and the corridor operator carves down to it
    Fill = 4,    // drapes, and the corridor operator builds up to it
};

constexpr bool road_structure_is_decked(RoadStructure s) {
    return s == RoadStructure::Bridge || s == RoadStructure::Tunnel;
}

// A sidewalk strip this wide hugs each carriageway edge on the classes that
// have them. Restated here rather than included from road/road_class.h for the
// module-dependency reason at the top of this file; spines.cpp static_asserts
// the two agree, exactly as city and terrain do for smoothstep.
inline constexpr float kWalkWidthM = 3.0f;

constexpr bool road_has_sidewalks(RoadClass c) {
    return c == RoadClass::Arterial || c == RoadClass::Street;
}

// Half the width of everything a road DRAWS: carriageway plus its sidewalks.
// This is the footprint the ground has to be flat under, and it is what the
// corridor operator's half width is derived from.
constexpr float road_ribbon_half_m(RoadClass c) {
    return road_width_m(c) * 0.5f + (road_has_sidewalks(c) ? kWalkWidthM : 0.0f);
}

// ---------------------------------------------------------------------------
//  THE NUMBER THAT MAKES A ROAD SIT ON THE GROUND AT EVERY LEVEL OF DETAIL
// ---------------------------------------------------------------------------
//
// A ribbon is baked onto the LEVEL 0 drawn surface. Draw the terrain under it
// at level 3 and the two are no longer the same surface: measured over the
// quarry, level 3 disagrees with level 0 by up to 1.020 m, which is why road
// draw distance was pinned at 640 m before this table existed.
//
// A Grade corridor fixes it, and the reason is exact rather than approximate.
// Inside the corridor at FULL WEIGHT the height is the corridor's own profile:
// linear along the path, constant across it — a PLANE. Every LOD level samples
// the same global lattice and interpolates linearly between its samples, and
// linear interpolation of a plane is that plane. So where the corridor is at
// full weight, every level agrees exactly, and the drape error is not small,
// it is zero.
//
// "Where the corridor is at full weight" is the whole trick, and it is why this
// margin is not decoration. Level 3 samples every 8 m. To reconstruct a point
// the mesher interpolates the four lattice corners AROUND it, and those sit up
// to 8 m away in x and 8 m away in z — up to 8*sqrt(2) = 11.32 m in the plane.
// So a point at the very edge of the ribbon needs full corridor weight out to
// 11.32 m beyond it, or one of the corners it is interpolated from sits in the
// feathered margin and drags the surface off the plane.
//
// 12 m, therefore: 11.32 rounded up, with the remainder as slack. Anything less
// is a road that is exact in the middle and floats at the kerb.
inline constexpr float kLodCorridorMarginM = 12.0f;

// Feathered margin outside a road corridor. Never zero — a hard edge draws a
// contour line along every road on the island, which is the exact failure the
// operator design exists to avoid (terrain_ops.h says so at length).
inline constexpr float kRoadCorridorFeatherM = 30.0f;

struct Road {
    // Short authored name. Printed by the measurement suite, so a failing road
    // says which road it is instead of which index it is.
    const char* name = nullptr;

    // STABLE AUTHORED IDENTITY, and never this entry's index in the table.
    // Everything downstream keys entropy on it — lane choices, roadblock sites
    // — so an id that moved when somebody inserted a road above it would
    // re-roll half the city. Unique, checked at compile time.
    uint32_t id = 0;

    // Which district's population scalars ride on this road. Count means the
    // countryside, which has its own (low) figures.
    DistrictId district = DistrictId::Count;

    RoadClass cls = RoadClass::Street;
    RoadStructure structure = RoadStructure::Ground;

    // Roadblock staging quality, straight through to the police layer. 0 means
    // never stage here; 255 means this is what this road is for. Authored
    // rather than derived because a purely derived selector will eventually
    // stage two cruisers across a hairpin at the top of the Shoulder and kill
    // the player with a cutscene (pinatty section 3.1).
    uint8_t block_quality = 128;

    // Deck height for Bridge / Tunnel, metres. Ignored otherwise.
    float deck_y_m = 0.0f;

    // Carriageway width override, metres. <= 0 means "use the class table".
    // A set value is used EXACTLY as given and is never snapped toward the
    // class width — see the PCG-170 note in road/road_class.h.
    float width_m = 0.0f;

    // Does this road shape the ground under it (a Grade corridor operator)?
    //
    // NOT ALWAYS TRUE, AND THE FALSE CASES ARE THE INTERESTING ONES.
    //
    //   * A decked road (Bridge, Tunnel) must NEVER shape the ground. Its
    //     ribbon is flat at deck_y_m and needs no help; a Grade under it would
    //     fill in the channel it was built to cross. That is a compile-time
    //     error below, not a convention.
    //   * A road lying entirely on a district plate that is already flat at
    //     full strength — Vellum Row's 12.0 m, Halloway's 14.0 m, Saltmarsh's
    //     5.5 m, Ostend's 5.0 m, Kepler's 9.0 m, Camber's 6.0 m — is already
    //     exact at every level, because a constant is as planar as a plane
    //     gets. An operator there would be pure cost, and a hundred of them
    //     would be a hundred times pure cost inside height_at().
    //
    // It is authored, but it is not TRUSTED: tests/city_roads_tests.cpp
    // measures level-3 drape under every road in this table and fails the ones
    // that need a corridor and do not have one.
    bool shapes_ground = false;

    // Extra flat ground either side of the ribbon, metres. 0 takes
    // kLodCorridorMarginM. Lower it only where the width of the flat ground IS
    // the gameplay — the causeway is the one place on the island where that is
    // true — and expect the measurement suite to report the drape you bought.
    float corridor_margin_m = 0.0f;

    // 0 takes kRoadCorridorFeatherM.
    float corridor_feather_m = 0.0f;

    // Centreline, world XZ plus the authored bed height. At least two points.
    RoadPoint path[kMaxRoadPoints]{};
    int count = 0;

    // --- derived, all constexpr, none of it authored -------------------------

    constexpr float ribbon_half_m() const {
        return width_m > 0.0f ? width_m * 0.5f + (road_has_sidewalks(cls)
                                                      ? kWalkWidthM
                                                      : 0.0f)
                              : road_ribbon_half_m(cls);
    }

    constexpr float corridor_half_m() const {
        return ribbon_half_m() + (corridor_margin_m > 0.0f ? corridor_margin_m
                                                           : kLodCorridorMarginM);
    }

    constexpr float feather_m() const {
        return corridor_feather_m > 0.0f ? corridor_feather_m
                                         : kRoadCorridorFeatherM;
    }

    // Planar length, metres.
    constexpr float length_m() const {
        float acc = 0.0f;
        for (int i = 0; i + 1 < count; ++i) {
            const float dx = path[i + 1].x - path[i].x;
            const float dz = path[i + 1].z - path[i].z;
            // No std::sqrt in a constant expression before C++26, and this is
            // authoring data rather than a hot path, so: Newton, seeded on the
            // larger component. Ten iterations is far past convergence for
            // anything a road segment can be.
            const float s = dx * dx + dz * dz;
            float r = s > 1.0f ? s : 1.0f;
            for (int k = 0; k < 24; ++k) r = 0.5f * (r + s / r);
            acc += r;
        }
        return acc;
    }

    // Steepest longitudinal grade, as a fraction. A road a loaded truck cannot
    // pull is an authoring mistake, and 25 per cent is generous even for the
    // fire road.
    constexpr float max_grade() const {
        float worst = 0.0f;
        for (int i = 0; i + 1 < count; ++i) {
            const float dx = path[i + 1].x - path[i].x;
            const float dz = path[i + 1].z - path[i].z;
            const float s = dx * dx + dz * dz;
            if (s <= 0.0f) continue;
            float r = s > 1.0f ? s : 1.0f;
            for (int k = 0; k < 24; ++k) r = 0.5f * (r + s / r);
            float dy = path[i + 1].y - path[i].y;
            if (dy < 0.0f) dy = -dy;
            const float g = dy / r;
            if (g > worst) worst = g;
        }
        return worst;
    }

    constexpr bool well_formed() const {
        if (name == nullptr) return false;
        if (count < 2 || count > kMaxRoadPoints) return false;
        if (width_m < 0.0f) return false;
        // A decked road that also grades the ground fills in the thing it was
        // built to cross. This is the Camber channel / causeway lesson applied
        // one level up.
        if (road_structure_is_decked(structure) && shapes_ground) return false;
        if (corridor_margin_m < 0.0f || corridor_feather_m < 0.0f) return false;
        for (int i = 0; i < count; ++i) {
            if (path[i].x < -kWorldHalfMetres) return false;
            if (path[i].x > kWorldHalfMetres) return false;
            if (path[i].z < -kWorldHalfMetres) return false;
            if (path[i].z > kWorldHalfMetres) return false;
        }
        // Two identical consecutive points are a zero-length segment, which is
        // a division by zero waiting in every consumer that normalises a
        // tangent.
        for (int i = 0; i + 1 < count; ++i) {
            const float dx = path[i + 1].x - path[i].x;
            const float dz = path[i + 1].z - path[i].z;
            if (dx * dx + dz * dz < 1.0f) return false;
        }
        if (max_grade() > 0.25f) return false;
        return true;
    }
};

// ---------------------------------------------------------------------------
//  The table
// ---------------------------------------------------------------------------
//
// Grouped by what the road is FOR, not by class. Ids are grouped in the same
// blocks so inserting a road into a district does not want a renumber.
//
//     1-19    Route 1, the fixed crossings, and the causeway
//    20-49    Vellum Row: the grid
//    50-59    Halloway Square: the plaza and its arms
//    60-79    Saltmarsh: lanes, cut-throughs and the four creek crossings
//    80-89    Ostend Docks
//    90-99    Kepler Flats
//   100-119   Ferrone Hill: the Shoulder, the fire road, the terrace streets
//   120-139   Nickel Heights: collectors, loops and dead ends
//   140-149   The Strand
//   150-159   Camber Point
//   160-179   Marrow
//   180-199   inter-district links
inline constexpr Road kRoads[] = {

    // =======================================================================
    //  1-19. ROUTE 1 — the Rimway. An open chain, never a ring.
    // =======================================================================

    {.name = "Route 1 - the Kepler Reach",
     .id = 1,
     .district = DistrictId::KeplerFlats,
     .cls = RoadClass::Freeway,
     .block_quality = 200,
     .shapes_ground = true,
     .path = {{60.0f, -1760.0f, 9.0f},
              {-500.0f, -1790.0f, 9.0f},
              {-1050.0f, -1720.0f, 9.0f},
              {-1380.0f, -1600.0f, 10.5f},
              {-1500.0f, -1524.0f, 12.0f}},
     .count = 5},

    // THE PREMIER ROADBLOCK SITE ON THE ISLAND, and the reason boats will
    // matter. 460 m of deck with no exits over 430 m of open water: commit to
    // it and both ends are known. It is a Bridge and therefore does NOT shape
    // the ground — a Grade here would fill in the Kessel Channel and quietly
    // delete the chokepoint, which is the same mistake as sorting the operator
    // table, made with a different tool.
    {.name = "the Kessel Bridge",
     .id = 2,
     .district = DistrictId::Count,
     .cls = RoadClass::Freeway,
     .structure = RoadStructure::Bridge,
     .block_quality = 255,
     .deck_y_m = 12.0f,
     .path = {{-1500.0f, -1524.0f, 12.0f}, {-1500.0f, -1064.0f, 12.0f}},
     .count = 2},

    {.name = "Route 1 - the Rimway",
     .id = 3,
     .district = DistrictId::OstendDocks,
     .cls = RoadClass::Freeway,
     .block_quality = 190,
     .shapes_ground = true,
     .path = {{-1500.0f, -1064.0f, 12.0f},
              {-1480.0f, -700.0f, 8.0f},
              {-1420.0f, -300.0f, 8.5f},
              {-1240.0f, 120.0f, 9.0f}},
     .count = 4},

    {.name = "Route 1 - the Marsh Reach",
     .id = 4,
     .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Freeway,
     .block_quality = 150,
     .shapes_ground = true,
     .path = {{-1240.0f, 120.0f, 9.0f},
              {-1050.0f, 110.0f, 7.0f},
              {-928.0f, 142.0f, 6.0f}},
     .count = 3},

    // Route 1 crosses the Saltmarsh creek on a deck rather than an embankment,
    // so the creek stays a creek. An embankment here would dam it and turn one
    // of the four soft crossings into a hard one.
    {.name = "the Rimway Creek Bridge",
     .id = 5,
     .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Freeway,
     .structure = RoadStructure::Bridge,
     .block_quality = 210,
     .deck_y_m = 6.0f,
     .path = {{-928.0f, 142.0f, 6.0f}, {-872.0f, 248.0f, 6.0f}},
     .count = 2},

    {.name = "Route 1 - the Vellum Reach",
     .id = 6,
     .district = DistrictId::VellumRow,
     .cls = RoadClass::Freeway,
     .block_quality = 120,  // in town a block is a suggestion, not a wall
     .shapes_ground = true,
     .path = {{-872.0f, 248.0f, 6.0f},
              {-620.0f, 400.0f, 8.0f},
              {-380.0f, 470.0f, 11.0f},
              {-40.0f, 490.0f, 12.0f},
              {420.0f, 520.0f, 12.0f},
              {1120.0f, 560.0f, 12.0f}},
     .count = 6},

    {.name = "Route 1 - the Nickel Reach",
     .id = 7,
     .district = DistrictId::NickelHeights,
     .cls = RoadClass::Freeway,
     .block_quality = 140,
     .shapes_ground = true,
     .path = {{1120.0f, 560.0f, 12.0f},
              {1450.0f, 300.0f, 12.5f},
              {1700.0f, -100.0f, 13.0f},
              {1780.0f, -380.0f, 12.5f},
              {1960.0f, -680.0f, 12.0f}},
     .count = 5},

    // 2.24 km, straight, and NOT ONE JUNCTION ON IT. Both ends are the only
    // ways on or off, which is the entire point: nothing to think about but the
    // throttle, and a roadblock with no alternative route to fall back to.
    // Adding a shape point here is fine; adding a road that touches it in the
    // middle is deleting the district.
    {.name = "Route 1 - the Strand",
     .id = 8,
     .district = DistrictId::TheStrand,
     .cls = RoadClass::Freeway,
     .block_quality = 255,
     .shapes_ground = true,
     .path = {{1960.0f, -680.0f, 12.0f}, {1610.0f, 1530.0f, 8.0f}},
     .count = 2},

    // z = 1420 is not a rounded-off number, it is the only continuously dry
    // line between the causeway head and the Strand. At z = 1500 the same run
    // crosses water twice.
    {.name = "Route 1 - the Camber Reach",
     .id = 9,
     .district = DistrictId::Count,
     .cls = RoadClass::Freeway,
     .block_quality = 180,
     .shapes_ground = true,
     .path = {{1610.0f, 1530.0f, 8.0f},
              {1350.0f, 1430.0f, 8.0f},
              {1020.0f, 1420.0f, 13.0f},
              {620.0f, 1420.0f, 18.0f},
              {215.0f, 1420.0f, 16.0f}},
     .count = 5},

    // THE MOST CONTESTED 900 m ON THE MAP. Two lanes, water both sides, and a
    // deliberately tight corridor: the margin is 4 m rather than the usual 12,
    // because the width of the dry ground IS the chokepoint here and buying
    // perfect level-3 drape with it would buy a strip you can drive a roadblock
    // around. tests/city_roads_tests.cpp prints exactly what that costs.
    {.name = "the Camber Causeway",
     .id = 10,
     .district = DistrictId::CamberPoint,
     .cls = RoadClass::Street,
     .block_quality = 255,
     .shapes_ground = true,
     .corridor_margin_m = 4.0f,
     .corridor_feather_m = 24.0f,
     .path = {{215.0f, 1420.0f, 16.0f},
              {200.0f, 1650.0f, 10.0f},
              {185.0f, 1880.0f, 7.0f},
              {170.0f, 2130.0f, 6.0f}},
     .count = 4},

    // =======================================================================
    //  20-49. VELLUM ROW — the grid.
    // =======================================================================
    //
    // Nine north-south streets 92 m apart and eleven east-west 62 m apart, on
    // the district's authored 6-degree rotation and its authored 92 x 62 m
    // block. Ninety-nine four-way junctions: EVERY ONE OF THEM IS FOUR CHOICES,
    // which is what makes this the district where you escape by reading the
    // pursuit rather than by out-driving it.
    //
    // None of them shapes the ground and none of them needs to. The Vellum Row
    // plate is a Flatten at strength 1.0, so the whole grid stands on ground
    // that is exactly 12.0 m at every sample — and a constant is as planar as a
    // plane gets, so every LOD level already agrees to the bit.
    //
    // The coordinates are the rotation arithmetic done once, not typed nine and
    // eleven times: centre (70, -40), north (sin 6, -cos 6), east (cos 6, sin 6).

    {.name = "Vellum NS 1", .id = 20, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-261.5f, -406.7f, 12.0f}, {-330.5f, 249.7f, 12.5f}}, .count = 2},
    {.name = "Vellum NS 2", .id = 21, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-170.0f, -397.0f, 12.0f}, {-239.0f, 259.3f, 13.0f}}, .count = 2},
    {.name = "Vellum NS 3", .id = 22, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-78.5f, -387.4f, 12.0f}, {-147.5f, 269.0f, 12.0f}}, .count = 2},
    {.name = "Vellum NS 4", .id = 23, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{13.0f, -377.8f, 12.0f}, {-56.0f, 278.6f, 13.5f}}, .count = 2},
    // The middle north-south run is the district spine and carries the traffic,
    // so it is the one street here wide enough to be worth blocking.
    {.name = "Vellum Row (the street)", .id = 24, .district = DistrictId::VellumRow,
     .cls = RoadClass::Arterial, .block_quality = 160,
     .path = {{104.5f, -368.2f, 12.0f}, {35.5f, 288.2f, 13.5f}}, .count = 2},
    {.name = "Vellum NS 6", .id = 25, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{196.0f, -358.6f, 12.0f}, {127.0f, 297.8f, 13.0f}}, .count = 2},
    {.name = "Vellum NS 7", .id = 26, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{287.5f, -349.0f, 12.0f}, {218.5f, 307.4f, 12.0f}}, .count = 2},
    {.name = "Vellum NS 8", .id = 27, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{379.0f, -339.3f, 12.0f}, {310.0f, 317.0f, 12.5f}}, .count = 2},
    {.name = "Vellum NS 9", .id = 28, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{470.5f, -329.7f, 12.0f}, {401.5f, 326.7f, 12.0f}}, .count = 2},

    {.name = "Vellum EW 1", .id = 30, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-380.1f, 224.4f, 11.0f}, {455.3f, 312.2f, 12.0f}}, .count = 2},
    {.name = "Vellum EW 2", .id = 31, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-373.6f, 162.7f, 11.5f}, {461.8f, 250.5f, 12.0f}}, .count = 2},
    {.name = "Vellum EW 3", .id = 32, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-367.1f, 101.1f, 11.5f}, {468.3f, 188.9f, 12.0f}}, .count = 2},
    {.name = "Vellum EW 4", .id = 33, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-360.7f, 39.4f, 11.5f}, {474.7f, 127.2f, 12.0f}}, .count = 2},
    {.name = "Vellum EW 5", .id = 34, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-354.2f, -22.2f, 12.0f}, {481.2f, 65.6f, 12.0f}}, .count = 2},
    // The east-west counterpart of Vellum Row itself: the two arterials cross
    // at the centre of the district, which is where its tallest buildings and
    // its worst traffic are.
    {.name = "Halloway Street", .id = 35, .district = DistrictId::VellumRow,
     .cls = RoadClass::Arterial, .block_quality = 160,
     .path = {{-347.7f, -83.9f, 12.0f}, {487.7f, 3.9f, 11.5f}}, .count = 2},
    {.name = "Vellum EW 7", .id = 36, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-341.2f, -145.6f, 12.0f}, {494.2f, -57.8f, 11.5f}}, .count = 2},
    {.name = "Vellum EW 8", .id = 37, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-334.7f, -207.2f, 12.0f}, {500.7f, -119.4f, 11.5f}}, .count = 2},
    {.name = "Vellum EW 9", .id = 38, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-328.3f, -268.9f, 12.0f}, {507.1f, -181.1f, 11.5f}}, .count = 2},
    {.name = "Vellum EW 10", .id = 39, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-321.8f, -330.5f, 12.0f}, {513.6f, -242.7f, 12.0f}}, .count = 2},
    {.name = "Vellum EW 11", .id = 40, .district = DistrictId::VellumRow,
     .cls = RoadClass::Street, .block_quality = 90,
     .path = {{-315.3f, -392.2f, 12.0f}, {520.1f, -304.4f, 12.0f}}, .count = 2},

    // The two ramps that put the grid on Route 1. Only two, and that is the
    // point: nine streets spilling straight onto a freeway would make the
    // south edge of downtown a slip road instead of a decision.
    {.name = "the West Ramp", .id = 41, .district = DistrictId::VellumRow,
     .cls = RoadClass::Arterial, .block_quality = 170, .shapes_ground = true,
     .path = {{-147.5f, 269.0f, 12.0f}, {-160.0f, 380.0f, 12.0f},
              {-147.5f, 483.7f, 12.0f}},
     .count = 3},
    {.name = "the East Ramp", .id = 42, .district = DistrictId::VellumRow,
     .cls = RoadClass::Arterial, .block_quality = 170, .shapes_ground = true,
     .path = {{218.5f, 307.4f, 12.0f}, {215.0f, 400.0f, 12.0f},
              {218.5f, 506.9f, 12.0f}},
     .count = 3},

    // =======================================================================
    //  50-59. HALLOWAY SQUARE — one plaza, four arms.
    // =======================================================================
    //
    // The radial plan is the police plan: every arm is covered from the centre,
    // which is why the response here is the shortest on the island. All four
    // arms are Arterials because the district table says so, and none of them
    // shapes the ground inside the plaza — that plate is flat at 14.0 m at full
    // strength. The arms that LEAVE the plate do shape it, because past the
    // feather the plaza stops being flat.

    {.name = "the North Arm", .id = 50, .district = DistrictId::HallowaySquare,
     .cls = RoadClass::Arterial, .block_quality = 150, .shapes_ground = true,
     .path = {{-70.0f, 780.0f, 14.0f}, {-55.0f, 620.0f, 13.0f},
              {-40.0f, 490.0f, 12.0f}},
     .count = 3},
    {.name = "the East Arm", .id = 51, .district = DistrictId::HallowaySquare,
     .cls = RoadClass::Arterial, .block_quality = 150, .shapes_ground = true,
     .path = {{-70.0f, 780.0f, 14.0f}, {230.0f, 830.0f, 14.0f},
              {480.0f, 800.0f, 13.0f}, {700.0f, 700.0f, 11.5f}},
     .count = 4},
    {.name = "the South Arm", .id = 52, .district = DistrictId::HallowaySquare,
     .cls = RoadClass::Arterial, .block_quality = 150, .shapes_ground = true,
     .path = {{-70.0f, 780.0f, 14.0f}, {-60.0f, 1050.0f, 14.0f},
              {20.0f, 1240.0f, 16.0f}, {215.0f, 1420.0f, 16.0f}},
     .count = 4},
    {.name = "the West Arm", .id = 53, .district = DistrictId::HallowaySquare,
     .cls = RoadClass::Arterial, .block_quality = 150, .shapes_ground = true,
     .path = {{-70.0f, 780.0f, 14.0f}, {-420.0f, 720.0f, 13.0f},
              {-780.0f, 620.0f, 9.0f}, {-1010.0f, 560.0f, 7.0f}},
     .count = 4},
    // The ring the four arms hang off. A radial district needs one or the
    // plaza is a roundabout with no roundabout.
    {.name = "the Plaza Ring (west)", .id = 54,
     .district = DistrictId::HallowaySquare, .cls = RoadClass::Arterial,
     .block_quality = 110, .shapes_ground = true,
     .path = {{-70.0f, 620.0f, 14.0f}, {-230.0f, 700.0f, 14.0f},
              {-230.0f, 860.0f, 14.0f}, {-70.0f, 940.0f, 14.0f}},
     .count = 4},
    {.name = "the Plaza Ring (east)", .id = 55,
     .district = DistrictId::HallowaySquare, .cls = RoadClass::Arterial,
     .block_quality = 110, .shapes_ground = true,
     .path = {{-70.0f, 620.0f, 14.0f}, {90.0f, 700.0f, 14.0f},
              {90.0f, 860.0f, 14.0f}, {-70.0f, 940.0f, 14.0f}},
     .count = 4},

    // =======================================================================
    //  60-79. SALTMARSH — the district that rewards memory.
    // =======================================================================
    //
    // Alleys, 6 m, no sidewalk and no kerb: too tight for a cruiser to swing.
    // There is deliberately NO through route the minimap could helpfully draw.
    // The lanes on the north bank connect to each other and to the four creek
    // crossings, and knowing which lane comes out at which crossing is the
    // local knowledge this whole district exists to reward.
    //
    // EVERY COORDINATE HERE IS MEASURED OFF THE CREEK, not placed by eye. The
    // Saltmarsh creek is a Carve in terrain_ops.h running
    // (-1560,420) (-1100,300) (-700,90) (-480,-260) with a 15 m half width, and
    // the first draft of this district had four lanes FORDING it while two of
    // the four bridges sat in dry ground 56 m from any water. The crossings
    // below sit at one fifth, two fifths, three fifths and four fifths of the
    // creek's length, square to it, 60 m each side; every lane stops at a bank.
    // tests/city_roads_tests.cpp re-derives that from the operator table on
    // every run, so moving the creek breaks the build rather than the fiction.
    //
    // All of it stands on the Saltmarsh plate, flat at 5.5 m at full strength,
    // so none of the lanes shapes the ground. The crossings are DECKS: an
    // embankment would dam the creek, and the creek is what makes a chase here
    // fragment instead of stop.

    // --- the north bank: the old town ---------------------------------------
    {.name = "Quay Lane", .id = 60, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 10,
     .path = {{-1300.0f, -190.0f, 5.5f}, {-1200.0f, -100.0f, 5.5f},
              {-1120.0f, 20.0f, 5.5f}},
     .count = 3},
    {.name = "Netmenders Row", .id = 61, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 10,
     .path = {{-1300.0f, -190.0f, 5.5f}, {-1270.0f, -110.0f, 5.5f},
              {-1200.0f, -100.0f, 5.5f}},
     .count = 3},
    {.name = "Cooper Steps", .id = 62, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 0,  // blind, both ends
     .path = {{-1200.0f, -100.0f, 5.5f}, {-1220.0f, 10.0f, 5.5f},
              {-1145.0f, 75.0f, 6.5f}, {-1120.0f, 20.0f, 5.5f}},
     .count = 4},
    {.name = "Old Tide Street", .id = 63, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 60,
     .path = {{-1300.0f, -190.0f, 5.5f}, {-1050.0f, -230.0f, 5.5f},
              {-800.0f, -190.0f, 5.5f}, {-660.0f, -140.0f, 5.5f}},
     .count = 4},
    {.name = "Gullet Lane", .id = 64, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 0,
     .path = {{-1050.0f, -230.0f, 5.5f}, {-1000.0f, -120.0f, 5.5f},
              {-1120.0f, 20.0f, 5.5f}},
     .count = 3},
    // THE CUT. From Old Tide Street it reads as a service alley behind the
    // warehouses; it is in fact the fastest way to the third crossing, and the
    // AI does not take it.
    {.name = "the Cut", .id = 65, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 0,
     .path = {{-800.0f, -190.0f, 5.5f}, {-880.0f, -80.0f, 5.5f},
              {-837.0f, 94.2f, 5.5f}},
     .count = 3},
    {.name = "Fishgate", .id = 66, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 10,
     .path = {{-660.0f, -140.0f, 5.5f}, {-673.7f, -64.5f, 5.5f}},
     .count = 2},
    {.name = "Marsh Row", .id = 67, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 10,
     .path = {{-1120.0f, 20.0f, 5.5f}, {-960.0f, -40.0f, 5.5f},
              {-880.0f, -80.0f, 5.5f}},
     .count = 3},
    {.name = "Lower Marsh", .id = 68, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .block_quality = 10,
     .path = {{-1000.0f, -120.0f, 5.5f}, {-960.0f, -40.0f, 5.5f}},
     .count = 2},
    // The slip that puts the old town on Route 1. One of them, at the far
    // western end, which is why a chase that enters Saltmarsh from the east has
    // to cross the creek to get out again.
    {.name = "the Rimway Slip", .id = 69, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 140, .shapes_ground = true,
     .path = {{-1050.0f, 110.0f, 7.0f}, {-1080.0f, 60.0f, 6.0f},
              {-1120.0f, 20.0f, 5.5f}},
     .count = 3},

    // --- the four crossings -------------------------------------------------
    // All passable, none fast. Soft chokepoints: a chase FRAGMENTS here instead
    // of stopping, which is a different and better thing than a wall.
    // The strip between Route 1 and the creek is cut off from the old town by
    // the freeway, so it is its own road with its own ramp. That is what a
    // freeway through a town DOES, and it is why the two crossings at this end
    // feel further away than they look.
    {.name = "the Bankside", .id = 76, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 80, .shapes_ground = true,
     .path = {{-1150.0f, 115.3f, 7.0f}, {-1074.2f, 218.7f, 5.5f},
              {-1200.0f, 260.0f, 5.5f}, {-1315.8f, 294.4f, 5.5f}},
     .count = 4},

    {.name = "Creek Crossing 1", .id = 70, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .structure = RoadStructure::Bridge,
     .block_quality = 30, .deck_y_m = 5.5f,
     .path = {{-1315.8f, 294.4f, 5.5f}, {-1285.6f, 410.5f, 5.5f}},
     .count = 2},
    {.name = "Creek Crossing 2", .id = 71, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .structure = RoadStructure::Bridge,
     .block_quality = 30, .deck_y_m = 5.5f,
     .path = {{-1074.2f, 218.7f, 5.5f}, {-1018.4f, 324.9f, 5.5f}},
     .count = 2},
    {.name = "Creek Crossing 3", .id = 72, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .structure = RoadStructure::Bridge,
     .block_quality = 30, .deck_y_m = 5.5f,
     .path = {{-837.0f, 94.2f, 5.5f}, {-781.2f, 200.4f, 5.5f}},
     .count = 2},
    {.name = "Creek Crossing 4", .id = 73, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Alley, .structure = RoadStructure::Bridge,
     .block_quality = 30, .deck_y_m = 5.5f,
     .path = {{-673.7f, -64.5f, 5.5f}, {-572.1f, -0.6f, 5.5f}},
     .count = 2},

    // --- the south bank -----------------------------------------------------
    {.name = "Saltings Road", .id = 74, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 70, .shapes_ground = true,
     .path = {{-1285.6f, 410.5f, 5.5f}, {-1120.0f, 390.0f, 5.5f},
              {-1018.4f, 324.9f, 5.5f}, {-900.0f, 270.0f, 5.5f},
              {-872.0f, 248.0f, 6.0f}},
     .count = 5},
    // The eastern half of the south bank. It is a SEPARATE road because Route 1
    // comes off the creek bridge between the two halves: a road running through
    // that point would be a crossroads on a motorway, so the two halves ramp on
    // at two different places instead.
    {.name = "Fishermans Road", .id = 77, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 70, .shapes_ground = true,
     .path = {{-803.5f, 289.3f, 7.0f}, {-781.2f, 200.4f, 5.5f},
              {-680.0f, 150.0f, 5.5f}, {-600.0f, 60.0f, 5.5f},
              {-572.1f, -0.6f, 5.5f}},
     .count = 5},

    {.name = "Marsh Hill Road", .id = 75, .district = DistrictId::Saltmarsh,
     .cls = RoadClass::Street, .block_quality = 120, .shapes_ground = true,
     .path = {{-1018.4f, 324.9f, 5.5f}, {-1010.0f, 450.0f, 6.2f},
              {-1010.0f, 560.0f, 7.0f}},
     .count = 3},

    // =======================================================================
    //  80-89. OSTEND DOCKS — straight-line speed inside a trap.
    // =======================================================================
    //
    // One spine down the apron with service stubs off it. Two of the stubs end
    // at the quay edge and one does not, and knowing which is which is the
    // whole district. The apron is flat at 5.0 m at full strength.

    {.name = "the Apron Spine", .id = 80, .district = DistrictId::OstendDocks,
     .cls = RoadClass::Arterial, .block_quality = 200, .shapes_ground = true,
     .path = {{-1453.0f, -522.0f, 6.0f}, {-1700.0f, -560.0f, 5.0f},
              {-1950.0f, -600.0f, 5.0f}},
     .count = 3},
    // Both berths GRADE, and the reason is not obvious from the map: the
    // Kessel Channel's feather reaches 300 m past its own half width and cuts a
    // five metre trough across the south end of the apron, which the drape
    // measurement found and no amount of looking at the table would have. A
    // container quay is dead level or it is not a container quay.
    {.name = "Berth 1", .id = 81, .district = DistrictId::OstendDocks,
     .cls = RoadClass::Arterial, .block_quality = 120, .shapes_ground = true,
     .path = {{-1700.0f, -560.0f, 5.0f}, {-1690.0f, -840.0f, 5.0f}},
     .count = 2},
    // DEAD END AT THE WATER, and it looks exactly like Berth 1 from the spine.
    {.name = "Berth 2", .id = 82, .district = DistrictId::OstendDocks,
     .cls = RoadClass::Arterial, .block_quality = 60, .shapes_ground = true,
     .path = {{-1950.0f, -600.0f, 5.0f}, {-1960.0f, -880.0f, 5.0f}},
     .count = 2},
    {.name = "the Container Run", .id = 83, .district = DistrictId::OstendDocks,
     .cls = RoadClass::Arterial, .block_quality = 220, .shapes_ground = true,
     .path = {{-1690.0f, -840.0f, 5.0f}, {-1960.0f, -880.0f, 5.0f}},
     .count = 2},
    // =======================================================================
    //  90-99. KEPLER FLATS — hazards as terrain.
    // =======================================================================
    //
    // Wide yard arterials and dirt spurs that let a pursuit off-road, on a
    // plate that is flat because a level crossing on a slope is not a level
    // crossing.

    {.name = "the Yard Road", .id = 90, .district = DistrictId::KeplerFlats,
     .cls = RoadClass::Arterial, .block_quality = 190,
     .path = {{-1050.0f, -1900.0f, 9.0f}, {-500.0f, -1930.0f, 9.0f},
              {0.0f, -1900.0f, 9.0f}},
     .count = 3},
    {.name = "the Tank Farm Loop", .id = 91, .district = DistrictId::KeplerFlats,
     .cls = RoadClass::Arterial, .block_quality = 100,
     .path = {{-500.0f, -1790.0f, 9.0f}, {-500.0f, -1930.0f, 9.0f}},
     .count = 2},
    {.name = "the Flare Spur", .id = 92, .district = DistrictId::KeplerFlats,
     .cls = RoadClass::Dirt, .block_quality = 40, .shapes_ground = true,
     .path = {{0.0f, -1900.0f, 9.0f}, {90.0f, -1800.0f, 12.0f},
              {60.0f, -1760.0f, 9.0f}},
     .count = 3},
    {.name = "the Kepler Approach", .id = 93, .district = DistrictId::KeplerFlats,
     .cls = RoadClass::Arterial, .block_quality = 150,
     .path = {{-1050.0f, -1720.0f, 9.0f}, {-1050.0f, -1900.0f, 9.0f}},
     .count = 2},


    // =======================================================================
    //  100-119. FERRONE HILL — vertical.
    // =======================================================================
    // ORDERED STUB-FIRST, AND THE ORDER IS THE MAP.
    //
    // Grade corridors compose in table order like every other operator, and a
    // corridor keeps applying its END height as a flat cap for half a width
    // past its last point. So where a short spur meets a road that is climbing,
    // whichever composes LAST decides the ground: put the spur second and it
    // levels off the through road's final twenty metres, which reads on a
    // hillside as a shelf and then a lump, and measured 0.30 m of level-3
    // drape on the Shoulder before these entries were swapped.
    //
    // So the spurs are listed first and the through road last. The spurs are
    // level and do not mind being capped at the junction height; the road whose
    // gradient IS the gameplay wins its own approach.

    // Terrace streets off the Shoulder. Short, level along the contour, and
    // every one of them a cul-de-sac: a missed hairpin is a 30 m drop.
    {.name = "Upper Terrace", .id = 103, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Street, .block_quality = 20, .shapes_ground = true,
     .path = {{880.0f, -1580.0f, 116.0f}, {1010.0f, -1660.0f, 116.0f}},
     .count = 2},
    {.name = "Middle Terrace", .id = 104, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Street, .block_quality = 20, .shapes_ground = true,
     .path = {{980.0f, -1360.0f, 87.26f}, {1060.0f, -1385.0f, 87.26f}},
     .count = 2},
    {.name = "Lower Terrace", .id = 105, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Street, .block_quality = 20, .shapes_ground = true,
     .path = {{900.0f, -1080.0f, 47.08f}, {1060.0f, -1140.0f, 47.08f}},
     .count = 2},
    // THE FIRE ROAD. Unmarked, unpaved, and the single most valuable piece of
    // local knowledge Pinatty has to teach: it is the way off the hill when the
    // Shoulder is shut. Never stage a block on it — a player who has earned
    // this route has earned the escape.
    {.name = "the fire road", .id = 102, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Dirt, .block_quality = 0, .shapes_ground = true,
     .path = {{880.0f, -1580.0f, 116.00f},
              {1080.0f, -1500.0f, 92.26f},
              {1162.6f, -1384.4f, 76.60f},
              {1180.0f, -1360.0f, 76.60f},
              {1174.5f, -1330.5f, 76.60f},
              {1150.0f, -1200.0f, 61.97f},
              {1040.0f, -1060.0f, 42.35f},
              {900.0f, -970.0f, 24.00f}},
     .count = 8},
    // The foot. Wide, straight and visible from a long way: THIS is where a
    // block on Ferrone Hill belongs.
    {.name = "the Hill Foot", .id = 101, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Arterial, .block_quality = 250, .shapes_ground = true,
     .path = {{470.0f, -900.0f, 13.0f}, {560.0f, -980.0f, 20.0f}},
     .count = 2},
    // THE SHOULDER. The one paved road up, and the best roadblock in the game:
    // block it and the hill is sealed to anyone who has not found the fire
    // road. block_quality is deliberately LOW on it — a derived-only selector
    // will happily stage two cruisers across a hairpin and kill the player with
    // a cutscene they cannot avoid (pinatty section 3.1). The place to block
    // this road is the foot, and the foot is its own entry above.
    //
    // EVERY HAIRPIN IS A LEVEL PLATFORM, AND THAT IS NOT DECORATION.
    //
    // A Grade corridor takes its height from the NEAREST point on its own
    // polyline. Where a road doubles back inside its own width, two arms are
    // equidistant from the ground between them and the operator has to pick
    // one -- so the height field STEPS, vertically, from one arm's profile to
    // the other's. Measured on the first draft of this road, with the climb
    // running straight through the apexes: a SEVEN METRE CLIFF down the middle
    // of the second hairpin, 26 m from the apex, directly under the kerb line.
    //
    // Levelling 70 m either side of each apex removes it at the source. Near a
    // hairpin both arms are then at the same height, so there is nothing for
    // the field to step between, and the climb happens on the straights where
    // the arms are far enough apart that neither is inside the other's
    // corridor. It is also simply what a real switchback does, because you
    // cannot climb and turn hard at the same time.
    //
    // The residual is OFF THE ROAD and is reported rather than hidden: on the
    // bank between two legs, about 35 m out, the field still steps by roughly
    // half a metre. That is a retaining wall between switchbacks, which is what
    // a retaining wall between switchbacks looks like. The carriageway, the
    // kerbs and the sidewalks are clean.
    //
    // 1568 m, 560 m of it level platform, 9.5 per cent on the climbing legs --
    // which is the figure docs/design/pinatty.md section 3.1 asks for, arrived
    // at rather than typed in.
    {.name = "the Shoulder", .id = 100, .district = DistrictId::FerroneHill,
     .cls = RoadClass::Street, .block_quality = 40, .shapes_ground = true,
     .corridor_feather_m = 20.0f,
     .path = {{560.0f, -980.0f, 20.00f},
              {832.8f, -1060.2f, 47.08f},
              {900.0f, -1080.0f, 47.08f},
              {839.2f, -1114.7f, 47.08f},
              {680.8f, -1205.3f, 64.46f},
              {620.0f, -1240.0f, 64.46f},
              {686.4f, -1262.1f, 64.46f},
              {913.6f, -1337.9f, 87.26f},
              {980.0f, -1360.0f, 87.26f},
              {914.8f, -1385.6f, 87.26f},
              {765.2f, -1444.4f, 102.58f},
              {700.0f, -1470.0f, 102.58f},
              {759.7f, -1506.5f, 102.58f},
              {880.0f, -1580.0f, 116.00f}},
     .count = 14},

    // =======================================================================
    //  120-139. NICKEL HEIGHTS — dead ends punish panic.
    // =======================================================================
    // The stadium ramp composes FIRST, by the same rule as the Ferrone Hill
    // block: it is the one road here that climbs, and a cul-de-sac sitting at
    // plate height sixty metres away should not be dragged up the berm with it.
    // The stadium is on a raised berm, so its access road climbs.
    {.name = "the Stadium Approach", .id = 129,
     .district = DistrictId::NickelHeights, .cls = RoadClass::Arterial,
     .block_quality = 130, .shapes_ground = true,
     .path = {{1160.0f, 660.0f, 11.5f}, {1210.0f, 700.0f, 15.0f},
              {1250.0f, 720.0f, 20.0f}},
     .count = 3},
    {.name = "Sycamore Loop", .id = 123, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 40, .shapes_ground = true,
     .path = {{950.0f, 200.0f, 11.0f}, {1090.0f, 180.0f, 11.5f},
              {1180.0f, 260.0f, 12.0f}, {1120.0f, 360.0f, 12.0f},
              {980.0f, 350.0f, 11.0f}, {950.0f, 200.0f, 11.0f}},
     .count = 6},
    {.name = "Marlow Loop", .id = 124, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 40, .shapes_ground = true,
     .path = {{950.0f, 470.0f, 11.0f}, {820.0f, 460.0f, 11.0f},
              {760.0f, 400.0f, 11.0f}, {830.0f, 330.0f, 11.0f},
              {950.0f, 380.0f, 11.0f}},
     .count = 5},
    {.name = "Pennant Loop", .id = 125, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 40, .shapes_ground = true,
     .path = {{1220.0f, 60.0f, 11.5f}, {1340.0f, 120.0f, 12.0f},
              {1330.0f, 250.0f, 12.0f}, {1200.0f, 240.0f, 12.0f},
              {1180.0f, 130.0f, 11.5f}, {1220.0f, 60.0f, 11.5f}},
     .count = 6},    // THE THREE THAT GO NOWHERE. From the collector they read exactly like the
    // three above.
    {.name = "Corvid Close", .id = 126, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 0, .shapes_ground = true,
     .path = {{1160.0f, 660.0f, 11.5f}, {1200.0f, 800.0f, 11.5f},
              {1120.0f, 850.0f, 11.5f}},
     .count = 3},
    {.name = "Aldermans End", .id = 127, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 0, .shapes_ground = true,
     .path = {{820.0f, 460.0f, 11.0f}, {750.0f, 410.0f, 11.0f}},
     .count = 2},
    {.name = "Kestrel Close", .id = 128, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Street, .block_quality = 0, .shapes_ground = true,
     .path = {{1400.0f, 160.0f, 12.0f}, {1450.0f, 300.0f, 12.5f}},
     .count = 2},
    //
    // Two collectors, three loops off them, and three cul-de-sacs that look
    // exactly like loops until you are in them. Long sight lines mean the
    // police keep contact even when they lose ground, so the correct play is to
    // LEAVE — and the layout is built to make leaving feel too slow.
    //
    // These DO shape the ground: the Nickel Heights plate is Flatten at
    // strength 0.75, which leaves the suburb rolling on purpose, and rolling
    // ground is exactly what a level-3 lattice disagrees with.

    // The one ramp from Route 1 down to the southern half of the district.
    // Everything south of the freeway is reached through here or the long way
    // round via Halloway Square, which is the whole reason a wrong turn in
    // Nickel Heights is expensive.
    {.name = "the South Ramp", .id = 130,
     .district = DistrictId::NickelHeights, .cls = RoadClass::Arterial,
     .block_quality = 200, .shapes_ground = true,
     .path = {{1010.0f, 553.7f, 12.0f}, {1010.0f, 660.0f, 11.5f},
              {1160.0f, 660.0f, 11.5f}},
     .count = 3},

    {.name = "the North Collector", .id = 120,
     .district = DistrictId::NickelHeights, .cls = RoadClass::Arterial,
     .block_quality = 170, .shapes_ground = true,
     .path = {{700.0f, 60.0f, 11.0f}, {950.0f, 40.0f, 11.0f},
              {1220.0f, 60.0f, 11.5f}, {1400.0f, 160.0f, 12.0f}},
     .count = 4},
    {.name = "the South Collector", .id = 121,
     .district = DistrictId::NickelHeights, .cls = RoadClass::Arterial,
     .block_quality = 170, .shapes_ground = true,
     .path = {{700.0f, 700.0f, 11.5f}, {950.0f, 720.0f, 11.5f},
              {1160.0f, 660.0f, 11.5f}, {1400.0f, 480.0f, 12.5f}},
     .count = 4},
    {.name = "the Spine", .id = 122, .district = DistrictId::NickelHeights,
     .cls = RoadClass::Arterial, .block_quality = 180, .shapes_ground = true,
     .path = {{950.0f, 40.0f, 11.0f}, {950.0f, 380.0f, 11.0f},
              {950.0f, 550.3f, 12.0f}},
     .count = 3},
    // =======================================================================
    //  140-149. THE STRAND — served by Route 1 and nothing else.
    // =======================================================================
    //
    // There is exactly one road here and it is Route 1 (id 8). The seafront
    // service road below touches it at ONE end only, because a second junction
    // on the Strand is the district deleted.

    {.name = "the Esplanade", .id = 140, .district = DistrictId::TheStrand,
     .cls = RoadClass::Street, .block_quality = 90, .shapes_ground = true,
     .path = {{1960.0f, -680.0f, 12.0f}, {2080.0f, -420.0f, 12.0f},
              {2060.0f, -160.0f, 12.0f}, {1980.0f, 60.0f, 11.5f}},
     .count = 4},

    // =======================================================================
    //  150-159. CAMBER POINT — the escape hatch and the arena.
    // =======================================================================
    //
    // A perimeter road around the runway, on the flattest large surface in the
    // world, reached only by the causeway.

    {.name = "the Perimeter Road", .id = 150,
     .district = DistrictId::CamberPoint, .cls = RoadClass::Street,
     .block_quality = 60,
     .path = {{170.0f, 2130.0f, 6.0f}, {-330.0f, 2110.0f, 6.0f},
              {-330.0f, 2230.0f, 6.0f}, {560.0f, 2240.0f, 6.0f},
              {600.0f, 2100.0f, 6.0f}, {170.0f, 2130.0f, 6.0f}},
     .count = 6},
    // Inside the loop, not outside it. It ran south to z = 2000 in the first
    // draft, and z = 2000 at this x is 220 m from the Camber channel's
    // centreline -- inside a 300 m feather that pulls the ground to -0.24 m.
    // The taxiway was under water and nothing but the measurement said so.
    {.name = "the Apron", .id = 151, .district = DistrictId::CamberPoint,
     .cls = RoadClass::Street, .block_quality = 20,
     .path = {{-330.0f, 2170.0f, 6.0f}, {100.0f, 2180.0f, 6.0f}},
     .count = 2},


    // =======================================================================
    //  160-179. MARROW — off-road, where the cruiser cannot follow.
    // =======================================================================
    // Stub-first, for the reason spelled out in the Ferrone Hill block above:
    // the farm track was cutting 0.37 m out of the haul road's embankment
    // where the two run parallel out of the same junction.

    {.name = "the quarry ramp", .id = 162, .district = DistrictId::Marrow,
     .cls = RoadClass::Dirt, .block_quality = 0, .shapes_ground = true,
     .path = {{-1470.0f, 1390.0f, 48.0f}, {-1530.0f, 1300.0f, 47.0f},
              {-1470.0f, 1210.0f, 47.0f}},
     .count = 3},
    {.name = "the spoil track", .id = 163, .district = DistrictId::Marrow,
     .cls = RoadClass::Dirt, .block_quality = 0, .shapes_ground = true,
     .path = {{-1300.0f, 1280.0f, 52.0f}, {-1230.0f, 1420.0f, 64.0f},
              {-1200.0f, 1540.0f, 76.0f}},
     .count = 3},
    {.name = "the farm track", .id = 164, .district = DistrictId::Marrow,
     .cls = RoadClass::Dirt, .block_quality = 20, .shapes_ground = true,
     .path = {{-1100.0f, 1200.0f, 36.0f}, {-1250.0f, 1120.0f, 36.0f},
              {-1420.0f, 1080.0f, 34.0f}, {-1560.0f, 1120.0f, 33.0f}},
     .count = 4},    //
    // A dirt web with no signage, one paved link out, and a haul road with a
    // drop on the outside. Grip changes under you.

    {.name = "the Marrow Link", .id = 160, .district = DistrictId::Marrow,
     .cls = RoadClass::Street, .block_quality = 200, .shapes_ground = true,
     .path = {{-1010.0f, 560.0f, 7.0f}, {-1020.0f, 760.0f, 17.0f},
              {-1050.0f, 940.0f, 30.0f}, {-1080.0f, 1080.0f, 35.0f},
              {-1100.0f, 1200.0f, 36.0f}},
     .count = 5},
    {.name = "the haul road", .id = 161, .district = DistrictId::Marrow,
     .cls = RoadClass::Dirt, .block_quality = 30, .shapes_ground = true,
     .path = {{-1470.0f, 1390.0f, 48.0f}, {-1337.8f, 1304.4f, 52.0f},
              {-1300.0f, 1280.0f, 52.0f}, {-1258.2f, 1263.3f, 52.0f},
              {-1100.0f, 1200.0f, 36.0f}},
     .count = 5},
    // =======================================================================
    //  180-199. INTER-DISTRICT LINKS.
    // =======================================================================
    //
    // These are the roads that make the island one place. Every one of them
    // leaves a district plate and crosses ground the noise shaped, so every one
    // of them grades its corridor.

    // Vellum Row to Ferrone Hill's foot: the only road onto the northern
    // massif that does not involve the Kessel Bridge.
    {.name = "the Ferrone Road", .id = 180, .district = DistrictId::Count,
     .cls = RoadClass::Arterial, .block_quality = 230, .shapes_ground = true,
     .path = {{104.5f, -368.2f, 12.0f}, {180.0f, -520.0f, 12.0f},
              {300.0f, -700.0f, 11.0f}, {410.0f, -820.0f, 11.0f},
              {470.0f, -900.0f, 13.0f}},
     .count = 5},
    // Vellum Row to Saltmarsh.
    {.name = "the Marsh Road", .id = 181, .district = DistrictId::Count,
     .cls = RoadClass::Arterial, .block_quality = 150, .shapes_ground = true,
     .path = {{-347.7f, -83.9f, 12.0f}, {-420.0f, -290.0f, 12.0f},
              {-560.0f, -340.0f, 9.0f}, {-680.0f, -250.0f, 5.5f},
              {-660.0f, -140.0f, 5.5f}},
     .count = 5},
    // Vellum Row to Nickel Heights.
    {.name = "the Nickel Road", .id = 182, .district = DistrictId::Count,
     .cls = RoadClass::Arterial, .block_quality = 160, .shapes_ground = true,
     .path = {{481.2f, 65.6f, 12.0f}, {590.0f, 62.0f, 11.5f},
              {700.0f, 60.0f, 11.0f}},
     .count = 3},
};

inline constexpr int kRoadCount =
    static_cast<int>(sizeof(kRoads) / sizeof(kRoads[0]));

// ---------------------------------------------------------------------------
//  Compile-time invariants
// ---------------------------------------------------------------------------

static_assert(kRoadCount > 0, "the road table is empty");

constexpr bool all_roads_well_formed() {
    for (int i = 0; i < kRoadCount; ++i) {
        if (!kRoads[i].well_formed()) return false;
    }
    return true;
}
static_assert(all_roads_well_formed(),
              "a road is malformed - see Road::well_formed(). A decked road "
              "that also grades its corridor, a grade over 25 per cent, a "
              "zero-length segment, or a point outside the world box.");

// Ids are the stable authored identity every downstream system keys entropy
// on. Two roads sharing one is two roads sharing their lane choices, their
// roadblock sites and their traffic.
constexpr bool road_ids_are_unique() {
    for (int i = 0; i < kRoadCount; ++i) {
        if (kRoads[i].id == 0) return false;  // 0 is the unset value
        for (int j = i + 1; j < kRoadCount; ++j) {
            if (kRoads[i].id == kRoads[j].id) return false;
        }
    }
    return true;
}
static_assert(road_ids_are_unique(), "two roads share an id, or one is zero");

// THE STRAND HAS ONE ROAD ON IT AND NOTHING MAY TOUCH IT IN THE MIDDLE.
//
// "2.2 km with no turnoffs" is the district. It is not a look, it is the reason
// a roadblock there is frightening, and it is exactly the property somebody
// deletes by adding a helpful slip road. So: no road except Route 1 itself may
// put a point inside the Strand's straight, other than at its two ends.
constexpr bool the_strand_has_no_turnoffs() {
    // Route 1 - the Strand, found by id rather than by index, so inserting a
    // road above it does not silently start checking a different road.
    int s = -1;
    for (int i = 0; i < kRoadCount; ++i) {
        if (kRoads[i].id == 8) s = i;
    }
    if (s < 0) return false;
    const RoadPoint a = kRoads[s].path[0];
    const RoadPoint b = kRoads[s].path[kRoads[s].count - 1];
    const float ex = b.x - a.x;
    const float ez = b.z - a.z;
    const float len2 = ex * ex + ez * ez;
    // No std::sqrt in a constant expression; Newton converges long before 24.
    float len = len2 > 1.0f ? len2 : 1.0f;
    for (int k = 0; k < 24; ++k) len = 0.5f * (len + len2 / len);

    for (int i = 0; i < kRoadCount; ++i) {
        if (i == s) continue;
        for (int seg = 0; seg + 1 < kRoads[i].count; ++seg) {
            const RoadPoint p0 = kRoads[i].path[seg];
            const RoadPoint p1 = kRoads[i].path[seg + 1];
            // SAMPLED ALONG THE SEGMENT, not just at its ends. A road that
            // crosses the Strand at right angles has both its vertices a long
            // way off and still puts a junction in the middle of the district,
            // which is exactly the thing being forbidden.
            for (int k = 0; k <= 16; ++k) {
                const float u = static_cast<float>(k) / 16.0f;
                const float px = p0.x + (p1.x - p0.x) * u;
                const float pz = p0.z + (p1.z - p0.z) * u;
                float t = ((px - a.x) * ex + (pz - a.z) * ez) / len2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                // The outer 160 m at each end is the junction APPROACH, and
                // roads are of course allowed to converge on a junction. Said
                // in metres rather than as a fraction of the length, because
                // the thing being described is a slip road and a slip road is
                // a distance, not a percentage.
                if (t * len < 160.0f || (1.0f - t) * len < 160.0f) continue;
                const float dx = px - (a.x + ex * t);
                const float dz = pz - (a.z + ez * t);
                // 60 m clears the widest corridor plus its feather on both
                // sides, so "close enough for the graph to weld a junction" and
                // "close enough for two Grade corridors to fight" are both out
                // of reach.
                if (dx * dx + dz * dz < 60.0f * 60.0f) return false;
            }
        }
    }
    return true;
}
static_assert(the_strand_has_no_turnoffs(),
              "something now touches Route 1 - the Strand between its ends. "
              "The Strand IS 2.2 km with no turnoffs; a slip road onto it is "
              "the district deleted, not the district improved");

}  // namespace city
}  // namespace apricot

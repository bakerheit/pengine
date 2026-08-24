#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// Road-type registry. A road's `type` byte (RoadGraphAuthor::Edge::type)
// indexes this table, which is the single source of truth for a type's
// carriageway width, its lane count and directionality, and the colour the
// editor ghosts it in.
//
// SINGLE SOURCE OF TRUTH IS THE ENTIRE POINT. The ribbon mesh, the lane
// producer, the junction plates and the sidewalk strips all derive their
// geometry from these widths. The moment one of them carries its own copy, a
// road is drawn one width and driven another, and the symptom is cars clipping
// a kerb that is not where the renderer put it.
//
// Header-only constexpr data so every consumer shares it without a translation
// unit or a circular include.
//
// PROVENANCE. Lifted from probablecause (PENG-29); PCG-nnn markers are its
// ticket numbers. See src/city/README.md.
enum class RoadType : std::uint8_t {
    Street  = 0,   // two-lane neighbourhood street
    Avenue  = 1,   // four-lane arterial
    OneWay  = 2,   // two same-direction lanes
    Highway = 3,   // six-lane divided
    Dirt    = 4,   // narrow unpaved rural track (stop-sign junctions)
    Count   = 5,
};

struct RoadTypeDef {
    const char* name;
    float       carriageway_width_m;  // -> the road ribbon mesh width
    int         lanes_per_dir;        // -> lanes emitted per travel direction
    bool        bidirectional;        // false = OneWay (single travel direction)
    float       lane_width_m;         // -> lane spacing across the carriageway
    glm::vec3   ghost_tint;           // editor preview colour
};

// Indexed by RoadType. Keep entries in enum order so `ROAD_TYPES[(int)t]` and
// road_type_def(t) agree.
//
// PCG-177: every carriageway is +2 m over the original table (Street 12->14,
// Avenue 20->22, One-Way 10->12, Highway 28->30, Dirt 7->9) — the founder's
// "slightly wider roads and intersections" directive. Newly drawn roads pick
// these up; already-authored edges get the same +2 delta at .roadgraph load
// (see RoadGraphAuthor::deserialize — a pure shift, custom widths keep their
// identity). The procgen grid's STREET_WIDTH grew 8 -> 10 alongside
// (road_grid.h). Junction plates/crosswalks/sidewalks derive from incident
// road widths, so intersections follow automatically.
inline constexpr RoadTypeDef ROAD_TYPES[static_cast<std::size_t>(RoadType::Count)] = {
    /* Street  */ {"Street",  14.f, 1, true,  3.50f, {0.85f, 0.85f, 0.85f}},
    /* Avenue  */ {"Avenue",  22.f, 2, true,  3.50f, {0.80f, 0.80f, 0.92f}},
    /* One-Way */ {"One-Way", 12.f, 2, false, 3.50f, {0.92f, 0.82f, 0.60f}},
    /* Highway */ {"Highway", 30.f, 3, true,  3.75f, {0.70f, 0.82f, 1.00f}},
    /* Dirt    */ {"Dirt",     9.f, 1, true,  3.25f, {0.62f, 0.47f, 0.32f}},
};

constexpr const RoadTypeDef& road_type_def(RoadType t) {
    return ROAD_TYPES[static_cast<std::size_t>(t)];
}

// Arterials (multi-lane through-roads). Used by junction classification: a
// T-junction earns a traffic signal only when a major road is involved;
// junctions of minor streets stay uncontrolled until they grow to a 4-way.
constexpr bool road_is_major(RoadType t) {
    return t == RoadType::Avenue || t == RoadType::Highway;
}

// Unpaved roads. Dirt tracks render as a packed-earth surface (no lane markings)
// and their junctions are controlled by stop signs rather than traffic lights.
constexpr bool road_is_unpaved(RoadType t) {
    return t == RoadType::Dirt;
}

// Junction-control policy hook: a junction touching one of these roads is
// stop-sign-controlled. Kept here so the single source of truth for "what kind
// of road wants a stop sign" sits beside the road table everything indexes.
constexpr bool road_uses_stop_signs(RoadType t) {
    return road_is_unpaved(t);
}

// Sidewalk geometry, shared by the road ribbon and the ped-path producer. A
// sidewalk is a `SIDEWALK_WIDTH_M` strip hugging each carriageway edge, raised
// `SIDEWALK_KERB_M` above the road surface; pedestrians walk down its centre,
// `SIDEWALK_WIDTH_M * 0.5` outboard of the kerb.
inline constexpr float SIDEWALK_WIDTH_M = 3.0f;
inline constexpr float SIDEWALK_KERB_M  = 0.12f;

// Global terrain -> drawn-surface drape. EVERY paved road surface (carriageway,
// junction plate, crosswalk, dirt track) is rendered this far ABOVE the bare
// terrain mesh to avoid z-fighting; a raised sidewalk slab is drawn one more
// `SIDEWALK_KERB_M` on top of that (slab top == terrain + DRAPE_EPS_M + KERB).
//
// This lives here, beside the kerb, because it is the OTHER half of the single
// source of truth for "how high above bare terrain is the drawn ground": the
// road mesh bake lifts its triangles by it, and the collision counterpart adds
// the SAME constant so feet and wheels rest on the drawn surface, not 6 cm
// under it. That was a real bug once — the kerb step was added and this drape
// was not, and characters sank into every pavement in the city. Drawn ground
// everywhere: grass is terrain; road is +DRAPE_EPS_M; sidewalk is
// +DRAPE_EPS_M+KERB.
inline constexpr float DRAPE_EPS_M = 0.06f;

}  // namespace apricot

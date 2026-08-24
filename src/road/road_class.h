#pragma once

#include <cstddef>
#include <cstdint>

#include "terrain/surface.h"

namespace apricot {

// The road hierarchy, as one constexpr table.
//
// Five classes, from docs/design/pinatty.md §3. This is the single source of
// truth for a class's carriageway width (which drives the ribbon bake), its
// lane count and spacing (which drives the lane graph), and how its junctions
// are controlled. Header-only so the graph builder, the ribbon baker and the
// lane-graph producer share it with no translation unit between them.
//
// ============================================================================
//  THE WIDTHS BELOW ALREADY INCLUDE THE +2 m PASS. DO NOT ADD IT AGAIN.
// ============================================================================
//
// probablecause widened every carriageway by 2 m (PCG-177: Street 12->14,
// Avenue 20->22, Highway 28->30, Dirt 7->9) and it felt right, so pinatty §3
// adopts the WIDENED numbers verbatim rather than relearning them. Four of the
// five values below are that post-migration table; Alley is new (there is no
// 6 m class in the reference).
//
// The reason this needs saying in capitals is the failure mode that ticket
// nearly shipped: a widen applied at authoring time AND again at load time
// compounds silently, and a 14 m street quietly becomes 16 m everywhere. There
// is no migration path here at all — apricot has no road file to load, the
// table IS the data — so the only way to reintroduce that bug is for somebody
// to read "+2 m founder pass" in a doc and apply it to these numbers. Do not.
//
// A related trap from the same chain, also inherited as a rule: never SNAP a
// custom width to its class width. PCG-170 did, and it turned every 8 m
// hand-authored street into a 16 m one; it was reverted. RoadSpine::width_m
// overrides this table when set, and nothing rounds it toward the class.
//
// (The PCG-168/170 junction splay/flare chain was reverted too, for feel. It
// is not reimplemented here and must not be resurrected without a plan to
// feel-check it in game — a test cannot tell you a junction reads right.)
enum class RoadClass : uint8_t {
    Freeway = 0,   // Route 1 only. Grade separated, ramps only.
    Arterial = 1,  // District spines and inter-district links.
    Street = 2,    // Everywhere urban.
    Alley = 3,     // Saltmarsh, Vellum Row, the docks. No sidewalk.
    Dirt = 4,      // Meadows, Marrow, the fire road.
};

inline constexpr std::size_t kRoadClassCount = 5;

inline constexpr std::size_t road_class_index(RoadClass c) {
    return static_cast<std::size_t>(c);
}

struct RoadClassDef {
    // Short stable name, for logs and debug overlays. Never parse this.
    const char* name;

    // Full carriageway width in metres, kerb to kerb. The ribbon bake spans
    // [-width/2, +width/2] about the centreline. Already +2 m — see above.
    float carriageway_width_m;

    // Travel lanes in EACH direction. Every class here is bidirectional; a
    // one-way pair is authored as two spines, which is how pinatty's district
    // table asks for it (`one_way_pair` in §5.3) and it keeps this table from
    // needing a directionality byte that only one class would ever set.
    int lanes_per_dir;

    // Nominal lane width. Used for signage-level reasoning only: the lane
    // graph spaces lanes evenly across the half carriageway (see
    // lane_centre_offset_m) so the outermost lane never hangs off the ribbon.
    float lane_width_m;

    // Does this class get raised kerbs and a sidewalk strip either side?
    bool sidewalks;

    // Paved classes bake into the marked-asphalt layer; unpaved into the
    // packed-earth one. It also decides junction control (see below).
    bool paved;

    // First-pass speed limit. THESE ARE GUESSES, not measurements: nothing in
    // pinatty.md sets them and there is no traffic system yet to feel them
    // against. They are here because the lane graph is the contract traffic
    // codes against and a lane with no speed on it forces every consumer to
    // invent one. Expect to retune them the first time cars actually drive.
    float speed_limit_mps;
};

// Indexed by RoadClass. Keep entries in enum order so road_class_def(c) and
// kRoadClasses[(int)c] can never disagree.
inline constexpr RoadClassDef kRoadClasses[kRoadClassCount] = {
    /* Freeway  */ {"Freeway",  30.0f, 3, 3.75f, false, true,  30.0f},
    /* Arterial */ {"Arterial", 22.0f, 2, 3.50f, true,  true,  16.7f},
    /* Street   */ {"Street",   14.0f, 1, 3.50f, true,  true,  11.1f},
    /* Alley    */ {"Alley",     6.0f, 1, 2.75f, false, true,   5.6f},
    /* Dirt     */ {"Dirt",      9.0f, 1, 3.25f, false, false,  8.3f},
};

constexpr const RoadClassDef& road_class_def(RoadClass c) {
    return kRoadClasses[road_class_index(c)];
}

// Multi-lane through-roads. A junction involving one of these is a real
// crossing and earns a signal; a T of minor streets does not (pinatty §3).
constexpr bool road_is_major(RoadClass c) {
    return c == RoadClass::Freeway || c == RoadClass::Arterial;
}

constexpr bool road_is_paved(RoadClass c) { return road_class_def(c).paved; }

// Unpaved roads take stop signs, never lights, and the stop wins even where a
// signal would otherwise apply — a dirt track crossing an arterial is still a
// dirt track. Inherited from the reference, and pinatty §3 asks for the same.
constexpr bool road_uses_stop_signs(RoadClass c) { return !road_is_paved(c); }

// Freeways are grade separated: they meet other roads at ramps, never at an
// at-grade crossing, so a node touching one is a merge and carries no control.
// An at-grade freeway crossing in the map tables is an AUTHORING ERROR, and it
// is the map validator's job to reject it — not this module's job to invent a
// traffic light for a road that is not supposed to have one.
constexpr bool road_is_grade_separated(RoadClass c) {
    return c == RoadClass::Freeway;
}

// What a road surface GRIPS like, expressed in the engine's one material enum.
//
// There is no Asphalt in terrain::Surface, and adding one is not this module's
// change to make: the enum's order is the component order of the per-vertex
// splat weights, that vector has exactly four components, and appending a
// fifth material re-textures the world and re-tunes the car at the same time
// (see terrain/surface.h and physics/surface.h, both of which say so at
// length). So roads map onto the existing rows, and the mapping is chosen on
// what the tyre should feel rather than on what the eye should see:
//
//   paved   -> Rock   (1.15 dry / 0.82 wet, rolling 1.00) — the grippiest,
//              lowest-drag row in the table, which is exactly what asphalt is.
//   unpaved -> Gravel (0.95 / 0.88, rolling 1.35) — loose, draggy, and it
//              keeps its grip in the rain, which is what a dirt track does.
//
// A real Asphalt material is a terrain-module ticket (append to Surface, widen
// the splat, add a row to kTyreSurfaceTable). Until then this is the honest
// interim and it is deliberately in ONE place.
constexpr Surface road_surface(RoadClass c) {
    return road_is_paved(c) ? Surface::Rock : Surface::Gravel;
}

// Lateral offset from the road centreline to the centre of lane `i` of one
// direction, where lane 0 is the one nearest the centreline. Unsigned; the
// lane graph applies the sign for handedness.
//
// The half carriageway is divided into `n` equal shares and the lane sits in
// the middle of its share, so lane spacing scales with the road instead of
// leaving the outermost lane hanging off a narrow ribbon. At one lane per
// direction this reduces to EXACTLY width/4 — the reference's PCG-178 offset,
// reproduced rather than reinvented, so a 14 m street still puts its lane
// centre 3.5 m off the middle and the oncoming lane 7 m away.
constexpr float lane_centre_offset_m(float carriageway_width_m, int lanes_per_dir,
                                     int lane_index) {
    const float half = carriageway_width_m * 0.5f;
    const int n = lanes_per_dir < 1 ? 1 : lanes_per_dir;
    const int i = lane_index < 0 ? 0 : (lane_index >= n ? n - 1 : lane_index);
    return half * (static_cast<float>(2 * i + 1) /
                   static_cast<float>(2 * n));
}

constexpr float lane_centre_offset_m(RoadClass c, int lane_index) {
    return lane_centre_offset_m(road_class_def(c).carriageway_width_m,
                                road_class_def(c).lanes_per_dir, lane_index);
}

// ---------------------------------------------------------------------------
//  Shared geometry constants
// ---------------------------------------------------------------------------

// A sidewalk is a strip this wide hugging each carriageway edge, raised by the
// kerb height. Pedestrians walk down its centre, half a width outboard of the
// kerb — the ped-path producer and the ribbon baker both derive from these two
// numbers so the walkable line is always on the drawn slab.
inline constexpr float kSidewalkWidthM = 3.0f;
inline constexpr float kKerbHeightM = 0.12f;

// Every drawn road surface sits this far ABOVE the terrain mesh, so it never
// z-fights the ground it drapes onto. A raised sidewalk slab sits one kerb
// height further up again: slab top == terrain + kDrapeEpsM + kKerbHeightM.
//
// This constant is load-bearing for collision, not just for looks. The road
// collision comes out of the BAKED RIBBON (see ribbon.h), so it inherits this
// lift for free — which is the whole point. The reference added the kerb step
// to its collision field but forgot the drape, and feet rested 6 cm under the
// surface they were visibly standing on.
inline constexpr float kDrapeEpsM = 0.06f;

}  // namespace apricot

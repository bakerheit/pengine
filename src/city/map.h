#pragma once

#include <cstdint>

namespace apricot {
namespace city {

// PINATTY — the map, as compiled C++ data.
//
// The city is a specific authored place you can learn by heart, and apricot
// ships no asset files. Those only look like they contradict. The map is a
// SKELETON — district polygons, terrain operators, landmarks, character
// parameters — and everything you can see is generated deterministically from
// it. So the skeleton is `constexpr` tables in this module: there is nothing
// to load, nothing to ship, and nothing to go missing on a fresh clone.
//
// Why not a data file, restated because somebody will propose one:
//
//   * World generation today CANNOT FAIL. It is arithmetic. A loader gives it
//     a runtime failure mode — missing, truncated, written by an older build —
//     and every caller of height_at() inherits a failure it has no way to
//     handle. There are hundreds of those calls per chunk.
//   * A file needs a parser, a schema, a version and a migration path. Four
//     pieces of real code with real bugs, bought to solve hot reload, which a
//     thirty-second incremental rebuild also solves.
//   * Compiled data gets the build's guarantees free: -Werror, designated
//     initialisers that stop compiling when a field is renamed, and
//     static_assert on the invariants. Everything below this line that says
//     "must" is checked at COMPILE TIME, not hoped for at runtime.
//   * It is still text in git. A district's character is thirty lines you can
//     read in a review, and changing a block size shows up in `git diff` as a
//     changed block size.
//
// The honest cost is no live editing. The mitigation is a later ticket: a
// dev-only overlay that DUMPS the table plus in-session tweaks back out as
// source you paste in. The source stays the truth; the tool is a printer.
//
// MODULE DEPENDENCY, one way only. `city` knows nothing about `terrain`. The
// height field calls into terrain_ops.h; nothing here calls back. Keep it that
// way — the day city includes terrain/heightmap.h, evaluating an operator can
// recurse into the field the operator is modifying.

// ---------------------------------------------------------------------------
//  The map seed
// ---------------------------------------------------------------------------

// THE MAP SEED. Pinned, part of the map, and as hard to change as a golden
// value.
//
// This is the split docs/architecture.md records as an amendment: the world
// used to be a pure function of (seed, coord) and is now a pure function of
// (map, seed, coord). kMapSeed selects the noise detail UNDERNEATH the
// authored skeleton — which hill the massif is, where the coastline wanders.
// The RUN seed (App::seed_) identifies the SESSION: mission shuffles, ambient
// variation, anything allowed to differ between two visits to the same corner.
//
// Changing this number changes the city. Every district polygon below was
// placed by measuring the terrain THIS seed produces, so moving it does not
// "reroll the world", it moves the ground out from under ten hand-placed
// districts and leaves the airfield in the sea. It also invalidates every
// replay tape and save.
//
// It was CHOSEN, not inherited. Nineteen candidates were measured over the
// 6144 m box at 12 m sampling after the retune and after the spawn dome was
// deleted; this one has the most buildable land (75.6% of it under 5 degrees,
// against 31.7% for the worst), puts its high ground in the north-east where
// Ferrone Hill wants it (mean land height NE 25.9 m against NW 12.1 m), and
// leaves the west and centre a broad low plain to lay a city on.
inline constexpr uint64_t kMapSeed = 0xDEADBEEFull;

// ---------------------------------------------------------------------------
//  The world box
// ---------------------------------------------------------------------------

// Half the world box, in metres. 6144 m across is 96 x 96 chunks exactly at
// kChunkMetres = 64, which is why it is this number and not 6000.
//
// The brief asked for ~16 km2 of land on a 4 km island, and those cannot both
// be true: a 4 x 4 km box IS 16 km2, so 16 km2 of land means no water and
// therefore no island. 6144 m of box holding a ~5.5 km island measures 16.4
// km2 of land after the terrain operators land — see tests/city_map_tests.cpp,
// which prints the figure it measured rather than repeating this one.
inline constexpr float kWorldHalfMetres = 3072.0f;

// ---------------------------------------------------------------------------
//  Small authoring types
// ---------------------------------------------------------------------------

// A point in the XZ plane, in world metres. +X is east, +Z is SOUTH, +Y is up.
//
// Deliberately not glm::vec2. This is authored data: it is designated-
// initialised in tables, it is compared at compile time, and it wants to be a
// literal aggregate with named members that read as coordinates. `{.x, .z}`
// also stops the reader wondering whether `.y` meant height or northing —
// which it did, in the reference implementation, in both directions.
struct Vec2 {
    float x = 0.0f;
    float z = 0.0f;
};

// An inclusive authored range: heights, footprint sizes, anything a generator
// samples between two ends. Spelled so `{24.0f, 88.0f}` initialises it.
struct Span {
    float lo = 0.0f;
    float hi = 0.0f;

    constexpr bool ordered() const { return lo <= hi; }
    constexpr float mid() const { return 0.5f * (lo + hi); }
};

// Longest district boundary the tables may use.
inline constexpr int kMaxBoundaryPoints = 12;

// A district boundary polygon.
//
// COARSE ON PURPOSE. This is the district's JURISDICTION — which rules apply,
// which palette, which police response — not its outline. The visible edge is
// wherever its generated blocks stop, which the road spines decide. Twelve
// points is already more than any of the ten need; if one wants more, the
// district is probably two districts.
struct Boundary {
    Vec2 points[kMaxBoundaryPoints]{};
    int count = 0;

    // Twice the signed area, by the shoelace formula in the XZ plane.
    //
    // The sign is the WINDING, and every boundary in the tables must have the
    // same one or point-in-polygon and any future inset/offset silently
    // disagree about which side is inside. Positive here, asserted at compile
    // time for all ten. (Positive in XZ with +Z south is what you draw as
    // clockwise on a screen; the name of the direction matters far less than
    // that all ten agree, so the invariant is stated as the sign.)
    constexpr float signed_area2() const {
        float acc = 0.0f;
        for (int i = 0; i < count; ++i) {
            const Vec2 a = points[i];
            const Vec2 b = points[(i + 1) % count];
            acc += a.x * b.z - b.x * a.z;
        }
        return acc;
    }

    constexpr float area_m2() const {
        const float a2 = signed_area2();
        return (a2 < 0.0f ? -a2 : a2) * 0.5f;
    }

    // Crossing-number point-in-polygon. Constexpr so a test can pin a
    // containment at compile time, and so the district lookup below is
    // evaluable in a constant expression.
    //
    // The half-open comparison `(a.z > z) != (b.z > z)` is what makes a point
    // exactly on a horizontal edge belong to exactly one side rather than to
    // both or neither. Two districts sharing an edge must not both claim a
    // point on it — a pedestrian standing on the boundary has to have one
    // police response time, not two.
    constexpr bool contains(float x, float z) const {
        bool in = false;
        for (int i = 0, j = count - 1; i < count; j = i++) {
            const Vec2 a = points[i];
            const Vec2 b = points[j];
            if ((a.z > z) != (b.z > z)) {
                const float t = (z - a.z) / (b.z - a.z);
                if (x < a.x + t * (b.x - a.x)) in = !in;
            }
        }
        return in;
    }

    // Axis-aligned bounds, for the bucket index and for measurement.
    constexpr Vec2 min_corner() const {
        Vec2 m{points[0].x, points[0].z};
        for (int i = 1; i < count; ++i) {
            if (points[i].x < m.x) m.x = points[i].x;
            if (points[i].z < m.z) m.z = points[i].z;
        }
        return m;
    }
    constexpr Vec2 max_corner() const {
        Vec2 m{points[0].x, points[0].z};
        for (int i = 1; i < count; ++i) {
            if (points[i].x > m.x) m.x = points[i].x;
            if (points[i].z > m.z) m.z = points[i].z;
        }
        return m;
    }

    constexpr Vec2 centroid() const {
        // Area-weighted, not the average of the vertices. The vertex average
        // drifts toward whichever edge somebody subdivided, so a district with
        // a detailed coastline would put its "centre" — and therefore its
        // tallest buildings, via HeightRamp::PeakAtCentre — out at the shore.
        float cx = 0.0f, cz = 0.0f;
        for (int i = 0; i < count; ++i) {
            const Vec2 a = points[i];
            const Vec2 b = points[(i + 1) % count];
            const float cross = a.x * b.z - b.x * a.z;
            cx += (a.x + b.x) * cross;
            cz += (a.z + b.z) * cross;
        }
        const float a6 = 3.0f * signed_area2();
        return Vec2{cx / a6, cz / a6};
    }
};

// ---------------------------------------------------------------------------
//  District identity
// ---------------------------------------------------------------------------

// The ten districts. Dense and ordered, because the id indexes kDistricts[]
// directly — a static_assert in districts.h pins that, so a reordered enum or
// a table entry out of order fails the build rather than quietly handing
// Saltmarsh the docks' police response time.
//
// The countryside — the Meadows — is deliberately NOT a district. It is
// everything the ten polygons do not claim, it has no boundary to author, and
// giving it one would mean maintaining the negative space of nine other
// polygons by hand. district_at() returns Count for it, and that is its name.
enum class DistrictId : uint8_t {
    VellumRow = 0,      // financial core, the canonical grid chase
    HallowaySquare,     // civic: police HQ, courthouse, hospital
    Saltmarsh,          // old town, pre-grid, rewards memory
    OstendDocks,        // container port, straight-line speed inside a trap
    KeplerFlats,        // refinery and rail, hazards as terrain
    FerroneHill,        // wealth on the massif, vertical
    NickelHeights,      // suburbs, dead ends punish panic
    TheStrand,          // beachfront, the top-speed stretch
    CamberPoint,        // airfield on a spit, the escape hatch
    Marrow,             // quarry and farms, off-road
    Count
};

inline constexpr int kDistrictCount = static_cast<int>(DistrictId::Count);

// ---------------------------------------------------------------------------
//  Character parameters
// ---------------------------------------------------------------------------

// How a district's streets are laid out. The generator turns one of these into
// a street graph; the district table says which and with what dimensions.
enum class BlockPattern : uint8_t {
    Grid,        // regular blocks at an authored angle. Vellum Row.
    Radial,      // arms off one plaza. Halloway Square.
    Organic,     // pre-grid lanes that do not line up. Saltmarsh.
    Spine,       // one arterial with service stubs. Ostend Docks.
    Yard,        // wide arterials, level crossings, dirt spurs. Kepler Flats.
    Switchback,  // hairpins up a slope with cul-de-sacs. Ferrone Hill.
    Loops,       // collectors feeding cul-de-sac loops. Nickel Heights.
    Boulevard,   // one road, almost no junctions. The Strand.
    Perimeter,   // a runway and the road around it. Camber Point.
    Tracks       // dirt web, one paved link. Marrow.
};

// Five road classes. Four widths come straight from the reference
// implementation's table (Highway 30, Avenue 22, Street 14, Dirt 9) — that is
// already a founder pass that feels right, so there is no reason to relearn
// it. Alley is new: there is no 6 m class in the reference.
enum class RoadClass : uint8_t { Freeway, Arterial, Street, Alley, Dirt };

// Metres of carriageway per class. Not a per-district parameter: a street is a
// street everywhere, and letting each district pick its own widths is how you
// get a lane graph that does not join up at a district boundary.
inline constexpr float road_width_m(RoadClass c) {
    return c == RoadClass::Freeway    ? 30.0f
           : c == RoadClass::Arterial ? 22.0f
           : c == RoadClass::Street   ? 14.0f
           : c == RoadClass::Alley    ? 6.0f
                                      : 9.0f;
}

// How building height varies across a district. THIS IS THE SILHOUETTE, and
// the silhouette is how a player at 800 m knows which way they are facing.
// Sample heights uniformly per building instead and every district looks like
// static.
enum class HeightRamp : uint8_t {
    Flat,           // a mat of near-equal heights. Saltmarsh, Nickel Heights.
    PeakAtCentre,   // a bell curve. Vellum Row: a skyline with a middle.
    FallToWater,    // tall inland, low at the shore. The Strand.
    Scattered       // low sheds punctuated by tall exceptions. The docks.
};

// Facade generation kit: window pitch, floor height, colour family.
enum class FacadeKit : uint8_t {
    Downtown, Civic, OldTown, Industrial, Suburban, Resort, Utility, Rural
};

// Street furniture kit. The lamp posts differ, the bins differ, the road
// markings differ. Cheap, one enum, and it does more for legibility per byte
// than anything else in this file.
enum class PropKit : uint8_t {
    Downtown, Civic, OldTown, Docks, Industrial, Hillside, Suburban, Seafront,
    Airfield, Rural
};

// What the ground is PAVED with, per district.
//
// Deliberately NOT terrain::Surface. That enum has four members — Rock,
// Gravel, Grass, Sand — it is append-only by contract, and every one of its
// members is wired to a tyre grip figure in physics/surface.h. Writing
// `Surface::Asphalt` here (as the design document does) would mean adding
// members to a physics contract as a side effect of authoring a map, and
// docs/design/pinatty.md itself flags that as a separate conversation.
//
// So this is the AUTHORED INTENT, carrying no physics meaning yet. When
// terrain::Surface grows a paved member, one mapping function connects the
// two and this enum stops being a placeholder without any district entry
// changing.
enum class PavingKit : uint8_t {
    None, Asphalt, CrackedAsphalt, Concrete, Cobble, Brick, Flagstone,
    Gravel, Dirt, Runway, Boardwalk, Sand, Lawn
};

struct BlockParams {
    BlockPattern pattern = BlockPattern::Grid;
    float grid_deg = 0.0f;   // rotation off north. Nothing real is square.
    Vec2 block_m{};          // block size, x by z, before rotation
    RoadClass street = RoadClass::Street;
    bool one_way_pair = false;
    float alley_odds = 0.0f;  // per block, hash-keyed
};

struct BuildParams {
    float coverage = 0.0f;   // fraction of a lot built on
    float setback_m = 0.0f;
    Span height_m{};
    HeightRamp height_ramp = HeightRamp::Flat;
    Span footprint_m{};
    FacadeKit facades = FacadeKit::Suburban;
};

struct GroundParams {
    PavingKit road = PavingKit::Asphalt;
    PavingKit walk = PavingKit::Concrete;
    PavingKit verge = PavingKit::None;
    float kerb_m = 0.0f;
};

struct PropParams {
    PropKit kit = PropKit::Rural;
    float density = 1.0f;
};

// Traffic, pedestrian and parked-car density multipliers. First-pass feel
// numbers, in one table on purpose so somebody with a controller can tune them
// in one place.
struct PopParams {
    float traffic = 0.0f;
    float ped = 0.0f;
    float parked = 0.0f;
};

// Patrol density and response time in seconds. Halloway Square is the shortest
// response on the island because the police HQ is in it, and Marrow is the
// longest because it is a quarry.
struct HeatParams {
    float patrol = 0.0f;
    float response_s = 0.0f;
};

struct District {
    DistrictId id = DistrictId::Count;
    const char* name = nullptr;
    Boundary boundary{};
    BlockParams blocks{};
    BuildParams build{};
    GroundParams ground{};
    PropParams props{};
    PopParams pop{};
    HeatParams heat{};
};

// ---------------------------------------------------------------------------
//  Landmarks
// ---------------------------------------------------------------------------

// How far a landmark READS. On a 5.5 km island you navigate by silhouette, so
// this is the tier that decides whether a thing tells you which island you are
// on, which district you are in, or which block you are on.
enum class LandmarkTier : uint8_t {
    Island,    // 4+ km. Two of these must be visible from anywhere on land.
    District,  // 1-2 km. Tells you which district.
    Corner     // 100-300 m. Tells you which block.
};

enum class LandmarkKind : uint8_t {
    Tower, Mast, Dome, Crane, FlareStack, WaterTower, FerrisWheel, Pier,
    Lighthouse, ControlTower, Clock, Steps, QuarryFace, BridgeTower,
    StadiumBowl, Neon, Mural, Silo, Windbreak
};

struct Landmark {
    const char* name = nullptr;
    Vec2 pos{};
    // Height of the STRUCTURE above the ground under it, not above sea level.
    // Above-sea-level is ground + this, and the ground is a measurement, not a
    // constant — pinning ASL here would be a second source of truth for the
    // terrain and the two would drift the first time anybody moved a hill.
    float height_m = 0.0f;
    LandmarkTier tier = LandmarkTier::Corner;
    LandmarkKind kind = LandmarkKind::Mural;
    DistrictId district = DistrictId::Count;
};

// ---------------------------------------------------------------------------
//  Queries
// ---------------------------------------------------------------------------

// The district containing a world position, or DistrictId::Count for the
// countryside. Pure, table-driven, no allocation.
//
// Polygons are tested in table order and the FIRST hit wins, so an overlap is
// resolved deterministically rather than by whichever the compiler laid out
// first. The tables are asserted non-overlapping at their bounding boxes where
// that is achievable; where two districts genuinely abut, table order is the
// tie-break and it is authored.
DistrictId district_at(float x, float z);

// The entry for an id. Never null for a valid id.
const District& district(DistrictId id);

// Human-readable name, including "the Meadows" for DistrictId::Count, so a
// caller printing a district never has to special-case the countryside.
const char* district_name(DistrictId id);

}  // namespace city
}  // namespace apricot

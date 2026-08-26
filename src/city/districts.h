#pragma once

#include <cstddef>

#include "city/map.h"

namespace apricot {
namespace city {

// THE TEN DISTRICTS.
//
// The test each one has to pass is not "does it look different" — it is: IT
// MUST CHASE DIFFERENTLY FROM ITS NEIGHBOURS. A district that plays like the
// one next door is filler with a different texture, and a city where downtown
// and the suburbs generate the same silhouette has failed even if every test
// passes. tests/city_map_tests.cpp measures that on the ground rather than
// taking this comment's word for it: flat-land fraction, height range and
// drivable fraction, per district, printed.
//
// Every polygon below was placed against MEASURED terrain for kMapSeed, not
// drawn on a napkin. Moving the seed moves the ground out from under all ten.
//
// The countryside — the Meadows — is not in this table and must not be. It is
// everything the ten polygons do not claim; authoring it would mean
// maintaining the negative space of nine other polygons by hand, and the day
// somebody nudged Marrow's edge the Meadows would have a seam in it.

inline constexpr District kDistricts[] = {

    // -----------------------------------------------------------------------
    // 1. VELLUM ROW — the canonical grid chase.
    //
    // Every intersection is four choices, so escape is about READING the
    // pursuit rather than out-driving it. Alleys are the pressure valve and
    // the police know them too. This is where the game teaches you that a
    // right turn at speed costs three seconds.
    // -----------------------------------------------------------------------
    {.id = DistrictId::VellumRow,
     .name = "Vellum Row",

     // Coarse on purpose: this is the district's JURISDICTION, not its
     // outline. The visible edge is where its generated blocks stop.
     .boundary = {.points = {{-470.0f, -520.0f},
                             {560.0f, -470.0f},
                             {610.0f, 300.0f},
                             {180.0f, 430.0f},
                             {-430.0f, 360.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Grid,
                .grid_deg = 6.0f,  // off north: nothing real is square
                .block_m = {92.0f, 62.0f},
                .street = RoadClass::Street,
                .one_way_pair = true,  // alternating N/S and E/W pairs
                .alley_odds = 0.75f},

     .build = {.coverage = 0.86f,  // fraction of a lot built on
               .setback_m = 1.0f,
               .height_m = {24.0f, 88.0f},
               .height_ramp = HeightRamp::PeakAtCentre,  // makes a skyline
               .footprint_m = {14.0f, 34.0f},
               .facades = FacadeKit::Downtown},

     .ground = {.road = PavingKit::Asphalt,
                .walk = PavingKit::Concrete,
                .verge = PavingKit::None,
                .kerb_m = 0.12f},

     .props = {.kit = PropKit::Downtown, .density = 1.0f,
               .wild = 0.0f},  // solid city block to block
     .pop = {.traffic = 1.4f, .ped = 2.2f, .parked = 0.9f},
     .heat = {.patrol = 1.2f, .response_s = 6.0f}},

    // -----------------------------------------------------------------------
    // 2. HALLOWAY SQUARE — heat generation and consequence.
    //
    // Police HQ, so the response here is the shortest on the island, and the
    // radial plan means every arm is covered from the centre. It is also the
    // mission hub, so you are forced to keep coming back to the most dangerous
    // place you know. The plaza is where a stray shot costs you two stars.
    // -----------------------------------------------------------------------
    {.id = DistrictId::HallowaySquare,
     .name = "Halloway Square",

     .boundary = {.points = {{-430.0f, 380.0f},
                             {180.0f, 440.0f},
                             {430.0f, 900.0f},
                             {-60.0f, 1140.0f},
                             {-560.0f, 880.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Radial,
                .grid_deg = 0.0f,  // the plaza sets the angle, not north
                .block_m = {110.0f, 74.0f},
                .street = RoadClass::Arterial,
                .one_way_pair = false,
                .alley_odds = 0.15f},

     .build = {.coverage = 0.55f,
               .setback_m = 6.0f,  // civic buildings stand back from the road
               .height_m = {18.0f, 40.0f},
               .height_ramp = HeightRamp::PeakAtCentre,
               .footprint_m = {22.0f, 60.0f},
               .facades = FacadeKit::Civic},

     .ground = {.road = PavingKit::Asphalt,
                .walk = PavingKit::Flagstone,
                .verge = PavingKit::Lawn,
                .kerb_m = 0.14f},

     .props = {.kit = PropKit::Civic, .density = 1.1f,
               .wild = 0.06f},  // a few planted squares
     .pop = {.traffic = 1.1f, .ped = 2.6f, .parked = 0.7f},
     .heat = {.patrol = 2.0f, .response_s = 3.0f}},

    // -----------------------------------------------------------------------
    // 3. SALTMARSH — the district that rewards memory.
    //
    // Lanes too narrow for a cruiser to swing, no through route the minimap
    // can helpfully draw, and cut-throughs a local knows and the AI never
    // takes. The "lose them because you know where you are" district, and it
    // only works if the layout is fixed forever — which is the argument for
    // the whole map being authored, in one district.
    // -----------------------------------------------------------------------
    {.id = DistrictId::Saltmarsh,
     .name = "Saltmarsh",

     .boundary = {.points = {{-1470.0f, -320.0f},
                             {-520.0f, -400.0f},
                             {-460.0f, 340.0f},
                             {-1010.0f, 560.0f},
                             {-1510.0f, 270.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Organic,
                .grid_deg = 0.0f,
                .block_m = {38.0f, 31.0f},  // pre-grid: small and irregular
                .street = RoadClass::Alley,
                .one_way_pair = false,
                .alley_odds = 0.9f},

     .build = {.coverage = 0.92f,  // the densest thing on the island
               .setback_m = 0.0f,  // the wall IS the street edge
               .height_m = {8.0f, 22.0f},
               .height_ramp = HeightRamp::Flat,  // a mat, not a skyline
               .footprint_m = {7.0f, 16.0f},
               .facades = FacadeKit::OldTown},

     .ground = {.road = PavingKit::Cobble,
                .walk = PavingKit::Brick,
                .verge = PavingKit::None,
                .kerb_m = 0.0f},  // no kerb at all: that is what makes it old

     .props = {.kit = PropKit::OldTown, .density = 1.3f,
               .wild = 0.04f},  // courtyard trees, nothing more
     .pop = {.traffic = 0.5f, .ped = 1.8f, .parked = 0.6f},
     .heat = {.patrol = 0.6f, .response_s = 11.0f}},

    // -----------------------------------------------------------------------
    // 4. OSTEND DOCKS — straight-line speed inside a trap.
    //
    // Long open runs between container stacks let you build real speed, and
    // then the quay ends in water. Chases here are decided by whether you know
    // which stub is a dead end.
    // -----------------------------------------------------------------------
    {.id = DistrictId::OstendDocks,
     .name = "Ostend Docks",

     .boundary = {.points = {{-2100.0f, -1000.0f},
                             {-1120.0f, -940.0f},
                             {-1080.0f, -500.0f},
                             {-1720.0f, -420.0f},
                             {-2130.0f, -620.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Spine,
                .grid_deg = -3.0f,
                .block_m = {180.0f, 90.0f},  // warehouse-sized
                .street = RoadClass::Arterial,
                .one_way_pair = false,
                .alley_odds = 0.4f},

     .build = {.coverage = 0.40f,
               .setback_m = 4.0f,
               .height_m = {6.0f, 30.0f},
               .height_ramp = HeightRamp::Scattered,  // low sheds, tall cranes
               .footprint_m = {30.0f, 90.0f},
               .facades = FacadeKit::Industrial},

     .ground = {.road = PavingKit::Concrete,
                .walk = PavingKit::Concrete,
                .verge = PavingKit::Gravel,
                .kerb_m = 0.06f},

     .props = {.kit = PropKit::Docks, .density = 0.8f,
               .wild = 0.0f},  // concrete apron
     .pop = {.traffic = 0.6f, .ped = 0.3f, .parked = 0.4f},
     .heat = {.patrol = 0.5f, .response_s = 14.0f}},

    // -----------------------------------------------------------------------
    // 5. KEPLER FLATS — hazards as terrain.
    //
    // Level crossings a train genuinely closes, tank farms you do not want to
    // shoot near, dirt spurs that let a pursuit off-road. The one district
    // where the world is chasing you too.
    // -----------------------------------------------------------------------
    {.id = DistrictId::KeplerFlats,
     .name = "Kepler Flats",

     .boundary = {.points = {{-1290.0f, -2040.0f},
                             {-120.0f, -2060.0f},
                             {160.0f, -1680.0f},
                             {-440.0f, -1620.0f},
                             {-1200.0f, -1660.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Yard,
                .grid_deg = 2.0f,
                .block_m = {220.0f, 140.0f},
                .street = RoadClass::Arterial,
                .one_way_pair = false,
                .alley_odds = 0.2f},

     .build = {.coverage = 0.30f,
               .setback_m = 8.0f,
               .height_m = {4.0f, 36.0f},  // sheds, and one flare stack
               .height_ramp = HeightRamp::Scattered,
               .footprint_m = {24.0f, 110.0f},
               .facades = FacadeKit::Industrial},

     .ground = {.road = PavingKit::CrackedAsphalt,
                .walk = PavingKit::Gravel,
                .verge = PavingKit::Dirt,
                .kerb_m = 0.0f},

     .props = {.kit = PropKit::Industrial, .density = 0.7f,
               .wild = 0.05f},  // scrub on the rail margins
     .pop = {.traffic = 0.7f, .ped = 0.4f, .parked = 0.3f},
     .heat = {.patrol = 0.4f, .response_s = 15.0f}},

    // -----------------------------------------------------------------------
    // 6. FERRONE HILL — vertical.
    //
    // Switchbacks turn a chase into a series of commitments; a missed hairpin
    // is a 30 m drop, not a scrape. One paved road in makes it the best
    // roadblock in the game, and the unmarked fire road out is the single most
    // valuable piece of local knowledge Pinatty has to teach. From the mast
    // you can see every other landmark, which is how a player builds a mental
    // map without opening one.
    // -----------------------------------------------------------------------
    {.id = DistrictId::FerroneHill,
     .name = "Ferrone Hill",

     .boundary = {.points = {{220.0f, -1900.0f},
                             {1190.0f, -2020.0f},
                             {1560.0f, -1300.0f},
                             {1040.0f, -960.0f},
                             {330.0f, -1120.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Switchback,
                .grid_deg = 0.0f,  // the contours set the angle
                .block_m = {140.0f, 90.0f},
                .street = RoadClass::Street,
                .one_way_pair = false,
                .alley_odds = 0.0f},  // no alleys: there is nowhere to put one

     .build = {.coverage = 0.22f,  // the emptiest built district
               .setback_m = 12.0f,
               .height_m = {6.0f, 14.0f},
               .height_ramp = HeightRamp::Flat,
               .footprint_m = {18.0f, 44.0f},
               .facades = FacadeKit::Suburban},

     .ground = {.road = PavingKit::Asphalt,
                .walk = PavingKit::Concrete,
                .verge = PavingKit::Lawn,
                .kerb_m = 0.15f},  // deep kerb: it is also a drainage channel

     .props = {.kit = PropKit::Hillside, .density = 0.6f,
               .wild = 0.85f},  // wooded switchbacks; the hill is the draw
     .pop = {.traffic = 0.3f, .ped = 0.4f, .parked = 0.5f},
     .heat = {.patrol = 0.9f, .response_s = 12.0f}},

    // -----------------------------------------------------------------------
    // 7. NICKEL HEIGHTS — dead ends punish panic.
    //
    // Wide, fast, inviting, and about a third of the loops go nowhere. Long
    // sight lines mean the police keep contact even when they lose ground. The
    // correct play is to LEAVE, and the district is built to make leaving feel
    // too slow.
    // -----------------------------------------------------------------------
    {.id = DistrictId::NickelHeights,
     .name = "Nickel Heights",

     .boundary = {.points = {{700.0f, -80.0f},
                             {1440.0f, -180.0f},
                             {1490.0f, 600.0f},
                             {1160.0f, 880.0f},
                             {680.0f, 720.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Loops,
                .grid_deg = -11.0f,
                .block_m = {150.0f, 110.0f},
                .street = RoadClass::Street,
                .one_way_pair = false,
                .alley_odds = 0.05f},

     .build = {.coverage = 0.35f,
               .setback_m = 7.0f,
               .height_m = {4.0f, 9.0f},  // a uniform carpet, by design
               .height_ramp = HeightRamp::Flat,
               .footprint_m = {11.0f, 19.0f},
               .facades = FacadeKit::Suburban},

     .ground = {.road = PavingKit::Asphalt,
                .walk = PavingKit::Concrete,
                .verge = PavingKit::Lawn,
                .kerb_m = 0.1f},

     .props = {.kit = PropKit::Suburban, .density = 0.9f,
               .wild = 0.35f},  // gardens and verges, not woodland
     .pop = {.traffic = 0.8f, .ped = 0.9f, .parked = 1.2f},
     .heat = {.patrol = 0.7f, .response_s = 10.0f}},

    // -----------------------------------------------------------------------
    // 8. THE STRAND — the top-speed stretch.
    //
    // One road, 2.2 km, gentle curve, sea on one side. Nowhere to turn off
    // means nothing to think about except throttle — and it makes a roadblock
    // genuinely frightening, because there is no alternative route to fall
    // back to. Every map needs one place where the answer is just speed.
    // -----------------------------------------------------------------------
    {.id = DistrictId::TheStrand,
     .name = "The Strand",

     .boundary = {.points = {{1740.0f, -600.0f},
                             {2280.0f, -460.0f},
                             {2180.0f, 500.0f},
                             {1940.0f, 1200.0f},
                             {1560.0f, 1880.0f},
                             {1330.0f, 1720.0f},
                             {1640.0f, 1060.0f},
                             {1560.0f, 300.0f},
                             {1580.0f, -480.0f}},
                  .count = 9},

     .blocks = {.pattern = BlockPattern::Boulevard,
                .grid_deg = 0.0f,
                .block_m = {170.0f, 80.0f},
                .street = RoadClass::Arterial,
                .one_way_pair = false,
                .alley_odds = 0.25f},

     .build = {.coverage = 0.50f,
               .setback_m = 5.0f,
               .height_m = {12.0f, 45.0f},
               .height_ramp = HeightRamp::FallToWater,  // hotels step down
               .footprint_m = {18.0f, 52.0f},
               .facades = FacadeKit::Resort},

     .ground = {.road = PavingKit::Asphalt,
                .walk = PavingKit::Boardwalk,
                .verge = PavingKit::Sand,
                .kerb_m = 0.1f},

     .props = {.kit = PropKit::Seafront, .density = 1.0f,
               .wild = 0.12f},  // palms along the promenade
     .pop = {.traffic = 1.2f, .ped = 1.6f, .parked = 1.0f},
     .heat = {.patrol = 0.8f, .response_s = 9.0f}},

    // -----------------------------------------------------------------------
    // 9. CAMBER POINT — the escape hatch and the arena.
    //
    // The plane is here, which makes the causeway the highest-value chokepoint
    // on the island. The runway is also the flattest large open surface in the
    // world: where handling gets tested, where stunt jumps land, and where a
    // shootout has no cover at all.
    // -----------------------------------------------------------------------
    {.id = DistrictId::CamberPoint,
     .name = "Camber Point",

     .boundary = {.points = {{-420.0f, 2010.0f},
                             {680.0f, 1985.0f},
                             {730.0f, 2270.0f},
                             {60.0f, 2380.0f},
                             {-440.0f, 2230.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Perimeter,
                .grid_deg = 0.0f,
                .block_m = {260.0f, 160.0f},
                .street = RoadClass::Street,
                .one_way_pair = false,
                .alley_odds = 0.0f},

     .build = {.coverage = 0.10f,  // emptiest district in the map, on purpose
               .setback_m = 14.0f,
               .height_m = {4.0f, 18.0f},
               .height_ramp = HeightRamp::Scattered,
               .footprint_m = {26.0f, 80.0f},
               .facades = FacadeKit::Utility},

     .ground = {.road = PavingKit::Runway,
                .walk = PavingKit::Concrete,
                .verge = PavingKit::Sand,
                .kerb_m = 0.0f},

     .props = {.kit = PropKit::Airfield, .density = 0.35f,
               .wild = 0.02f},  // cleared for sightlines, by regulation
     .pop = {.traffic = 0.2f, .ped = 0.2f, .parked = 0.2f},
     .heat = {.patrol = 0.6f, .response_s = 16.0f}},

    // -----------------------------------------------------------------------
    // 10. MARROW — off-road, where the cruiser cannot follow.
    //
    // Grip changes under you, the quarry has a ramp with a drop on the
    // outside, and the dirt web has no signage. The countryside around it
    // exists so the distance between districts is FELT; a city where every
    // district touches every other is a theme park, not a place.
    // -----------------------------------------------------------------------
    {.id = DistrictId::Marrow,
     .name = "Marrow",

     .boundary = {.points = {{-1690.0f, 960.0f},
                             {-930.0f, 930.0f},
                             {-880.0f, 1480.0f},
                             {-1250.0f, 1660.0f},
                             {-1670.0f, 1480.0f}},
                  .count = 5},

     .blocks = {.pattern = BlockPattern::Tracks,
                .grid_deg = 0.0f,
                .block_m = {320.0f, 240.0f},
                .street = RoadClass::Dirt,
                .one_way_pair = false,
                .alley_odds = 0.0f},

     .build = {.coverage = 0.04f,
               .setback_m = 20.0f,
               .height_m = {4.0f, 12.0f},
               .height_ramp = HeightRamp::Flat,
               .footprint_m = {14.0f, 46.0f},
               .facades = FacadeKit::Rural},

     .ground = {.road = PavingKit::Dirt,
                .walk = PavingKit::None,
                .verge = PavingKit::Gravel,
                .kerb_m = 0.0f},

     .props = {.kit = PropKit::Rural, .density = 0.5f,
               .wild = 1.0f},  // open country: this is the baseline
     .pop = {.traffic = 0.15f, .ped = 0.05f, .parked = 0.05f},
     .heat = {.patrol = 0.2f, .response_s = 22.0f}},
};

// ---------------------------------------------------------------------------
//  Compile-time invariants
// ---------------------------------------------------------------------------

static_assert(sizeof(kDistricts) / sizeof(kDistricts[0]) ==
                  static_cast<std::size_t>(kDistrictCount),
              "kDistricts and DistrictId::Count disagree about how many "
              "districts there are");

// The id indexes the table directly. Everything downstream — police response,
// traffic density, the palette — reads kDistricts[int(id)], so an entry out of
// order hands Saltmarsh the docks' response time and nothing says a word.
constexpr bool district_ids_are_dense_and_ordered() {
    for (int i = 0; i < kDistrictCount; ++i) {
        if (static_cast<int>(kDistricts[i].id) != i) return false;
        if (kDistricts[i].name == nullptr) return false;
    }
    return true;
}
static_assert(district_ids_are_dense_and_ordered(),
              "district ids must be dense, unique and in table order");

// One winding for all ten, or point-in-polygon and any future inset disagree
// about which side is inside.
constexpr bool district_boundaries_are_well_formed() {
    for (int i = 0; i < kDistrictCount; ++i) {
        const Boundary& b = kDistricts[i].boundary;
        if (b.count < 3 || b.count > kMaxBoundaryPoints) return false;
        if (b.signed_area2() <= 0.0f) return false;  // consistent winding
        if (b.area_m2() < 100000.0f) return false;   // 0.1 km2 floor
        for (int p = 0; p < b.count; ++p) {
            if (b.points[p].x < -kWorldHalfMetres) return false;
            if (b.points[p].x > kWorldHalfMetres) return false;
            if (b.points[p].z < -kWorldHalfMetres) return false;
            if (b.points[p].z > kWorldHalfMetres) return false;
        }
    }
    return true;
}
static_assert(district_boundaries_are_well_formed(),
              "a district boundary is degenerate, wound the wrong way, tiny, "
              "or hanging outside the world box");

// The character parameters have to be parameters, not decoration.
constexpr bool district_character_is_sane() {
    for (int i = 0; i < kDistrictCount; ++i) {
        const District& d = kDistricts[i];
        if (!d.build.height_m.ordered()) return false;
        if (!d.build.footprint_m.ordered()) return false;
        if (d.build.height_m.lo <= 0.0f) return false;
        if (d.build.coverage <= 0.0f || d.build.coverage > 1.0f) return false;
        if (d.blocks.alley_odds < 0.0f || d.blocks.alley_odds > 1.0f) return false;
        if (d.blocks.block_m.x <= 0.0f || d.blocks.block_m.z <= 0.0f) return false;
        if (d.heat.response_s <= 0.0f) return false;
        if (d.pop.traffic < 0.0f || d.pop.ped < 0.0f) return false;
        // A grid rotated past a right angle is the same grid rotated the other
        // way, and an authored 91 degrees is always a typo.
        if (d.blocks.grid_deg < -45.0f || d.blocks.grid_deg > 45.0f) return false;
    }
    return true;
}
static_assert(district_character_is_sane(),
              "a district's character parameters are out of range");

// Halloway Square holds the police HQ, so it must have the fastest response
// and the heaviest patrol on the island. This is a GAMEPLAY invariant, pinned
// at compile time because it is exactly the sort of thing that gets quietly
// undone by somebody tuning one number in isolation.
constexpr bool halloway_is_the_hottest() {
    const District& h = kDistricts[static_cast<int>(DistrictId::HallowaySquare)];
    for (int i = 0; i < kDistrictCount; ++i) {
        if (i == static_cast<int>(DistrictId::HallowaySquare)) continue;
        if (kDistricts[i].heat.response_s <= h.heat.response_s) return false;
        if (kDistricts[i].heat.patrol >= h.heat.patrol) return false;
    }
    return true;
}
static_assert(halloway_is_the_hottest(),
              "somewhere responds faster than the district with the police HQ");

}  // namespace city
}  // namespace apricot

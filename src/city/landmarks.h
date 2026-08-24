#pragma once

#include <cstddef>

#include "city/districts.h"
#include "city/map.h"

namespace apricot {
namespace city {

// LANDMARKS — how you navigate a 5.5 km island without opening a map.
//
// You navigate by silhouette, so the table is ranked by HOW FAR A THING READS
// rather than by what it is. Three tiers:
//
//   Island   4+ km   — visible from anywhere on land. Tells you where you are
//                      on the island. There must be at least two.
//   District 1-2 km  — tells you which district you are in.
//   Corner   100-300 m — tells you which block you are on.
//
// The Island tier is not a hope, it is a constraint to be tested: two of them
// must clear the horizon from anywhere on land. That test needs the road
// network to sample against and lands with the spine ticket; what
// tests/city_map_tests.cpp pins today is the part that is answerable now —
// that the Island-tier entries actually stand where the terrain makes them
// tall, measured, rather than where a comment claims they do.
//
// `height_m` is the STRUCTURE's height above the ground beneath it, never
// above sea level. ASL is ground + this. Authoring ASL would put a second
// source of truth for the terrain in this table, and the two would disagree
// the first time anybody moved a hill by five metres.

inline constexpr Landmark kLandmarks[] = {
    // ---- Island tier: three, and two must be visible from anywhere --------
    {.name = "Ferrone Mast",
     .pos = {760.0f, -1480.0f},
     .height_m = 60.0f,
     .tier = LandmarkTier::Island,
     .kind = LandmarkKind::Mast,
     .district = DistrictId::FerroneHill},

    {.name = "Trinity Tower",
     .pos = {60.0f, -60.0f},
     .height_m = 88.0f,
     .tier = LandmarkTier::Island,
     .kind = LandmarkKind::Tower,
     .district = DistrictId::VellumRow},

    {.name = "Kepler flare stack",
     .pos = {-520.0f, -1740.0f},
     .height_m = 46.0f,
     .tier = LandmarkTier::Island,
     .kind = LandmarkKind::FlareStack,
     .district = DistrictId::KeplerFlats},

    // ---- District tier: which district am I in ---------------------------
    {.name = "Ostend gantry crane 1",
     .pos = {-1620.0f, -760.0f},
     .height_m = 42.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Crane,
     .district = DistrictId::OstendDocks},

    {.name = "Ostend gantry crane 2",
     .pos = {-1660.0f, -600.0f},
     .height_m = 42.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Crane,
     .district = DistrictId::OstendDocks},

    {.name = "Ostend gantry crane 3",
     .pos = {-1700.0f, -440.0f},
     .height_m = 42.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Crane,
     .district = DistrictId::OstendDocks},

    {.name = "Ostend gantry crane 4",
     .pos = {-1740.0f, -290.0f},
     .height_m = 42.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Crane,
     .district = DistrictId::OstendDocks},

    {.name = "Halloway courthouse dome",
     .pos = {-70.0f, 760.0f},
     .height_m = 38.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Dome,
     .district = DistrictId::HallowaySquare},

    {.name = "Nickel water tower",
     .pos = {1150.0f, 300.0f},
     .height_m = 34.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::WaterTower,
     .district = DistrictId::NickelHeights},

    {.name = "Nickel stadium bowl",
     .pos = {1450.0f, 700.0f},
     .height_m = 26.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::StadiumBowl,
     .district = DistrictId::NickelHeights},

    {.name = "Strand Ferris wheel",
     .pos = {1620.0f, 1420.0f},
     .height_m = 44.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::FerrisWheel,
     .district = DistrictId::TheStrand},

    {.name = "Camber lighthouse",
     .pos = {700.0f, 2180.0f},
     .height_m = 32.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Lighthouse,
     .district = DistrictId::CamberPoint},

    {.name = "Camber control tower",
     .pos = {200.0f, 2020.0f},
     .height_m = 24.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::ControlTower,
     .district = DistrictId::CamberPoint},

    {.name = "Marrow quarry face",
     .pos = {-1980.0f, 1420.0f},
     .height_m = 30.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::QuarryFace,
     .district = DistrictId::Marrow},

    {.name = "Marrow grain silo",
     .pos = {-1420.0f, 1180.0f},
     .height_m = 21.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::Silo,
     .district = DistrictId::Marrow},

    // The bridge towers stand IN the Kessel Channel, which no district claims.
    // DistrictId::Count is the Meadows, and a landmark is allowed to be out
    // there — most of the countryside's legibility comes from things that are
    // nobody's.
    {.name = "Kessel Bridge south tower",
     .pos = {-150.0f, -1130.0f},
     .height_m = 52.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::BridgeTower,
     .district = DistrictId::Count},

    {.name = "Kessel Bridge north tower",
     .pos = {-150.0f, -1390.0f},
     .height_m = 52.0f,
     .tier = LandmarkTier::District,
     .kind = LandmarkKind::BridgeTower,
     .district = DistrictId::Count},

    // ---- Corner tier: which block am I on --------------------------------
    {.name = "Fishmarket clock",
     .pos = {-980.0f, 20.0f},
     .height_m = 14.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Clock,
     .district = DistrictId::Saltmarsh},

    {.name = "The Halloway steps",
     .pos = {-40.0f, 870.0f},
     .height_m = 6.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Steps,
     .district = DistrictId::HallowaySquare},

    {.name = "Vellum Row cinema neon",
     .pos = {-240.0f, 120.0f},
     .height_m = 12.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Neon,
     .district = DistrictId::VellumRow},

    {.name = "Strand pier head",
     .pos = {1700.0f, 1180.0f},
     .height_m = 10.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Pier,
     .district = DistrictId::TheStrand},

    // Six authored murals. Cheap, fixed forever, and the thing a player
    // actually says out loud when giving directions.
    {.name = "Mural: the diver",
     .pos = {-870.0f, -180.0f},
     .height_m = 9.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::Saltmarsh},

    {.name = "Mural: the net menders",
     .pos = {-1180.0f, 210.0f},
     .height_m = 8.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::Saltmarsh},

    {.name = "Mural: the ledger",
     .pos = {300.0f, -300.0f},
     .height_m = 16.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::VellumRow},

    {.name = "Mural: the long shift",
     .pos = {-700.0f, -1830.0f},
     .height_m = 11.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::KeplerFlats},

    {.name = "Mural: the crane operator",
     .pos = {-1950.0f, -560.0f},
     .height_m = 10.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::OstendDocks},

    {.name = "Mural: the last summer",
     .pos = {1480.0f, 1620.0f},
     .height_m = 12.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Mural,
     .district = DistrictId::TheStrand},

    // Windbreak tree lines: the Meadows' only legibility. Not a structure and
    // not in a district, but they are what tells you which rural road you are
    // on, which is exactly what a landmark is for.
    {.name = "Windbreak: the north line",
     .pos = {-1560.0f, -820.0f},
     .height_m = 13.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Windbreak,
     .district = DistrictId::Count},

    {.name = "Windbreak: the south line",
     .pos = {-620.0f, 1420.0f},
     .height_m = 13.0f,
     .tier = LandmarkTier::Corner,
     .kind = LandmarkKind::Windbreak,
     .district = DistrictId::Count},
};

inline constexpr int kLandmarkCount =
    static_cast<int>(sizeof(kLandmarks) / sizeof(kLandmarks[0]));

// ---------------------------------------------------------------------------
//  Compile-time invariants
// ---------------------------------------------------------------------------

// A landmark that claims a district must actually STAND in it. This is the
// invariant that catches the copy-paste where the fourth gantry crane keeps
// district three's coordinates and ends up half a kilometre out to sea, still
// labelled "Ostend Docks" on a minimap that has no way to know better.
constexpr bool landmarks_stand_in_their_district() {
    for (int i = 0; i < kLandmarkCount; ++i) {
        const Landmark& l = kLandmarks[i];
        if (l.name == nullptr) return false;
        if (l.height_m <= 0.0f) return false;
        if (l.pos.x < -kWorldHalfMetres || l.pos.x > kWorldHalfMetres) return false;
        if (l.pos.z < -kWorldHalfMetres || l.pos.z > kWorldHalfMetres) return false;
        if (l.district == DistrictId::Count) continue;  // out in the Meadows
        const Boundary& b = kDistricts[static_cast<int>(l.district)].boundary;
        if (!b.contains(l.pos.x, l.pos.z)) return false;
    }
    return true;
}
static_assert(landmarks_stand_in_their_district(),
              "a landmark claims a district it does not stand in");

// Two Island-tier landmarks is the floor the whole legibility argument rests
// on. With one, half the island has nothing to navigate by; with none, the
// map is a maze.
constexpr int count_tier(LandmarkTier t) {
    int n = 0;
    for (int i = 0; i < kLandmarkCount; ++i) {
        if (kLandmarks[i].tier == t) ++n;
    }
    return n;
}
static_assert(count_tier(LandmarkTier::Island) >= 2,
              "fewer than two island-tier landmarks: there is nothing to "
              "navigate this island by");
static_assert(count_tier(LandmarkTier::District) >= 8,
              "not enough district-tier landmarks to tell the districts apart");

// WHY THERE IS NO "ISLAND TIER IS TALLER THAN DISTRICT TIER" ASSERTION HERE.
//
// There was one, and it failed the moment it was written, correctly. The
// Kepler flare stack is 46 m of structure and the Kessel Bridge towers are 52
// m, so by structure height the island-tier entry is the shorter one. That is
// not a bug in the table: the flare is island-tier because IT BURNS AT NIGHT,
// and the bridge towers stand at sea level while the flare stands on a plate
// at 9 m and the Ferrone Mast stands on a hill at over a hundred.
//
// Tier is about how far a thing READS, which is ground height plus structure
// height plus, for the flare, whether it is lit. Ground height is a
// measurement of the terrain, not a constant, so this cannot be a compile-time
// invariant without pinning a second copy of the terrain in this file.
// tests/city_map_tests.cpp checks it where the ground is actually available,
// in metres above sea level, which is the number that decides whether you can
// see the thing.

}  // namespace city
}  // namespace apricot

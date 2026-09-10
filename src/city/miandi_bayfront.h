#pragma once

#include <cstddef>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// BF-2 is deliberately independent of the older Pinatty tower inventory.  The
// site is axis aligned because Miandi's first-wave grid is the source of truth
// for this parcel, not the downtown six-degree basis.
inline constexpr StartSite kMiandiBayfrontSite{
    "BF-2 Crown Residences", {7600.0f, 8300.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 2400.0f};

inline constexpr float kMiandiBayfrontPodiumHeightM = 8.5f;
inline constexpr float kMiandiBayfrontRoofHeightM = 116.0f;

inline constexpr BuildingOpening kMiandiBayfrontLobbyDoor{
    "crown residences recessed lobby", OpeningKind::Door, 52.0f, 8.0f,
    0.0f, 3.4f, BuildingFinish::TealDoor, 0, 0, BuildingFinish::Steel, false};

// The front wall is intentionally split around the door.  A glass pane or a
// visual decal is not allowed to stand in for the entrance's collision gap.
inline constexpr BuildingWall kMiandiBayfrontWalls[] = {
    {"crown podium west wall", {-54.0f, -45.0f}, {-54.0f, 45.0f}, 0.0f,
     kMiandiBayfrontPodiumHeightM, 0.5f, BuildingFinish::Concrete},
    {"crown podium east wall", {54.0f, 45.0f}, {54.0f, -45.0f}, 0.0f,
     kMiandiBayfrontPodiumHeightM, 0.5f, BuildingFinish::Concrete},
    {"crown podium north service wall", {-54.0f, -45.0f}, {54.0f, -45.0f},
     0.0f, kMiandiBayfrontPodiumHeightM, 0.5f, BuildingFinish::WarmWall},
    {"crown podium south west frontage", {-54.0f, 45.0f}, {-6.0f, 45.0f},
     0.0f, kMiandiBayfrontPodiumHeightM, 0.5f, BuildingFinish::WarmWall},
    {"crown podium south east frontage", {6.0f, 45.0f}, {54.0f, 45.0f},
     0.0f, kMiandiBayfrontPodiumHeightM, 0.5f, BuildingFinish::WarmWall},
    {"crown podium lobby lintel", {-6.0f, 45.0f}, {6.0f, 45.0f}, 3.4f, 5.1f,
     0.5f, BuildingFinish::Concrete},
};

inline constexpr BuildingRoof kMiandiBayfrontRoofs[] = {
    {"crown podium roof", {0.0f, 0.0f}, 8.2f, 109.0f, 91.0f, 0.0f, 0.35f,
     0.5f, 0.3f, RoofStyle::Flat, RidgeAxis::AlongX, BuildingFinish::Concrete,
     BuildingFinish::Concrete},
};

// Fixtures are kept modest in count so the skyline remains a cheap authored
// shell.  Repeated façade detail is expressed as bands and balcony slabs,
// while the actual glass stays non-solid and the structural cores stay solid.
inline constexpr BuildingPiece kMiandiBayfrontFixtures[] = {
    {"crown parcel paving", {0.0f, 0.0f}, 0.0f, 160.0f, 0.08f, 150.0f,
     BuildingFinish::Asphalt, false},
    {"crown lobby floor", {0.0f, 18.0f}, 0.10f, 100.0f, 0.12f, 52.0f,
     BuildingFinish::Concrete, false},
    // Runs from the recessed lobby to Bayfront Avenue's north sidewalk edge.
    {"crown forecourt walk", {0.0f, 65.5f}, 0.10f, 8.0f, 0.12f, 41.0f,
     BuildingFinish::Concrete, false},
    {"crown forecourt threshold", {0.0f, 47.5f}, 0.10f, 8.0f, 0.12f, 1.8f,
     BuildingFinish::Concrete, false},
    {"crown entrance canopy", {0.0f, 50.0f}, 5.7f, 15.0f, 0.28f, 6.0f,
     BuildingFinish::Steel, false},
    {"crown lobby glazing left", {-25.0f, 45.28f}, 0.8f, 42.0f, 5.9f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown lobby glazing right", {25.0f, 45.28f}, 0.8f, 42.0f, 5.9f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown service apron", {38.0f, -58.0f}, 0.10f, 25.0f, 0.12f, 22.0f,
     BuildingFinish::Concrete, false},
    {"crown service drive", {70.25f, -58.0f}, 0.10f, 39.5f, 0.12f, 9.0f,
     BuildingFinish::Concrete, false},
    {"crown service gate", {54.25f, -18.0f}, 0.2f, 0.3f, 3.0f, 10.0f,
     BuildingFinish::Steel, false, 0.0f, 0.0f, 90.0f},
    {"crown loading bay", {38.0f, -43.0f}, 0.2f, 18.0f, 3.8f, 0.5f,
     BuildingFinish::DarkRoof, true},
    {"crown lower shaft", {0.0f, 0.0f}, 8.5f, 62.0f, 77.0f, 38.0f,
     BuildingFinish::Concrete, true},
    {"crown lower glass north", {0.0f, -19.12f}, 9.0f, 58.0f, 75.0f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown lower glass south", {0.0f, 19.12f}, 9.0f, 58.0f, 75.0f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown lower glass west", {-31.12f, 0.0f}, 9.0f, 0.08f, 75.0f, 34.0f,
     BuildingFinish::Glass, false},
    {"crown lower glass east", {31.12f, 0.0f}, 9.0f, 0.08f, 75.0f, 34.0f,
     BuildingFinish::Glass, false},
    {"crown first shaft setback", {0.0f, 0.0f}, 85.5f, 66.0f, 0.35f, 42.0f,
     BuildingFinish::Concrete, true},
    {"crown upper shaft", {0.0f, 0.0f}, 85.85f, 50.0f, 22.0f, 31.0f,
     BuildingFinish::Concrete, true},
    {"crown upper glass north", {0.0f, -15.62f}, 86.2f, 46.0f, 20.8f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown upper glass south", {0.0f, 15.62f}, 86.2f, 46.0f, 20.8f, 0.08f,
     BuildingFinish::Glass, false},
    {"crown upper glass west", {-25.12f, 0.0f}, 86.2f, 0.08f, 20.8f, 27.0f,
     BuildingFinish::Glass, false},
    {"crown upper glass east", {25.12f, 0.0f}, 86.2f, 0.08f, 20.8f, 27.0f,
     BuildingFinish::Glass, false},
    {"crown second shaft setback", {0.0f, 0.0f}, 107.85f, 54.0f, 0.35f, 35.0f,
     BuildingFinish::Concrete, true},
    {"crown lantern structural core", {0.0f, 0.0f}, 108.2f, 24.0f, 7.8f, 15.0f,
     BuildingFinish::Concrete, true},
    {"crown lantern glass core", {0.0f, 0.0f}, 108.2f, 30.0f, 7.8f, 21.0f,
     BuildingFinish::Glass, false},
    {"crown lantern west fin", {-16.0f, 0.0f}, 108.2f, 1.2f, 7.8f, 24.0f,
     BuildingFinish::Steel, true},
    {"crown lantern east fin", {16.0f, 0.0f}, 108.2f, 1.2f, 7.8f, 24.0f,
     BuildingFinish::Steel, true},
    {"crown lantern cap", {0.0f, 0.0f}, 115.55f, 36.0f, 0.45f, 27.0f,
     BuildingFinish::Steel, false},
    {"crown roof plant", {10.0f, -3.0f}, 108.2f, 8.0f, 5.2f, 6.0f,
     BuildingFinish::DarkRoof, true},
    {"crown HVAC west", {-12.0f, -5.0f}, 108.2f, 4.0f, 2.8f, 3.0f,
     BuildingFinish::Steel, true},
    {"crown HVAC east", {12.0f, -5.0f}, 108.2f, 4.0f, 2.8f, 3.0f,
     BuildingFinish::Steel, true},
};

inline constexpr BuildingPlan kMiandiBayfrontPlan{
    "BF-2 Crown Residences", kMiandiBayfrontWalls,
    std::size(kMiandiBayfrontWalls), kMiandiBayfrontRoofs,
    std::size(kMiandiBayfrontRoofs), kMiandiBayfrontFixtures,
    std::size(kMiandiBayfrontFixtures)};

inline std::vector<BuildingPiece> bake_miandi_bayfront() {
    auto parts = bake_building(kMiandiBayfrontPlan);

    // Balcony/spandrel rhythm is generated from the same baked output as the
    // collision shell. Glass and trim remain presentation-only overlays.
    for (int floor = 0; floor < 22; ++floor) {
        const float y = 12.0f + static_cast<float>(floor) * 3.4f;
        parts.push_back({"crown horizontal balcony band", {0.0f, 0.0f}, y,
                         64.0f, 0.28f, 40.0f, BuildingFinish::White, true});
        if (floor % 2 == 0) {
            parts.push_back({"crown balcony north rail", {0.0f, -20.2f}, y + .3f,
                             52.0f, 1.0f, .16f, BuildingFinish::Steel, false});
            parts.push_back({"crown balcony south rail", {0.0f, 20.2f}, y + .3f,
                             52.0f, 1.0f, .16f, BuildingFinish::Steel, false});
        }
    }
    return parts;
}

}  // namespace apricot::city

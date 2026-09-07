#pragma once

#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"

namespace apricot {
namespace city {

// PS-3 is deliberately axis aligned: Coral Way is the public north edge and
// the south half of the parcel is a working apron. Keeping the apron outside
// the hall also leaves a clean truck approach for the later access integrator.
inline constexpr StartSite kMiandiPortSolSite{
    "Miandi Port Sol Fish Market", {7400.0f, 8700.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 1000.0f};

inline constexpr BuildingOpening kMiandiPortSolFrontOpenings[] = {
    {"market entrance", OpeningKind::Door, 36.0f, 4.5f, 0.0f, 3.4f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"public display window west", OpeningKind::Window, 16.0f, 10.0f, 0.85f,
     2.4f, BuildingFinish::Glass, 3, 1},
    {"public display window east", OpeningKind::Window, 67.0f, 10.0f, 0.85f,
     3.4f, BuildingFinish::Glass, 3, 1},
};

// Three eight-metre openings face the rear apron. The door records make the
// gaps explicit in the plan; their frames remain visible while the leaves are
// omitted, so no hidden collision box can close a loading bay.
inline constexpr BuildingOpening kMiandiPortSolLoadingOpenings[] = {
    {"loading bay west", OpeningKind::Door, 66.0f, 8.0f, 0.0f, 4.2f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
    {"loading bay centre", OpeningKind::Door, 44.0f, 8.0f, 0.0f, 4.2f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
    {"loading bay east", OpeningKind::Door, 22.0f, 8.0f, 0.0f, 4.2f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
};

inline constexpr BuildingWall kMiandiPortSolWalls[] = {
    {"market public frontage", {-58.0f, -30.0f}, {28.0f, -30.0f}, 0.0f,
     6.0f, 0.30f, BuildingFinish::WarmWall, kMiandiPortSolFrontOpenings, 3},
    {"market west wall", {-58.0f, -30.0f}, {-58.0f, 22.0f}, 0.0f, 6.0f,
     0.30f, BuildingFinish::White, nullptr, 0},
    {"market east wall", {28.0f, 22.0f}, {28.0f, -30.0f}, 0.0f, 6.0f,
     0.30f, BuildingFinish::WarmWall, nullptr, 0},
    {"market rear loading wall", {28.0f, 22.0f}, {-58.0f, 22.0f}, 0.0f, 6.0f,
     0.30f, BuildingFinish::Steel, kMiandiPortSolLoadingOpenings, 3},
};

inline constexpr BuildingRoof kMiandiPortSolRoofs[] = {
    {"market stepped roof", {-15.0f, -4.0f}, 6.0f, 88.0f, 53.0f, 0.0f,
     0.35f, 0.45f, 0.9f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::Steel},
};

// Paint and light trim are visible but not collision. Posts, barriers,
// equipment, and the crane legs are solid records from this same bake.
inline constexpr BuildingPiece kMiandiPortSolFixtures[] = {
    {"market floor slab", {-15.0f, -4.0f}, 0.0f, 88.0f, 0.12f, 53.0f,
     BuildingFinish::Concrete, true},
    {"rear apron", {-15.0f, 48.0f}, 0.0f, 100.0f, 0.12f, 48.0f,
     BuildingFinish::Concrete, false},
    {"public entry pad", {-22.0f, -34.0f}, 0.02f, 12.0f, 0.08f, 8.0f,
     BuildingFinish::Concrete, false},
    {"public market walk", {-22.0f, -60.0f}, 0.02f, 4.5f, 0.08f, 60.0f,
     BuildingFinish::Concrete, false},
    {"rear truck lane", {44.5f, -15.0f}, 0.02f, 9.0f, 0.08f, 150.0f,
     BuildingFinish::Concrete, false},
    {"seafood shed north canopy", {-44.0f, 40.0f}, 6.0f, 24.0f, 0.20f,
     14.0f, BuildingFinish::Steel, false},
    {"seafood shed south canopy", {-12.0f, 49.0f}, 5.5f, 22.0f, 0.20f,
     14.0f, BuildingFinish::DarkRoof, false},
    {"seafood shed north counter", {-44.0f, 40.0f}, 0.0f, 18.0f, 1.1f,
     2.0f, BuildingFinish::Concrete, true},
    {"seafood shed south counter", {-12.0f, 49.0f}, 0.0f, 16.0f, 1.1f,
     2.0f, BuildingFinish::Concrete, true},
    {"worker parking paint west", {-43.0f, 38.0f}, 0.14f, 26.0f, 0.025f,
     0.12f, BuildingFinish::Yellow, false},
    {"worker parking paint east", {-27.0f, 38.0f}, 0.14f, 26.0f, 0.025f,
     0.12f, BuildingFinish::Yellow, false},
    {"loading stripe west", {-38.0f, 29.0f}, 0.14f, 8.0f, 0.025f, 0.35f,
     BuildingFinish::Yellow, false},
    {"loading stripe centre", {-16.0f, 29.0f}, 0.14f, 8.0f, 0.025f, 0.35f,
     BuildingFinish::Yellow, false},
    {"loading stripe east", {6.0f, 29.0f}, 0.14f, 8.0f, 0.025f, 0.35f,
     BuildingFinish::Yellow, false},
    {"cold store compressor", {17.0f, 42.0f}, 0.0f, 8.0f, 3.2f, 5.0f,
     BuildingFinish::Steel, true},
    {"cold store tank", {27.0f, 54.0f}, 0.0f, 3.0f, 4.5f, 3.0f,
     BuildingFinish::White, true},
    {"roof vent west", {-39.0f, -7.0f}, 6.4f, 3.0f, 1.2f, 3.0f,
     BuildingFinish::Steel, false},
    {"roof vent east", {9.0f, -7.0f}, 6.4f, 3.0f, 1.2f, 3.0f,
     BuildingFinish::Steel, false},
    {"roof service box", {-2.0f, 10.0f}, 6.0f, 8.0f, 1.5f, 5.0f,
     BuildingFinish::DarkRoof, true},
    {"barrier west", {35.0f, 34.0f}, 0.0f, 2.0f, 1.0f, 0.65f,
     BuildingFinish::Concrete, true},
    {"barrier centre", {35.0f, 43.0f}, 0.0f, 2.0f, 1.0f, 0.65f,
     BuildingFinish::Concrete, true},
    {"barrier east", {35.0f, 52.0f}, 0.0f, 2.0f, 1.0f, 0.65f,
     BuildingFinish::Concrete, true},
    {"gantry west leg", {54.0f, 37.0f}, 0.0f, 1.2f, 14.0f, 1.2f,
     BuildingFinish::Steel, true},
    {"gantry east leg", {68.0f, 37.0f}, 0.0f, 1.2f, 14.0f, 1.2f,
     BuildingFinish::Steel, true},
    {"gantry high beam", {61.0f, 37.0f}, 13.0f, 15.2f, 1.0f, 1.2f,
     BuildingFinish::Steel, false},
    {"gantry trolley", {61.0f, 37.0f}, 11.7f, 2.2f, 1.5f, 2.2f,
     BuildingFinish::RedTrim, false},
};

inline constexpr BuildingPlan kMiandiPortSolPlan{
    "PS-3 fish market", kMiandiPortSolWalls, 4, kMiandiPortSolRoofs, 1,
    kMiandiPortSolFixtures, sizeof(kMiandiPortSolFixtures) /
                                sizeof(kMiandiPortSolFixtures[0]),
    nullptr, 0};

inline std::vector<BuildingPiece> bake_miandi_port_sol() {
    return bake_building(kMiandiPortSolPlan);
}

}  // namespace city
}  // namespace apricot

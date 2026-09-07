#pragma once

#include <cstddef>
#include <iterator>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// CO-1 is deliberately axis aligned.  The block is bounded by Calle Ocho on
// the north, Solana Avenue on the east, and the future CO-1 service lane on
// its south side.  All records below are in site-local metres.
inline constexpr StartSite kMiandiCalleOchoSite{
    "Miandi Calle Ocho CO-1", {7000.0f, 8300.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 950.0f};

inline constexpr BuildingOpening kMiandiCafeOpenings[] = {
    {"Sol Cafe west door", OpeningKind::Door, 8.0f, 2.4f, 0.0f, 3.1f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Sol Cafe east door", OpeningKind::Door, 27.0f, 2.4f, 0.0f, 3.1f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Sol Cafe window one", OpeningKind::Window, 15.0f, 4.0f, 0.85f, 2.25f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::White},
    {"Sol Cafe window two", OpeningKind::Window, 24.0f, 4.0f, 0.85f, 2.25f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::White},
};

inline constexpr BuildingWall kMiandiCafeWalls[] = {
    {"Sol Cafe Calle facade", {-68.0f, -55.0f}, {-38.0f, -55.0f}, 0.0f, 5.4f,
     0.30f, BuildingFinish::WarmWall, kMiandiCafeOpenings,
     std::size(kMiandiCafeOpenings)},
    {"Sol Cafe east wall", {-38.0f, -55.0f}, {-38.0f, -18.0f}, 0.0f, 5.4f,
     0.30f, BuildingFinish::RedTrim},
    {"Sol Cafe rear wall", {-38.0f, -18.0f}, {-68.0f, -18.0f}, 0.0f, 5.4f,
     0.30f, BuildingFinish::Brick},
    {"Sol Cafe west wall", {-68.0f, -18.0f}, {-68.0f, -55.0f}, 0.0f, 5.4f,
     0.30f, BuildingFinish::WarmWall},
};
inline constexpr BuildingRoof kMiandiCafeRoofs[] = {
    {"Sol Cafe stepped parapet", {-53.0f, -36.5f}, 5.4f, 30.0f, 37.0f, 0.0f,
     0.26f, 0.45f, 0.85f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::TealDoor},
};

inline constexpr BuildingOpening kMiandiMercadoOpenings[] = {
    {"Mercado main door", OpeningKind::Door, 7.0f, 2.6f, 0.0f, 3.3f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
    {"Mercado side door", OpeningKind::Door, 30.0f, 2.6f, 0.0f, 3.3f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
    {"Mercado display one", OpeningKind::Window, 16.0f, 5.2f, 0.7f, 2.55f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Yellow},
    {"Mercado display two", OpeningKind::Window, 39.0f, 5.2f, 0.7f, 2.55f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Yellow},
};
inline constexpr BuildingWall kMiandiMercadoWalls[] = {
    {"Mercado Calle facade", {-36.0f, -55.0f}, {8.0f, -55.0f}, 0.0f, 6.2f,
     0.32f, BuildingFinish::Yellow, kMiandiMercadoOpenings,
     std::size(kMiandiMercadoOpenings)},
    {"Mercado east wall", {8.0f, -55.0f}, {8.0f, -14.0f}, 0.0f, 6.2f,
     0.32f, BuildingFinish::WarmWall},
    {"Mercado rear wall", {8.0f, -14.0f}, {-36.0f, -14.0f}, 0.0f, 6.2f,
     0.32f, BuildingFinish::Brick},
    {"Mercado west wall", {-36.0f, -14.0f}, {-36.0f, -55.0f}, 0.0f, 6.2f,
     0.32f, BuildingFinish::Yellow},
};
inline constexpr BuildingRoof kMiandiMercadoRoofs[] = {
    {"Mercado high parapet", {-14.0f, -34.5f}, 6.2f, 44.0f, 41.0f, 0.0f,
     0.30f, 0.40f, 1.35f, RoofStyle::Flat, RidgeAxis::AlongZ,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
};

inline constexpr BuildingOpening kMiandiCigarOpenings[] = {
    {"Cigar workshop door", OpeningKind::Door, 9.0f, 2.5f, 0.0f, 3.0f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Steel, false},
    {"Cigar barred window one", OpeningKind::Window, 19.0f, 4.6f, 0.8f, 2.0f,
     BuildingFinish::Glass, 3, 1, BuildingFinish::Steel},
    {"Cigar barred window two", OpeningKind::Window, 27.0f, 4.6f, 0.8f, 2.0f,
     BuildingFinish::Glass, 3, 1, BuildingFinish::Steel},
};
inline constexpr BuildingWall kMiandiCigarWalls[] = {
    {"Cigar Calle facade", {10.0f, -53.0f}, {43.0f, -53.0f}, 0.0f, 4.8f,
     0.28f, BuildingFinish::RedTrim, kMiandiCigarOpenings,
     std::size(kMiandiCigarOpenings)},
    {"Cigar east wall", {43.0f, -53.0f}, {43.0f, -20.0f}, 0.0f, 4.8f,
     0.28f, BuildingFinish::WarmWall},
    {"Cigar rear wall", {43.0f, -20.0f}, {10.0f, -20.0f}, 0.0f, 4.8f,
     0.28f, BuildingFinish::Brick},
    {"Cigar west wall", {10.0f, -20.0f}, {10.0f, -53.0f}, 0.0f, 4.8f,
     0.28f, BuildingFinish::RedTrim},
};
inline constexpr BuildingRoof kMiandiCigarRoofs[] = {
    {"Cigar workshop roof", {26.5f, -36.5f}, 4.8f, 33.0f, 33.0f, 0.0f,
     0.24f, 0.35f, 0.55f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::WarmWall},
};

inline constexpr BuildingOpening kMiandiMusicOpenings[] = {
    {"Corner music bar front door", OpeningKind::Door, 8.0f, 2.6f, 0.0f, 3.6f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
    {"Corner music bar window", OpeningKind::Window, 13.0f, 2.8f, 0.9f, 2.7f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::RedTrim},
};
inline constexpr BuildingOpening kMiandiMusicSideOpenings[] = {
    {"Corner music bar side door", OpeningKind::Door, 13.0f, 2.6f, 0.0f, 3.6f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
};
inline constexpr BuildingWall kMiandiMusicWalls[] = {
    {"Music bar Calle facade", {45.0f, -53.0f}, {68.0f, -53.0f}, 0.0f, 7.4f,
     0.34f, BuildingFinish::TealDoor, kMiandiMusicOpenings,
     std::size(kMiandiMusicOpenings)},
    {"Music bar corner wall", {68.0f, -53.0f}, {68.0f, -18.0f}, 0.0f, 7.4f,
     0.34f, BuildingFinish::WarmWall, kMiandiMusicSideOpenings,
     std::size(kMiandiMusicSideOpenings)},
    {"Music bar rear wall", {68.0f, -18.0f}, {45.0f, -18.0f}, 0.0f, 7.4f,
     0.34f, BuildingFinish::Brick},
    {"Music bar west wall", {45.0f, -18.0f}, {45.0f, -53.0f}, 0.0f, 7.4f,
     0.34f, BuildingFinish::TealDoor},
};
inline constexpr BuildingRoof kMiandiMusicRoofs[] = {
    {"Music bar crown", {56.5f, -35.5f}, 7.4f, 23.0f, 35.0f, 0.0f,
     0.30f, 0.55f, 1.05f, RoofStyle::Flat, RidgeAxis::AlongZ,
     BuildingFinish::DarkRoof, BuildingFinish::Yellow},
};

inline constexpr BuildingPiece kMiandiCafeFixtures[] = {
    {"Sol Cafe deep awning", {-53.0f, -59.0f}, 4.15f, 29.0f, 0.16f, 3.6f,
     BuildingFinish::TealDoor, false},
    {"Sol Cafe awning fascia", {-53.0f, -60.7f}, 3.65f, 29.0f, 0.65f, 0.18f,
     BuildingFinish::RedTrim, false},
    {"Sol Cafe rooftop HVAC", {-62.0f, -28.0f}, 5.55f, 3.2f, 1.2f, 2.3f,
     BuildingFinish::Steel, true},
    {"Sol Cafe rooftop vent", {-45.0f, -29.0f}, 5.6f, 0.8f, 1.6f, 0.8f,
     BuildingFinish::DarkRoof, false},
};
inline constexpr BuildingPiece kMiandiMercadoFixtures[] = {
    {"Mercado striped awning", {-14.0f, -59.0f}, 4.6f, 42.0f, 0.18f, 4.2f,
     BuildingFinish::Yellow, false},
    {"Mercado awning support", {-36.0f, -57.2f}, 2.8f, 0.16f, 2.8f, 0.16f,
     BuildingFinish::Steel, false},
    {"Mercado awning support", {8.0f, -57.2f}, 2.8f, 0.16f, 2.8f, 0.16f,
     BuildingFinish::Steel, false},
    {"Mercado rooftop condenser", {0.0f, -27.0f}, 6.5f, 4.0f, 1.0f, 2.0f,
     BuildingFinish::Steel, true},
    {"Mercado rooftop vent bank", {-25.0f, -28.0f}, 6.45f, 1.0f, 1.3f, 1.0f,
     BuildingFinish::DarkRoof, false},
};
inline constexpr BuildingPiece kMiandiCigarFixtures[] = {
    {"Cigar workshop deep awning", {26.5f, -56.5f}, 3.7f, 33.0f, 0.15f, 3.5f,
     BuildingFinish::RedTrim, false},
    {"Cigar workshop shutter rail", {18.0f, -53.35f}, 2.4f, 0.2f, 2.5f, 0.2f,
     BuildingFinish::Steel, false},
    {"Cigar workshop roof vent", {35.0f, -27.0f}, 5.1f, 1.0f, 1.5f, 1.0f,
     BuildingFinish::Steel, true},
};
inline constexpr BuildingPiece kMiandiMusicFixtures[] = {
    {"Music bar corner canopy", {67.0f, -55.0f}, 5.9f, 3.8f, 0.18f, 9.0f,
     BuildingFinish::Steel, false},
    {"Music bar rooftop HVAC", {50.0f, -27.0f}, 7.55f, 2.8f, 1.3f, 2.0f,
     BuildingFinish::Steel, true},
    {"Music bar rooftop vent", {63.0f, -31.0f}, 7.55f, 0.9f, 1.7f, 0.9f,
     BuildingFinish::DarkRoof, false},
    {"Calle Ocho corner lantern", {69.5f, -54.5f}, 0.0f, 2.0f, 9.6f, 2.0f,
     BuildingFinish::Yellow, false},
    {"Corner lantern cap", {69.5f, -54.5f}, 9.6f, 2.7f, 0.35f, 2.7f,
     BuildingFinish::RedTrim, false},
};

inline constexpr BuildingPiece kMiandiPocketPlazaFixtures[] = {
    {"Shaded pocket plaza paving", {-7.0f, 7.0f}, 0.0f, 34.0f, 0.12f, 25.0f,
     BuildingFinish::Concrete, false},
    {"Pocket plaza shade canopy", {-7.0f, 7.0f}, 4.4f, 17.0f, 0.18f, 10.0f,
     BuildingFinish::Steel, false},
    {"Pocket plaza shade post", {-15.0f, 7.0f}, 0.0f, 0.22f, 4.4f, 0.22f,
     BuildingFinish::Steel, true},
    {"Pocket plaza shade post", {1.0f, 7.0f}, 0.0f, 0.22f, 4.4f, 0.22f,
     BuildingFinish::Steel, true},
    {"Pocket plaza planter west", {-21.0f, 4.0f}, 0.0f, 2.0f, 0.65f, 7.0f,
     BuildingFinish::WarmWall, true},
    {"Pocket plaza planter east", {7.0f, 4.0f}, 0.0f, 2.0f, 0.65f, 7.0f,
     BuildingFinish::TealDoor, true},
    {"Pocket plaza bench", {-7.0f, 15.0f}, 0.35f, 8.0f, 0.45f, 0.7f,
     BuildingFinish::Steel, true},
    {"Pocket plaza mural wall", {-7.0f, 20.0f}, 0.0f, 20.0f, 2.8f, 0.22f,
     BuildingFinish::RedTrim, true},
    // The north edge is Calle Ocho's south sidewalk edge (world z=8210), not
    // the road centreline. The strip runs continuously to every shop threshold.
    {"Calle Ocho continuous public pavement", {0.0f, -72.5f}, 0.0f, 136.0f,
     0.12f, 35.0f, BuildingFinish::Concrete, false},
    {"Sol Cafe west door connector", {-60.0f, -59.0f}, 0.0f, 2.5f, 0.12f,
     8.0f, BuildingFinish::Concrete, false},
    {"Sol Cafe east door connector", {-41.0f, -59.0f}, 0.0f, 2.5f, 0.12f,
     8.0f, BuildingFinish::Concrete, false},
    {"Mercado main door connector", {-29.0f, -59.0f}, 0.0f, 2.7f, 0.12f,
     8.0f, BuildingFinish::Concrete, false},
    {"Mercado side door connector", {-6.0f, -59.0f}, 0.0f, 2.7f, 0.12f,
     8.0f, BuildingFinish::Concrete, false},
    {"Cigar door connector", {19.0f, -59.0f}, 0.0f, 2.6f, 0.12f, 8.0f,
     BuildingFinish::Concrete, false},
    {"Music bar front door connector", {53.0f, -59.0f}, 0.0f, 2.7f, 0.12f,
     8.0f, BuildingFinish::Concrete, false},
    {"Music bar side door connector", {70.0f, -52.0f}, 0.0f, 4.0f, 0.12f,
     26.0f, BuildingFinish::Concrete, false},
    {"CO-1 rear service lane", {0.0f, 55.0f}, 0.0f, 136.0f, 0.12f, 8.0f,
     BuildingFinish::Asphalt, false},
};

inline constexpr BuildingPlan kMiandiSolCafePlan{
    "Sol Cafe", kMiandiCafeWalls, std::size(kMiandiCafeWalls),
    kMiandiCafeRoofs, std::size(kMiandiCafeRoofs), kMiandiCafeFixtures,
    std::size(kMiandiCafeFixtures)};
inline constexpr BuildingPlan kMiandiMercadoPlan{
    "Mercado Miandi", kMiandiMercadoWalls, std::size(kMiandiMercadoWalls),
    kMiandiMercadoRoofs, std::size(kMiandiMercadoRoofs), kMiandiMercadoFixtures,
    std::size(kMiandiMercadoFixtures)};
inline constexpr BuildingPlan kMiandiCigarWorkshopPlan{
    "Cigar workshop", kMiandiCigarWalls, std::size(kMiandiCigarWalls),
    kMiandiCigarRoofs, std::size(kMiandiCigarRoofs), kMiandiCigarFixtures,
    std::size(kMiandiCigarFixtures)};
inline constexpr BuildingPlan kMiandiCornerMusicBarPlan{
    "Corner music bar", kMiandiMusicWalls, std::size(kMiandiMusicWalls),
    kMiandiMusicRoofs, std::size(kMiandiMusicRoofs), kMiandiMusicFixtures,
    std::size(kMiandiMusicFixtures)};

inline std::vector<BuildingPiece> bake_miandi_calle_ocho() {
    std::vector<BuildingPiece> out;
    const BuildingPlan plans[] = {kMiandiSolCafePlan, kMiandiMercadoPlan,
                                  kMiandiCigarWorkshopPlan,
                                  kMiandiCornerMusicBarPlan};
    for (const BuildingPlan& plan : plans) {
        const auto baked = bake_building(plan);
        out.insert(out.end(), baked.begin(), baked.end());
    }
    out.insert(out.end(), std::begin(kMiandiPocketPlazaFixtures),
               std::end(kMiandiPocketPlazaFixtures));
    return out;
}

}  // namespace apricot::city

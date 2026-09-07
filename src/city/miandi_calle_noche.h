#pragma once

#include <iterator>
#include <vector>

#include "city/start_area.h"
#include "city/miandi_neon_sign.h"
#include "city/miandi_venue_identity.h"

namespace apricot::city {

// N1 occupies the west Bayfront block. Local -Z is north here: the public
// walk reaches Bayfront's arterial sidewalk seam at z=-86 while the dedicated service
// lane reaches Palm Avenue's sidewalk seam at x=-90.
inline constexpr StartSite kMiandiCalleNocheSite{
    "Miandi Calle Noche N1", {7000.0f, 8500.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 950.0f};

inline constexpr BuildingOpening kMiandiSolSocialOpenings[] = {
    {"Club Candela entry", OpeningKind::Door, 11.0f, 2.8f, 0.0f, 3.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Club Candela patio door", OpeningKind::Door, 26.0f, 2.6f, 0.0f, 3.4f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
    {"Club Candela window", OpeningKind::Window, 18.5f, 5.0f, 0.8f, 2.4f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::RedTrim},
};
inline constexpr BuildingOpening kMiandiSolServiceOpenings[] = {
    {"Club Candela service gate", OpeningKind::Door, 25.0f, 6.0f, 0.0f,
     3.4f, BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
};
inline constexpr BuildingWall kMiandiSolSocialWalls[] = {
    {"Club Candela north front", {-62.0f, -42.0f}, {-26.0f, -42.0f},
     0.0f, 6.6f, 0.32f, BuildingFinish::WarmWall, kMiandiSolSocialOpenings,
     std::size(kMiandiSolSocialOpenings)},
    {"Club Candela east wall", {-26.0f, -42.0f}, {-26.0f, -4.0f}, 0.0f,
     6.6f, 0.32f, BuildingFinish::RedTrim},
    {"Club Candela rear wall", {-26.0f, -4.0f}, {-62.0f, -4.0f}, 0.0f,
     6.6f, 0.32f, BuildingFinish::Brick},
    {"Club Candela west service wall", {-62.0f, -4.0f}, {-62.0f, -42.0f},
     0.0f, 6.6f, 0.32f, BuildingFinish::Brick, kMiandiSolServiceOpenings,
     std::size(kMiandiSolServiceOpenings)},
};
inline constexpr BuildingRoof kMiandiSolSocialRoofs[] = {
    {"Club Candela stepped roof", {-44.0f, -23.0f}, 6.6f, 36.0f, 38.0f,
     0.0f, 0.28f, 0.70f, 1.2f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
};

inline constexpr BuildingOpening kMiandiPalmaOpenings[] = {
    {"Tropico Ballroom entry", OpeningKind::Door, 10.0f, 3.0f, 0.0f, 3.8f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Yellow, false},
    {"Tropico Ballroom court door", OpeningKind::Door, 31.0f, 2.8f, 0.0f,
     3.5f, BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Tropico Ballroom glass", OpeningKind::Window, 21.0f, 6.5f, 0.9f, 2.8f,
     BuildingFinish::Glass, 3, 1, BuildingFinish::Yellow},
};
inline constexpr BuildingWall kMiandiPalmaWalls[] = {
    {"Tropico Ballroom north front", {-19.0f, -42.0f}, {24.0f, -42.0f},
     0.0f, 8.2f, 0.34f, BuildingFinish::Yellow, kMiandiPalmaOpenings,
     std::size(kMiandiPalmaOpenings)},
    {"Tropico Ballroom east wall", {24.0f, -42.0f}, {24.0f, 1.0f}, 0.0f,
     8.2f, 0.34f, BuildingFinish::WarmWall},
    {"Tropico Ballroom rear wall", {24.0f, 1.0f}, {-19.0f, 1.0f}, 0.0f,
     8.2f, 0.34f, BuildingFinish::Brick},
    {"Tropico Ballroom west wall", {-19.0f, 1.0f}, {-19.0f, -42.0f}, 0.0f,
     8.2f, 0.34f, BuildingFinish::Yellow},
};
inline constexpr BuildingRoof kMiandiPalmaRoofs[] = {
    {"Tropico Ballroom crown", {2.5f, -20.5f}, 8.2f, 43.0f, 43.0f, 0.0f,
     0.30f, 0.80f, 1.5f, RoofStyle::Flat, RidgeAxis::AlongZ,
     BuildingFinish::DarkRoof, BuildingFinish::Yellow},
};

inline constexpr BuildingOpening kMiandiCafecitoOpenings[] = {
    {"late-night cafecito counter", OpeningKind::Door, 8.0f, 3.6f, 1.0f,
     2.2f, BuildingFinish::Glass, 0, 0, BuildingFinish::RedTrim, false},
    {"late-night cafecito door", OpeningKind::Door, 22.0f, 2.5f, 0.0f, 3.2f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"late-night cafecito window", OpeningKind::Window, 15.0f, 4.8f, 0.9f,
     2.3f, BuildingFinish::Glass, 2, 1, BuildingFinish::RedTrim},
};
inline constexpr BuildingWall kMiandiCafecitoWalls[] = {
    {"late-night cafecito north front", {32.0f, -42.0f}, {66.0f, -42.0f},
     0.0f, 5.4f, 0.30f, BuildingFinish::RedTrim, kMiandiCafecitoOpenings,
     std::size(kMiandiCafecitoOpenings)},
    {"late-night cafecito east wall", {66.0f, -42.0f}, {66.0f, -8.0f}, 0.0f,
     5.4f, 0.30f, BuildingFinish::WarmWall},
    {"late-night cafecito rear wall", {66.0f, -8.0f}, {32.0f, -8.0f}, 0.0f,
     5.4f, 0.30f, BuildingFinish::Brick},
    {"late-night cafecito west wall", {32.0f, -8.0f}, {32.0f, -42.0f}, 0.0f,
     5.4f, 0.30f, BuildingFinish::RedTrim},
};
inline constexpr BuildingRoof kMiandiCafecitoRoofs[] = {
    {"late-night cafecito roof", {49.0f, -25.0f}, 5.4f, 34.0f, 34.0f, 0.0f,
     0.25f, 0.65f, 0.9f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
};

// Paving, trim, murals, and neon remain non-solid.  Every collision-bearing
// item is a fixture in this same deterministic bake, never a World-side box.
inline constexpr BuildingPiece kMiandiCalleNocheFixtures[] = {
    {"Calle Noche north public walk", {0.0f, -65.5f}, 0.0f, 146.0f, 0.12f,
     41.0f, BuildingFinish::Concrete, false},
    {"Calle Noche west service route", {-76.0f, -29.0f}, 0.0f, 28.0f, 0.12f,
     6.0f, BuildingFinish::Concrete, false},
    {"Club Candela entry connector", {-51.0f, -48.0f}, 0.0f, 3.0f, 0.12f,
     12.0f, BuildingFinish::Concrete, false},
    {"Club Candela patio connector", {-36.0f, -48.0f}, 0.0f, 3.0f, 0.12f,
     12.0f, BuildingFinish::Concrete, false},
    {"Tropico Ballroom entry connector", {-9.0f, -48.0f}, 0.0f, 3.2f, 0.12f,
     12.0f, BuildingFinish::Concrete, false},
    {"Tropico Ballroom court connector", {12.0f, -48.0f}, 0.0f, 3.0f, 0.12f,
     12.0f, BuildingFinish::Concrete, false},
    {"late-night cafecito counter connector", {40.0f, -48.0f}, 0.0f, 3.8f,
     0.12f, 12.0f, BuildingFinish::Concrete, false},
    {"late-night cafecito door connector", {54.0f, -48.0f}, 0.0f, 3.0f,
     0.12f, 12.0f, BuildingFinish::Concrete, false},
    {"Club Candela deep awning", {-44.0f, -46.0f}, 4.7f, 35.0f, 0.18f,
     7.5f, BuildingFinish::TealDoor, false},
    {"Club Candela awning fascia", {-44.0f, -49.6f}, 4.1f, 35.0f, 0.7f,
     0.22f, BuildingFinish::RedTrim, false},
    {"Tropico Ballroom deep awning", {2.5f, -46.2f}, 5.8f, 42.0f, 0.20f,
     8.0f, BuildingFinish::Yellow, false},
    {"Tropico Ballroom awning fascia", {2.5f, -50.0f}, 5.1f, 42.0f, 0.8f,
     0.22f, BuildingFinish::RedTrim, false},
    {"late-night cafecito deep awning", {49.0f, -46.0f}, 3.9f, 33.0f, 0.18f,
     7.0f, BuildingFinish::RedTrim, false},
    {"late-night cafecito awning fascia", {49.0f, -49.4f}, 3.3f, 33.0f,
     0.65f, 0.22f, BuildingFinish::Yellow, false},
    {"domino patio paving", {-45.0f, 15.0f}, 0.0f, 26.0f, 0.12f, 20.0f,
     BuildingFinish::Concrete, false},
    {"domino patio canopy", {-45.0f, 15.0f}, 4.6f, 26.0f, 0.20f, 20.0f,
     BuildingFinish::Steel, false},
    {"domino patio post northwest", {-56.0f, 7.0f}, 0.0f, 0.35f, 4.6f,
     0.35f, BuildingFinish::Steel, true},
    {"domino patio post northeast", {-34.0f, 7.0f}, 0.0f, 0.35f, 4.6f,
     0.35f, BuildingFinish::Steel, true},
    {"domino patio post southwest", {-56.0f, 23.0f}, 0.0f, 0.35f, 4.6f,
     0.35f, BuildingFinish::Steel, true},
    {"domino patio post southeast", {-34.0f, 23.0f}, 0.0f, 0.35f, 4.6f,
     0.35f, BuildingFinish::Steel, true},
    {"domino table one", {-50.0f, 12.0f}, 0.0f, 2.2f, 1.0f, 2.2f,
     BuildingFinish::Concrete, true},
    {"domino table two", {-40.0f, 18.0f}, 0.0f, 2.2f, 1.0f, 2.2f,
     BuildingFinish::Concrete, true},
    {"open music court paving", {21.0f, 23.0f}, 0.0f, 54.0f, 0.12f, 42.0f,
     BuildingFinish::Concrete, false},
    {"open music court dance floor", {19.0f, 20.0f}, 0.13f, 28.0f, 0.10f,
     20.0f, BuildingFinish::Yellow, false},
    {"music court stage shell back", {21.0f, 42.0f}, 0.0f, 30.0f, 6.5f,
     1.2f, BuildingFinish::Brick, true},
    {"music court stage shell west", {6.6f, 37.0f}, 0.0f, 1.2f, 5.0f, 11.0f,
     BuildingFinish::Brick, true},
    {"music court stage shell east", {35.4f, 37.0f}, 0.0f, 1.2f, 5.0f, 11.0f,
     BuildingFinish::Brick, true},
    {"music court stage canopy", {21.0f, 37.0f}, 6.5f, 31.0f, 0.20f, 12.0f,
     BuildingFinish::DarkRoof, false},
    {"music court stage deck", {21.0f, 36.0f}, 0.0f, 28.0f, 0.8f, 10.0f,
     BuildingFinish::Concrete, true},
    {"music court speaker west", {8.0f, 31.0f}, 0.8f, 1.2f, 3.2f, 1.2f,
     BuildingFinish::DarkRoof, true},
    {"music court speaker east", {34.0f, 31.0f}, 0.8f, 1.2f, 3.2f, 1.2f,
     BuildingFinish::DarkRoof, true},
    {"geometric mural relief red", {42.0f, 7.5f}, 1.2f, 10.0f, 3.8f, 0.18f,
     BuildingFinish::RedTrim, false},
    {"geometric mural relief gold", {48.0f, 7.25f}, 1.2f, 6.0f, 3.3f, 0.18f,
     BuildingFinish::Yellow, false, 0.0f, 0.0f, 32.0f},
    {"geometric mural relief blue", {54.0f, 7.6f}, 1.2f, 7.0f, 4.0f, 0.18f,
     BuildingFinish::TealDoor, false, 0.0f, 0.0f, -28.0f},
    {"Candela rooftop HVAC", {-51.0f, -17.0f}, 7.8f, 4.5f, 1.8f, 3.0f,
     BuildingFinish::Steel, true},
    {"Candela rooftop fan", {-35.0f, -22.0f}, 7.8f, 2.0f, 1.2f, 2.0f,
     BuildingFinish::Steel, false},
    {"Tropico rooftop HVAC", {-6.0f, -18.0f}, 10.1f, 5.0f, 1.8f, 3.5f,
     BuildingFinish::Steel, true},
    {"Tropico rooftop fan", {13.0f, -20.0f}, 10.1f, 2.0f, 1.2f, 2.0f,
     BuildingFinish::Steel, false},
    {"cafecito rooftop HVAC", {56.0f, -22.0f}, 6.6f, 3.5f, 1.5f, 2.5f,
     BuildingFinish::Steel, true},
    {"cafecito rooftop fan", {42.0f, -25.0f}, 6.5f, 1.8f, 1.1f, 1.8f,
     BuildingFinish::Steel, false},
    {"miandi neon cyan Candela band", {-44.0f, -50.2f}, 5.05f, 20.0f, 0.12f,
     0.22f, BuildingFinish::TealDoor, false},
    {"miandi neon pink Tropico halo", {2.5f, -50.6f}, 8.25f, 30.0f, 0.12f,
     0.22f, BuildingFinish::RedTrim, false},
    {"miandi neon amber cafecito line", {49.0f, -50.0f}, 4.6f, 18.0f,
     0.28f, 0.22f, BuildingFinish::Yellow, false},
    {"miandi neon cyan music court", {21.0f, 43.0f}, 6.8f, 18.0f, 0.24f,
     0.20f, BuildingFinish::TealDoor, false},
    {"miandi neon pink patio trim", {-45.0f, 4.9f}, 4.8f, 18.0f, 0.24f,
     0.20f, BuildingFinish::RedTrim, false},
    {"miandi neon amber stage trim", {21.0f, 30.8f}, 5.0f, 18.0f, 0.24f,
     0.20f, BuildingFinish::Yellow, false},
};

inline constexpr BuildingPlan kMiandiSolSocialClubPlan{
    kCandelaIdentity.name, kMiandiSolSocialWalls, std::size(kMiandiSolSocialWalls),
    kMiandiSolSocialRoofs, std::size(kMiandiSolSocialRoofs), nullptr, 0};
inline constexpr BuildingPlan kMiandiPalmaDanceHallPlan{
    kTropicoIdentity.name, kMiandiPalmaWalls, std::size(kMiandiPalmaWalls),
    kMiandiPalmaRoofs, std::size(kMiandiPalmaRoofs), nullptr, 0};
inline constexpr BuildingPlan kMiandiLateNightCafecitoPlan{
    "late-night cafecito", kMiandiCafecitoWalls, std::size(kMiandiCafecitoWalls),
    kMiandiCafecitoRoofs, std::size(kMiandiCafecitoRoofs), nullptr, 0};

inline void calle_noche_box(std::vector<BuildingPiece>& out, const char* name,
                            Vec2 centre, float bottom, float width,
                            float height, float depth, BuildingFinish finish,
                            bool solid = false, float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

inline std::vector<BuildingPiece> bake_miandi_calle_noche() {
    std::vector<BuildingPiece> out;
    for (const BuildingPlan& plan : {kMiandiSolSocialClubPlan,
                                     kMiandiPalmaDanceHallPlan,
                                     kMiandiLateNightCafecitoPlan}) {
        auto baked = bake_building(plan);
        out.insert(out.end(), baked.begin(), baked.end());
    }
    out.insert(out.end(), std::begin(kMiandiCalleNocheFixtures),
               std::end(kMiandiCalleNocheFixtures));

    // Warm neighborhood-club lettering and a broader old dance-hall marquee.
    // Both sit above the existing awnings, outside the wall/door planes.
    calle_noche_box(out, "Candela name backing", {-44.0f, -49.95f},
                    5.2f, 21.0f, 1.85f, .24f, BuildingFinish::DarkRoof);
    miandi_neon_facade_name(out, "miandi neon amber Candela lettering",
                            kCandelaIdentity.sign, {-44.0f, -50.16f}, 5.43f,
                            1.35f, BuildingFinish::Yellow, 90.0f);
    calle_noche_box(out, "Tropico Ballroom marquee name backing", {2.5f, -50.25f},
                    6.05f, 30.0f, 2.1f, .24f, BuildingFinish::DarkRoof);
    miandi_neon_facade_name(out, "miandi neon amber Tropico lettering",
                            kTropicoIdentity.sign, {2.5f, -50.46f}, 6.36f,
                            1.5f, BuildingFinish::Yellow, 90.0f);

    // The first pass established the big rooms.  These are deliberately small
    // street-reading pieces: they break up the long fronts without putting
    // collision into the Bayfront arrival strip.
    for (float x : {-58.0f, -48.0f, -38.0f, -28.0f})
        calle_noche_box(out, "Candela facade pilaster", {x, -42.22f}, .25f,
                        .55f, 6.0f, .14f, BuildingFinish::RedTrim);
    for (float x : {-16.0f, -7.0f, 2.0f, 11.0f, 20.0f})
        calle_noche_box(out, "Tropico Ballroom facade fin", {x, -42.22f}, .25f,
                        .45f, 7.35f, .14f, BuildingFinish::TealDoor);
    for (float x : {36.0f, 46.0f, 58.0f})
        calle_noche_box(out, "cafecito facade tile pier", {x, -42.20f}, .25f,
                        .55f, 4.65f, .14f, BuildingFinish::Yellow);
    calle_noche_box(out, "Candela blade sign", {-61.1f, -47.2f}, 3.6f,
                    .16f, 3.8f, 5.0f, BuildingFinish::TealDoor);
    calle_noche_box(out, "Tropico Ballroom blade sign", {23.1f, -47.4f}, 4.1f,
                    .16f, 4.6f, 4.4f, BuildingFinish::RedTrim);
    calle_noche_box(out, "cafecito menu lightbox", {65.1f, -47.0f}, 2.8f,
                    .16f, 2.2f, 4.0f, BuildingFinish::Yellow);

    for (const Vec2 chair : {Vec2{-52.8f, 12.0f}, Vec2{-47.2f, 12.0f},
                             Vec2{-42.8f, 18.0f}, Vec2{-37.2f, 18.0f}})
        calle_noche_box(out, "domino patio chair", chair, .12f, 1.1f, .72f,
                        1.1f, BuildingFinish::Steel, true);
    calle_noche_box(out, "domino patio bench west", {-56.0f, 19.5f}, .12f,
                    1.2f, .75f, 5.5f, BuildingFinish::Concrete, true);
    calle_noche_box(out, "domino patio bench east", {-34.0f, 10.5f}, .12f,
                    1.2f, .75f, 5.5f, BuildingFinish::Concrete, true);
    for (float x : {-55.0f, -45.0f, -35.0f})
        calle_noche_box(out, "miandi neon warm patio string", {x, 5.2f}, 4.35f,
                        7.5f, .10f, .10f, BuildingFinish::Yellow);

    for (float x : {5.0f, 13.0f, 29.0f, 37.0f})
        calle_noche_box(out, "music court dance bollard", {x, 12.0f}, .10f,
                        .65f, 1.0f, .65f, BuildingFinish::Steel, true);
    calle_noche_box(out, "music court DJ plinth", {21.0f, 34.0f}, .8f, 5.0f,
                    1.1f, 2.4f, BuildingFinish::DarkRoof, true);
    for (float x : {9.0f, 21.0f, 33.0f})
        calle_noche_box(out, "miandi neon cyan court string", {x, 29.8f}, 5.8f,
                        8.5f, .10f, .10f, BuildingFinish::TealDoor);
    for (float z : {8.0f, 19.0f})
        calle_noche_box(out, "cafecito mosaic planter", {63.0f, z}, .10f,
                        2.5f, .85f, 2.5f, BuildingFinish::RedTrim, true);
    calle_noche_box(out, "cafecito terrazzo mosaic", {49.0f, 7.0f}, .13f,
                    21.0f, .08f, 8.0f, BuildingFinish::White);
    return out;
}

}  // namespace apricot::city

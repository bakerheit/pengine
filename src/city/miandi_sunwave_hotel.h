#pragma once

#include <cstddef>
#include <vector>

#include "city/start_area.h"
#include "city/miandi_neon_sign.h"
#include "city/miandi_venue_identity.h"

namespace apricot::city {

// H2 is world-aligned: Ocean Drive is east (+X), while the separate
// Seabreeze service connection is west (-X).  All dimensions below are local
// to this 160 x 150 m parcel and heights are relative to its 8 m ground.
inline constexpr StartSite kMiandiSunwaveHotelSite{
    kPalmeraIdentity.name, {8000.0f, 8500.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 1200.0f};

inline constexpr BuildingOpening kSunwaveLobbyOpenings[] = {
    {"Palmera recessed lobby west window", OpeningKind::Window, 3.0f, 1.55f,
     1.0f, 2.15f, BuildingFinish::Glass, 1, 1, BuildingFinish::TealDoor},
    {"Palmera recessed lobby door", OpeningKind::Door, 10.0f, 4.8f, 0.0f,
     3.6f, BuildingFinish::Glass, 0, 0, BuildingFinish::TealDoor, false},
    {"Palmera recessed lobby east window", OpeningKind::Window, 17.0f, 1.55f,
     1.0f, 2.15f, BuildingFinish::Glass, 1, 1, BuildingFinish::TealDoor},
};

inline constexpr BuildingOpening kSunwaveClubOpenings[] = {
    {"Palmera cabana club door", OpeningKind::Door, 13.0f, 4.8f, 0.0f, 3.5f,
     BuildingFinish::Glass, 0, 0, BuildingFinish::RedTrim, false},
    {"Palmera cabana club window north", OpeningKind::Window, 4.0f, 2.0f,
     1.1f, 2.0f, BuildingFinish::Glass, 1, 0, BuildingFinish::RedTrim},
    {"Palmera cabana club window south", OpeningKind::Window, 22.0f, 2.0f,
     1.1f, 2.0f, BuildingFinish::Glass, 1, 0, BuildingFinish::RedTrim},
};

inline constexpr BuildingOpening kSunwaveServiceOpenings[] = {
    {"Palmera west service door", OpeningKind::Door, 18.0f, 3.4f, 0.0f, 3.2f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
};

// The east face deliberately steps inward around the lobby.  The three-wall
// recess makes a genuine threshold: the opening is cut from a wall, rather
// than drawn over a collision-filled facade.
inline constexpr BuildingWall kSunwaveHotelWalls[] = {
    {"Palmera hotel west service wall", {-38.0f, 30.0f}, {-38.0f, -48.0f},
     0.0f, 21.5f, 0.34f, BuildingFinish::WarmWall, kSunwaveServiceOpenings,
     std::size(kSunwaveServiceOpenings)},
    {"Palmera hotel south rounded facade", {-38.0f, -48.0f}, {30.0f, -48.0f},
     0.0f, 21.5f, 0.34f, BuildingFinish::WarmWall},
    {"Palmera hotel southeast corner", {30.0f, -48.0f}, {30.0f, -24.0f},
     0.0f, 21.5f, 0.34f, BuildingFinish::WarmWall},
    {"Palmera lobby south reveal", {30.0f, -24.0f}, {20.0f, -24.0f}, 0.0f,
     21.5f, 0.34f, BuildingFinish::TealDoor},
    {"Palmera recessed lobby facade", {20.0f, -24.0f}, {20.0f, -4.0f},
     0.0f, 21.5f, 0.34f, BuildingFinish::WarmWall, kSunwaveLobbyOpenings,
     std::size(kSunwaveLobbyOpenings)},
    {"Palmera lobby north reveal", {20.0f, -4.0f}, {30.0f, -4.0f}, 0.0f,
     21.5f, 0.34f, BuildingFinish::TealDoor},
    {"Palmera hotel northeast corner", {30.0f, -4.0f}, {30.0f, 30.0f},
     0.0f, 21.5f, 0.34f, BuildingFinish::WarmWall},
    {"Palmera hotel north wall", {30.0f, 30.0f}, {-38.0f, 30.0f}, 0.0f,
     21.5f, 0.34f, BuildingFinish::WarmWall},

    {"Palmera cabana club west wall", {-20.0f, 62.0f}, {-20.0f, 36.0f},
     0.0f, 7.2f, 0.30f, BuildingFinish::White},
    {"Palmera cabana club south wall", {-20.0f, 62.0f}, {30.0f, 62.0f},
     0.0f, 7.2f, 0.30f, BuildingFinish::White},
    {"Palmera cabana club east facade", {30.0f, 62.0f}, {30.0f, 36.0f},
     0.0f, 7.2f, 0.30f, BuildingFinish::White, kSunwaveClubOpenings,
     std::size(kSunwaveClubOpenings)},
    {"Palmera cabana club north wall", {30.0f, 36.0f}, {-20.0f, 36.0f},
     0.0f, 7.2f, 0.30f, BuildingFinish::White},
};

inline constexpr BuildingRoof kSunwaveHotelRoofs[] = {
    {"Palmera hotel storm parapet roof", {-4.0f, -9.0f}, 21.5f, 68.0f, 78.0f,
     0.0f, 0.30f, 0.35f, 1.15f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::TealDoor},
    {"Palmera cabana club storm parapet roof", {5.0f, 49.0f}, 7.2f, 50.0f,
     26.0f, 0.0f, 0.25f, 0.30f, 0.65f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
};

inline constexpr BuildingPiece kSunwaveHotelFixtures[] = {
    {"Palmera parcel paving", {0.0f, 0.0f}, 0.02f, 160.0f, 0.10f, 150.0f,
     BuildingFinish::Concrete, false},
    // These are support surfaces, never collision obstacles.  They meet the
    // Ocean Drive's arterial sidewalk edge at local +86 m and Seabreeze's
    // street sidewalk edge at local -90 m without touching either road.
    {"Palmera public route to Ocean Drive", {53.0f, -14.0f}, 0.10f, 66.0f,
     0.12f, 5.0f, BuildingFinish::Concrete, false},
    {"Palmera club route to Ocean Drive", {58.0f, 49.0f}, 0.10f, 56.0f,
     0.12f, 4.4f, BuildingFinish::Concrete, false},
    {"Palmera seven metre Seabreeze service lane", {-64.0f, 12.0f}, 0.10f,
     52.0f, 0.12f, 7.0f, BuildingFinish::Concrete, false},
    {"Palmera terrazzo-like entry slab", {22.5f, -14.0f}, 0.14f, 5.0f, 0.13f,
     6.8f, BuildingFinish::White, false},
    {"Palmera club terrazzo threshold", {31.6f, 49.0f}, 0.14f, 3.0f, 0.13f,
     6.8f, BuildingFinish::White, false},
    {"Palmera pool court paving", {-48.0f, 49.0f}, 0.10f, 28.0f, 0.12f,
     32.0f, BuildingFinish::White, false},
    {"Palmera pool water", {-48.0f, 49.0f}, 0.20f, 17.0f, 0.08f, 18.0f,
     BuildingFinish::PoolWater, false},
    {"Palmera pool north coping", {-48.0f, 58.7f}, 0.22f, 20.0f, 0.12f, 0.7f,
     BuildingFinish::TealDoor, false},
    {"Palmera pool south coping", {-48.0f, 39.3f}, 0.22f, 20.0f, 0.12f, 0.7f,
     BuildingFinish::TealDoor, false},
    {"Palmera pool west coping", {-57.7f, 49.0f}, 0.22f, 0.7f, 0.12f, 20.0f,
     BuildingFinish::TealDoor, false},
    {"Palmera pool east coping", {-38.3f, 49.0f}, 0.22f, 0.7f, 0.12f, 20.0f,
     BuildingFinish::TealDoor, false},
};

inline constexpr BuildingPlan kMiandiSunwaveHotelPlan{
    kPalmeraIdentity.name, kSunwaveHotelWalls, std::size(kSunwaveHotelWalls),
    kSunwaveHotelRoofs, std::size(kSunwaveHotelRoofs),
    kSunwaveHotelFixtures, std::size(kSunwaveHotelFixtures)};

inline void sunwave_box(std::vector<BuildingPiece>& out, const char* name,
                        Vec2 centre, float bottom, float width, float height,
                        float depth, BuildingFinish finish, bool solid = false,
                        float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

inline std::vector<BuildingPiece> bake_miandi_sunwave_hotel() {
    auto parts = bake_building(kMiandiSunwaveHotelPlan);

    // Resort-scale roof sign, seated on the parapet above the east entrance.
    // Pool/cabana geometry and all ground-level access remain unchanged.
    sunwave_box(parts, "Palmera rooftop name backing", {30.4f, -9.0f},
                22.85f, .32f, 2.85f, 25.0f, BuildingFinish::DarkRoof);
    for (float z : {-19.0f, 1.0f})
        sunwave_box(parts, "Palmera rooftop sign bracket", {30.3f, z},
                    21.7f, .3f, 1.3f, .3f, BuildingFinish::Steel);
    miandi_neon_facade_name(parts, "miandi neon warm-white Palmera lettering",
                            kPalmeraIdentity.sign, {30.66f, -9.0f}, 23.13f,
                            2.15f, BuildingFinish::White);

    // Four balcony levels and five broad eyebrows make the hotel read as a
    // compact Tropical Deco stack, not a copied facade or a generic tower.
    for (float y : {4.2f, 7.8f, 11.4f, 15.0f, 18.6f})
        sunwave_box(parts, "Palmera horizontal eyebrow", {30.35f, -26.0f}, y,
                    0.70f, 0.24f, 43.0f, BuildingFinish::TealDoor);
    for (float y : {5.3f, 8.9f, 12.5f, 16.1f}) {
        sunwave_box(parts, "Palmera east balcony rail", {30.62f, -35.0f}, y,
                    0.12f, 1.05f, 21.0f, BuildingFinish::Steel);
        sunwave_box(parts, "Palmera north balcony rail", {30.62f, 13.0f}, y,
                    0.12f, 1.05f, 29.0f, BuildingFinish::Steel);
    }
    for (float y : {4.4f, 8.0f, 11.6f, 15.2f, 18.8f})
        for (float z : {-43.0f, -36.0f, -29.0f, -22.0f, -7.5f, 1.0f,
                        9.5f, 18.0f, 25.5f})
            sunwave_box(parts, "Palmera narrow window rhythm", {30.23f, z}, y,
                        0.10f, 1.85f, 2.05f, BuildingFinish::Glass);

    // The layered center bay is deliberately taller and slightly proud of the
    // recess.  Angled short bands soften both outside corners into readable
    // rounded Deco geometry at street distance.
    sunwave_box(parts, "Palmera stepped center bay lower", {20.42f, -14.0f},
                3.8f, 0.38f, 4.0f, 12.0f, BuildingFinish::RedTrim);
    sunwave_box(parts, "Palmera stepped center bay middle", {20.62f, -14.0f},
                7.8f, 0.42f, 5.0f, 10.0f, BuildingFinish::TealDoor);
    sunwave_box(parts, "Palmera stepped center bay crown", {20.85f, -14.0f},
                12.8f, 0.46f, 8.1f, 7.4f, BuildingFinish::White);
    for (float y : {2.0f, 9.0f, 16.0f}) {
        sunwave_box(parts, "Palmera rounded southeast corner", {29.85f, -47.75f},
                    y, 0.75f, 3.2f, 0.75f, BuildingFinish::White, false, 45.0f);
        sunwave_box(parts, "Palmera rounded northeast corner", {29.85f, 29.75f},
                    y, 0.75f, 3.2f, 0.75f, BuildingFinish::White, false, -45.0f);
    }

    for (float z : {-34.0f, -10.0f, 14.0f})
        sunwave_box(parts, "Palmera storm parapet fin", {3.0f, z}, 22.95f,
                    14.0f, 1.15f, 0.35f, BuildingFinish::TealDoor);
    for (float x : {-24.0f, -8.0f, 9.0f, 21.0f})
        sunwave_box(parts, "Palmera rooftop vent", {x, -30.0f}, 22.95f,
                    2.4f, 1.35f, 2.0f, BuildingFinish::Steel, true);

    for (float z : {40.0f, 58.0f}) {
        sunwave_box(parts, "Palmera cabana roof", {-31.0f, z}, 3.5f, 9.0f,
                    0.18f, 6.0f, BuildingFinish::TealDoor);
        for (float x : {-34.8f, -27.2f})
            sunwave_box(parts, "Palmera cabana post", {x, z}, 0.15f, 0.20f,
                        3.35f, 0.20f, BuildingFinish::Steel, true);
    }

    // Integration keys off this exact lowercase substring.  The tubes are
    // presentation-only so their glow can never close a route or doorway.
    sunwave_box(parts, "miandi neon pink Palmera lobby band", {31.0f, -14.0f},
                4.0f, 0.12f, 0.22f, 14.0f, BuildingFinish::RedTrim);
    sunwave_box(parts, "miandi neon blue Palmera club band", {31.0f, 49.0f},
                4.0f, 0.12f, 0.22f, 14.0f, BuildingFinish::TealDoor);
    sunwave_box(parts, "miandi neon warm-white Palmera roofline", {30.8f, -2.0f},
                20.6f, 0.12f, 0.22f, 52.0f, BuildingFinish::White);

    // Street level needs some life below the tall window stack.  These pieces
    // form a lobby lounge and a resort pool terrace while leaving both Ocean
    // Drive walks and the Seabreeze service lane collision-free.
    for (float y : {3.0f, 6.6f, 10.2f, 13.8f, 17.4f})
        sunwave_box(parts, "Palmera vertical Deco rib", {30.45f, -2.0f}, y,
                    .24f, 2.2f, 59.0f, BuildingFinish::White);
    sunwave_box(parts, "Palmera lobby canopy", {24.5f, -20.0f}, 5.8f, 10.0f,
                .20f, 9.0f, BuildingFinish::TealDoor);
    for (float z : {-23.0f, -17.0f})
        sunwave_box(parts, "Palmera lobby canopy fin", {25.5f, z}, 3.4f, .20f,
                    2.6f, 3.2f, BuildingFinish::RedTrim);
    sunwave_box(parts, "Palmera lobby terrazzo star", {25.5f, -14.0f}, .15f,
                4.5f, .08f, 4.5f, BuildingFinish::Yellow, false, 45.0f);
    for (float z : {39.0f, 58.0f}) {
        sunwave_box(parts, "Palmera pool chaise west", {-63.0f, z}, .14f, 2.1f,
                    .55f, 5.2f, BuildingFinish::White, true);
        sunwave_box(parts, "Palmera pool chaise east", {-33.0f, z}, .14f, 2.1f,
                    .55f, 5.2f, BuildingFinish::White, true);
    }
    for (float x : {-61.0f, -35.0f}) {
        sunwave_box(parts, "Palmera pool umbrella mast", {x, 49.0f}, .14f,
                    .25f, 4.1f, .25f, BuildingFinish::Steel, true);
        sunwave_box(parts, "Palmera pool umbrella shade", {x, 49.0f}, 4.0f,
                    5.6f, .14f, 5.6f, BuildingFinish::TealDoor);
    }
    for (float z : {40.0f, 58.0f})
        sunwave_box(parts, "Palmera cabana curtain", {-26.4f, z}, 1.0f, .10f,
                    2.3f, 4.0f, BuildingFinish::RedTrim);
    sunwave_box(parts, "Palmera cabana bar", {5.0f, 39.2f}, .14f, 14.0f, 1.1f,
                1.5f, BuildingFinish::WarmWall, true);
    for (float x : {-1.0f, 5.0f, 11.0f})
        sunwave_box(parts, "Palmera cabana bar stool", {x, 36.6f}, .14f, 1.0f,
                    .72f, 1.0f, BuildingFinish::Steel, true);
    sunwave_box(parts, "miandi neon aqua Palmera pool ribbon", {-48.0f, 59.2f},
                1.4f, 21.0f, .12f, .12f, BuildingFinish::TealDoor);
    sunwave_box(parts, "miandi neon pink Palmera cabana string", {5.0f, 34.8f},
                5.4f, 31.0f, .10f, .10f, BuildingFinish::RedTrim);
    return parts;
}

}  // namespace apricot::city

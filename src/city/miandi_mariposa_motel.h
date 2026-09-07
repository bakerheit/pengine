#pragma once

#include <cstddef>
#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"

namespace apricot::city {

// H1 sits between Bayfront Avenue on its north edge and Seabreeze Avenue to
// the east.  The lodge is intentionally world aligned so its motor court and
// drive read cleanly against the Miandi grid.
inline constexpr StartSite kMiandiMariposaMotelSite{
    "H1 Mariposa Motor Lodge", {7800.0f, 8500.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 1000.0f};

inline constexpr BuildingOpening kMiandiMariposaLobbyOpenings[] = {
    {"mariposa lobby door", OpeningKind::Door, 28.0f, 4.8f, 0.0f, 3.2f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::Steel, false},
    {"mariposa lobby window west", OpeningKind::Window, 12.0f, 5.0f, 0.8f,
     2.5f, BuildingFinish::Glass, 2, 1, BuildingFinish::RedTrim},
    {"mariposa lobby window east", OpeningKind::Window, 44.0f, 5.0f, 0.8f,
     2.5f, BuildingFinish::Glass, 2, 1, BuildingFinish::RedTrim},
};

// Room doors are permanent openings, rather than painted rectangles, so both
// gallery levels retain the requested rhythm and actual collision gaps.
inline constexpr BuildingOpening kMiandiMariposaWestRoomDoors[] = {
    {"mariposa west room door 01", OpeningKind::Door, 9.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa west room door 02", OpeningKind::Door, 25.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa west room door 03", OpeningKind::Door, 41.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa west room door 04", OpeningKind::Door, 57.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
};
inline constexpr BuildingOpening kMiandiMariposaEastRoomDoors[] = {
    {"mariposa east room door 01", OpeningKind::Door, 9.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa east room door 02", OpeningKind::Door, 25.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa east room door 03", OpeningKind::Door, 41.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"mariposa east room door 04", OpeningKind::Door, 57.0f, 2.4f, 0.0f, 2.5f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
};

inline constexpr BuildingWall kMiandiMariposaWalls[] = {
    {"mariposa lobby north wall", {-29.0f, -48.0f}, {29.0f, -48.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::WarmWall, kMiandiMariposaLobbyOpenings, 3},
    {"mariposa lobby west wall", {-29.0f, -48.0f}, {-29.0f, -27.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
    {"mariposa lobby east wall", {29.0f, -27.0f}, {29.0f, -48.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
    {"mariposa lobby court wall", {29.0f, -27.0f}, {-29.0f, -27.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::WarmWall},
    {"mariposa west outer wall", {-59.0f, -28.0f}, {-59.0f, 51.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::WarmWall},
    {"mariposa west south wall", {-59.0f, 51.0f}, {-35.0f, 51.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
    {"mariposa west court doors", {-35.0f, 51.0f}, {-35.0f, -28.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White, kMiandiMariposaWestRoomDoors, 4},
    {"mariposa west north wall", {-35.0f, -28.0f}, {-59.0f, -28.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
    {"mariposa east outer wall", {59.0f, 51.0f}, {59.0f, -28.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::WarmWall},
    {"mariposa east north wall", {59.0f, -28.0f}, {35.0f, -28.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
    {"mariposa east court doors", {35.0f, -28.0f}, {35.0f, 51.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White, kMiandiMariposaEastRoomDoors, 4},
    {"mariposa east south wall", {35.0f, 51.0f}, {59.0f, 51.0f}, 0.0f,
     7.2f, .32f, BuildingFinish::White},
};

inline constexpr BuildingRoof kMiandiMariposaRoofs[] = {
    {"mariposa lobby deep eave", {0.0f, -37.5f}, 7.2f, 58.0f, 21.0f, 0.0f,
     .35f, 2.4f, .25f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
    {"mariposa west wing deep eave", {-47.0f, 11.5f}, 7.2f, 24.0f, 79.0f,
     0.0f, .35f, 2.4f, .25f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
    {"mariposa east wing deep eave", {47.0f, 11.5f}, 7.2f, 24.0f, 79.0f,
     0.0f, .35f, 2.4f, .25f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::RedTrim},
};

inline void mariposa_box(std::vector<BuildingPiece>& out, const char* name,
                         Vec2 centre, float bottom, float width, float height,
                         float depth, BuildingFinish finish, bool solid = false,
                         float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

inline std::vector<BuildingPiece> bake_miandi_mariposa_motel() {
    std::vector<BuildingPiece> out = bake_building(
        {"H1 Mariposa Motor Lodge", kMiandiMariposaWalls,
         std::size(kMiandiMariposaWalls), kMiandiMariposaRoofs,
         std::size(kMiandiMariposaRoofs)});

    mariposa_box(out, "mariposa motor court", {0.0f, 14.0f}, .02f, 68.0f,
                 .10f, 70.0f, BuildingFinish::Asphalt);
    mariposa_box(out, "mariposa lobby floor", {0.0f, -37.5f}, .02f, 57.0f,
                 .12f, 20.0f, BuildingFinish::Concrete);
    mariposa_box(out, "mariposa west wing floor", {-47.0f, 11.5f}, .02f,
                 23.0f, .12f, 78.0f, BuildingFinish::Concrete);
    mariposa_box(out, "mariposa east wing floor", {47.0f, 11.5f}, .02f,
                 23.0f, .12f, 78.0f, BuildingFinish::Concrete);
    // This walk deliberately starts at the north lot edge and meets the lobby
    // gap; it is a pedestrian-only line separate from the east drive.
    mariposa_box(out, "mariposa Bayfront lobby walk", {-1.0f, -67.5f}, .10f,
                 4.0f, .10f, 37.0f, BuildingFinish::Concrete);
    mariposa_box(out, "mariposa lobby threshold", {-1.0f, -48.6f}, .10f,
                 4.0f, .10f, 1.4f, BuildingFinish::Concrete);
    // The L-shaped seven-metre drive leaves the motor court below the east
    // wing, then reaches Seabreeze's sidewalk seam at x=90. Both pieces are
    // non-solid support and stay well clear of the pool deck.
    mariposa_box(out, "mariposa motor court driveway turn", {30.5f, 52.0f},
                 .02f, 7.0f, .10f, 9.0f, BuildingFinish::Concrete);
    mariposa_box(out, "mariposa Seabreeze driveway", {61.5f, 55.0f}, .02f,
                 57.0f, .10f, 7.0f, BuildingFinish::Concrete);

    for (float z : {-17.0f, 3.0f, 23.0f, 43.0f}) {
        mariposa_box(out, "mariposa west lower gallery", {-33.6f, z}, 2.85f,
                     4.0f, .18f, 15.0f, BuildingFinish::Concrete);
        mariposa_box(out, "mariposa west upper gallery", {-33.6f, z}, 6.65f,
                     4.0f, .18f, 15.0f, BuildingFinish::Concrete);
        mariposa_box(out, "mariposa east lower gallery", {33.6f, z}, 2.85f,
                     4.0f, .18f, 15.0f, BuildingFinish::Concrete);
        mariposa_box(out, "mariposa east upper gallery", {33.6f, z}, 6.65f,
                     4.0f, .18f, 15.0f, BuildingFinish::Concrete);
        for (float x : {-35.2f, -32.0f, 32.0f, 35.2f})
            mariposa_box(out, "mariposa gallery rail", {x, z}, 3.03f, .12f,
                         1.0f, 15.0f, BuildingFinish::Steel);
    }
    for (float z : {-17.0f, 3.0f, 23.0f, 43.0f}) {
        mariposa_box(out, "mariposa west upper rail", {-33.6f, z}, 6.83f,
                     3.8f, .95f, .12f, BuildingFinish::Steel);
        mariposa_box(out, "mariposa east upper rail", {33.6f, z}, 6.83f,
                     3.8f, .95f, .12f, BuildingFinish::Steel);
    }
    // Repeated open breeze blocks screen the court-facing stair cores without
    // becoming an invisible collision fence.
    for (float z : {-12.0f, -4.0f, 4.0f, 12.0f, 20.0f, 28.0f, 36.0f, 44.0f}) {
        mariposa_box(out, "mariposa breeze-block screen", {-30.8f, z}, .8f,
                     .35f, 5.4f, 4.0f, BuildingFinish::White);
        mariposa_box(out, "mariposa breeze-block screen", {30.8f, z}, .8f,
                     .35f, 5.4f, 4.0f, BuildingFinish::White);
    }
    mariposa_box(out, "mariposa west gallery stair", {-33.0f, 48.0f}, .1f,
                 3.0f, 3.0f, 6.0f, BuildingFinish::Concrete, true);
    mariposa_box(out, "mariposa east gallery stair", {33.0f, 42.0f}, .1f,
                 3.0f, 3.0f, 6.0f, BuildingFinish::Concrete, true);

    mariposa_box(out, "mariposa fenced pool deck", {-47.0f, 61.0f}, .02f,
                 36.0f, .12f, 21.0f, BuildingFinish::Concrete);
    mariposa_box(out, "mariposa pool water", {-47.0f, 61.0f}, .14f, 20.0f,
                 .08f, 9.0f, BuildingFinish::PoolWater);
    for (float x : {-64.0f, -47.0f, -30.0f}) {
        mariposa_box(out, "mariposa pool fence north", {x, 71.0f}, .15f,
                     .12f, 1.5f, .12f, BuildingFinish::Steel, true);
        mariposa_box(out, "mariposa pool fence south", {x, 50.5f}, .15f,
                     .12f, 1.5f, .12f, BuildingFinish::Steel, true);
    }
    for (float z : {53.0f, 61.0f, 69.0f}) {
        mariposa_box(out, "mariposa pool fence west", {-65.0f, z}, .15f,
                     .12f, 1.5f, .12f, BuildingFinish::Steel, true);
        mariposa_box(out, "mariposa pool fence east", {-29.0f, z}, .15f,
                     .12f, 1.5f, .12f, BuildingFinish::Steel, true);
    }
    mariposa_box(out, "mariposa roof plant", {-47.0f, 12.0f}, 7.55f, 8.0f,
                 2.1f, 5.0f, BuildingFinish::DarkRoof, true);
    mariposa_box(out, "mariposa roof condenser", {47.0f, 12.0f}, 7.55f, 5.0f,
                 1.5f, 4.0f, BuildingFinish::Steel, true);

    // An abstract roadside pylon: shape and color only, with no copied name.
    mariposa_box(out, "mariposa abstract pylon base", {68.0f, -34.0f}, .0f,
                 4.5f, 2.0f, 3.0f, BuildingFinish::Concrete, true);
    mariposa_box(out, "mariposa abstract pylon spine", {68.0f, -34.0f}, 2.0f,
                 1.1f, 20.5f, 1.1f, BuildingFinish::Steel, true);
    mariposa_box(out, "miandi neon aqua pylon blade", {68.0f, -34.0f}, 8.0f,
                 5.2f, 8.0f, .16f, BuildingFinish::TealDoor);
    mariposa_box(out, "miandi neon coral pylon blade", {68.0f, -34.22f},
                 16.2f, 3.6f, 5.4f, .16f, BuildingFinish::RedTrim);
    mariposa_box(out, "miandi neon warm-white pylon cap", {68.0f, -34.0f},
                 22.5f, 6.0f, .45f, 1.4f, BuildingFinish::White);
    mariposa_box(out, "miandi neon aqua lobby band", {0.0f, -48.25f}, 5.5f,
                 49.0f, .25f, .14f, BuildingFinish::TealDoor);
    mariposa_box(out, "miandi neon coral court band", {0.0f, -26.7f}, 5.8f,
                 46.0f, .25f, .14f, BuildingFinish::RedTrim);
    mariposa_box(out, "miandi neon warm-white pool glow", {-47.0f, 70.7f},
                 1.8f, 27.0f, .18f, .14f, BuildingFinish::White);

    // Roadside-resort detail: a shaded arrival, distinct gallery room markers,
    // and actual pool-deck furniture. None of this reaches either access route.
    mariposa_box(out, "mariposa lobby porte cochere", {0.0f, -52.0f}, 5.3f,
                 24.0f, .20f, 8.0f, BuildingFinish::White);
    for (float x : {-10.0f, 10.0f})
        mariposa_box(out, "mariposa porte cochere column", {x, -52.0f}, .12f,
                     .55f, 5.2f, .55f, BuildingFinish::White, true);
    mariposa_box(out, "mariposa lobby terrazzo medallion", {0.0f, -44.0f},
                 .14f, 20.0f, .07f, 5.5f, BuildingFinish::Yellow);
    for (float z : {-17.0f, 3.0f, 23.0f, 43.0f}) {
        mariposa_box(out, "mariposa west room number fin", {-34.5f, z}, 4.0f,
                     .16f, 1.7f, 3.2f, BuildingFinish::TealDoor);
        mariposa_box(out, "mariposa east room number fin", {34.5f, z}, 4.0f,
                     .16f, 1.7f, 3.2f, BuildingFinish::RedTrim);
        mariposa_box(out, "miandi neon warm gallery line", {-33.6f, z}, 6.35f,
                     3.6f, .12f, .12f, BuildingFinish::Yellow);
        mariposa_box(out, "miandi neon aqua gallery line", {33.6f, z}, 6.35f,
                     3.6f, .12f, .12f, BuildingFinish::TealDoor);
    }
    for (float z : {55.0f, 66.0f}) {
        mariposa_box(out, "mariposa pool chaise west", {-59.0f, z}, .14f, 2.0f,
                     .55f, 5.2f, BuildingFinish::White, true);
        mariposa_box(out, "mariposa pool chaise east", {-35.0f, z}, .14f, 2.0f,
                     .55f, 5.2f, BuildingFinish::White, true);
    }
    for (float x : {-58.0f, -36.0f}) {
        mariposa_box(out, "mariposa pool umbrella mast", {x, 61.0f}, .14f,
                     .25f, 4.1f, .25f, BuildingFinish::Steel, true);
        mariposa_box(out, "mariposa pool umbrella shade", {x, 61.0f}, 4.0f,
                     5.6f, .14f, 5.6f, BuildingFinish::TealDoor);
    }
    mariposa_box(out, "mariposa pool towel cabinet", {-66.0f, 61.0f}, .14f,
                 2.4f, 2.1f, 1.1f, BuildingFinish::WarmWall, true);
    mariposa_box(out, "miandi neon coral pool ribbon", {-47.0f, 50.1f}, 1.55f,
                 29.0f, .12f, .12f, BuildingFinish::RedTrim);
    return out;
}

inline constexpr BuildingPlan kMiandiMariposaMotelPlan{
    "H1 Mariposa Motor Lodge", kMiandiMariposaWalls,
    std::size(kMiandiMariposaWalls), kMiandiMariposaRoofs,
    std::size(kMiandiMariposaRoofs)};

}  // namespace apricot::city

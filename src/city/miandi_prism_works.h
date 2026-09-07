#pragma once

#include <iterator>
#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"
#include "city/miandi_neon_sign.h"
#include "city/miandi_venue_identity.h"

namespace apricot::city {

// N2 keeps its long garment-warehouse footprint low and deliberately leaves
// the north arrival and east-side loading movements outside the old shell.
// All coordinates are parcel-local metres; Bayfront is north (-Z) and Coral
// Way is south (+Z).
inline constexpr StartSite kMiandiPrismWorksSite{
    kMirageIdentity.name, {7200.0f, 8500.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 950.0f};

inline constexpr BuildingOpening kMiandiPrismFrontOpenings[] = {
    {"Mirage gallery door gap", OpeningKind::Door, 20.0f, 3.4f, 0.0f, 4.2f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::White, false},
    {"Mirage gallery clerestory", OpeningKind::Window, 35.0f, 8.0f, 1.2f, 3.8f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Mirage club door gap", OpeningKind::Door, 73.0f, 4.2f, 0.0f, 4.8f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Mirage club display window", OpeningKind::Window, 91.0f, 9.0f, 1.0f, 4.1f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
};

inline constexpr BuildingOpening kMiandiPrismLoadingOpenings[] = {
    {"Mirage loading door gap", OpeningKind::Door, 54.0f, 7.0f, 0.0f, 4.6f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
};

inline constexpr BuildingWall kMiandiPrismWorksWalls[] = {
    {"Club Mirage north warehouse facade", {-58.0f, -42.0f}, {52.0f, -42.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Brick, kMiandiPrismFrontOpenings,
     std::size(kMiandiPrismFrontOpenings)},
    {"Club Mirage east loading facade", {52.0f, -42.0f}, {52.0f, 28.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Steel, kMiandiPrismLoadingOpenings,
     std::size(kMiandiPrismLoadingOpenings)},
    {"Club Mirage rear warehouse facade", {52.0f, 28.0f}, {-58.0f, 28.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Brick},
    {"Club Mirage west warehouse facade", {-58.0f, 28.0f}, {-58.0f, -42.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::WarmWall},
};

inline constexpr BuildingRoof kMiandiPrismWorksRoofs[] = {
    {"Club Mirage broad industrial roof", {-3.0f, -7.0f}, 8.0f, 110.0f, 70.0f,
     0.0f, 0.28f, 0.45f, 0.65f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::DarkRoof, BuildingFinish::Steel},
    {"Club Mirage sawtooth monitor west", {-39.0f, -7.0f}, 8.3f, 18.0f, 18.0f,
     2.3f, 0.18f, 0.12f, 0.0f, RoofStyle::Gable, RidgeAxis::AlongX,
     BuildingFinish::Steel},
    {"Club Mirage sawtooth monitor two", {-13.0f, -7.0f}, 8.3f, 18.0f, 18.0f,
     2.3f, 0.18f, 0.12f, 0.0f, RoofStyle::Gable, RidgeAxis::AlongX,
     BuildingFinish::Steel},
    {"Club Mirage sawtooth monitor three", {13.0f, -7.0f}, 8.3f, 18.0f, 18.0f,
     2.3f, 0.18f, 0.12f, 0.0f, RoofStyle::Gable, RidgeAxis::AlongX,
     BuildingFinish::Steel},
    {"Club Mirage sawtooth monitor east", {39.0f, -7.0f}, 8.3f, 18.0f, 18.0f,
     2.3f, 0.18f, 0.12f, 0.0f, RoofStyle::Gable, RidgeAxis::AlongX,
     BuildingFinish::Steel},
};

// Mural, neon, rails, and paving intentionally stay non-solid. The small
// posts and fence bases are collision geometry from this exact same plan.
inline constexpr BuildingPiece kMiandiPrismWorksFixtures[] = {
    {"Club Mirage warehouse floor", {-3.0f, -7.0f}, 0.0f, 110.0f, 0.12f, 70.0f, BuildingFinish::Concrete, true},
    {"Mirage north public walk to Bayfront", {-28.0f, -64.0f}, 0.0f, 36.0f, 0.12f, 44.0f, BuildingFinish::Concrete, false},
    {"Mirage club public walk to Bayfront", {15.0f, -64.0f}, 0.0f, 6.0f, 0.12f, 44.0f, BuildingFinish::Concrete, false},
    {"Mirage gallery threshold connector", {-38.0f, -44.0f}, 0.0f, 5.0f, 0.12f, 9.0f, BuildingFinish::Concrete, false},
    {"Mirage club threshold connector", {15.0f, -44.0f}, 0.0f, 6.0f, 0.12f, 9.0f, BuildingFinish::Concrete, false},
    {"Mirage south loading route to Coral Way", {66.0f, 48.0f}, 0.0f, 12.0f, 0.12f, 84.0f, BuildingFinish::Asphalt, false},
    {"Mirage loading threshold connector", {56.0f, 12.0f}, 0.0f, 8.0f, 0.12f, 7.0f, BuildingFinish::Concrete, false},
    {"Mirage loading court apron", {41.0f, 40.0f}, 0.0f, 50.0f, 0.12f, 18.0f, BuildingFinish::Concrete, false},
    {"Mirage food yard paving", {-31.0f, 43.0f}, 0.0f, 38.0f, 0.12f, 21.0f, BuildingFinish::Concrete, false},
    {"Mirage food yard canopy", {-31.0f, 43.0f}, 4.8f, 34.0f, 0.20f, 16.0f, BuildingFinish::Steel, false},
    {"Mirage food canopy post west", {-47.0f, 36.0f}, 0.0f, 0.28f, 4.8f, 0.28f, BuildingFinish::Steel, true},
    {"Mirage food canopy post east", {-15.0f, 36.0f}, 0.0f, 0.28f, 4.8f, 0.28f, BuildingFinish::Steel, true},
    {"Mirage food canopy post rear west", {-47.0f, 50.0f}, 0.0f, 0.28f, 4.8f, 0.28f, BuildingFinish::Steel, true},
    {"Mirage food canopy post rear east", {-15.0f, 50.0f}, 0.0f, 0.28f, 4.8f, 0.28f, BuildingFinish::Steel, true},
    {"Mirage food counter", {-31.0f, 50.0f}, 0.0f, 14.0f, 1.05f, 1.5f, BuildingFinish::Concrete, true},
    {"Mirage queue rail west", {-1.5f, -53.0f}, 0.0f, 0.12f, 1.05f, 16.0f, BuildingFinish::Steel, true},
    {"Mirage queue rail east", {9.5f, -53.0f}, 0.0f, 0.12f, 1.05f, 16.0f, BuildingFinish::Steel, true},
    {"Mirage queue rail return", {4.0f, -60.5f}, 0.0f, 11.0f, 1.05f, 0.12f, BuildingFinish::Steel, true},
    {"Mirage queue rail inner", {4.0f, -48.0f}, 0.0f, 7.0f, 1.05f, 0.12f, BuildingFinish::Steel, true},
    {"Mirage queue bollard one", {-1.5f, -60.5f}, 0.0f, 0.5f, 1.15f, 0.5f, BuildingFinish::Steel, true},
    {"Mirage queue bollard two", {9.5f, -60.5f}, 0.0f, 0.5f, 1.15f, 0.5f, BuildingFinish::Steel, true},
    {"Mirage loading fence west", {-55.0f, 57.0f}, 0.0f, 0.16f, 2.4f, 28.0f, BuildingFinish::Steel, true},
    {"Mirage loading fence rear", {-12.5f, 71.0f}, 0.0f, 85.0f, 2.4f, 0.16f, BuildingFinish::Steel, true},
    {"Mirage loading fence east north", {30.0f, 60.0f}, 0.0f, 0.16f, 2.4f, 22.0f, BuildingFinish::Steel, true},
    {"Mirage loading fence east south", {30.0f, 45.0f}, 0.0f, 0.16f, 2.4f, 8.0f, BuildingFinish::Steel, true},
    {"Mirage loading gate frame west", {30.0f, 53.0f}, 0.0f, 0.35f, 2.8f, 0.35f, BuildingFinish::Steel, true},
    {"Mirage loading gate frame east", {52.0f, 53.0f}, 0.0f, 0.35f, 2.8f, 0.35f, BuildingFinish::Steel, true},
    {"Mirage loading gate open header", {41.0f, 53.0f}, 2.45f, 22.0f, 0.18f, 0.18f, BuildingFinish::Steel, false},
    {"Mirage mural violet chevron", {-58.22f, -24.0f}, 2.0f, 0.10f, 4.0f, 12.0f, BuildingFinish::RedTrim, false, 0.0f, 0.0f, -24.0f},
    {"Mirage mural aqua shard", {-58.24f, -10.0f}, 1.1f, 0.10f, 5.2f, 10.0f, BuildingFinish::TealDoor, false, 0.0f, 0.0f, 19.0f},
    {"Mirage mural warm panel", {-58.26f, 4.0f}, 2.5f, 0.10f, 3.4f, 14.0f, BuildingFinish::Yellow, false, 0.0f, 0.0f, -16.0f},
    {"Mirage mural violet bar", {-58.28f, 17.0f}, 4.6f, 0.10f, 1.5f, 11.0f, BuildingFinish::RedTrim, false, 0.0f, 0.0f, 28.0f},
    {"miandi neon violet gallery blade", {-39.0f, -43.0f}, 4.8f, 6.0f, 0.16f, 0.32f, BuildingFinish::RedTrim, false},
    {"miandi neon aqua club band", {15.0f, -43.0f}, 5.4f, 13.0f, 0.16f, 0.32f, BuildingFinish::TealDoor, false},
    {"miandi neon warm-white canopy line", {-31.0f, 34.8f}, 4.9f, 30.0f, 0.16f, 0.28f, BuildingFinish::White, false},
    {"Mirage roof HVAC west", {-47.0f, 10.0f}, 8.3f, 4.0f, 1.3f, 3.0f, BuildingFinish::Steel, true},
    {"Mirage roof HVAC east", {33.0f, 10.0f}, 8.3f, 5.0f, 1.3f, 3.0f, BuildingFinish::Steel, true},
    {"Mirage roof vent bank", {-4.0f, 17.0f}, 8.3f, 18.0f, 1.0f, 2.0f, BuildingFinish::Steel, false},
};

inline constexpr BuildingPlan kMiandiPrismWorksPlan{
    kMirageIdentity.name, kMiandiPrismWorksWalls,
    std::size(kMiandiPrismWorksWalls), kMiandiPrismWorksRoofs,
    std::size(kMiandiPrismWorksRoofs), kMiandiPrismWorksFixtures,
    std::size(kMiandiPrismWorksFixtures)};

inline void prism_works_box(std::vector<BuildingPiece>& out, const char* name,
                            Vec2 centre, float bottom, float width,
                            float height, float depth, BuildingFinish finish,
                            bool solid = false, float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

inline std::vector<BuildingPiece> bake_miandi_prism_works() {
    auto out = bake_building(kMiandiPrismWorksPlan);

    // A nightclub identity on the existing warehouse, mounted above the real
    // north club door. The smoked glazing is tinted locally at presentation.
    prism_works_box(out, "Mirage nightclub name backing", {15.0f, -42.7f},
                    5.7f, 23.0f, 2.1f, .30f, BuildingFinish::DarkRoof);
    miandi_neon_facade_name(out, "miandi neon 80s cyan Mirage lettering",
                            kMirageIdentity.sign, {15.0f, -42.94f}, 5.97f,
                            1.5f, BuildingFinish::TealDoor, 90.0f);

    // Reused warehouses need a human-scale layer under the long roof: brick
    // piers, art plinths, loading protection, and an outdoor food counter.
    // These stay behind the dedicated public/loading routes authored above.
    for (float x : {-54.0f, -46.0f, -30.0f, -22.0f, -6.0f, 2.0f, 28.0f, 44.0f})
        prism_works_box(out, "Mirage front brick pier", {x, -42.22f}, .20f,
                        .55f, 7.15f, .14f, BuildingFinish::WarmWall);
    for (float x : {-39.0f, -13.0f, 13.0f, 39.0f})
        prism_works_box(out, "Mirage monitor glazed end", {x, -41.75f}, 8.4f,
                        10.0f, 1.55f, .08f, BuildingFinish::Glass);
    prism_works_box(out, "Mirage gallery entry plinth", {-45.0f, -34.0f}, .10f,
                    3.6f, 1.5f, 2.4f, BuildingFinish::White, true);
    prism_works_box(out, "Mirage club entry plinth", {24.0f, -32.0f}, .10f,
                    3.6f, 1.5f, 2.4f, BuildingFinish::RedTrim, true);
    for (float x : {-43.0f, -31.0f, -19.0f}) {
        prism_works_box(out, "Mirage food yard stool", {x, 39.0f}, .10f, 1.0f,
                        .72f, 1.0f, BuildingFinish::Steel, true);
        prism_works_box(out, "Mirage food yard pendant", {x, 42.0f}, 4.25f,
                        .35f, .35f, .35f, BuildingFinish::Yellow);
    }
    for (float z : {33.0f, 40.0f, 47.0f})
        prism_works_box(out, "Mirage loading dock bumper", {51.0f, z}, .10f,
                        2.2f, .55f, .65f, BuildingFinish::Steel, true);
    prism_works_box(out, "Mirage loading dock wheel guide west", {39.0f, 48.0f},
                    .12f, .32f, .15f, 12.0f, BuildingFinish::Yellow);
    prism_works_box(out, "Mirage loading dock wheel guide east", {47.0f, 48.0f},
                    .12f, .32f, .15f, 12.0f, BuildingFinish::Yellow);
    for (float z : {-30.0f, -15.0f, 0.0f, 15.0f})
        prism_works_box(out, "Mirage mural frame", {-58.34f, z}, 1.4f, .08f,
                        5.8f, 7.5f, BuildingFinish::White);
    prism_works_box(out, "miandi neon violet monitor edge", {-39.0f, -42.35f},
                    9.5f, 12.0f, .12f, .12f, BuildingFinish::RedTrim);
    prism_works_box(out, "miandi neon aqua monitor edge", {13.0f, -42.35f},
                    9.5f, 12.0f, .12f, .12f, BuildingFinish::TealDoor);
    prism_works_box(out, "miandi neon warm food-yard string", {-31.0f, 34.5f},
                    5.2f, 28.0f, .10f, .10f, BuildingFinish::Yellow);
    return out;
}

}  // namespace apricot::city

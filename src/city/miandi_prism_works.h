#pragma once

#include <iterator>
#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"
#include "city/miandi_neon_sign.h"
#include "city/miandi_venue_identity.h"

namespace apricot::city {

// CLUB MIRAGE — the reference detail pass.
//
// The first pass gave this parcel a warehouse shell, two doors, a loading
// yard and a name in cyan tube. Stood in front of on Bayfront it read as a
// decorated box on a lawn, which is the failure docs/design/
// miandi-vice-city-visual-direction.md already named and did not fix.
//
// This pass is the worked example for docs/design/miandi-club-detail.md.
// Every addition below belongs to one of that document's seven layers, and
// the section comments name which. The layers are the transferable part; the
// numbers here are only how they land on this particular block.
//
// All coordinates are parcel-local metres. The site sits at world
// (7200, 8500) on flat 8 m ground, boxed by four streets:
//
//        Bayfront Avenue  (arterial)   local z = -100, kerb seam z = -86
//        Coral Way        (street)     local z = +100, kerb seam z = +90
//        Solana Avenue    (street)     local x = -100, kerb seam x = -90
//        Mango Avenue     (street)     local x = +100, kerb seam x = +90
//
// Four streets means four public faces. The shell is x -58..52, z -42..28.
inline constexpr StartSite kMiandiPrismWorksSite{
    kMirageIdentity.name, {7200.0f, 8500.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 160.0f, 150.0f, 8.0f, 950.0f};

// Front wall openings run west to east from the wall's a endpoint at x=-58.
// The two steel-panelled sashes are the reuse story in the shell itself: the
// factory had window bays there and the club closed them, so the surviving
// bays read as a choice rather than as an author running out of ideas.
inline constexpr BuildingOpening kMiandiPrismFrontOpenings[] = {
    {"Mirage boarded sash west", OpeningKind::Window, 7.0f, 3.2f, 3.2f, 2.4f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Brick},
    {"Mirage gallery door gap", OpeningKind::Door, 20.0f, 3.4f, 0.0f, 4.2f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::White, false},
    {"Mirage gallery clerestory", OpeningKind::Window, 35.0f, 8.0f, 1.2f, 3.8f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Mirage club door gap", OpeningKind::Door, 73.0f, 4.2f, 0.0f, 4.8f,
     BuildingFinish::TealDoor, 0, 0, BuildingFinish::RedTrim, false},
    {"Mirage club display window", OpeningKind::Window, 91.0f, 9.0f, 1.0f, 4.1f,
     BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Mirage boarded sash east", OpeningKind::Window, 103.0f, 3.2f, 3.2f, 2.4f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Brick},
};

inline constexpr BuildingOpening kMiandiPrismLoadingOpenings[] = {
    {"Mirage loading door gap", OpeningKind::Door, 54.0f, 7.0f, 0.0f, 4.6f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel, false},
};

// The west wall's a endpoint is at z=+28, so center_m counts north. The alley
// door lands at z=+22, clear of the mural field between z=-30 and z=+17.
inline constexpr BuildingOpening kMiandiPrismAlleyOpenings[] = {
    {"Mirage alley fire door", OpeningKind::Door, 6.0f, 1.7f, 0.0f, 2.4f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::RedTrim},
};

// The rear wall's a endpoint is at x=+52, so center_m counts west. The kitchen
// door opens onto the food yard, the crew door onto the loading court: both
// back-of-house rooms that already exist on this plan.
inline constexpr BuildingOpening kMiandiPrismRearOpenings[] = {
    {"Mirage crew door", OpeningKind::Door, 32.0f, 1.6f, 0.0f, 2.3f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel},
    {"Mirage kitchen door", OpeningKind::Door, 82.0f, 1.8f, 0.0f, 2.4f,
     BuildingFinish::Steel, 0, 0, BuildingFinish::Steel},
};

inline constexpr BuildingWall kMiandiPrismWorksWalls[] = {
    {"Club Mirage north warehouse facade", {-58.0f, -42.0f}, {52.0f, -42.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Brick, kMiandiPrismFrontOpenings,
     std::size(kMiandiPrismFrontOpenings)},
    {"Club Mirage east loading facade", {52.0f, -42.0f}, {52.0f, 28.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Brick, kMiandiPrismLoadingOpenings,
     std::size(kMiandiPrismLoadingOpenings)},
    {"Club Mirage rear warehouse facade", {52.0f, 28.0f}, {-58.0f, 28.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::Brick, kMiandiPrismRearOpenings,
     std::size(kMiandiPrismRearOpenings)},
    {"Club Mirage west warehouse facade", {-58.0f, 28.0f}, {-58.0f, -42.0f},
     0.0f, 8.0f, 0.34f, BuildingFinish::WarmWall, kMiandiPrismAlleyOpenings,
     std::size(kMiandiPrismAlleyOpenings)},
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
    // LAYER 1 — ground before building. A city block has no lawn on it. The
    // block's four quarters tile the parcel edge to edge without overlapping
    // each other or the shell, and each one says what its side of the block
    // is for: concrete where people walk, asphalt where trucks turn.
    {"Mirage north forecourt paving", {0.0f, -49.0f}, 0.0f, 180.0f, 0.10f, 12.0f, BuildingFinish::Concrete, false},
    {"Mirage north kerbside lot paving", {0.0f, -70.5f}, 0.0f, 180.0f, 0.10f, 31.0f, BuildingFinish::Asphalt, false},
    {"Mirage west alley paving", {-74.5f, -7.0f}, 0.0f, 31.0f, 0.10f, 72.0f, BuildingFinish::Concrete, false},
    {"Mirage east yard paving", {71.5f, -7.0f}, 0.0f, 37.0f, 0.10f, 72.0f, BuildingFinish::Asphalt, false},
    {"Mirage south yard paving", {0.0f, 59.5f}, 0.0f, 180.0f, 0.10f, 61.0f, BuildingFinish::Asphalt, false},

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
    {"miandi neon aqua club band", {15.0f, -46.12f}, 5.68f, 10.4f, 0.14f, 0.16f, BuildingFinish::TealDoor, false},
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

// The north wall face sits at z = -42.17 and the rear face at z = +28.17; the
// west face at x = -58.17 and the east at x = +52.17. Anything mounted on a
// wall is placed against those planes, not against the wall centre line.
inline constexpr float kMiandiMirageFrontFaceZ = -42.17f;
inline constexpr float kMiandiMirageRearFaceZ = 28.17f;
inline constexpr float kMiandiMirageWestFaceX = -58.17f;
inline constexpr float kMiandiMirageEastFaceX = 52.17f;

// Depth is what makes a wall a wall. Anything shallower than this reads as a
// painted line at street distance and disappears entirely from a car, which
// is what the first pass's 0.14 m piers did. Facade articulation on this
// parcel is authored at or above it and the suite checks that.
inline constexpr float kMiandiMirageReliefDepthM = 0.30f;

inline std::vector<BuildingPiece> bake_miandi_prism_works() {
    auto out = bake_building(kMiandiPrismWorksPlan);
    using F = BuildingFinish;
    const auto box = [&out](const char* name, Vec2 centre, float bottom,
                            float width, float height, float depth, F finish,
                            bool solid = false, float yaw = 0.0f) {
        prism_works_box(out, name, centre, bottom, width, height, depth,
                        finish, solid, yaw);
    };

    // A nightclub identity on the existing warehouse, mounted above the real
    // north club door. The smoked glazing is tinted locally at presentation.
    box("Mirage nightclub name backing", {15.0f, -42.7f}, 5.7f, 23.0f, 2.1f,
        .30f, F::DarkRoof);
    miandi_neon_facade_name(out, "miandi neon 80s cyan Mirage lettering",
                            kMirageIdentity.sign, {15.0f, -42.94f}, 5.97f,
                            1.5f, F::TealDoor, 90.0f);

    // -------------------------------------------------------------------
    // LAYER 1 — the ground realm, continued. The four paving quarters are
    // in the constexpr plan above; these are the things that make a paved
    // lot read as a lot rather than as a grey rectangle.
    // -------------------------------------------------------------------
    for (float x : {-78.0f, -74.6f, -71.2f, -67.8f, -64.4f, -61.0f, -57.6f,
                    41.0f, 44.4f, 47.8f, 51.2f, 54.6f, 58.0f, 61.4f})
        box("Mirage lot bay stripe", {x, -57.6f}, .10f, .14f, .03f, 5.2f,
            F::White);
    for (float x : {-76.3f, -69.5f, -62.7f, 42.7f, 49.5f, 56.3f})
        box("Mirage lot wheel stop", {x, -55.4f}, .10f, 1.7f, .16f, .20f,
            F::Concrete, true);
    for (float x : {-88.0f, -84.0f, -80.0f, -76.0f, -72.0f, -68.0f, -64.0f,
                    -60.0f, -56.0f, -52.0f, 24.0f, 28.0f, 32.0f, 36.0f, 40.0f,
                    44.0f, 48.0f, 52.0f, 56.0f, 60.0f, 64.0f, 68.0f, 72.0f,
                    76.0f, 80.0f, 84.0f, 88.0f})
        box("Mirage lot kerbside bay stripe", {x, -82.4f}, .10f, .14f, .03f,
            5.2f, F::White);
    // A kerb line of bollards keeps the parked cars off the concrete apron
    // and gives the 12 m forecourt band an edge to be a band against.
    for (float x : {-70.0f, -58.0f, -22.0f, -4.0f, 24.0f, 36.0f, 52.0f, 66.0f})
        box("Mirage forecourt bollard", {x, -54.6f}, 0.0f, .24f, .95f, .24f,
            F::Steel, true);

    // -------------------------------------------------------------------
    // LAYER 2 — the four faces get four jobs. North is the front, west is
    // the art alley, east is the truck yard, south is the food yard. Every
    // face gets the same base/shaft/cap grammar so the block reads as one
    // building from any street, and then diverges in what hangs on it.
    // -------------------------------------------------------------------

    // North: base course, in three runs so the two real doors keep their gaps.
    box("Mirage north base course west", {-48.9f, -42.33f}, 0.0f, 18.5f, 1.10f,
        .32f, F::Concrete);
    box("Mirage north base course centre", {-11.7f, -42.33f}, 0.0f, 49.2f,
        1.10f, .32f, F::Concrete);
    box("Mirage north base course east", {34.7f, -42.33f}, 0.0f, 35.1f, 1.10f,
        .32f, F::Concrete);

    // North: the bay rhythm. 0.44 m deep and 1.1 m wide, on the structural
    // lines the factory would have had, stepping around every opening rather
    // than crossing one. The two entrance portals below stand in for the
    // piers that would otherwise fall at x=10 and x=19.5.
    for (float x : {-56.0f, -47.0f, -42.0f, -33.5f, -29.0f, -15.5f, -7.0f,
                    1.5f, 27.5f, 40.5f, 49.5f})
        box("Mirage north brick pilaster", {x, -42.39f}, 0.0f, 1.10f, 7.60f,
            .44f, F::Brick, true);

    // North: a string course at window-head height and a cornice under the
    // existing roof parapet. Two horizontals are what stop eleven verticals
    // from reading as a fence.
    box("Mirage north string course west", {-33.0f, -42.35f}, 5.20f, 50.0f,
        .30f, .38f, F::Concrete);
    box("Mirage north string course east", {26.0f, -42.35f}, 5.20f, 52.0f,
        .30f, .38f, F::Concrete);
    box("Mirage north cornice", {-3.0f, -42.44f}, 7.85f, 110.6f, .45f, .56f,
        F::Concrete);

    // North: a raised brick attic over the entrance bay, with shoulders. The
    // shell is a 110 m long 8 m box; without this the roofline is one flat
    // line and the club has no silhouette from anywhere on Bayfront.
    box("Mirage entrance attic", {15.0f, -42.35f}, 8.28f, 26.0f, 3.40f, .70f,
        F::Brick);
    box("Mirage entrance attic coping", {15.0f, -42.35f}, 11.68f, 27.0f, .35f,
        .86f, F::Concrete);
    for (float x : {-4.0f, 34.0f}) {
        box("Mirage entrance attic shoulder", {x, -42.35f}, 8.28f, 12.0f, 1.70f,
            .62f, F::Brick);
        box("Mirage entrance attic shoulder coping", {x, -42.35f}, 9.98f, 12.8f,
            .30f, .78f, F::Concrete);
    }

    // -------------------------------------------------------------------
    // LAYER 3 — the threshold is a room, not a rectangle. From the kerb the
    // club entrance is now: lot, bay stripes, walk, queue pen under a
    // canopy, rope line, portal, door. Six moves over 30 m instead of one.
    // -------------------------------------------------------------------
    for (float x : {10.9f, 19.1f})
        box("Mirage club portal jamb", {x, -44.0f}, 0.0f, 1.6f, 4.85f, 3.8f,
            F::Brick, true);
    box("Mirage club portal head", {15.0f, -44.0f}, 4.85f, 9.8f, .75f, 3.8f,
        F::Brick);
    box("Mirage club portal fascia", {15.0f, -45.98f}, 4.28f, 10.2f, .62f, .22f,
        F::DarkRoof);
    box("Mirage club threshold mat", {15.0f, -43.2f}, .12f, 5.4f, .04f, 1.6f,
        F::DarkRoof);
    // A door gap with no room behind it shows the daylit warehouse slab
    // straight through the wall. Two baffles a few metres in give both public
    // doors the dark interior a player expects to be looking into.
    box("Mirage club vestibule baffle", {15.0f, -37.6f}, 0.0f, 9.0f, 5.60f,
        .30f, F::DarkRoof, true);
    box("Mirage gallery vestibule baffle", {-38.0f, -37.6f}, 0.0f, 8.0f, 5.00f,
        .30f, F::DarkRoof, true);

    box("Mirage queue canopy", {4.0f, -53.0f}, 3.00f, 12.0f, .18f, 17.0f,
        F::Steel);
    for (float z : {-44.6f, -61.4f})
        box("Mirage queue canopy fascia", {4.0f, z}, 2.62f, 12.0f, .56f, .20f,
            F::DarkRoof);
    for (float x : {-2.1f, 10.1f})
        box("Mirage queue canopy edge beam", {x, -53.0f}, 2.62f, .20f, .56f,
            17.0f, F::DarkRoof);
    for (const Vec2 post : {Vec2{-1.7f, -45.4f}, Vec2{9.7f, -45.4f},
                            Vec2{-1.7f, -60.6f}, Vec2{9.7f, -60.6f}})
        box("Mirage queue canopy post", post, 0.0f, .20f, 3.00f, .20f, F::Steel,
            true);
    box("Mirage door podium", {20.9f, -46.4f}, 0.0f, 1.10f, 1.15f, .70f,
        F::DarkRoof, true);
    for (float x : {11.0f, 19.0f})
        box("Mirage rope stanchion", {x, -46.6f}, 0.0f, .16f, 1.00f, .16f,
            F::Steel, true);
    box("Mirage rope span", {15.0f, -46.6f}, .88f, 8.0f, .08f, .08f, F::RedTrim);
    box("Mirage queue board", {-3.6f, -57.0f}, 0.0f, .90f, 1.20f, .70f,
        F::DarkRoof, true, 18.0f);
    box("Mirage queue butt bin", {11.4f, -47.4f}, 0.0f, .40f, 1.00f, .40f,
        F::Steel, true);

    // The gallery keeps the factory's own threshold: a loading dock that was
    // never demolished, now a terrace beside its door. It sits west of the
    // door so the walk to Bayfront stays a clear 3 m gap.
    box("Mirage gallery dock terrace", {-48.5f, -45.6f}, 0.0f, 14.0f, .85f,
        6.2f, F::Concrete, true);
    box("Mirage gallery dock step upper", {-48.5f, -49.4f}, 0.0f, 14.0f, .57f,
        .80f, F::Concrete, true);
    box("Mirage gallery dock step lower", {-48.5f, -50.2f}, 0.0f, 14.0f, .28f,
        .80f, F::Concrete, true);
    box("Mirage gallery dock nosing", {-48.5f, -48.62f}, .63f, 14.2f, .22f,
        .22f, F::Steel);
    box("Mirage gallery dock rail", {-55.3f, -45.6f}, .85f, .10f, 1.05f, 6.2f,
        F::Steel);
    for (const Vec2 post : {Vec2{-55.3f, -48.5f}, Vec2{-55.3f, -42.9f}})
        box("Mirage gallery dock rail post", post, .85f, .12f, 1.05f, .12f,
            F::Steel, true);
    box("Mirage gallery canopy", {-38.0f, -46.2f}, 4.30f, 9.0f, .20f, 5.6f,
        F::Steel);
    for (const Vec2 post : {Vec2{-42.2f, -48.6f}, Vec2{-33.8f, -48.6f}})
        box("Mirage gallery canopy post", post, 0.0f, .22f, 4.30f, .22f,
            F::Steel, true);

    // -------------------------------------------------------------------
    // LAYER 4 — the west flank as a real alley. Solana Avenue used to look
    // at 70 m of blank wall over 30 m of lawn. The mural field stays flat
    // on purpose: piers and a mural compete for the same wall, so this face
    // spends its depth on the base, the cornice and the servicing instead.
    // -------------------------------------------------------------------
    box("Mirage west base course", {-58.50f, -7.0f}, 0.0f, .34f, 1.10f, 70.0f,
        F::Concrete);
    box("Mirage west cornice", {-58.53f, -7.0f}, 7.85f, .56f, .45f, 70.6f,
        F::Concrete);
    for (float z : {-38.0f, -20.0f, 1.0f, 24.0f})
        box("Mirage west downpipe", {-58.42f, z}, 0.0f, .24f, 7.90f, .24f,
            F::Steel, true);
    box("Mirage west roof ladder", {-58.45f, 26.4f}, 0.0f, .60f, 8.70f, .12f,
        F::Steel, true);
    box("Mirage west roof ladder cage", {-58.92f, 26.4f}, 2.40f, .10f, 6.20f,
        .84f, F::Steel);
    for (float z : {-36.0f, -30.0f, -8.0f, 0.0f, 20.0f, 26.0f}) {
        box("Mirage alley planter", {-70.0f, z}, 0.0f, 2.20f, .70f, 2.20f,
            F::Concrete, true);
        box("Mirage alley planter soil", {-70.0f, z}, .70f, 1.80f, .10f, 1.80f,
            F::DarkRoof);
    }
    for (float z : {18.0f, 19.4f})
        box("Mirage alley crate stack", {-61.0f, z}, 0.0f, 1.10f, 1.00f, 1.10f,
            F::WarmWall, true);
    for (float z : {-20.0f, -17.0f, -14.0f})
        box("Mirage alley bike rack", {-64.0f, z}, 0.0f, .10f, .85f, 1.60f,
            F::Steel, true);
    box("Mirage alley bench", {-66.0f, 9.0f}, 0.0f, .55f, .45f, 4.00f,
        F::Concrete, true);
    for (float z : {-36.0f, -22.0f, -8.0f, 6.0f, 20.0f})
        box("Mirage alley bollard", {-78.0f, z}, 0.0f, .24f, .95f, .24f,
            F::Steel, true);
    for (float z : {-28.0f, 0.0f, 26.0f}) {
        box("Mirage alley light column", {-76.0f, z}, 0.0f, .26f, 5.20f, .26f,
            F::Steel, true);
        box("Mirage alley light head", {-75.5f, z}, 5.20f, 1.20f, .26f, .40f,
            F::Yellow);
    }

    // -------------------------------------------------------------------
    // LAYER 5 — the east flank tells the truth about servicing. A club this
    // size runs on deliveries, bins and a transformer; hiding all of that
    // is what makes an authored building feel like a facade on a film lot.
    // Everything here stays north of the Coral Way loading run at z>=6.
    // -------------------------------------------------------------------
    box("Mirage east base course", {52.50f, -7.0f}, 0.0f, .34f, 1.10f, 70.0f,
        F::Concrete);
    box("Mirage east cornice", {52.53f, -7.0f}, 7.85f, .56f, .45f, 70.6f,
        F::Concrete);
    for (float z : {-34.0f, -12.0f, 22.0f})
        box("Mirage east downpipe", {52.42f, z}, 0.0f, .24f, 7.90f, .24f,
            F::Steel, true);
    box("Mirage east dock canopy", {55.6f, 12.0f}, 5.20f, 7.0f, .22f, 10.0f,
        F::Steel);
    for (const Vec2 post : {Vec2{58.6f, 7.4f}, Vec2{58.6f, 16.6f}})
        box("Mirage east dock canopy post", post, 0.0f, .24f, 5.20f, .24f,
            F::Steel, true);
    box("Mirage east roll-up header", {52.40f, 12.0f}, 4.62f, .34f, .34f, 7.8f,
        F::Steel);
    for (float z : {8.3f, 15.7f})
        box("Mirage east roll-up guide", {52.40f, z}, 0.0f, .34f, 4.62f, .22f,
            F::Steel);
    for (float z : {-22.0f, -18.6f})
        box("Mirage east dumpster", {61.6f, z}, 0.0f, 2.20f, 1.35f, 1.60f,
            F::DarkRoof, true);
    box("Mirage east bin corral rear", {61.6f, -15.6f}, 0.0f, 8.00f, 1.90f,
        .25f, F::Concrete, true);
    for (float x : {57.7f, 65.5f})
        box("Mirage east bin corral return", {x, -19.6f}, 0.0f, .25f, 1.90f,
            8.00f, F::Concrete, true);
    box("Mirage east transformer housing", {58.5f, -34.0f}, 0.0f, 3.20f, 2.40f,
        2.20f, F::Steel, true);
    box("Mirage east transformer fence", {58.5f, -30.6f}, 0.0f, 4.40f, 2.20f,
        .10f, F::Steel);
    for (const Vec2 post : {Vec2{56.3f, -30.6f}, Vec2{60.7f, -30.6f}})
        box("Mirage east transformer fence post", post, 0.0f, .14f, 2.30f, .14f,
            F::Steel, true);
    for (float z : {-6.0f, -2.4f})
        box("Mirage east pallet stack", {66.0f, z}, 0.0f, 1.20f, .90f, 1.10f,
            F::WarmWall, true);
    box("Mirage east gas bottle rack", {55.0f, -8.0f}, 0.0f, 1.60f, 1.50f, .70f,
        F::Steel, true);
    box("Mirage east yard fence base", {79.0f, -7.0f}, 0.0f, .22f, 1.05f, 70.0f,
        F::Concrete, true);
    box("Mirage east yard fence rail", {79.0f, -7.0f}, 2.05f, .10f, .14f, 70.0f,
        F::Steel);
    for (float z : {-37.0f, -25.0f, -13.0f, -1.0f, 11.0f, 23.0f})
        box("Mirage east yard fence post", {79.0f, z}, 0.0f, .18f, 2.20f, .18f,
            F::Steel, true);
    for (float z : {4.0f, 8.0f, 16.0f, 20.0f})
        box("Mirage east yard hatch marking", {62.0f, z}, .10f, 20.0f, .03f,
            .18f, F::Yellow);

    // -------------------------------------------------------------------
    // LAYER 5, continued — the south back. Two new real doors open onto two
    // rooms this plan already had: the food yard and the loading court.
    // -------------------------------------------------------------------
    box("Mirage south base course", {-3.0f, 28.33f}, 0.0f, 110.5f, 1.10f, .32f,
        F::Concrete);
    box("Mirage south cornice", {-3.0f, 28.44f}, 7.85f, 110.6f, .45f, .56f,
        F::Concrete);
    box("Mirage south kitchen extract", {-30.0f, 29.5f}, 3.20f, 1.60f, 3.00f,
        1.40f, F::Steel);
    box("Mirage south kitchen extract cowl", {-30.0f, 29.5f}, 6.20f, 2.00f,
        .60f, 1.80f, F::Steel);
    for (float x : {-25.0f, -23.8f, -24.4f})
        box("Mirage south keg", {x, 29.6f}, 0.0f, .62f, .90f, .62f, F::Steel,
            true);
    box("Mirage south crate stack", {-20.0f, 29.9f}, 0.0f, 1.20f, 1.20f, 1.00f,
        F::WarmWall, true);
    for (float x : {-44.0f, 12.0f, 44.0f})
        box("Mirage south downpipe", {x, 28.42f}, 0.0f, .24f, 7.90f, .24f,
            F::Steel, true);

    // -------------------------------------------------------------------
    // LAYER 6 — the roof is the fifth face and the only one the towers see.
    // The sign gantry is the reason this club is findable from Biscayne.
    // -------------------------------------------------------------------
    box("Mirage roof duct run west", {-25.0f, 10.0f}, 8.50f, 44.0f, .90f, .90f,
        F::Steel);
    box("Mirage roof duct run east", {14.0f, 10.0f}, 8.50f, 38.0f, .90f, .90f,
        F::Steel);
    box("Mirage roof condenser new", {5.0f, 22.0f}, 8.30f, 2.40f, 1.10f, 2.40f,
        F::White, true);
    for (const Vec2 cowl : {Vec2{-45.0f, -22.0f}, Vec2{-10.0f, -22.0f},
                            Vec2{25.0f, -22.0f}})
        box("Mirage roof exhaust cowl", cowl, 8.30f, .90f, 1.30f, .90f,
            F::Steel);
    box("Mirage roof hatch", {-54.0f, 24.0f}, 8.28f, 1.40f, .50f, 1.40f,
        F::DarkRoof);
    for (const Vec2 hopper : {Vec2{-57.0f, -40.0f}, Vec2{-57.0f, 26.0f},
                              Vec2{51.0f, -40.0f}, Vec2{51.0f, 26.0f}})
        box("Mirage roof drain hopper", hopper, 8.28f, .70f, .45f, .70f,
            F::Steel);
    for (float x : {6.0f, 24.0f})
        box("Mirage roof sign gantry post", {x, -38.0f}, 8.28f, .35f, 7.00f,
            .35f, F::Steel, true);
    box("Mirage roof sign gantry rail", {15.0f, -38.0f}, 15.03f, 19.0f, .30f,
        .30f, F::Steel);
    box("Mirage roof sign panel", {15.0f, -38.2f}, 11.70f, 16.0f, 3.40f, .35f,
        F::DarkRoof);

    // -------------------------------------------------------------------
    // LAYER 7 — time on the building. The club took a working factory over;
    // the removed sign's frame, its unfaded rectangle of paint and three
    // patched brick panels are what says so without any weathering texture.
    // -------------------------------------------------------------------
    box("Mirage west painted-out sign panel", {-58.30f, 21.0f}, 3.00f, .06f,
        2.60f, 11.0f, F::WarmWall);
    box("Mirage west removed sign frame head", {-58.38f, 21.0f}, 5.60f, .10f,
        .14f, 11.4f, F::Steel);
    box("Mirage west removed sign frame sill", {-58.38f, 21.0f}, 2.86f, .10f,
        .14f, 11.4f, F::Steel);
    for (float z : {15.3f, 26.7f})
        box("Mirage west removed sign frame post", {-58.38f, z}, 2.86f, .10f,
            2.88f, .14f, F::Steel);
    box("Mirage north patched brick panel", {-21.0f, -42.24f}, 5.60f, 6.00f,
        2.00f, .08f, F::WarmWall);
    box("Mirage north patched brick panel low", {44.0f, -42.24f}, 1.10f, 4.40f,
        2.60f, .08f, F::WarmWall);
    box("Mirage south patched brick panel", {8.0f, 28.24f}, 2.20f, 7.00f, 3.00f,
        .08f, F::WarmWall);

    // Reused warehouses need a human-scale layer under the long roof: art
    // plinths at both doors, a food counter with stools, and dock protection.
    box("Mirage gallery entry plinth", {-45.0f, -34.0f}, .10f, 3.6f, 1.5f, 2.4f,
        F::White, true);
    box("Mirage club entry plinth", {24.0f, -32.0f}, .10f, 3.6f, 1.5f, 2.4f,
        F::RedTrim, true);
    for (float x : {-39.0f, -13.0f, 13.0f, 39.0f})
        box("Mirage monitor glazed end", {x, -41.75f}, 8.4f, 10.0f, 1.55f, .08f,
            F::Glass);
    for (float x : {-43.0f, -31.0f, -19.0f}) {
        box("Mirage food yard stool", {x, 39.0f}, .10f, 1.0f, .72f, 1.0f,
            F::Steel, true);
        box("Mirage food yard pendant", {x, 42.0f}, 4.25f, .35f, .35f, .35f,
            F::Yellow);
    }
    for (float z : {33.0f, 40.0f, 47.0f})
        box("Mirage loading dock bumper", {51.0f, z}, .10f, 2.2f, .55f, .65f,
            F::Steel, true);
    box("Mirage loading dock wheel guide west", {39.0f, 48.0f}, .12f, .32f, .15f,
        12.0f, F::Yellow);
    box("Mirage loading dock wheel guide east", {47.0f, 48.0f}, .12f, .32f, .15f,
        12.0f, F::Yellow);
    for (float z : {-30.0f, -15.0f, 0.0f, 15.0f})
        box("Mirage mural frame", {-58.34f, z}, 1.4f, .08f, 5.8f, 7.5f,
            F::White);

    // -------------------------------------------------------------------
    // Neon last, so it follows shapes that already exist. Tubes never carry
    // collision and never stand in for the authored venue lights.
    // -------------------------------------------------------------------
    box("miandi neon violet monitor edge", {-39.0f, -42.35f}, 9.5f, 12.0f, .12f,
        .12f, F::RedTrim);
    box("miandi neon aqua monitor edge", {13.0f, -42.35f}, 9.5f, 12.0f, .12f,
        .12f, F::TealDoor);
    box("miandi neon warm-white food-yard string", {-31.0f, 34.5f}, 5.2f, 28.0f,
        .10f, .10f, F::Yellow);
    box("miandi neon aqua portal edge", {15.0f, -46.06f}, 4.20f, 10.0f, .10f,
        .10f, F::TealDoor);
    for (float z : {-24.0f, 0.0f, 24.0f}) {
        box("miandi neon warm-white alley string", {-67.1f, z}, 4.95f, 17.8f,
            .10f, .10f, F::White);
        for (float x : {-74.5f, -70.8f, -67.1f, -63.4f, -59.7f})
            box("miandi neon warm-white alley bulb", {x, z}, 4.72f, .26f, .26f,
                .26f, F::White);
    }
    // The roof mark is a prism outline: a base and two legs, no lettering. A
    // shape reads from Biscayne Boulevard where a second copy of the name
    // would not, and it does not cost seventy glyph strokes to draw.
    box("miandi neon violet roof mark base", {15.0f, -38.42f}, 12.30f, 7.80f,
        .16f, .16f, F::RedTrim);
    for (float side : {-1.0f, 1.0f})
        out.push_back({"miandi neon violet roof mark leg",
                       {15.0f + side * 1.95f, -38.42f}, 11.51f, .16f, 5.37f,
                       .16f, F::RedTrim, false, 0.0f, 0.0f, side * 45.0f});
    return out;
}

}  // namespace apricot::city

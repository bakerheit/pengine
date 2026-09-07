#pragma once

#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <vector>

#include "city/start_area.h"
#include "city/miandi_neon_sign.h"
#include "city/miandi_venue_identity.h"

namespace apricot::city {

// OD-1 is deliberately authored in world-aligned site space.  Ocean Drive is
// east of the hotels (+X); Seabreeze service access is west (-X).
inline constexpr StartSite kMiandiOceanDriveSite{
    "OD-1 Ocean Drive hotels", {8000.0f, 8300.0f}, 1.0f, 0.0f,
    {3.0f, 0.0f}, 166.0f, 150.0f, 8.0f, 1100.0f};

// Both hotels form one street wall, with an 8 m terrace to the public sidewalk.
// Keep the west parcel edge unchanged; the east edge now meets that sidewalk.
inline constexpr float kMiandiOceanHotelFrontX = 78.0f;
inline constexpr float kMiandiOceanSidewalkX = 86.0f;

inline constexpr StartSite kMiandiNorthPromenadeSite{
    "Ocean Drive north promenade", {8222.0f, 8300.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 216.0f, 150.0f, 8.0f, 900.0f};

inline constexpr BuildingOpening kCoralCrownFrontOpenings[] = {
    {"Bellmar lobby door", OpeningKind::Door, 25.0f, 3.6f, 0.0f,
     3.0f, BuildingFinish::Glass, 0, 0, BuildingFinish::TealDoor, false},
    {"Bellmar narrow window 1", OpeningKind::Window, 8.0f, 2.2f, 3.0f,
     2.0f, BuildingFinish::Glass, 1, 2, BuildingFinish::RedTrim},
    {"Bellmar narrow window 2", OpeningKind::Window, 16.0f, 2.2f, 3.0f,
     2.0f, BuildingFinish::Glass, 1, 2, BuildingFinish::RedTrim},
    {"Bellmar narrow window 3", OpeningKind::Window, 34.0f, 2.2f, 3.0f,
     2.0f, BuildingFinish::Glass, 1, 2, BuildingFinish::RedTrim},
    {"Bellmar narrow window 4", OpeningKind::Window, 42.0f, 2.2f, 3.0f,
     2.0f, BuildingFinish::Glass, 1, 2, BuildingFinish::RedTrim},
};

inline constexpr BuildingOpening kBlueHeronFrontOpenings[] = {
    {"Maravelle lobby door", OpeningKind::Door, 24.0f, 3.4f, 0.0f,
     2.9f, BuildingFinish::Glass, 0, 0, BuildingFinish::Steel, false},
    {"Maravelle narrow window 1", OpeningKind::Window, 7.0f, 2.0f, 2.8f,
     2.1f, BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Maravelle narrow window 2", OpeningKind::Window, 14.0f, 2.0f, 2.8f,
     2.1f, BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Maravelle narrow window 3", OpeningKind::Window, 30.0f, 2.0f, 2.8f,
     2.1f, BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
    {"Maravelle narrow window 4", OpeningKind::Window, 37.0f, 2.0f, 2.8f,
     2.1f, BuildingFinish::Glass, 2, 1, BuildingFinish::Steel},
};

inline constexpr BuildingWall kCoralCrownWalls[] = {
    {"Bellmar west service wall", {28.0f, -54.0f}, {28.0f, -4.0f},
     0.0f, 18.0f, 0.30f, BuildingFinish::WarmWall},
    {"Bellmar east facade", {78.0f, -4.0f}, {78.0f, -54.0f},
     0.0f, 18.0f, 0.30f, BuildingFinish::WarmWall,
     kCoralCrownFrontOpenings, std::size(kCoralCrownFrontOpenings)},
    {"Bellmar north wall", {28.0f, -4.0f}, {78.0f, -4.0f},
     0.0f, 18.0f, 0.30f, BuildingFinish::WarmWall},
    {"Bellmar south wall", {78.0f, -54.0f}, {28.0f, -54.0f},
     0.0f, 18.0f, 0.30f, BuildingFinish::WarmWall},
};

inline constexpr BuildingWall kBlueHeronWalls[] = {
    {"Maravelle west service wall", {36.0f, 4.0f}, {36.0f, 52.0f},
     0.0f, 14.8f, 0.30f, BuildingFinish::White},
    {"Maravelle east facade", {78.0f, 52.0f}, {78.0f, 4.0f},
     0.0f, 14.8f, 0.30f, BuildingFinish::White,
     kBlueHeronFrontOpenings, std::size(kBlueHeronFrontOpenings)},
    {"Maravelle north wall", {36.0f, 52.0f}, {78.0f, 52.0f},
     0.0f, 14.8f, 0.30f, BuildingFinish::White},
    {"Maravelle south wall", {78.0f, 4.0f}, {36.0f, 4.0f},
     0.0f, 14.8f, 0.30f, BuildingFinish::White},
};

inline constexpr BuildingRoof kCoralCrownRoofs[] = {
    {"Bellmar stepped parapet", {53.0f, -29.0f}, 18.0f, 52.0f, 52.0f,
     0.0f, 0.35f, 0.0f, 1.0f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::RedTrim, BuildingFinish::WarmWall},
};

inline constexpr BuildingRoof kBlueHeronRoofs[] = {
    {"Maravelle stepped parapet", {57.0f, 28.0f}, 14.8f, 44.0f, 50.0f,
     0.0f, 0.35f, 0.0f, 0.7f, RoofStyle::Flat, RidgeAxis::AlongX,
     BuildingFinish::TealDoor, BuildingFinish::White},
};

inline constexpr BuildingPlan kCoralCrownHotelPlan{
    kBellmarIdentity.name, kCoralCrownWalls, std::size(kCoralCrownWalls),
    kCoralCrownRoofs, std::size(kCoralCrownRoofs)};
inline constexpr BuildingPlan kBlueHeronHotelPlan{
    kMaravelleIdentity.name, kBlueHeronWalls, std::size(kBlueHeronWalls),
    kBlueHeronRoofs, std::size(kBlueHeronRoofs)};

inline void od_box(std::vector<StartPart>& out, const char* name, Vec2 centre,
                   float bottom, float width, float height, float depth,
                   BuildingFinish finish, bool solid = false,
                   float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

inline void od_eighties_frontage(std::vector<StartPart>& out, bool coral) {
    const float facade = kMiandiOceanHotelFrontX;
    const float z = coral ? -29.0f : 28.0f;
    const float top = coral ? 18.0f : 14.8f;
    const float half = coral ? 25.0f : 24.0f;
    const auto pink = BuildingFinish::RedTrim;
    const auto cyan = BuildingFinish::TealDoor;
    const auto primary = coral ? pink : cyan;
    const auto secondary = coral ? cyan : pink;
    const char* lettering = coral ? "miandi neon 80s pink Bellmar lettering"
                                 : "miandi neon 80s cyan Maravelle lettering";
    const char* trim = coral ? "miandi neon 80s cyan Bellmar architecture"
                            : "miandi neon 80s pink Maravelle architecture";
    // Recess-colored backing makes the illuminated letters read against stucco.
    // Keep the names legible, but do not turn the whole middle elevation into
    // a blank billboard. The compact dark panel leaves room bays visible at
    // both sides and above/below it.
    od_box(out, "Ocean Drive 80s sign backing", {facade + .98f, z}, top - 6.35f,
           .20f, 3.75f, 25.0f, BuildingFinish::DarkRoof);
    miandi_neon_name(out, coral ? "miandi neon 80s pink Bellmar name lettering"
                               : "miandi neon 80s cyan Maravelle name lettering",
                 coral ? kBellmarIdentity.sign : kMaravelleIdentity.sign,
                 facade + 1.18f, z, top - 5.82f, coral ? 2.3f : 2.1f, primary);

    // A stepped illuminated crown rises above the parapet rather than hiding
    // behind its solid east face, as the previous flat roofline did.
    od_box(out, "Ocean Drive 80s crown base", {facade - .1f, z}, top + .35f,
           1.0f, 1.0f, 20.0f, BuildingFinish::White);
    od_box(out, "Ocean Drive 80s crown middle", {facade - .1f, z}, top + 1.35f,
           1.0f, 1.0f, 13.0f, BuildingFinish::White);
    od_box(out, "Ocean Drive 80s crown peak", {facade - .1f, z}, top + 2.35f,
           1.0f, 1.0f, 6.0f, BuildingFinish::White);
    const std::array<Vec2, 8> crown{{{-10,top+1.35f},{-6.5f,top+1.35f},
        {-6.5f,top+2.35f},{-3,top+2.35f},{-3,top+3.35f},
        {3,top+3.35f},{3,top+2.35f},{6.5f,top+2.35f}}};
    for (std::size_t i = 1; i < crown.size(); ++i)
        miandi_neon_stroke(out, trim, facade+.65f, z, crown[i-1], crown[i], secondary);
    miandi_neon_stroke(out, trim, facade+.65f, z, crown.back(), {6.5f,top+1.35f}, secondary);
    miandi_neon_stroke(out, trim, facade+.65f, z, {6.5f,top+1.35f}, {10,top+1.35f}, secondary);

    for (float y : {6.3f, 7.0f, top - .4f}) {
        miandi_neon_stroke(out, trim, facade+1.3f, z, {-half+1.2f,y}, {half-1.2f,y}, secondary);
    }
    for (float u : {-half+1.2f, half-1.2f})
        miandi_neon_stroke(out, lettering, facade+1.3f, z, {u,3.5f}, {u,top-.4f}, primary);

    // Projecting, double-sided HOTEL blade, read along Ocean Drive as well as
    // head-on. Its high mounting leaves the entire ground route open.
    const float blade_z = z - half + 4.0f;
    od_box(out, "Ocean Drive 80s HOTEL blade backing", {facade+3.0f, blade_z},
           6.9f, 4.0f, 11.0f, .42f, BuildingFinish::DarkRoof);
    const auto blade_start = out.size();
    for (int row=0; row<5; ++row)
        miandi_neon_glyph(out, lettering, "HOTEL"[row], 0, 0, -.55f,
                      15.8f - row*2.0f, 1.6f, primary);
    const auto blade_end = out.size();
    for (std::size_t i=blade_start; i<blade_end; ++i) {
        auto& p = out[i];
        const float local_x = p.centre.x;
        p.centre = {facade+3.0f + p.centre.z, blade_z-.3f-local_x};
        p.yaw_deg = 90.0f;
        auto back=p;
        back.centre.x=2.0f*(facade+3.0f)-p.centre.x;
        back.centre.z=blade_z+.3f+local_x;
        back.yaw_deg=-90.0f;
        out.push_back(back);
    }

    // Entrance canopy and glass bays give the lights real surfaces.
    od_box(out, "Ocean Drive 80s entrance canopy", {facade+3.2f,z}, 3.8f,
           6.4f, .32f, 12.0f, BuildingFinish::White);
    miandi_neon_stroke(out, trim, facade+6.5f, z, {-6,3.9f}, {6,3.9f}, secondary);
    miandi_neon_stroke(out, lettering, facade+6.5f, z, {-6,4.3f}, {6,4.3f}, primary);
}

// A hotel room bay is deliberately a layered assembly rather than a colored
// rectangle on the wall: the reveal reads as shade, the glass sits behind the
// plaster, and the sill projects just enough to catch daytime light.
inline void od_front_room_bay(std::vector<StartPart>& out, const char* hotel,
                              float facade_x, float z, float bottom,
                              BuildingFinish frame) {
    od_box(out, hotel, {facade_x + .14f, z}, bottom - .12f, .18f, 2.85f,
           4.55f, frame);
    od_box(out, hotel, {facade_x + .27f, z}, bottom + .16f, .10f, 2.25f,
           3.78f, BuildingFinish::Glass);
    od_box(out, hotel, {facade_x + .42f, z}, bottom - .08f, .62f, .16f,
           4.25f, BuildingFinish::White);
    // Mullion is separate from the glass so a whole elevation does not become
    // one flat blue band when viewed obliquely from Ocean Drive.
    od_box(out, hotel, {facade_x + .43f, z}, bottom + .16f, .12f, 2.18f,
           .14f, frame);
}

inline void od_side_room_bay(std::vector<StartPart>& out, const char* hotel,
                             float x, float facade_z, float outward_sign,
                             float bottom,
                             BuildingFinish frame) {
    const char* backing = std::strstr(hotel, "Bellmar")
        ? "Bellmar side room window backing"
        : "Maravelle side room window backing";
    const char* glass = std::strstr(hotel, "Bellmar")
        ? "Bellmar side room window glass"
        : "Maravelle side room window glass";
    const char* sill = std::strstr(hotel, "Bellmar")
        ? "Bellmar side room window sill"
        : "Maravelle side room window sill";
    const char* mullion = std::strstr(hotel, "Bellmar")
        ? "Bellmar side room window mullion"
        : "Maravelle side room window mullion";
    // Each layer clears the 30 cm shell wall before moving farther outward.
    // `outward_sign` makes the north and south end walls mirror correctly.
    const float backing_z = facade_z + outward_sign * .24f;
    const float glass_z = facade_z + outward_sign * .36f;
    const float sill_z = facade_z + outward_sign * .48f;
    const float mullion_z = facade_z + outward_sign * .52f;
    od_box(out, backing, {x, backing_z}, bottom - .12f, 4.55f, 2.85f, .18f,
           frame);
    od_box(out, glass, {x, glass_z}, bottom + .16f, 3.78f, 2.25f, .10f,
           BuildingFinish::Glass);
    od_box(out, sill, {x, sill_z}, bottom - .08f, 4.25f, .16f, .62f,
           BuildingFinish::White);
    od_box(out, mullion, {x, mullion_z}, bottom + .16f, .14f, 2.18f, .12f,
           frame);
}

inline void od_balcony_assembly(std::vector<StartPart>& out, const char* hotel,
                                float facade_x, float z, float level,
                                float span, BuildingFinish accent) {
    // Front facades face +X. The floor projects 2.35 m, while the rail itself
    // runs along Z. This corrects the old reversed 7 m X / 10 cm Z rail.
    constexpr float kProjection = 2.35f;
    const float front_x = facade_x + kProjection;
    od_box(out, hotel, {facade_x + kProjection * .5f, z}, level, kProjection,
           .22f, span, BuildingFinish::White);
    // Open rail: top/bottom members plus individual balusters, never a
    // single opaque one-metre parapet.
    od_box(out, hotel, {front_x, z}, level + .28f, .14f, .10f, span,
           BuildingFinish::Steel);
    od_box(out, hotel, {front_x, z}, level + 1.14f, .16f, .12f, span,
           BuildingFinish::Steel);
    od_box(out, hotel, {front_x + .03f, z}, level + 1.27f, .18f, .13f, span,
           accent);
    for (float dz : {-1.0f, -.5f, 0.0f, .5f, 1.0f}) {
        const float rail_z = z + dz * (span * .42f);
        od_box(out, hotel, {front_x, rail_z}, level + .34f, .10f, .76f, .10f,
               BuildingFinish::Steel);
    }
    // Slender returns and brackets make the floating slab believable, but all
    // stay visual-only and above the lobby approach's head-clear zone.
    for (float edge : {-span * .43f, span * .43f}) {
        od_box(out, hotel, {facade_x + 1.15f, z + edge}, level - .62f, .14f,
               .72f, .14f, accent);
        od_box(out, hotel, {facade_x + 1.15f, z + edge}, level - .04f, 2.15f,
               .14f, .14f, accent);
    }
}

inline void od_hotel_details(std::vector<StartPart>& out, bool coral) {
    const float x = coral ? 53.0f : 57.0f;
    const float z = coral ? -29.0f : 28.0f;
    const float width = coral ? 50.0f : 42.0f;
    const float depth = coral ? 50.0f : 48.0f;
    const float top = coral ? 18.0f : 14.8f;
    const BuildingFinish accent = coral ? BuildingFinish::RedTrim
                                        : BuildingFinish::TealDoor;

    // These non-solid strips run from each actual door to Ocean Drive's west
    // sidewalk edge (world x=8086), never overlapping the road/sidewalk ribbon.
    const float door_x = kMiandiOceanHotelFrontX;
    const float edge_x = kMiandiOceanSidewalkX;
    // Thin non-solid support completes each otherwise hollow shell. It stays
    // inside the 30 cm walls and reaches a local top of .20 m, so main's
    // ground-piece routing can remove the visible grass at both lobby doors.
    od_box(out, coral ? "Bellmar lobby floor" : "Maravelle lobby floor",
           {x, z}, .10f, width - .60f, .10f, depth - .60f,
           coral ? BuildingFinish::Concrete : BuildingFinish::White, false);
    od_box(out, coral ? "Bellmar lobby walk" : "Maravelle lobby walk",
           {(door_x + edge_x) * .5f, z}, .08f, edge_x - door_x, .10f, 4.8f,
           BuildingFinish::Concrete, false);
    od_box(out, coral ? "Bellmar terrazzo threshold"
                      : "Maravelle terrazzo threshold",
           {door_x + .8f, z}, .10f, 1.6f, .10f, 3.8f,
           BuildingFinish::White, false);

    // Each hotel gets its own rhythm: Coral is long and symmetrical with a
    // pink centre spine; Heron is tighter, with cyan corner fins. Room bays
    // are facade detail only, not a claim that upper floors are enterable.
    const char* front_bay = coral ? "Bellmar front room window bay"
                                  : "Maravelle front room window bay";
    const char* side_hotel = coral ? "Bellmar" : "Maravelle";
    const float facade = kMiandiOceanHotelFrontX;
    const float north = coral ? -4.0f : 52.0f;
    const float south = coral ? -54.0f : 4.0f;
    const auto add_room_level = [&](float level,
                                    std::initializer_list<float> bays) {
        for (float bay : bays) od_front_room_bay(out, front_bay, facade, bay,
                                                 level, accent);
        // End-wall bays supply a visible room rhythm on the oblique approach.
        // They remain clear of the west service lane and all ground routes.
        for (float xx : {x - width * .5f + 8.0f,
                         x + width * .5f - 8.0f}) {
            od_side_room_bay(out, side_hotel, xx, north, 1.0f, level, accent);
            od_side_room_bay(out, side_hotel, xx, south, -1.0f, level, accent);
        }
    };
    if (coral) {
        for (float level : {4.55f, 8.05f, 11.55f, 15.05f})
            add_room_level(level, {-48.0f, -39.0f, -19.0f, -10.0f});
    } else {
        for (float level : {4.55f, 8.15f, 11.75f})
            add_room_level(level, {9.5f, 28.0f, 46.5f});
    }

    // Layered shade bands sit above each storey rather than across the lobby.
    const auto add_eyebrow = [&](float level) {
        od_box(out, coral ? "Bellmar horizontal eyebrow"
                          : "Maravelle horizontal eyebrow",
               {x + width * .5f + .35f, z}, level, .70f,
               .20f, depth - 4.0f, accent);
    };
    if (coral) {
        for (float level : {4.0f, 7.5f, 11.0f, 14.5f}) add_eyebrow(level);
    } else {
        for (float level : {4.0f, 8.0f, 12.0f}) add_eyebrow(level);
    }
    const auto add_fin = [&](float zz) {
        od_box(out, coral ? "Bellmar corner fin" : "Maravelle corner fin",
               {x + width * .5f, zz}, 3.3f, .45f, top - 2.3f, .35f, accent,
               false, coral ? 0.0f : 2.0f);
    };
    if (coral) {
        for (float zz : {-47.0f, -29.0f, -11.0f}) add_fin(zz);
    } else {
        for (float zz : {10.0f, 28.0f, 46.0f}) add_fin(zz);
    }
    // Original Deco identity: a raised vertical spine for Coral and paired
    // rounded-looking stepped corner caps for Heron. These are shallow trim,
    // never substitute doors or collision-bearing architectural claims.
    if (coral) {
        od_box(out, "Bellmar Deco centre spine", {facade + .48f, z}, 4.3f,
               .34f, top - 3.6f, 1.6f, accent);
        od_box(out, "Bellmar Deco centre spine cap", {facade + .58f, z},
               top - .1f, .52f, 1.25f, 3.0f, BuildingFinish::White);
    } else {
        for (float corner : {10.0f, 46.0f}) {
            od_box(out, "Maravelle Deco corner pilaster", {facade + .42f, corner},
                   4.3f, .34f, top - 3.5f, 1.05f, accent);
            od_box(out, "Maravelle Deco corner cap", {facade + .55f, corner},
                   top - .15f, .58f, 1.15f, 2.1f, BuildingFinish::White);
        }
    }
    const auto add_balcony = [&](float zz, float level, float span) {
        od_balcony_assembly(out, coral ? "Bellmar balcony assembly"
                                       : "Maravelle balcony assembly",
                            facade, zz, level, span, accent);
    };
    if (coral) {
        // The centre bay deliberately leaves the Bellmar door/sign axis
        // clear; side balconies sit on room floors, not at ground level.
        add_balcony(-43.5f, 7.52f, 7.2f);
        add_balcony(-14.5f, 11.02f, 7.2f);
    } else {
        add_balcony(11.5f, 8.02f, 6.6f);
        add_balcony(44.0f, 8.02f, 6.6f);
    }
    od_box(out, coral ? "Bellmar rooftop HVAC" : "Maravelle rooftop HVAC",
           {x + width * .25f, z}, top + .2f, 5.0f, 1.4f, 3.0f,
           BuildingFinish::DarkRoof, true);
    od_box(out, coral ? "Bellmar rooftop vent" : "Maravelle rooftop vent",
           {x - width * .18f, z + 8.0f}, top + .35f, 1.2f, 1.8f, 1.2f,
           BuildingFinish::Steel, true);

    od_eighties_frontage(out, coral);
}

inline std::vector<StartPart> bake_miandi_ocean_drive() {
    std::vector<StartPart> out = bake_building(kCoralCrownHotelPlan);
    auto heron = bake_building(kBlueHeronHotelPlan);
    out.insert(out.end(), heron.begin(), heron.end());
    od_hotel_details(out, true);
    od_hotel_details(out, false);

    // The corner cafe sits beside Bellmar, not inside its moved shell.
    // Its pavement meets the sidewalk, with the same .20 m terrace top.
    od_box(out, "Ocean Drive cafe court", {67.0f, -65.0f}, .12f, 38.0f, .08f,
           16.0f, BuildingFinish::Concrete, false);
    od_box(out, "Ocean Drive cafe canopy", {65.0f, -65.0f}, 4.1f, 22.0f, .20f,
           10.0f, BuildingFinish::Yellow);
    for (float xx : {55.0f, 75.0f})
        od_box(out, "Ocean Drive cafe canopy post", {xx, -65.0f}, .2f, .25f,
               4.0f, .25f, BuildingFinish::Steel, true);
    // A real hotel service lane reaches Seabreeze's east sidewalk edge at
    // world x=7910. The access baker cuts its curb mouth separately.
    od_box(out, "Ocean Drive hotel service lane", {-72.0f, 0.0f}, .02f, 36.0f,
           .08f, 120.0f, BuildingFinish::Concrete, false);
    return out;
}

inline std::vector<StartPart> bake_miandi_north_promenade() {
    std::vector<StartPart> out;
    // The connector begins at Ocean Drive's east sidewalk edge (world x=8114),
    // not its centreline. It meets the broad walk edge-to-edge, so neither slab
    // can fight the road surface in the depth buffer.
    od_box(out, "north promenade Ocean Drive connector", {-60.0f, 0.0f}, .02f,
           96.0f, .12f, 12.0f, BuildingFinish::Concrete, false);
    od_box(out, "north promenade broad clear walk", {0.0f, 0.0f}, .02f, 24.0f,
           .12f, 132.0f, BuildingFinish::Concrete, false);
    // Four narrow-X/deep-Z seawall segments sit on the east edge. The spaces
    // between them are three real beach-access gaps, not walk-crossing walls.
    for (float zz : {-52.75f, -18.0f, 18.0f, 52.75f})
        od_box(out, "north promenade low seawall", {104.0f, zz}, .12f, 2.0f,
               1.15f, 26.5f, BuildingFinish::Concrete, true);
    od_box(out, "north promenade shade pavilion roof", {25.0f, 0.0f}, 4.2f,
           14.0f, .20f, 10.0f, BuildingFinish::TealDoor);
    for (float xx : {-6.0f, 6.0f})
        for (float zz : {-4.0f, 4.0f})
            od_box(out, "north promenade pavilion post", {25.0f + xx, zz}, .12f, .25f,
                   4.0f, .25f, BuildingFinish::Steel, true);
    for (float zz : {-43.0f, -14.0f, 15.0f, 44.0f}) {
        od_box(out, "north promenade bench seat", {0.0f, zz}, .75f, 3.0f,
               .18f, .55f, BuildingFinish::Steel, true);
        od_box(out, "north promenade bench back", {0.0f, zz + .28f}, 1.2f,
               3.0f, .65f, .12f, BuildingFinish::Steel, false);
    }
    for (float zz : {-52.0f, -32.0f, 32.0f, 52.0f}) {
        od_box(out, "north promenade palm trunk", {76.0f, zz}, .12f, .35f,
               3.2f, .35f, BuildingFinish::WarmWall, false);
        od_box(out, "north promenade palm crown", {76.0f, zz}, 3.0f, 3.2f,
               1.0f, 3.2f, BuildingFinish::White, false);
    }

    return out;
}

// Stable aliases used by later integration code.
inline std::vector<StartPart> bake_miandi_ocean_drive_prom_building() {
    return bake_miandi_ocean_drive();
}
inline std::vector<StartPart> bake_miandi_north_promenade_strip() {
    return bake_miandi_north_promenade();
}

}  // namespace apricot::city

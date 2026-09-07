#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// The Galleria occupies the clear side of the East Arm, opposite the Cinder
// Underpass terminal. Local +Z faces the arterial and local X follows Road 51.
// The street edge stays fixed while the enlarged parcel reaches north into
// clear land, avoiding the road gore that broke the first plaza layout.
inline constexpr float kEastArmPlazaGroundM = 13.82f;
inline constexpr float kEastArmPlazaRetainingDepthM = 0.80f;
inline constexpr StartSite kEastArmPlazaSite{
    "East Arm Galleria", {351.968f, 915.617f}, -0.99287684f, -0.11914522f,
    {0.0f, 0.0f}, 148.0f, 170.0f, kEastArmPlazaGroundM};

inline constexpr std::size_t kEastArmPlazaMaxParts = 700u;

enum class EastArmPlazaUse { Mall, CinemaLobby, Bookstore, CoffeeShop, Restaurant };

inline const char* east_arm_plaza_use_name(EastArmPlazaUse use) {
    switch (use) {
    case EastArmPlazaUse::Mall: return "mall";
    case EastArmPlazaUse::CinemaLobby: return "cinema lobby";
    case EastArmPlazaUse::Bookstore: return "bookstore";
    case EastArmPlazaUse::CoffeeShop: return "coffee shop";
    case EastArmPlazaUse::Restaurant: return "restaurant";
    }
    return "";
}

inline StartPart east_arm_plaza_ground_piece() {
    return {"east arm plaza parking lot", {0.0f, 0.0f}, 0.0f, 148.0f, 0.10f,
            170.0f, StartFinish::Asphalt, false};
}

inline void east_arm_plaza_box(std::vector<StartPart>& out, const char* name,
                               float x, float z, float bottom, float width,
                               float height, float depth, StartFinish finish,
                               bool solid = false) {
    out.push_back({name, {x, z}, bottom, width, height, depth, finish, solid});
}

// One enclosed mall, not a row of unrelated boxes. The public plan is a long
// north-south concourse with a cross-gallery near the anchors. Stores open to
// that circulation; the rear edge is reserved for loading and plant.
inline std::vector<StartPart> bake_east_arm_plaza() {
    std::vector<StartPart> out;
    out.reserve(340);
    out.push_back(east_arm_plaza_ground_piece());

    const auto box = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool solid = false) {
        east_arm_plaza_box(out, n, x, z, b, w, h, d, f, solid);
    };

    // Engineered terrace and rear service edge.
    box("east arm plaza west retaining wall", -73.78f, 0.0f,
        .10f - kEastArmPlazaRetainingDepthM, .44f,
        kEastArmPlazaRetainingDepthM, 169.6f, StartFinish::Concrete, true);
    box("east arm plaza east retaining wall", 73.78f, 0.0f,
        .10f - kEastArmPlazaRetainingDepthM, .44f,
        kEastArmPlazaRetainingDepthM, 169.6f, StartFinish::Concrete, true);
    box("east arm plaza rear retaining wall", 0.0f, -84.78f,
        .10f - kEastArmPlazaRetainingDepthM, 147.2f,
        kEastArmPlazaRetainingDepthM, .44f, StartFinish::Concrete, true);
    box("east arm plaza service lane", 0.0f, -77.0f, .101f, 132.0f, .06f,
        12.0f, StartFinish::Concrete);
    for (float x : {-49.0f, -15.0f, 19.0f, 53.0f}) {
        box("east arm plaza loading bay stripe", x, -77.0f, .104f, .12f,
            .018f, 9.0f, StartFinish::White);
        box("east arm plaza loading dock bumper", x + 7.0f, -70.8f, .20f,
            5.5f, .75f, .65f, StartFinish::DarkRoof, true);
    }
    for (float x : {-58.0f, 58.0f})
        box("east arm plaza service gate post", x, -77.0f, .12f, .45f,
            2.6f, .45f, StartFinish::Steel, true);

    // Front parking keeps two broad vehicle aisles and two curb-cut paths.
    box("east arm plaza entry walk", 0.0f, 41.5f, .101f, 136.0f, .06f,
        10.0f, StartFinish::Concrete);
    box("east arm plaza pedestrian court", 0.0f, 47.0f, .102f, 22.0f,
        .065f, 8.0f, StartFinish::Concrete);
    box("east arm plaza painted crossing", 0.0f, 63.0f, .105f, 5.0f,
        .018f, 30.0f, StartFinish::White);
    for (float row_z : {56.0f, 76.0f}) {
        for (float x = -65.0f; x <= 65.0f; x += 6.5f) {
            if (std::fabs(x + 46.0f) < 6.0f || std::fabs(x - 46.0f) < 6.0f ||
                std::fabs(x) < 4.0f)
                continue;
            box("east arm plaza parking stripe", x, row_z, .104f, .12f,
                .018f, 5.8f, StartFinish::White);
            box("east arm plaza parking stop", x + 2.35f, row_z - 3.15f,
                .105f, 2.35f, .16f, .24f, StartFinish::Concrete, true);
        }
    }
    for (float x : {-62.0f, -30.0f, 30.0f, 62.0f})
        box("east arm plaza forecourt planter", x, 43.0f, .10f, 3.2f,
            .78f, 3.2f, StartFinish::Brick, true);

    // 136 x 106 metre weather-tight shell. Front openings are real gaps for
    // the main lobby plus independent coffee and restaurant doors.
    box("mall exterior west wall", -68.0f, -17.0f, .10f, .34f, 8.6f,
        106.0f, StartFinish::Brick, true);
    box("mall exterior east wall", 68.0f, -17.0f, .10f, .34f, 8.6f,
        106.0f, StartFinish::Brick, true);
    box("mall exterior rear wall", 0.0f, -70.0f, .10f, 136.0f, 8.6f,
        .34f, StartFinish::Brick, true);
    box("mall front wall far west", -58.0f, 36.0f, .10f, 20.0f, 8.6f,
        .34f, StartFinish::WarmWall, true);
    box("mall front wall west", -25.0f, 36.0f, .10f, 38.0f, 8.6f,
        .34f, StartFinish::WarmWall, true);
    box("mall front wall east", 26.0f, 36.0f, .10f, 40.0f, 8.6f,
        .34f, StartFinish::WarmWall, true);
    box("mall front wall far east", 59.0f, 36.0f, .10f, 18.0f, 8.6f,
        .34f, StartFinish::WarmWall, true);
    for (float x : {-62.0f, -55.0f, -34.0f, -25.0f, 25.0f, 34.0f,
                    55.0f, 62.0f})
        box("mall storefront glass", x, 36.20f, .55f, 5.0f, 3.25f,
            .035f, StartFinish::Glass, true);
    box("mall main interior floor", 0.0f, -17.0f, .12f, 135.2f, .10f,
        105.2f, StartFinish::Concrete);
    box("mall west wing ceiling", -39.0f, -17.0f, 8.35f, 57.5f, .10f,
        105.0f, StartFinish::White);
    box("mall east wing ceiling", 39.0f, -17.0f, 8.35f, 57.5f, .10f,
        105.0f, StartFinish::White);
    box("mall west wing roof", -39.0f, -17.0f, 8.62f, 59.0f, .30f,
        107.0f, StartFinish::DarkRoof, true);
    box("mall east wing roof", 39.0f, -17.0f, 8.62f, 59.0f, .30f,
        107.0f, StartFinish::DarkRoof, true);

    // The raised glazed spine makes the circulation legible from outside.
    box("mall concourse interior floor", 0.0f, -14.0f, .135f, 14.0f,
        .025f, 96.0f, StartFinish::White);
    box("mall cross gallery interior floor", 0.0f, -25.0f, .136f, 112.0f,
        .025f, 11.0f, StartFinish::White);
    box("mall atrium skylight", 0.0f, -14.0f, 9.0f, 18.0f, .22f, 91.0f,
        StartFinish::Glass, true);
    box("mall atrium north clerestory", 0.0f, -58.5f, 8.6f, 18.0f, 3.0f,
        .32f, StartFinish::Glass, true);
    box("mall atrium south clerestory", 0.0f, 31.5f, 8.6f, 18.0f, 3.0f,
        .32f, StartFinish::Glass, true);

    // Main entrance, porte cochere and directory node.
    box("mall entrance threshold", 0.0f, 35.6f, .13f, 10.0f, .08f, 1.15f,
        StartFinish::Concrete);
    box("mall entrance glass west", -4.8f, 36.18f, .55f, .35f, 3.4f,
        .035f, StartFinish::Glass, true);
    box("mall entrance glass east", 4.8f, 36.18f, .55f, .35f, 3.4f,
        .035f, StartFinish::Glass, true);
    box("mall entry tower", 0.0f, 34.8f, 4.0f, 18.0f, 8.8f, 2.8f,
        StartFinish::WarmWall, true);
    box("mall entry tower cap", 0.0f, 34.8f, 12.8f, 19.5f, .55f, 3.4f,
        StartFinish::TealDoor, true);
    for (float x : {-6.0f, 0.0f, 6.0f})
        box("mall entry tower vertical fin", x, 36.3f, 7.5f, .42f, 4.7f,
            .34f, StartFinish::RedTrim);
    box("mall arcade sign face", 0.0f, 36.52f, 8.5f, 18.0f, 2.4f,
        .025f, StartFinish::White);
    box("mall covered arcade canopy", 0.0f, 39.2f, 4.65f, 134.0f,
        .32f, 6.0f, StartFinish::TealDoor, true);
    for (float x = -63.0f; x <= 63.0f; x += 10.5f) {
        box("mall arcade column", x, 41.8f, .16f, .48f, 4.45f, .48f,
            StartFinish::White, true);
        box("mall arcade light lens", x, 39.0f, 4.35f, 1.4f, .08f, .55f,
            StartFinish::White);
    }

    // Rear cinema anchor. The lobby opens onto the cross-gallery; the deeper
    // auditorium and two side aisles read as an actual multiplex plan.
    box("cinema lobby interior floor", 28.0f, -36.0f, .15f, 71.0f, .06f,
        8.0f, StartFinish::Concrete);
    box("cinema auditorium interior floor", 28.0f, -52.0f, .15f, 71.0f,
        .06f, 27.0f, StartFinish::Concrete);
    box("cinema lobby front wall west", -6.0f, -32.0f, .14f, 4.0f, 6.4f,
        .30f, StartFinish::WarmWall, true);
    box("cinema lobby front wall east", 34.0f, -32.0f, .14f, 60.0f, 6.4f,
        .30f, StartFinish::WarmWall, true);
    box("cinema threshold", 0.0f, -31.6f, .15f, 8.0f, .08f, 1.15f,
        StartFinish::Concrete);
    box("cinema ticket counter", 13.0f, -37.5f, .22f, 12.0f, 1.1f, 1.2f,
        StartFinish::TealDoor, true);
    box("cinema concessions counter", 43.0f, -37.5f, .22f, 18.0f, 1.1f,
        1.2f, StartFinish::RedTrim, true);
    box("cinema marquee backing", 31.0f, -31.78f, 3.7f, 17.0f, 2.9f,
        .20f, StartFinish::DarkRoof);
    box("cinema marquee sign face", 31.0f, -31.66f, 3.85f, 16.0f,
        2.55f, .025f, StartFinish::White);
    for (float z : {-46.0f, -50.0f, -54.0f, -58.0f, -62.0f})
        box("cinema auditorium seat row", 34.0f, z, .22f, 48.0f, .85f,
            1.2f, StartFinish::RedTrim, true);
    box("cinema raised roof", 28.0f, -50.0f, 8.94f, 74.0f, 2.35f,
        40.0f, StartFinish::DarkRoof, true);
    box("cinema rooftop crown", 28.0f, -50.0f, 11.3f, 30.0f, .65f,
        17.0f, StartFinish::RedTrim, true);

    // West bookstore anchor. Its door faces the central spine, not the road.
    box("bookstore interior floor", -41.5f, -47.0f, .15f, 44.0f, .06f,
        34.0f, StartFinish::Concrete);
    box("bookstore front partition", -41.5f, -30.0f, .14f, 45.0f, 5.8f,
        .30f, StartFinish::WarmWall, true);
    box("bookstore east wall north", -19.0f, -57.0f, .14f, .30f, 5.8f,
        14.0f, StartFinish::WarmWall, true);
    box("bookstore east wall south", -19.0f, -37.0f, .14f, .30f, 5.8f,
        14.0f, StartFinish::WarmWall, true);
    box("bookstore threshold", -18.6f, -46.5f, .15f, 1.1f, .08f, 5.0f,
        StartFinish::Concrete);
    box("bookstore sign face", -41.5f, -29.78f, 3.2f, 16.5f, 2.0f,
        .025f, StartFinish::White);
    for (float z : {-57.0f, -50.0f, -38.0f}) {
        box("bookstore shelf west", -52.0f, z, .22f, 2.1f, 2.25f, 4.4f,
            StartFinish::DarkRoof, true);
        box("bookstore shelf east", -34.0f, z, .22f, 2.1f, 2.25f, 4.4f,
            StartFinish::DarkRoof, true);
    }
    box("bookstore cashier counter", -43.0f, -33.0f, .22f, 10.0f,
        1.08f, 1.1f, StartFinish::TealDoor, true);
    box("bookstore roof step", -41.5f, -47.0f, 8.94f, 47.0f, .78f,
        37.0f, StartFinish::TealDoor, true);

    // Coffee and restaurant bookend the front lobby. Each has an exterior
    // door for early/late trade and a second door into the mall concourse.
    box("coffee shop interior floor", -42.0f, 19.0f, .15f, 43.0f, .06f,
        26.0f, StartFinish::Concrete);
    box("coffee shop rear partition", -42.0f, 6.0f, .14f, 44.0f, 5.2f,
        .30f, StartFinish::WarmWall, true);
    box("coffee shop east wall south", -20.0f, 11.5f, .14f, .30f, 5.2f,
        11.0f, StartFinish::WarmWall, true);
    box("coffee shop east wall north", -20.0f, 26.0f, .14f, .30f, 5.2f,
        12.0f, StartFinish::WarmWall, true);
    box("coffee shop mall threshold", -19.6f, 18.5f, .15f, 1.1f, .08f,
        4.0f, StartFinish::Concrete);
    box("coffee shop exterior threshold", -46.0f, 35.6f, .15f, 4.0f,
        .08f, 1.15f, StartFinish::Concrete);
    box("coffee shop sign face", -34.0f, 36.22f, 5.4f, 10.5f, 1.65f,
        .025f, StartFinish::White);
    box("coffee shop bar", -48.0f, 10.0f, .22f, 12.0f, 1.1f, 1.3f,
        StartFinish::TealDoor, true);
    for (float x : {-52.0f, -43.0f, -34.0f})
        box("coffee shop table", x, 23.0f, .22f, 1.3f, .72f, 1.3f,
            StartFinish::Steel, true);

    box("restaurant interior floor", 42.0f, 18.0f, .15f, 43.0f, .06f,
        28.0f, StartFinish::Concrete);
    box("restaurant rear partition", 42.0f, 4.0f, .14f, 44.0f, 5.6f,
        .30f, StartFinish::WarmWall, true);
    box("restaurant west wall south", 20.0f, 10.0f, .14f, .30f, 5.6f,
        12.0f, StartFinish::WarmWall, true);
    box("restaurant west wall north", 20.0f, 27.0f, .14f, .30f, 5.6f,
        10.0f, StartFinish::WarmWall, true);
    box("restaurant mall threshold", 19.6f, 18.5f, .15f, 1.1f, .08f,
        5.0f, StartFinish::Concrete);
    box("restaurant exterior threshold", 48.0f, 35.6f, .15f, 4.0f,
        .08f, 1.15f, StartFinish::Concrete);
    box("restaurant sign face", 39.0f, 36.22f, 5.4f, 16.5f, 2.0f,
        .025f, StartFinish::White);
    box("restaurant service counter", 48.0f, 7.5f, .22f, 15.0f, 1.1f,
        1.25f, StartFinish::RedTrim, true);
    for (float x : {30.0f, 38.0f, 46.0f, 54.0f})
        box("restaurant dining table", x, 24.0f, .22f, 1.35f, .75f,
            1.35f, StartFinish::Steel, true);

    // Inline shops and food court make the spine feel occupied without
    // narrowing its 14 metre public clear zone.
    for (const auto& bay : std::vector<Vec2>{{28.0f, 6.0f}, {22.5f, 3.0f},
                                              {13.5f, 5.0f}, {6.0f, 8.0f},
                                              {-4.0f, 10.0f}, {-16.0f, 12.0f}}) {
        box("mall west shop partition", -12.0f, bay.x, .14f, .30f, 5.0f,
            bay.z, StartFinish::WarmWall, true);
        box("mall west shop display glass", -11.82f, bay.x, .55f, .035f,
            3.1f, std::max(.8f, bay.z - .8f), StartFinish::Glass, true);
    }
    for (const auto& bay : std::vector<Vec2>{{28.0f, 6.0f}, {22.5f, 3.0f},
                                              {13.5f, 5.0f}, {6.0f, 8.0f}}) {
        box("mall east shop partition", 12.0f, bay.x, .14f, .30f, 5.0f,
            bay.z, StartFinish::WarmWall, true);
        box("mall east shop display glass", 11.82f, bay.x, .55f, .035f,
            3.1f, std::max(.8f, bay.z - .8f), StartFinish::Glass, true);
    }
    box("mall food court interior floor", 40.0f, -12.0f, .15f, 48.0f,
        .06f, 23.0f, StartFinish::Concrete);
    for (float x : {24.0f, 36.0f, 48.0f, 60.0f})
        box("mall food court counter", x, -20.0f, .22f, 8.0f, 1.1f,
            1.3f, StartFinish::TealDoor, true);
    for (float x : {24.0f, 36.0f, 48.0f, 60.0f})
        box("mall food court table", x, -7.0f, .22f, 1.35f, .75f, 1.35f,
            StartFinish::Steel, true);
    for (float z : {-15.0f, 0.0f, 15.0f}) {
        const float planter_x = z == 0.0f ? -5.0f : 5.0f;
        box("mall concourse planter", planter_x, z, .16f, 2.4f, .62f, 2.4f,
            StartFinish::Brick, true);
        box("mall concourse bench west", -8.5f, z, .18f, 2.6f, .72f,
            .72f, StartFinish::DarkRoof, true);
        box("mall concourse bench east", 8.5f, z, .18f, 2.6f, .72f,
            .72f, StartFinish::DarkRoof, true);
    }
    for (float z : {-22.0f, -8.0f, 6.0f, 20.0f}) {
        box("mall concourse floor accent", 0.0f, z, .162f, 13.5f, .018f,
            .45f, z < 0.0f ? StartFinish::TealDoor : StartFinish::RedTrim);
        box("mall concourse ceiling light", 0.0f, z, 8.15f, 2.4f, .08f,
            .8f, StartFinish::White);
    }

    // Street frontage and service plant.
    for (float x : {-64.0f, -24.0f, 24.0f, 64.0f}) {
        box("east arm plaza lamp pole", x, 47.0f, .15f, .22f, 5.8f,
            .22f, StartFinish::Steel, true);
        box("east arm plaza lamp head", x, 47.0f, 5.85f, 1.25f, .28f,
            .75f, StartFinish::White);
    }
    for (float x : {-58.0f, -30.0f, 30.0f, 58.0f}) {
        box("east arm plaza palm trunk", x, 44.0f, .15f, .65f, 5.2f,
            .65f, StartFinish::Brick, true);
        box("east arm plaza palm crown", x, 44.0f, 5.15f, 4.8f, 2.1f,
            4.8f, StartFinish::TealDoor);
    }
    for (float x : {-48.0f, -35.0f, 35.0f, 48.0f})
        box("east arm plaza bench", x, 41.5f, .18f, 3.2f, .72f, .72f,
            StartFinish::DarkRoof, true);
    for (float x : {-52.0f, -46.0f, -40.0f}) {
        box("coffee shop patio table", x, 46.5f, .18f, 1.25f, .74f, 1.25f,
            StartFinish::Steel, true);
        box("coffee shop patio umbrella", x, 46.5f, 2.55f, 3.0f, .18f,
            3.0f, StartFinish::RedTrim);
    }
    for (float x : {-46.0f, 0.0f, 46.0f}) {
        box("mall rooftop hvac unit", x, -15.0f, 8.95f, 8.0f, 1.6f,
            5.0f, StartFinish::Steel, true);
        box("mall rooftop hvac screen", x, -15.0f, 10.2f, 9.0f, 1.5f,
            .25f, StartFinish::TealDoor, true);
    }

    return out;
}

inline bool east_arm_plaza_has_use(const std::vector<StartPart>& parts,
                                   EastArmPlazaUse use) {
    const char* token = east_arm_plaza_use_name(use);
    for (const StartPart& part : parts)
        if (part.name && std::strstr(part.name, token)) return true;
    return false;
}

inline bool east_arm_plaza_point_clear(const std::vector<StartPart>& parts, Vec2 p) {
    for (const StartPart& part : parts) {
        if (!part.solid || part.bottom_m >= 2.0f) continue;
        const float yaw = part.yaw_deg * 0.01745329251994329577f;
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float dx = p.x - part.centre.x, dz = p.z - part.centre.z;
        const float local_x = c * dx - s * dz;
        const float local_z = s * dx + c * dz;
        if (std::fabs(local_x) < part.width_m * .5f &&
            std::fabs(local_z) < part.depth_m * .5f)
            return false;
    }
    return true;
}

inline bool east_arm_plaza_walk_routes_clear(const std::vector<StartPart>& parts) {
    constexpr Vec2 route[] = {
        {0, 47}, {0, 44}, {0, 41}, {0, 38}, {0, 36}, {0, 33},
        {0, 30}, {0, 27}, {0, 24}, {0, 21}, {0, 18}, {0, 15},
        {0, 12}, {0, 9}, {0, 6}, {0, 3}, {0, 0}, {0, -3},
        {0, -6}, {0, -9}, {0, -12}, {0, -15}, {0, -18},
        {0, -21}, {0, -24}, {0, -27}, {0, -30}, {0, -32}, {0, -40},
        {-8, -46}, {-19, -46}, {-28, -46},
        {-8, 18.5f}, {-20, 18.5f}, {-29, 18.5f},
        {8, 18.5f}, {20, 18.5f}, {30, 18.5f},
        {-46, 36}, {48, 36}};
    for (const Vec2 point : route)
        if (!east_arm_plaza_point_clear(parts, point)) return false;
    return true;
}

}  // namespace apricot::city

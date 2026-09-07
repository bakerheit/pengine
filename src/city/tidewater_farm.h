#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "city/start_area.h"
#include "road/road_graph.h"

namespace apricot::city {

// A working family farm in the open wedge east of Route 1, between Old Tide
// Street and the north end of the Apron Spine. The parcel stays on the natural
// Saltmarsh/Ostend grade; buildings use measured foundations and each crop row
// follows the terrain instead of asking for another district-sized flat plate.
inline constexpr StartSite kTidewaterFarmSite{
    "Tidewater Farm", {-1285.0f, -415.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 125.0f, 160.0f, 0.0f, 1050.0f};

inline constexpr uint32_t kTidewaterFarmTrackRoadId = 78u;

inline glm::vec2 tidewater_farm_world(glm::vec2 local) {
    const auto& site = kTidewaterFarmSite;
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

inline bool tidewater_farm_lot_contains(float world_x, float world_z,
                                        float margin_m = 0.0f) {
    const float x = world_x - kTidewaterFarmSite.origin.x;
    const float z = world_z - kTidewaterFarmSite.origin.z;
    return std::fabs(x) <= kTidewaterFarmSite.lot_width_m * 0.5f + margin_m &&
           std::fabs(z) <= kTidewaterFarmSite.lot_depth_m * 0.5f + margin_m;
}

inline bool tidewater_farm_ground_piece(const StartPart& part) {
    return part.name &&
           (std::strcmp(part.name, "farm house floor") == 0 ||
            std::strcmp(part.name, "farm house porch") == 0 ||
            std::strcmp(part.name, "farm barn floor") == 0 ||
            std::strcmp(part.name, "farm barn threshold") == 0 ||
            std::strcmp(part.name, "farm yard drive") == 0);
}

inline std::vector<StartPart> bake_tidewater_farm(GroundSampler ground) {
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f, float pitch_deg = 0.0f,
                         float roll_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish, solid};
        part.yaw_deg = yaw_deg;
        part.pitch_deg = pitch_deg;
        part.roll_deg = roll_deg;
        out.push_back(part);
    };
    const auto height = [&](float x, float z) {
        const auto p = tidewater_farm_world({x, z});
        return ground.at(p.x, p.y);
    };
    const auto bounds = [&](float cx, float cz, float width, float depth) {
        glm::vec2 range{1000.0f, -1000.0f};
        for (float x = cx - width * 0.5f; x <= cx + width * 0.5f + .01f; x += 1.0f)
            for (float z = cz - depth * 0.5f; z <= cz + depth * 0.5f + .01f; z += 1.0f) {
                const float y = height(x, z);
                range.x = std::min(range.x, y);
                range.y = std::max(range.y, y);
            }
        return range;
    };

    // The first piece deliberately names the house. World uses that prefix to
    // keep the natural yard as grass instead of painting the whole lot rock.
    const glm::vec2 house_ground = bounds(32.0f, -38.0f, 18.0f, 14.0f);
    const float house_floor = house_ground.y + 0.14f;
    add("farm house stone foundation", 32, -38, house_ground.x - .12f, 18,
        house_floor - house_ground.x + .12f, 14, StartFinish::Brick, true);
    add("farm house floor", 32, -38, house_floor - .12f, 18, .12f, 14,
        StartFinish::WarmWall, true);
    const BuildingOpening house_front[] = {
        {"farm house front window", OpeningKind::Window, 4.0f, 2.5f, .78f,
         1.55f, StartFinish::Glass, 1, 1, StartFinish::White},
        {"farm house front door", OpeningKind::Door, 9.0f, 1.6f, 0.0f, 2.35f,
         StartFinish::TealDoor, 0, 0, StartFinish::White, true},
        {"farm house kitchen window", OpeningKind::Window, 14.0f, 2.5f, .78f,
         1.55f, StartFinish::Glass, 1, 1, StartFinish::White},
    };
    const BuildingOpening house_rear[] = {
        {"farm house rear window west", OpeningKind::Window, 4.5f, 2.2f, .85f,
         1.45f, StartFinish::Glass, 1, 0, StartFinish::White},
        {"farm house rear window east", OpeningKind::Window, 13.5f, 2.2f, .85f,
         1.45f, StartFinish::Glass, 1, 0, StartFinish::White},
    };
    const BuildingWall house_walls[] = {
        {"farm house front wall", {23, -31}, {41, -31}, house_floor, 3.15f,
         .24f, StartFinish::WarmWall, house_front, 3},
        {"farm house east wall", {41, -31}, {41, -45}, house_floor, 3.15f,
         .24f, StartFinish::WarmWall},
        {"farm house rear wall", {41, -45}, {23, -45}, house_floor, 3.15f,
         .24f, StartFinish::WarmWall, house_rear, 2},
        {"farm house west wall", {23, -45}, {23, -31}, house_floor, 3.15f,
         .24f, StartFinish::WarmWall},
    };
    const BuildingRoof house_roof{
        "farm house gable roof", {32, -38}, house_floor + 3.15f, 18, 14,
        1.8f, .18f, .55f, 0, RoofStyle::Gable, RidgeAxis::AlongX,
        StartFinish::DarkRoof, StartFinish::WarmWall, .24f,
        StartFinish::WarmWall};
    auto house = bake_building({"Tidewater farmhouse", house_walls, 4,
                                &house_roof, 1});
    out.insert(out.end(), house.begin(), house.end());
    add("farm house porch", 32, -29.8f, house_floor - .12f, 5.0f, .12f,
        2.2f, StartFinish::Concrete, true);
    for (float x : {29.9f, 34.1f})
        add("farm house porch post", x, -29.0f, house_floor, .14f, 2.6f,
            .14f, StartFinish::White, true);
    add("farm house porch roof", 32, -29.4f, house_floor + 2.6f, 5.4f, .16f,
        2.8f, StartFinish::DarkRoof);
    add("farm house chimney", 38.5f, -35.5f, house_floor + 3.0f, 1.0f, 2.6f,
        1.0f, StartFinish::Brick);

    // The barn is genuinely open. The seven-metre northern doorway is a wall
    // subtraction and the drive, threshold, and floor share collision support.
    const glm::vec2 barn_ground = bounds(-30.0f, -35.0f, 34.0f, 24.0f);
    const float barn_floor = barn_ground.y + .16f;
    add("farm barn stone foundation", -30, -35, barn_ground.x - .12f, 34,
        barn_floor - barn_ground.x + .12f, 24, StartFinish::Brick, true);
    add("farm barn floor", -30, -35, barn_floor - .12f, 34, .12f, 24,
        StartFinish::Concrete, true);
    const BuildingOpening barn_front[] = {
        {"farm barn open double door", OpeningKind::Door, 17.0f, 7.0f, 0.0f,
         4.6f, StartFinish::DarkRoof, 0, 0, StartFinish::White, false},
    };
    const BuildingWall barn_walls[] = {
        {"farm barn front wall", {-47, -23}, {-13, -23}, barn_floor, 5.8f,
         .30f, StartFinish::RedTrim, barn_front, 1},
        {"farm barn east wall", {-13, -23}, {-13, -47}, barn_floor, 5.8f,
         .30f, StartFinish::RedTrim},
        {"farm barn rear wall", {-13, -47}, {-47, -47}, barn_floor, 5.8f,
         .30f, StartFinish::RedTrim},
        {"farm barn west wall", {-47, -47}, {-47, -23}, barn_floor, 5.8f,
         .30f, StartFinish::RedTrim},
    };
    const BuildingRoof barn_roof{
        "farm barn gable roof", {-30, -35}, barn_floor + 5.8f, 34, 24,
        3.7f, .22f, .8f, 0, RoofStyle::Gable, RidgeAxis::AlongX,
        StartFinish::DarkRoof, StartFinish::RedTrim, .30f,
        StartFinish::RedTrim};
    auto barn = bake_building({"Tidewater barn", barn_walls, 4, &barn_roof, 1});
    out.insert(out.end(), barn.begin(), barn.end());
    add("farm barn threshold", -30, -23.0f, barn_floor - .12f, 6.8f, .12f,
        1.2f, StartFinish::Concrete, true);
    // Pale battens and parked sliding leaves give the big red shell a working
    // scale while keeping the seven-metre doorway genuinely open.
    for (float x : {-45.5f, -41.0f, -36.5f, -23.5f, -19.0f, -14.5f})
        add("farm barn pale batten", x, -22.80f, barn_floor + .15f, .11f,
            5.35f, .10f, StartFinish::White);
    for (float x : {-38.5f, -21.5f}) {
        add("farm barn parked sliding door", x, -22.72f, barn_floor + .05f,
            5.0f, 4.45f, .14f, StartFinish::RedTrim);
        for (float edge : {-2.35f, 2.35f})
            add("farm barn door edge", x + edge, -22.62f, barn_floor + .08f,
                .14f, 4.30f, .10f, StartFinish::White);
        for (float y : {.10f, 4.18f})
            add("farm barn door rail", x, -22.60f, barn_floor + y, 4.82f,
                .14f, .10f, StartFinish::White);
        add("farm barn door diagonal brace", x, -22.54f, barn_floor + .22f,
            5.35f, .13f, .10f, StartFinish::White, false, 0.0f, 0.0f,
            x < -30.0f ? -40.0f : 40.0f);
    }
    add("farm barn loft hatch", -30.0f, -22.68f, barn_floor + 4.75f, 2.5f,
        2.0f, .15f, StartFinish::RedTrim);
    add("farm barn loft hatch crossbar", -30.0f, -22.57f,
        barn_floor + 5.62f, 2.32f, .11f, .10f, StartFinish::White);
    add("farm barn hoist beam", -30.0f, -21.80f, barn_floor + 7.0f, .16f,
        .18f, 1.8f, StartFinish::WarmWall);
    add("farm barn galvanized gutter", -30.0f, -22.48f,
        barn_floor + 5.63f, 34.2f, .15f, .16f, StartFinish::Steel);
    add("farm barn galvanized downspout", -46.4f, -22.25f,
        barn_floor + .20f, .16f, 5.45f, .16f, StartFinish::Steel);
    add("farm galvanized rain barrel", -46.0f, -20.8f,
        height(-46.0f, -20.8f), 1.15f, 1.35f, 1.15f,
        StartFinish::Steel, true);
    for (float x : {-42.0f, -18.0f}) {
        add("farm barn stall rail", x, -31.5f, barn_floor + .85f, .15f, .15f,
            8.0f, StartFinish::WarmWall, true);
        add("farm barn hay bale", x, -27.0f, barn_floor, 2.2f, 1.35f, 1.4f,
            StartFinish::Yellow, true);
    }

    // A packed-earth drive ties the road-end gate to both buildings. It is
    // segmented and sampled so the visible surface stays on the terrain.
    for (int i = 0; i < 102; ++i) {
        const float z = 78.5f - static_cast<float>(i) * 1.55f;
        add("farm yard drive", 0, z, height(0, z) + .025f, 6.0f, .055f,
            1.6f, StartFinish::Brick);
    }
    for (int i = 0; i < 17; ++i) {
        const float x = -2.0f - static_cast<float>(i) * 1.55f;
        add("farm yard drive", x, -20.0f, height(x, -20.0f) + .025f,
            1.6f, .055f, 6.0f, StartFinish::Brick);
    }
    for (int i = 0; i < 20; ++i) {
        const float x = 2.0f + static_cast<float>(i) * 1.55f;
        add("farm yard drive", x, -27.5f, height(x, -27.5f) + .025f,
            1.6f, .055f, 4.0f, StartFinish::Brick);
    }

    // Working-yard clutter tells the waterfront-market story without closing
    // the truck loop. The damp patches are visual-only ground decals.
    for (float x : {-7.5f, 7.5f})
        for (int i = 0; i < 12; ++i) {
            const float z = -17.0f + static_cast<float>(i) * 7.0f;
            add("farm wheel rut", x, z, height(x, z) + .022f, .32f, .035f,
                5.4f, StartFinish::Brick);
        }
    add("farm drainage swale", -31.0f, -2.0f, height(-31.0f, -2.0f) + .012f,
        50.0f, .028f, 1.15f, StartFinish::Brick);
    add("farm drainage swale", 31.0f, -2.0f, height(31.0f, -2.0f) + .012f,
        50.0f, .028f, 1.15f, StartFinish::Brick);
    for (float x : {-2.2f, -0.7f, .8f, 2.3f})
        add("farm drainage crossing plank", x, -2.0f,
            height(x, -2.0f) + .05f, 1.25f, .12f, 2.0f,
            StartFinish::WarmWall, true);

    for (int stack = 0; stack < 6; ++stack) {
        const float x = 7.8f + static_cast<float>(stack % 3) * 1.35f;
        const float z = -15.8f + static_cast<float>(stack / 3) * 1.25f;
        add("farm produce crate", x, z, height(x, z) + .03f,
            1.1f, .72f, .9f, StartFinish::WarmWall, true,
            stack == 5 ? 12.0f : 0.0f);
    }
    const float cart_y = height(-8.5f, -13.0f);
    add("farm produce cart bed", -8.5f, -13.0f, cart_y + .75f, 2.7f, .28f,
        1.45f, StartFinish::WarmWall, true, 15.0f);
    for (float z : {-13.85f, -12.15f})
        add("farm produce cart wheel", -8.5f, z, cart_y + .12f, 1.25f,
            1.25f, .16f, StartFinish::DarkRoof, true, 15.0f);
    add("farm produce cart drawbar", -6.4f, -12.45f, cart_y + .52f, 2.7f,
        .16f, .16f, StartFinish::WarmWall, false, 15.0f);

    const float wash_y = height(16.5f, -18.0f);
    add("farm wash table top", 16.5f, -18.0f, wash_y + 1.05f, 3.4f, .18f,
        1.35f, StartFinish::WarmWall, true);
    for (float x : {15.1f, 17.9f})
        for (float z : {-18.45f, -17.55f})
            add("farm wash table leg", x, z, wash_y + .04f, .14f, 1.02f,
                .14f, StartFinish::WarmWall, true);
    for (float x : {15.8f, 17.2f})
        add("farm galvanized wash tub", x, -18.0f, wash_y + 1.23f, 1.05f,
            .46f, .82f, StartFinish::Steel);
    add("farm damp patch", 16.5f, -18.0f, wash_y + .015f, 4.8f, .025f,
        2.8f, StartFinish::Brick);

    // Two crop blocks leave the centre lane open. The four repeating row types
    // have deliberately different silhouettes: tall corn, fine ripe grain,
    // broad leafy greens, and low pumpkin vines. Placement variation comes from
    // a tiny integer hash so the field stays identical across runs and replays.
    const auto crop_jitter = [](int row, int plant, int salt) {
        std::uint32_t h = static_cast<std::uint32_t>(row + 17) * 0x45d9f3bu;
        h ^= static_cast<std::uint32_t>(plant + 31) * 0x119de1f3u;
        h ^= static_cast<std::uint32_t>(salt + 7) * 0x27d4eb2du;
        h ^= h >> 16u;
        return static_cast<float>(h & 1023u) / 1023.0f - .5f;
    };
    for (float side : {-1.0f, 1.0f}) {
        for (int row = 0; row < 7; ++row) {
            const float x = side * (9.0f + static_cast<float>(row) * 7.0f);
            const int field_row = row + (side > 0.0f ? 7 : 0);
            add("farm crop soil furrow", x, 33.0f, height(x, 33.0f) + .018f,
                3.7f, .045f,
                64.0f, StartFinish::Brick);

            const int crop = field_row % 4;
            if (crop == 0) {
                // Widely spaced stalks read above the neighboring low crops.
                for (int plant = 0; plant < 12; ++plant) {
                    const float px = x + crop_jitter(field_row, plant, 0) * .50f;
                    const float pz = 4.2f + static_cast<float>(plant) * 5.15f +
                                     crop_jitter(field_row, plant, 1) * .60f;
                    const float y = height(px, pz);
                    const float stalk_h = 1.55f +
                                          crop_jitter(field_row, plant, 2) * .24f;
                    add("farm corn stalk", px, pz, y + .04f, .16f, stalk_h,
                        .16f, StartFinish::TealDoor);
                    add("farm corn leaf", px, pz, y + .48f, 1.05f, .10f,
                        .18f, StartFinish::TealDoor, false,
                        18.0f + crop_jitter(field_row, plant, 3) * 12.0f);
                    add("farm corn leaf", px, pz, y + .88f, .18f, .09f,
                        .92f, StartFinish::TealDoor, false,
                        -15.0f + crop_jitter(field_row, plant, 4) * 12.0f);
                    add("farm corn tassel", px, pz, y + stalk_h, .28f, .20f,
                        .28f, StartFinish::Yellow);
                }
            } else if (crop == 1) {
                // Grain is planted as narrow bunches with bright seed heads.
                for (int plant = 0; plant < 16; ++plant) {
                    const float px = x + crop_jitter(field_row, plant, 5) * 1.35f;
                    const float pz = 3.2f + static_cast<float>(plant) * 3.82f +
                                     crop_jitter(field_row, plant, 6) * .42f;
                    const float y = height(px, pz);
                    const float grain_h = .68f +
                                          crop_jitter(field_row, plant, 7) * .12f;
                    add("farm grain stems", px, pz, y + .04f, .34f, grain_h,
                        .34f, StartFinish::WarmWall, false,
                        crop_jitter(field_row, plant, 8) * 18.0f);
                    add("farm grain heads", px, pz, y + grain_h, .48f, .18f,
                        .48f, StartFinish::Yellow, false,
                        crop_jitter(field_row, plant, 9) * 18.0f);
                }
            } else if (crop == 2) {
                // Crossed blades make each leafy plant a small star from above.
                for (int plant = 0; plant < 14; ++plant) {
                    const float px = x + crop_jitter(field_row, plant, 10) * .90f;
                    const float pz = 3.8f + static_cast<float>(plant) * 4.42f +
                                     crop_jitter(field_row, plant, 11) * .45f;
                    const float y = height(px, pz);
                    const float yaw = crop_jitter(field_row, plant, 12) * 22.0f;
                    add("farm leafy crop blade", px, pz, y + .055f, 1.22f,
                        .18f, .27f, StartFinish::TealDoor, false, yaw + 25.0f);
                    add("farm leafy crop blade", px, pz, y + .075f, .27f,
                        .20f, 1.12f, StartFinish::TealDoor, false, yaw - 25.0f);
                    add("farm leafy crop heart", px, pz, y + .12f, .42f,
                        .32f, .42f, StartFinish::Yellow);
                }
            } else {
                // A continuous vine and chunky fruit keep these rows visibly
                // distinct without spending geometry on round high-poly gourds.
                for (int vine = 0; vine < 20; ++vine) {
                    const float pz = 2.6f + static_cast<float>(vine) * 3.18f;
                    add("farm pumpkin vine", x, pz, height(x, pz) + .05f,
                        .20f, .10f, 3.05f, StartFinish::TealDoor, false,
                        crop_jitter(field_row, vine, 13) * 8.0f);
                }
                for (int plant = 0; plant < 10; ++plant) {
                    const float px = x + crop_jitter(field_row, plant, 14) * 1.75f;
                    const float pz = 4.4f + static_cast<float>(plant) * 6.10f +
                                     crop_jitter(field_row, plant, 15) * .65f;
                    const float y = height(px, pz);
                    const float size = .62f +
                                       crop_jitter(field_row, plant, 16) * .16f;
                    add("farm pumpkin fruit", px, pz, y + .07f, size, .46f,
                        size, StartFinish::Yellow, false, 45.0f);
                    add("farm pumpkin stem", px, pz, y + .50f, .12f, .20f,
                        .12f, StartFinish::TealDoor);
                }
            }
        }
    }

    // Sparse post-and-rail boundary with a wide north gate for vehicles.
    for (float x = -60.0f; x <= 60.01f; x += 5.0f) {
        if (std::fabs(x) < 4.0f) continue;
        for (float z : {-78.0f, 78.0f})
            add("farm fence post", x, z, height(x, z), .14f, 1.15f, .14f,
                StartFinish::WarmWall, true);
    }
    for (float z = -73.0f; z <= 73.01f; z += 5.0f)
        for (float x : {-60.0f, 60.0f})
            add("farm fence post", x, z, height(x, z), .14f, 1.15f, .14f,
                StartFinish::WarmWall, true);
    for (float x : {-4.2f, 4.2f})
        add("farm gate post", x, -78.0f, height(x, -78.0f), .24f, 1.5f,
            .24f, StartFinish::White, true);

    // The old tank is now a small braced water tower: tall enough to mark the
    // farm from the road, but still subordinate to the barn roofline.
    const float tank_y = height(52.0f, -56.0f);
    for (float x : {50.25f, 53.75f})
        for (float z : {-57.75f, -54.25f})
            add("farm water tank steel leg", x, z, tank_y, .24f, 3.0f,
                .24f, StartFinish::Steel, true);
    for (float z : {-57.88f, -54.12f}) {
        add("farm water tank cross brace", 52.0f, z, tank_y + .18f, 4.7f,
            .16f, .12f, StartFinish::Steel, false, 0.0f, 0.0f, 30.0f);
        add("farm water tank cross brace", 52.0f, z, tank_y + .18f, 4.7f,
            .16f, .12f, StartFinish::Steel, false, 0.0f, 0.0f, -30.0f);
    }
    add("farm water tank", 52.0f, -56.0f, tank_y + 3.0f, 4.2f,
        4.8f, 4.2f, StartFinish::Steel, true);
    add("farm water tank cap", 52.0f, -56.0f, tank_y + 7.8f,
        4.5f, .18f, 4.5f, StartFinish::DarkRoof);
    for (int rung = 0; rung < 12; ++rung)
        add("farm water tank ladder rung", 49.78f, -56.0f,
            tank_y + .40f + static_cast<float>(rung) * .58f, .12f, .10f,
            1.0f, StartFinish::Steel);
    add("farm galvanized tank pipe", 49.7f, -53.2f, tank_y + .05f, .18f,
        4.0f, .18f, StartFinish::Steel);
    add("farm galvanized pump housing", 47.5f, -52.0f,
        height(47.5f, -52.0f), 1.8f, 1.45f, 1.5f, StartFinish::Steel, true);
    return out;
}

}  // namespace apricot::city

#include <algorithm>
#include <cmath>
#include <cstring>

#include "city/roads.h"
#include "city/tidewater_farm.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void farm_has_working_parts() {
    const TerrainGround ground{city::kMapSeed};
    const auto parts = city::bake_tidewater_farm(ground.sampler());
    int barn = 0, house = 0, soil = 0;
    int corn_stalks = 0, corn_leaves = 0, corn_tassels = 0;
    int grain_stems = 0, grain_heads = 0;
    int leafy_blades = 0, leafy_hearts = 0;
    int pumpkin_vines = 0, pumpkin_fruit = 0, pumpkin_stems = 0;
    int open_barn_dressing = 0;
    int barn_details = 0, yard_props = 0, tower_parts = 0;
    for (const auto& part : parts) {
        REQUIRE(part.name != nullptr);
        REQUIRE(part.width_m > 0 && part.height_m > 0 && part.depth_m > 0);
        barn += std::strstr(part.name, "farm barn") != nullptr;
        house += std::strstr(part.name, "farm house") != nullptr;
        soil += std::strcmp(part.name, "farm crop soil furrow") == 0;
        corn_stalks += std::strcmp(part.name, "farm corn stalk") == 0;
        corn_leaves += std::strcmp(part.name, "farm corn leaf") == 0;
        corn_tassels += std::strcmp(part.name, "farm corn tassel") == 0;
        grain_stems += std::strcmp(part.name, "farm grain stems") == 0;
        grain_heads += std::strcmp(part.name, "farm grain heads") == 0;
        leafy_blades += std::strcmp(part.name, "farm leafy crop blade") == 0;
        leafy_hearts += std::strcmp(part.name, "farm leafy crop heart") == 0;
        pumpkin_vines += std::strcmp(part.name, "farm pumpkin vine") == 0;
        pumpkin_fruit += std::strcmp(part.name, "farm pumpkin fruit") == 0;
        pumpkin_stems += std::strcmp(part.name, "farm pumpkin stem") == 0;
        open_barn_dressing +=
            std::strcmp(part.name, "farm barn open double door") == 0;
        barn_details += std::strcmp(part.name, "farm barn pale batten") == 0 ||
                        std::strcmp(part.name,
                                    "farm barn parked sliding door") == 0 ||
                        std::strcmp(part.name, "farm barn loft hatch") == 0 ||
                        std::strcmp(part.name,
                                    "farm barn galvanized gutter") == 0;
        yard_props += std::strcmp(part.name, "farm produce crate") == 0 ||
                      std::strcmp(part.name, "farm produce cart bed") == 0 ||
                      std::strcmp(part.name, "farm wash table top") == 0 ||
                      std::strcmp(part.name,
                                  "farm galvanized wash tub") == 0;
        tower_parts += std::strstr(part.name, "farm water tank") != nullptr ||
                       std::strcmp(part.name,
                                   "farm galvanized pump housing") == 0;
        REQUIRE_MSG(std::fabs(part.centre.x) + part.width_m * .5f <=
                        city::kTidewaterFarmSite.lot_width_m * .5f + .01f,
                    "farm piece leaves parcel", part.name);
        REQUIRE_MSG(std::fabs(part.centre.z) + part.depth_m * .5f <=
                        city::kTidewaterFarmSite.lot_depth_m * .5f + .01f,
                    "farm piece leaves parcel", part.name);
    }
    REQUIRE(barn >= 12);
    REQUIRE(house >= 12);
    REQUIRE(soil == 14);
    REQUIRE(corn_stalks == 48);
    REQUIRE(corn_leaves == 96);
    REQUIRE(corn_tassels == 48);
    REQUIRE(grain_stems == 64);
    REQUIRE(grain_heads == 64);
    REQUIRE(leafy_blades == 84);
    REQUIRE(leafy_hearts == 42);
    REQUIRE(pumpkin_vines == 60);
    REQUIRE(pumpkin_fruit == 30);
    REQUIRE(pumpkin_stems == 30);
    // An open door bakes two jambs and one head. A fourth piece would be the
    // leaf filling the cutout.
    REQUIRE(open_barn_dressing == 3);
    REQUIRE(barn_details >= 10);
    REQUIRE(yard_props == 10);
    REQUIRE(tower_parts >= 20);
    apricot_test::pass("farm bakes four distinct crops across fourteen rows");
}

void crop_layout_is_deterministic_terrain_following_and_drive_safe() {
    const TerrainGround ground{city::kMapSeed};
    const auto sampler = ground.sampler();
    const auto first = city::bake_tidewater_farm(sampler);
    const auto second = city::bake_tidewater_farm(sampler);
    REQUIRE(first.size() == second.size());

    int grounded_crop_pieces = 0;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto& part = first[i];
        const auto& repeat = second[i];
        REQUIRE(std::strcmp(part.name, repeat.name) == 0);
        REQUIRE_NEAR(part.centre.x, repeat.centre.x, .00001f);
        REQUIRE_NEAR(part.centre.z, repeat.centre.z, .00001f);
        REQUIRE_NEAR(part.bottom_m, repeat.bottom_m, .00001f);
        REQUIRE_NEAR(part.yaw_deg, repeat.yaw_deg, .00001f);

        const bool crop_piece = std::strstr(part.name, "farm crop") != nullptr ||
                                std::strstr(part.name, "farm corn") != nullptr ||
                                std::strstr(part.name, "farm grain") != nullptr ||
                                std::strstr(part.name, "farm leafy") != nullptr ||
                                std::strstr(part.name, "farm pumpkin") != nullptr;
        if (!crop_piece) continue;
        REQUIRE(!part.solid);
        // The drive is six metres wide. Even the broad leaves and soil beds
        // retain another metre of shoulder on either side.
        REQUIRE(std::fabs(part.centre.x) - part.width_m * .5f > 7.0f);

        const bool rooted = std::strcmp(part.name, "farm crop soil furrow") == 0 ||
                            std::strcmp(part.name, "farm corn stalk") == 0 ||
                            std::strcmp(part.name, "farm grain stems") == 0 ||
                            std::strcmp(part.name, "farm leafy crop blade") == 0 ||
                            std::strcmp(part.name, "farm pumpkin vine") == 0 ||
                            std::strcmp(part.name, "farm pumpkin fruit") == 0;
        if (!rooted) continue;
        const auto world = city::tidewater_farm_world(
            {part.centre.x, part.centre.z});
        const float ground_y = sampler.at(world.x, world.y);
        REQUIRE(part.bottom_m >= ground_y);
        REQUIRE(part.bottom_m - ground_y < .13f);
        ++grounded_crop_pieces;
    }
    REQUIRE(grounded_crop_pieces == 300);
    apricot_test::pass("crop variety is deterministic, grounded, and drive-safe");
}

void farm_track_connects_old_tide_without_crossing_route_one() {
    const city::Road* old_tide = nullptr;
    const city::Road* track = nullptr;
    for (const auto& road : city::kRoads) {
        if (road.id == 63) old_tide = &road;
        if (road.id == city::kTidewaterFarmTrackRoadId) track = &road;
    }
    REQUIRE(old_tide != nullptr);
    REQUIRE(track != nullptr);
    REQUIRE(track->cls == city::RoadClass::Dirt);
    REQUIRE_NEAR(track->path[0].x, old_tide->path[0].x, .001f);
    REQUIRE_NEAR(track->path[0].z, old_tide->path[0].z, .001f);
    REQUIRE_NEAR(track->path[track->count - 1].x,
                 city::kTidewaterFarmSite.origin.x, .001f);
    REQUIRE_NEAR(track->path[track->count - 1].z,
                 city::kTidewaterFarmSite.origin.z +
                     city::kTidewaterFarmSite.lot_depth_m * .5f, .001f);
    for (int i = 0; i < track->count; ++i) {
        // Route 1 is west of the full spur; the closest authored centreline
        // remains over 100 m away, so this is not an at-grade freeway crossing.
        float route_distance = 10000.0f;
        for (const auto& road : city::kRoads) {
            if (road.id != 3) continue;
            for (int segment = 0; segment + 1 < road.count; ++segment) {
                const glm::vec2 a{road.path[segment].x, road.path[segment].z};
                const glm::vec2 b{road.path[segment + 1].x,
                                  road.path[segment + 1].z};
                const glm::vec2 p{track->path[i].x, track->path[i].z};
                const glm::vec2 d = b - a;
                const float t = std::clamp(glm::dot(p - a, d) /
                                           glm::dot(d, d), 0.0f, 1.0f);
                route_distance = std::min(route_distance,
                                          glm::length(p - (a + d * t)));
            }
        }
        REQUIRE(route_distance > 65.0f);
    }
    apricot_test::pass("farm track joins Old Tide and stays clear of Route 1");
}

void parcel_clears_existing_roads_and_suppresses_scatter() {
    for (const auto& road : city::kRoads) {
        if (road.id == city::kTidewaterFarmTrackRoadId) continue;
        for (int segment = 0; segment + 1 < road.count; ++segment) {
            const glm::vec2 a{road.path[segment].x, road.path[segment].z};
            const glm::vec2 b{road.path[segment + 1].x,
                              road.path[segment + 1].z};
            const glm::vec2 d = b - a;
            for (float x : {-62.5f, 0.0f, 62.5f})
                for (float z : {-80.0f, 0.0f, 80.0f}) {
                    const auto p = city::tidewater_farm_world({x, z});
                    const float t = std::clamp(glm::dot(p - a, d) /
                                               glm::dot(d, d), 0.0f, 1.0f);
                    REQUIRE_MSG(glm::length(p - (a + d * t)) >
                                    road.ribbon_half_m() + 2.0f,
                                "farm parcel crosses existing road", road.name);
                }
        }
    }
    for (float x : {-60.0f, 0.0f, 60.0f})
        for (float z : {-77.0f, 0.0f, 77.0f}) {
            const auto p = city::tidewater_farm_world({x, z});
            REQUIRE_NEAR(city::wild_scatter_at(p.x, p.y), 0.0f, .0001f);
        }
    apricot_test::pass("farm parcel clears roads and excludes random trees");
}

}  // namespace

int main() {
    farm_has_working_parts();
    crop_layout_is_deterministic_terrain_following_and_drive_safe();
    farm_track_connects_old_tide_without_crossing_route_one();
    parcel_clears_existing_roads_and_suppresses_scatter();
    return apricot_test::done("tidewater_farm_tests");
}

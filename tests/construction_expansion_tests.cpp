#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/construction_expansion.h"
#include "city/construction_site.h"
#include "city/construction_neighbor_materials.h"
#include "city/construction_neighbor_equipment.h"
#include "city/neighborhood_towers.h"
#include "city/roads.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec2 world(const city::StartSite& site, glm::vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

float road_clearance(glm::vec2 p) {
    float clearance = 1e9f;
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const glm::vec2 a{road.path[i - 1].x, road.path[i - 1].z};
            const glm::vec2 b{road.path[i].x, road.path[i].z};
            const glm::vec2 delta = b - a;
            const float t = std::clamp(glm::dot(p - a, delta) /
                                           glm::dot(delta, delta),
                                       0.0f, 1.0f);
            const float half = road.width_m > 0.0f
                                   ? road.width_m * 0.5f
                                   : city::road_width_m(road.cls) * 0.5f;
            clearance = std::min(clearance,
                                 glm::length(p - a - delta * t) - half -
                                     city::kWalkWidthM);
        }
    }
    return clearance;
}

bool lots_are_separate(const city::StartSite& a, const city::StartSite& b,
                       float margin = 2.0f) {
    const glm::vec2 delta{b.origin.x - a.origin.x, b.origin.z - a.origin.z};
    const glm::vec2 local{a.cos_yaw * delta.x - a.sin_yaw * delta.y,
                          a.sin_yaw * delta.x + a.cos_yaw * delta.y};
    return std::fabs(local.x) >
               (a.lot_width_m + b.lot_width_m) * .5f + margin ||
           std::fabs(local.y) >
               (a.lot_depth_m + b.lot_depth_m) * .5f + margin;
}

void every_new_lot_clears_the_grid() {
    for (const auto& expansion : city::kAdditionalConstructionSites) {
        const auto& site = expansion.site;
        for (float x : {-site.lot_width_m * .5f, site.lot_width_m * .5f}) {
            for (float z : {-site.lot_depth_m * .5f, site.lot_depth_m * .5f}) {
                const glm::vec2 p = world(site, {x, z});
                REQUIRE_MSG(road_clearance(p) > 1.5f,
                            "construction expansion lot clips a road",
                            site.name);
                const float height = mesh_height_at(city::kMapSeed, p.x, p.y);
                if (std::fabs(height - site.ground_m) > .02f)
                    std::printf("  terrain %.3f at %s corner %.1f %.1f\n",
                                height, site.name, x, z);
                REQUIRE_NEAR(height, site.ground_m, .02f);
            }
        }
        for (const auto& tower : city::kNeighborhoodTowers)
            REQUIRE(lots_are_separate(site, tower.site));
        REQUIRE(lots_are_separate(site, city::kConstructionSite.site));
        REQUIRE(lots_are_separate(site,
                                  city::kConstructionNeighborMaterialsSite));
        REQUIRE(lots_are_separate(site,
                                  city::kConstructionNeighborEquipment.site));
    }
    for (std::size_t i = 0; i < city::kAdditionalConstructionSites.size(); ++i)
        for (std::size_t j = i + 1;
             j < city::kAdditionalConstructionSites.size(); ++j)
            REQUIRE(lots_are_separate(
                city::kAdditionalConstructionSites[i].site,
                city::kAdditionalConstructionSites[j].site));
    for (const auto& twin : city::kTwinSkyscraperBlockSites) {
        for (float x : {-twin.lot_width_m * .5f, twin.lot_width_m * .5f})
            for (float z : {-twin.lot_depth_m * .5f, twin.lot_depth_m * .5f}) {
                REQUIRE(road_clearance(world(twin, {x, z})) > 1.5f);
                const auto p = world(twin, {x, z});
                REQUIRE_NEAR(mesh_height_at(city::kMapSeed, p.x, p.y),
                             twin.ground_m, .02f);
            }
        for (const auto& tower : city::kNeighborhoodTowers)
            REQUIRE(lots_are_separate(twin, tower.site));
        REQUIRE(lots_are_separate(twin, city::kConstructionSite.site));
        REQUIRE(lots_are_separate(twin,
                                  city::kConstructionNeighborMaterialsSite));
        REQUIRE(lots_are_separate(twin,
                                  city::kConstructionNeighborEquipment.site));
    }
    REQUIRE(lots_are_separate(city::kTwinSkyscraperBlockSites[0],
                              city::kTwinSkyscraperBlockSites[1]));
    apricot_test::pass("new Vellum construction blocks clear the authored road grid and existing lots");
}

void four_sites_are_visibly_different() {
    const auto frame = city::bake_additional_construction_site(0);
    const auto deck = city::bake_additional_construction_site(1);
    const auto retrofit = city::bake_additional_construction_site(2);
    const auto demolition = city::bake_additional_construction_site(3);
    for (const auto* parts : {&frame, &deck, &retrofit, &demolition}) {
        REQUIRE(!parts->empty());
        REQUIRE(city::valid_start_parts(parts->data(), parts->size()));
        REQUIRE(parts->size() > 20u);
    }
    const auto has = [](const auto& parts, const char* text) {
        return std::any_of(parts.begin(), parts.end(), [&](const auto& part) {
            return std::strstr(part.name, text) != nullptr;
        });
    };
    REQUIRE(has(frame, "steel frame column"));
    REQUIRE(has(frame, "construction crane"));
    REQUIRE(has(deck, "deck concrete column"));
    REQUIRE(has(deck, "deck floor slab"));
    REQUIRE(has(retrofit, "scaffold upright"));
    REQUIRE(has(retrofit, "facade panel stack"));
    REQUIRE(has(demolition, "rubble pile"));
    REQUIRE(has(demolition, "excavator"));
    REQUIRE(frame.size() != deck.size() || deck.size() != retrofit.size());
    std::printf("  expansion pieces: frame=%zu deck=%zu retrofit=%zu demolition=%zu\n",
                frame.size(), deck.size(), retrofit.size(), demolition.size());
    apricot_test::pass("four added construction sites have distinct authored prop vocabularies");
}

void each_shared_block_has_two_towers() {
    for (std::size_t i = 0; i < city::kTwinSkyscraperBlockSites.size(); ++i) {
        const auto parts = city::bake_twin_skyscraper_block(i);
        REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
        std::size_t cores = 0;
        std::size_t crowns = 0;
        std::size_t window_lights = 0;
        float highest = 0.0f;
        for (const auto& part : parts) {
            cores += std::strcmp(part.name, "twin tower structural core") == 0;
            crowns += std::strcmp(part.name, "twin tower crown cap") == 0;
            window_lights +=
                std::strcmp(part.name, "twin tower office window light") == 0;
            highest = std::max(highest, part.bottom_m + part.height_m);
        }
        REQUIRE(cores == 2u);
        REQUIRE(crowns == 2u);
        const int floors = i == 0 ? 24 + 21 : 28 + 25;
        REQUIRE(window_lights == static_cast<std::size_t>(floors * 20));
        REQUIRE(highest > 75.0f);
    }
    apricot_test::pass("both shared blocks contain two independent skyscraper cores and crowns");
}

void low_parts_stay_inside_their_parcels() {
    const auto check = [](const city::StartSite& site,
                          const std::vector<city::StartPart>& parts) {
        for (const auto& part : parts) {
            if (part.bottom_m >= 3.0f) continue;
            const float yaw = glm::radians(part.yaw_deg);
            const float half_x = std::fabs(std::cos(yaw)) * part.width_m * .5f +
                                 std::fabs(std::sin(yaw)) * part.depth_m * .5f;
            const float half_z = std::fabs(std::sin(yaw)) * part.width_m * .5f +
                                 std::fabs(std::cos(yaw)) * part.depth_m * .5f;
            const float local_x = part.centre.x - site.lot_centre.x;
            const float local_z = part.centre.z - site.lot_centre.z;
            REQUIRE_MSG(std::fabs(local_x) + half_x <=
                            site.lot_width_m * .5f + .01f,
                        "low construction part escapes lot width", part.name);
            REQUIRE_MSG(std::fabs(local_z) + half_z <=
                            site.lot_depth_m * .5f + .01f,
                        "low construction part escapes lot depth", part.name);
        }
    };
    for (std::size_t i = 0; i < city::kAdditionalConstructionSites.size(); ++i)
        check(city::kAdditionalConstructionSites[i].site,
              city::bake_additional_construction_site(i));
    for (std::size_t i = 0; i < city::kTwinSkyscraperBlockSites.size(); ++i)
        check(city::kTwinSkyscraperBlockSites[i],
              city::bake_twin_skyscraper_block(i));
    apricot_test::pass("street-level construction pieces stay inside their parcels");
}

}  // namespace

int main() {
    every_new_lot_clears_the_grid();
    four_sites_are_visibly_different();
    each_shared_block_has_two_towers();
    low_parts_stay_inside_their_parcels();
    return apricot_test::done("construction_expansion_tests");
}

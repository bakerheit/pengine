#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

#include "city/miandi_streets.h"
#include "city/roads.h"
#include "test_assert.h"

using namespace apricot;

namespace {

const city::Road& miandi_road(uint32_t id) {
    for (const city::Road& road : city::kRoads) {
        if (road.id == id) return road;
    }
    REQUIRE_MSG(false, "missing Miandi road", "road lookup");
    return city::kRoads[0];
}

bool has_point(const city::Road& road, float x, float z) {
    for (int i = 0; i < road.count; ++i) {
        if (road.path[i].x == x && road.path[i].z == z) return true;
    }
    return false;
}

void ids_and_classes_are_stable() {
    const uint32_t ids[] = {
        city::kMiandiBiscayneBoulevardRoadId,
        city::kMiandiCalleOchoRoadId,
        city::kMiandiBayfrontAvenueRoadId,
        city::kMiandiCoralWayRoadId,
        city::kMiandiPortSolDriveRoadId,
        city::kMiandiPalmAvenueRoadId,
        city::kMiandiOceanDriveRoadId,
        city::kMiandiCausewayBoulevardRoadId,
        city::kMiandiGatewayDriveRoadId,
        city::kMiandiSolanaAvenueRoadId,
        city::kMiandiMangoAvenueRoadId,
        city::kMiandiRoyalPalmAvenueRoadId,
        city::kMiandiSeabreezeAvenueRoadId,
    };
    for (std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        REQUIRE(ids[i] >= 222 && ids[i] <= 234);
        for (std::size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); ++j)
            REQUIRE(ids[i] != ids[j]);
        const city::Road& road = miandi_road(ids[i]);
        REQUIRE(road.id == ids[i]);
        REQUIRE(road.shapes_ground);
        REQUIRE(road.path[0].y == 8.0f);
        for (int p = 1; p < road.count; ++p) REQUIRE(road.path[p].y == 8.0f);
    }
    REQUIRE(miandi_road(222).cls == city::RoadClass::Arterial);
    REQUIRE(miandi_road(224).cls == city::RoadClass::Arterial);
    REQUIRE(miandi_road(226).cls == city::RoadClass::Arterial);
    REQUIRE(miandi_road(228).cls == city::RoadClass::Arterial);
    REQUIRE(city::road_width_m(city::RoadClass::Arterial) == 22.0f);
    REQUIRE(city::road_width_m(city::RoadClass::Street) == 14.0f);
    for (uint32_t id : {223u, 225u, 227u, 229u, 230u, 231u, 232u, 233u, 234u})
        REQUIRE(miandi_road(id).cls == city::RoadClass::Street);
    apricot_test::pass("Miandi IDs, classes, and 8 m terrain support are stable");
}

void shared_nodes_are_exact_and_connected() {
    const city::Road& biscayne = miandi_road(222);
    REQUIRE(has_point(biscayne, 7000.0f, 8000.0f));
    REQUIRE(has_point(biscayne, 7100.0f, 8040.0f));
    REQUIRE(has_point(biscayne, 7300.0f, 8120.0f));
    REQUIRE(has_point(biscayne, 7500.0f, 8200.0f));
    REQUIRE(has_point(miandi_road(231), 7100.0f, 8040.0f));
    REQUIRE(has_point(miandi_road(232), 7300.0f, 8120.0f));

    for (uint32_t horizontal : {223u, 224u, 225u}) {
        const city::Road& road = miandi_road(horizontal);
        for (float x : {6900.0f, 7100.0f, 7300.0f, 7500.0f, 7700.0f,
                        7900.0f, 8100.0f})
            REQUIRE(has_point(road, x, horizontal == 223 ? 8200.0f
                                                           : horizontal == 224 ? 8400.0f
                                                                               : 8600.0f));
    }
    for (uint32_t vertical : {231u, 232u, 233u, 234u}) {
        const city::Road& road = miandi_road(vertical);
        const float x = vertical == 231 ? 7100.0f : vertical == 232 ? 7300.0f
                     : vertical == 233 ? 7700.0f : 7900.0f;
        for (float z : {8000.0f, 8200.0f, 8400.0f, 8600.0f, 8800.0f})
            REQUIRE(has_point(road, x, z));
    }
    const city::Road& highway = miandi_road(220);
    REQUIRE(highway.path[highway.count - 1].x == biscayne.path[0].x);
    REQUIRE(highway.path[highway.count - 1].z == biscayne.path[0].z);
    apricot_test::pass("Miandi junctions use exact shared graph nodes through road 220");
}

void fixture_bake_is_bounded_and_clear() {
    const auto pieces = city::bake_miandi_street_fixtures();
    REQUIRE(pieces.size() == city::kMiandiStreetFixtureCount);
    int light_poles = 0;
    int light_heads = 0;
    int shelter_posts = 0;
    for (const city::BuildingPiece& piece : pieces) {
        const std::string_view name = piece.name ? piece.name : "";
        const bool visual_only = name.find("crown") != std::string_view::npos ||
                                 name.find("light head") != std::string_view::npos ||
                                 name.find("shelter roof") != std::string_view::npos ||
                                 name.find("shelter back") != std::string_view::npos;
        if (visual_only) {
            REQUIRE(!piece.solid);
        } else {
            REQUIRE(piece.solid);
        }
        if (name.find("light pole") != std::string_view::npos) ++light_poles;
        if (name.find("light head") != std::string_view::npos) ++light_heads;
        if (name.find("shelter post") != std::string_view::npos) {
            ++shelter_posts;
            REQUIRE(piece.solid);
        }
        const float world_x = city::kMiandiStreetFixtureSite.origin.x +
                              piece.centre.x;
        const float world_z = city::kMiandiStreetFixtureSite.origin.z +
                              piece.centre.z;
        REQUIRE(world_x >= 6900.0f && world_x <= 8100.0f);
        REQUIRE(world_z >= 8000.0f && world_z <= 8800.0f);
        REQUIRE(piece.bottom_m >= 0.0f);
        REQUIRE(piece.bottom_m + piece.height_m <= 8.0f);
        if (!piece.solid) continue;
        // Check the complete authored grid with the widest sidewalk envelope
        // (14 m). This is deliberately conservative for street-class roads.
        float nearest_horizontal_gap = 10000.0f;
        for (float z : {8000.0f, 8200.0f, 8400.0f, 8600.0f, 8800.0f}) {
            const float gap = std::fabs(world_z - z) - piece.depth_m * 0.5f - 14.0f;
            REQUIRE(gap > 0.0f);
            nearest_horizontal_gap = std::min(nearest_horizontal_gap, gap);
        }
        float nearest_vertical_gap = 10000.0f;
        for (float x : {6900.0f, 7100.0f, 7300.0f, 7500.0f, 7700.0f,
                        7900.0f, 8100.0f}) {
            const float gap = std::fabs(world_x - x) - piece.width_m * 0.5f - 14.0f;
            REQUIRE(gap > 0.0f);
            nearest_vertical_gap = std::min(nearest_vertical_gap, gap);
        }
        // Keep at least a 4 m clear pedestrian lane between every solid and
        // its nearest road envelope, including the shelter posts.
        REQUIRE(std::min(nearest_horizontal_gap, nearest_vertical_gap) >= 4.0f);
        for (float x : {7000.0f, 7100.0f, 7300.0f, 7500.0f, 7700.0f,
                        7900.0f, 8100.0f}) {
            for (float z : {8000.0f, 8200.0f, 8400.0f, 8600.0f, 8800.0f}) {
                const float dx = world_x - x;
                const float dz = world_z - z;
                REQUIRE(std::sqrt(dx * dx + dz * dz) >= 24.0f);
            }
        }
    }
    REQUIRE(light_poles == 2);
    REQUIRE(light_heads == 2);
    REQUIRE(shelter_posts == 2);
    apricot_test::pass("Miandi fixture solids, open shelter, road envelopes, and pedestrian lane are valid");
}

}  // namespace

int main() {
    ids_and_classes_are_stable();
    shared_nodes_are_exact_and_connected();
    fixture_bake_is_bounded_and_clear();
    return apricot_test::done("miandi_streets_tests");
}

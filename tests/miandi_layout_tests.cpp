#include <array>
#include <cstdio>

#include "city/miandi_layout.h"
#include "city/roads.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot::city;

namespace {

const Road* road_by_id(uint32_t id) {
    for (const Road& road : kRoads)
        if (road.id == id) return &road;
    return nullptr;
}

void districts_are_separate_and_in_florangia() {
    for (std::size_t i = 0; i < kMiandiDistricts.size(); ++i) {
        const Boundary& a = kMiandiDistricts[i].boundary;
        const Vec2 amin = a.min_corner();
        const Vec2 amax = a.max_corner();
        REQUIRE(miandi_city_contains(miandi_world_point(amin).x,
                                     miandi_world_point(amin).z));
        REQUIRE(miandi_city_contains(miandi_world_point(amax).x,
                                     miandi_world_point(amax).z));
        for (std::size_t j = i + 1; j < kMiandiDistricts.size(); ++j) {
            const Boundary& b = kMiandiDistricts[j].boundary;
            const Vec2 bmin = b.min_corner();
            const Vec2 bmax = b.max_corner();
            const bool separated = amax.x < bmin.x || bmax.x < amin.x ||
                                   amax.z < bmin.z || bmax.z < amin.z;
            REQUIRE_MSG(separated, "Miandi district polygons overlap",
                        kMiandiDistricts[i].name);
        }
    }
    REQUIRE(kMiandiWorldOrigin.x > kFlorangiaMinXMetres);
    REQUIRE(kMiandiWorldOrigin.x < kFlorangiaMaxXMetres);
    REQUIRE(kMiandiWorldOrigin.z > kFlorangiaMinZMetres);
    REQUIRE(kMiandiWorldOrigin.z < kFlorangiaMaxZMetres);
    apricot_test::pass("Miandi districts occupy southeast Florangia");
}

bool roads_touch(const Road& a, const Road& b) {
    for (int i = 0; i < a.count; ++i)
        for (int j = 0; j < b.count; ++j)
            if (a.path[i].x == b.path[j].x && a.path[i].z == b.path[j].z)
                return true;
    return false;
}

void real_roads_connect_to_florangia_highway() {
    std::array<const Road*, 14> roads{};
    roads[0] = road_by_id(kFlorangiaHighwayRoadId);
    for (uint32_t id = 222; id <= 234; ++id)
        roads[static_cast<std::size_t>(id - 221)] = road_by_id(id);
    for (const Road* road : roads) REQUIRE(road != nullptr);

    std::array<bool, roads.size()> reached{};
    reached[0] = true;
    for (std::size_t pass = 0; pass < roads.size(); ++pass) {
        for (std::size_t i = 0; i < roads.size(); ++i) {
            if (!reached[i]) continue;
            for (std::size_t j = 0; j < roads.size(); ++j)
                if (!reached[j] && roads_touch(*roads[i], *roads[j]))
                    reached[j] = true;
        }
    }
    for (bool connected : reached) REQUIRE(connected);

    const Road& highway = *roads[0];
    const Road& biscayne = *roads[1];
    REQUIRE(highway.path[highway.count - 1].x == biscayne.path[0].x);
    REQUIRE(highway.path[highway.count - 1].z == biscayne.path[0].z);
    apricot_test::pass("thirteen Miandi roads join the Florangia Highway graph");
}

void city_plate_supports_roads_and_buildings() {
    std::size_t solids = 0;
    std::size_t lots = 0;
    float tallest = 0.0f;
    for (const StartPart& part : kMiandiBuildingParts) {
        REQUIRE(part.name != nullptr);
        REQUIRE(part.width_m > 0.0f && part.height_m > 0.0f &&
                part.depth_m > 0.0f);
        REQUIRE(part.centre.x - part.width_m * 0.5f >= -kMiandiHalfWidthM);
        REQUIRE(part.centre.x + part.width_m * 0.5f <= kMiandiHalfWidthM);
        REQUIRE(part.centre.z - part.depth_m * 0.5f >= -kMiandiHalfDepthM);
        REQUIRE(part.centre.z + part.depth_m * 0.5f <= kMiandiHalfDepthM);
        if (part.solid) ++solids;
        if (!part.solid && part.finish == StartFinish::Asphalt) ++lots;
        const float top = part.bottom_m + part.height_m;
        if (top > tallest) tallest = top;
        const Vec2 world = miandi_world_point(part.centre);
        REQUIRE_NEAR(height_at(kMapSeed, world.x, world.z), kMiandiGroundM,
                     0.001f);
    }
    REQUIRE(solids >= 17u);
    REQUIRE(lots >= 12u);
    REQUIRE(tallest >= 80.0f);

    for (uint32_t id = 222; id <= 234; ++id) {
        const Road& road = *road_by_id(id);
        for (int point = 0; point < road.count; ++point)
            REQUIRE_NEAR(height_at(kMapSeed, road.path[point].x,
                                   road.path[point].z),
                         road.path[point].y, 0.001f);
    }
    std::printf("  %zu massing pieces: %zu solid buildings, %zu block lots\n",
                kMiandiBuildingPartCount, solids, lots);
    apricot_test::pass("Miandi city plate supports roads and building massing");
}

void runtime_keeps_only_unreplaced_context_massing() {
    std::size_t retained = 0;
    std::size_t retained_solids = 0;
    for (const StartPart& part : kMiandiBuildingParts) {
        if (!miandi_keeps_rough_part(part)) continue;
        ++retained;
        retained_solids += part.solid;
        REQUIRE(part.centre.x <= -700.0f);
    }
    REQUIRE(retained == 4u);
    REQUIRE(retained_solids == 2u);
    apricot_test::pass("runtime suppresses replaced Miandi placeholder massing");
}

}  // namespace

int main() {
    districts_are_separate_and_in_florangia();
    real_roads_connect_to_florangia_highway();
    city_plate_supports_roads_and_buildings();
    runtime_keeps_only_unreplaced_context_massing();
    return apricot_test::done("miandi_layout_tests");
}

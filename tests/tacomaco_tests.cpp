#include <algorithm>
#include <cstring>
#include "city/building_access.h"
#include "city/burgerpiz_asset.h"
#include "city/spines.h"
#include "city/tacomaco.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    const auto& site=city::kTacomacoSite;
    const auto outside=city::burgerpiz_world({-24,0,15},site);
    REQUIRE(outside.z<site.origin.z);
    REQUIRE(std::strcmp(site.name,"TacoMaco")==0);
    REQUIRE(city::valid_building_plan(city::kTacomacoPlan));
    REQUIRE(city::kTacomacoPlan.wall_count==0); // No second Cloggers shell.
    REQUIRE(city::kTacomacoPlan.fixtures==city::kBurgerPizLotParts);
    TerrainGround ground{city::kMapSeed};
    RoadGraph graph;graph.build(city::map_spines(),{},ground.sampler());
    const auto ribbon=bake_ribbons(graph,ground.sampler());
    const auto lots=city::authored_building_access_lots();
    const auto access=city::bake_building_access(graph,ribbon,ground.sampler(),lots);
    unsigned count=0;
    for(std::size_t i=0;i<lots.size();++i)if(city::access_same_site(site,lots[i].site)) {
        ++count;
        REQUIRE(access.lots[i].connected);
        REQUIRE((access.lots[i].entrance.road_key>>32)==33u);
        REQUIRE(access.lots[i].entrance.sidewalk);
        REQUIRE_NEAR(lots[i].pavement.width_m,60,.001f);
        REQUIRE_NEAR(lots[i].pavement.depth_m,38,.001f);
        REQUIRE(!city::access_neighborhood_plot(site,lots[i].pavement));
        REQUIRE(!lots[i].parking_tracks_frontage);
        REQUIRE(city::access_clear_of_junctions(graph,access.lots[i].entrance.curb,3));
    }
    REQUIRE(count==1u); // Old drive-through exit/parking cuts are gone.
    apricot_test::pass("TacoMaco keeps north-facing Fourth Street access, fixed imported lot and no obsolete drive-through cuts");
    return apricot_test::done("tacomaco_tests");
}

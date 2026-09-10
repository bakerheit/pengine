#include <cstring>
#include "game/road_name.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    CurrentRoadName road;
    const char* name=road.update({-1450,-500});
    REQUIRE(name!=nullptr);REQUIRE(std::strcmp(name,"Route 1 - the Rimway")==0);
    // At the shared bridge endpoint, keep the current name. Once clearly on
    // the bridge, switch; this is the junction hysteresis players actually see.
    name=road.update({-1500,-1064});
    REQUIRE(name!=nullptr);REQUIRE(std::strcmp(name,"Route 1 - the Rimway")==0);
    name=road.update({-1500,-1080});
    REQUIRE(name!=nullptr);REQUIRE(std::strcmp(name,"the Kessel Bridge")==0);
    REQUIRE(road.update({-2500,2500})==nullptr);
    road.reset();
    // Stand on the road itself rather than at a typed-in coordinate. The old
    // probe {-1973, -614.8} sits 6.7 m off an 8 m road, outside the width/2 +
    // 1.5 m capture, so it named nothing; Boatworks' centreline is unchanged,
    // the probe simply never followed it. Sampling the authored centreline
    // checks the thing this test is for -- that being ON a named road reports
    // that road -- and cannot drift when the road is re-authored.
    const city::Road* boatworks=nullptr;
    for (const city::Road& candidate : city::kRoads)
        if (std::strcmp(candidate.name,"Boatworks Road")==0) boatworks=&candidate;
    REQUIRE(boatworks!=nullptr);
    REQUIRE(boatworks->count>=2);
    for (int i=0;i+1<boatworks->count;++i) {
        const auto& a=boatworks->path[i];
        const auto& b=boatworks->path[i+1];
        name=road.update({(a.x+b.x)*.5f,(a.z+b.z)*.5f});
        REQUIRE(name!=nullptr);
        REQUIRE(std::strcmp(name,"Boatworks Road")==0);
    }
    road.reset();
    REQUIRE(road.update({-2500,2500})==nullptr);
    apricot_test::pass("current-road label switches on-road, holds through a junction, and clears off-road");
    return apricot_test::done("road_name_tests");
}

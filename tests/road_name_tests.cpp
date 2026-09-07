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
    name=road.update({-1973,-614.8f});
    REQUIRE(name!=nullptr);REQUIRE(std::strcmp(name,"Boatworks Road")==0);
    road.reset();
    REQUIRE(road.update({-2500,2500})==nullptr);
    apricot_test::pass("current-road label switches on-road, holds through a junction, and clears off-road");
    return apricot_test::done("road_name_tests");
}

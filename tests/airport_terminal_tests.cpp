#include <cmath>
#include <cstring>
#include <vector>

#include "city/airport.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    std::vector<city::StartPart> details;
    city::append_airport_terminal_details(details);
    std::size_t seats=0, bins=0, directories=0, fins=0;
    for (const auto& part:details) {
        REQUIRE(part.width_m>0 && part.height_m>0 && part.depth_m>0);
        if (std::strcmp(part.name,"terminal furnishing bench seat")==0) ++seats;
        if (std::strcmp(part.name,"terminal furnishing litter bin")==0) ++bins;
        if (std::strcmp(part.name,"terminal architectural facade fin")==0) {
            ++fins;
            REQUIRE(part.bottom_m>=4.9f);
            REQUIRE(!part.solid);
        }
        if (std::strcmp(part.name,"terminal directory sign face")==0) {
            ++directories;
            REQUIRE_NEAR(part.width_m/part.height_m,2.f/3.f,1e-5f);
            REQUIRE(part.centre.x>-107.f && part.centre.x<-38.f);
            REQUIRE(part.centre.z>149.57f);
            REQUIRE(!part.solid);
        }
        if (!part.solid || part.bottom_m>2.5f) continue;
        // Real walkways retain a clear 6m door approach; furnishings never
        // narrow the continuous outer pedestrian strip beside the curb.
        REQUIRE(part.centre.z+part.depth_m*.5f<=150.6f);
        for (const float x : {-110.f,-35.f,40.f}) {
            REQUIRE(std::fabs(part.centre.x-x)>=3.f+part.width_m*.5f);
        }
    }
    REQUIRE(seats==6u);
    REQUIRE(bins==3u);
    REQUIRE(directories==1u);
    REQUIRE(fins==27u);
    const auto complete=city::bake_airport();
    bool found_totem=false;
    for (const auto& part:complete) {
        if (std::strcmp(part.name,"terminal zone totem centre")==0) {
            found_totem=true;
            REQUIRE(std::fabs(part.centre.x+35.f)>3.f+part.width_m*.5f);
        }
    }
    REQUIRE(found_totem);
    return apricot_test::done("airport_terminal_tests");
}

#include <cmath>
#include <cstring>

#include "city/florangia_airport.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    using namespace city;

    REQUIRE(std::strcmp(kFlorangiaAirportSite.name,
                        "Florangia Regional Airport") == 0);
    REQUIRE_NEAR(kFlorangiaAirportAccess.x, 4800.0f, 0.001f);
    REQUIRE_NEAR(kFlorangiaAirportAccess.z, 4700.0f, 0.001f);
    REQUIRE(florangia_airport_lot_contains(4800.0f, 4400.0f));
    REQUIRE(florangia_airport_lot_contains(kFlorangiaAirportAccess.x,
                                           kFlorangiaAirportAccess.z));

    const auto parts = bake_florangia_airport();
    std::size_t runway = 0;
    std::size_t dashes = 0;
    std::size_t thresholds = 0;
    std::size_t parking = 0;
    bool apron = false;
    bool terminal = false;
    bool tower = false;
    bool hangar = false;
    bool access = false;
    for (const StartPart& part : parts) {
        REQUIRE(part.name != nullptr);
        REQUIRE(part.width_m > 0.0f);
        REQUIRE(part.height_m > 0.0f);
        REQUIRE(part.depth_m > 0.0f);
        const float x = kFlorangiaAirportSite.origin.x + part.centre.x;
        const float z = kFlorangiaAirportSite.origin.z + part.centre.z;
        REQUIRE(florangia_airport_lot_contains(x, z, 0.1f));

        if (std::strcmp(part.name, "Florangia runway 08-26") == 0) {
            ++runway;
            REQUIRE_NEAR(part.width_m, 1000.0f, 0.001f);
            REQUIRE_NEAR(part.depth_m, 48.0f, 0.001f);
            REQUIRE(!part.solid);
        }
        if (std::strcmp(part.name,
                        "Florangia runway centreline dash") == 0) ++dashes;
        if (std::strcmp(part.name,
                        "Florangia runway threshold bar") == 0) ++thresholds;
        if (std::strcmp(part.name, "Florangia parking stripe") == 0) ++parking;
        apron |= std::strcmp(part.name, "Florangia passenger apron") == 0;
        terminal |= std::strcmp(part.name, "Florangia terminal hall") == 0;
        tower |= std::strcmp(part.name, "Florangia control tower cab") == 0;
        hangar |= std::strcmp(part.name,
                              "Florangia maintenance hangar") == 0;
        access |= std::strcmp(part.name,
                              "Florangia airport access road") == 0;

        // The centre six metres of the public approach must stay free of
        // solid authored geometry all the way to the parking field.
        if (part.solid && part.centre.z >= 205.0f) {
            REQUIRE(std::fabs(part.centre.x) >=
                    6.0f + part.width_m * 0.5f);
        }
    }
    REQUIRE(runway == 1u);
    REQUIRE(dashes == 19u);
    REQUIRE(thresholds == 12u);
    REQUIRE(parking >= 60u);
    REQUIRE(apron && terminal && tower && hangar && access);

    // The airport terrain plate must match the site support height across the
    // runway, apron, terminal frontage, and freeway access throat.
    constexpr uint64_t seed = city::kMapSeed;
    for (const Vec2 p : {Vec2{4300.0f, 4295.0f},
                         Vec2{4800.0f, 4295.0f},
                         Vec2{5300.0f, 4295.0f},
                         Vec2{4730.0f, 4408.0f},
                         Vec2{4800.0f, 4700.0f}}) {
        REQUIRE_NEAR(height_at(seed, p.x, p.z),
                     kFlorangiaAirportSite.ground_m, 0.06f);
    }

    return apricot_test::done("florangia_airport_tests");
}

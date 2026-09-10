#include "app/snowplow_service.h"
#include "game/local_snow_conditions.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    SnowClearanceField field;
    SnowplowService service;
    VehicleAgent truck;
    truck.snowplow_unit = true;
    truck.lane_key = 12;
    truck.slot = 7;
    truck.pos = {0, 10, 0};
    truck.fwd = {1, 0, 0};
    truck.speed_mps = 6;
    service.step({truck}, field, 0.25f);
    REQUIRE(field.strips().empty());
    for (int i = 0; i < 240; ++i) {
        truck.pos.x += 0.05f;
        service.step({truck}, field, 0.25f);
    }
    REQUIRE(service.cleared_distance_m() > 11.9f);
    REQUIRE(field.depth_at(8, 10, 0, 0.25f) < 0.01f);
    REQUIRE_NEAR(field.depth_at(8, 10, 2, 0.25f), 0.25f, 0.00001f);
    REQUIRE_NEAR(field.depth_at(8, 18, 0, 0.25f), 0.25f, 0.00001f);
    truck.pos.x = 100;
    service.step({truck}, field, 0.25f);
    REQUIRE_NEAR(field.depth_at(60, 10, 0, 0.25f), 0.25f, 0.00001f);
    service.step({}, field, 0.25f);
    truck.pos.x = 100.2f;
    service.step({truck}, field, 0.25f);
    REQUIRE_NEAR(field.depth_at(103, 10, 0, 0.25f), 0.25f, 0.00001f);
    const float distance = service.cleared_distance_m();
    truck.collision_recovery_seconds = 1;
    truck.pos.x += 0.05f;
    service.step({truck}, field, 0.25f);
    REQUIRE_NEAR(service.cleared_distance_m(), distance, 0.00001f);
    apricot_test::pass("only contiguous forward plow motion clears its own grade and width");

    Conditions weather;
    weather.snow = 1;
    weather.rain = 0.4f;
    weather.wetness = 0.6f;
    weather.sun_elevation = -0.5f;
    const auto packed = conditions_with_local_snow(weather, 0.25f);
    const auto cleared = conditions_with_local_snow(weather, 0.008f);
    REQUIRE(cleared.grip > packed.grip);
    REQUIRE(cleared.grip < 1.0f);
    REQUIRE(cleared.snow == weather.snow);
    REQUIRE(cleared.wetness == weather.wetness);
    REQUIRE(cleared.rain == weather.rain);
    const VehicleTuning tuning;
    const auto packed_tuning = conditioned_tuning(tuning, packed);
    const auto cleared_tuning = conditioned_tuning(tuning, cleared);
    REQUIRE(cleared_tuning.grip_scale > packed_tuning.grip_scale);
    REQUIRE(cleared_tuning.rolling_resistance < packed_tuning.rolling_resistance);
    apricot_test::pass("cleared snow improves tyre grip and drag while preserving other weather");
}

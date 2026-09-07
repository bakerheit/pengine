#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "city/miandi_night_lighting.h"
#include "city/miandi_ocean_drive.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool finite(const city::MiandiNightLightVec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

float length_squared(const city::MiandiNightLightVec3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

void authored_lights_are_bounded_and_downward() {
    REQUIRE(city::kMiandiNightLights.size() >= 16u);
    REQUIRE(city::kMiandiNightLights.size() <= 28u);
    for (const auto& light : city::kMiandiNightLights) {
        REQUIRE(light.name && std::strlen(light.name) > 0u);
        REQUIRE(finite(light.world_position));
        REQUIRE(finite(light.world_direction));
        REQUIRE(finite(light.linear_rgb));
        REQUIRE(std::isfinite(light.range_m));
        REQUIRE(std::isfinite(light.base_power));
        REQUIRE(std::isfinite(light.outer_cone_cos));
        REQUIRE_NEAR(length_squared(light.world_direction), 1.0f, 1e-5f);
        REQUIRE(light.world_direction.y < 0.0f);
        REQUIRE(light.linear_rgb.x >= 0.0f && light.linear_rgb.x <= 1.0f);
        REQUIRE(light.linear_rgb.y >= 0.0f && light.linear_rgb.y <= 1.0f);
        REQUIRE(light.linear_rgb.z >= 0.0f && light.linear_rgb.z <= 1.0f);
        REQUIRE(light.range_m >= 8.0f && light.range_m <= 22.0f);
        REQUIRE(light.base_power > 0.0f && light.base_power <= 8.0f);
        REQUIRE(light.outer_cone_cos >= 0.50f && light.outer_cone_cos <= 0.80f);
    }
    apricot_test::pass("Miandi venue lights are finite, normalized, downward and bounded");
}

void beachfront_properties_have_authored_lighting() {
    unsigned int ocean_drive = 0;
    for (const auto& light : city::kMiandiNightLights) {
        const bool in_ocean_drive = light.world_position.x >= 7920.0f &&
                                    light.world_position.x <= 8086.0f &&
                                    light.world_position.z >= 8225.0f &&
                                    light.world_position.z <= 8375.0f;
        ocean_drive += in_ocean_drive;
        if (in_ocean_drive) {
            // The center ray must actually strike the east facade above
            // ground, within range. The old outward cones missed it entirely.
            const float facade_x = city::kMiandiOceanDriveSite.origin.x +
                                    city::kMiandiOceanHotelFrontX;
            REQUIRE_NEAR(light.world_position.x - facade_x, 6.0f, 1e-5f);
            REQUIRE(light.world_position.x > facade_x);
            REQUIRE(light.world_direction.x < 0.0f);
            const float travel = (facade_x - light.world_position.x) /
                                  light.world_direction.x;
            REQUIRE(travel < light.range_m);
            const float hit_y = light.world_position.y +
                                travel * light.world_direction.y;
            REQUIRE(hit_y > 8.0f && hit_y < 22.8f);
        }
    }
    REQUIRE(ocean_drive >= 4u);
    apricot_test::pass("both Ocean Drive beachfront hotel properties have authored night lights");
}

void every_new_parcel_has_authored_lighting() {
    constexpr std::array<city::MiandiNightLightVec3, 4> parcel_centers{{
        {7000.0f, 8.0f, 8500.0f}, {7200.0f, 8.0f, 8500.0f},
        {7800.0f, 8.0f, 8500.0f}, {8000.0f, 8.0f, 8500.0f},
    }};
    std::array<unsigned int, parcel_centers.size()> counts{};
    for (const auto& light : city::kMiandiNightLights) {
        for (std::size_t parcel = 0; parcel < parcel_centers.size(); ++parcel) {
            const float dx = std::fabs(light.world_position.x - parcel_centers[parcel].x);
            const float dz = std::fabs(light.world_position.z - parcel_centers[parcel].z);
            if (dx <= 80.0f && dz <= 75.0f) ++counts[parcel];
        }
    }
    for (const auto count : counts) REQUIRE(count >= 4u);
    apricot_test::pass("all four Miandi nightlife parcels have local venue lights");
}

void night_power_is_strictly_zero_by_day_and_deterministic() {
    const auto& light = city::kMiandiNightLights[0];
    REQUIRE(city::miandi_night_light_power(light, 0.0f) == 0.0f);
    REQUIRE(city::miandi_night_light_power(light, -0.75f) == 0.0f);
    REQUIRE(city::miandi_night_light_power(
                light, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    REQUIRE_NEAR(city::miandi_night_light_power(light, 0.25f),
                 light.base_power * 0.25f, 1e-6f);
    REQUIRE_NEAR(city::miandi_night_light_power(light, 1.0f), light.base_power,
                 1e-6f);
    REQUIRE_NEAR(city::miandi_night_light_power(light, 2.0f), light.base_power,
                 1e-6f);
    for (const auto& authored : city::kMiandiNightLights) {
        const float expected = city::miandi_night_light_power(authored, 0.63f);
        for (int repeat = 0; repeat < 8; ++repeat)
            REQUIRE(city::miandi_night_light_power(authored, 0.63f) == expected);
    }
    apricot_test::pass("Miandi night power clamps cleanly, is day-zero and deterministic");
}

}  // namespace

int main() {
    authored_lights_are_bounded_and_downward();
    every_new_parcel_has_authored_lighting();
    beachfront_properties_have_authored_lighting();
    night_power_is_strictly_zero_by_day_and_deterministic();
    return apricot_test::done("miandi_night_lighting_tests");
}

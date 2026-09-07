#include <vector>

#include "city/interior_streaming.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec3 site_point(const city::StartSite& site, float x, float y, float z) {
    return {site.origin.x + site.cos_yaw * x + site.sin_yaw * z,
            site.ground_m + y,
            site.origin.z - site.sin_yaw * x + site.cos_yaw * z};
}

void authored_floor_drives_interior_state() {
    const auto parts = city::bake_building(city::kFastFoodPlan);
    std::vector<city::InteriorStreamingVolume> volumes;
    city::append_interior_streaming_volumes(
        city::kFastFoodSite, parts.data(), parts.size(), volumes);
    REQUIRE(volumes.size() == 1u);
    REQUIRE(city::contains(
        volumes.front(), site_point(city::kFastFoodSite, -5.0f, 0.20f, 3.0f)));
    REQUIRE(!city::contains(
        volumes.front(), site_point(city::kFastFoodSite, -5.0f, 0.20f, -5.1f)));
    REQUIRE(city::contains(
        volumes.front(), site_point(city::kFastFoodSite, -5.0f, 0.20f, -5.1f),
        city::kInteriorExitMarginM));
    REQUIRE(!city::contains(
        volumes.front(), site_point(city::kFastFoodSite, -5.0f, 9.0f, 3.0f)));
    apricot_test::pass("authored quickbite floor drives interior state and exit hysteresis");
}

void local_floor_yaw_is_respected() {
    city::StartSite site{"rotated", {12.0f, -7.0f}, 1.0f, 0.0f,
                         {}, 20.0f, 20.0f, 3.0f};
    city::StartPart floor{"test interior floor", {2.0f, 4.0f}, 0.0f,
                          8.0f, 0.2f, 2.0f, city::StartFinish::Concrete,
                          true, 0.0f, 90.0f};
    const auto volume = city::interior_streaming_volume(site, floor);
    REQUIRE(city::contains(volume, site_point(site, 2.0f, 0.2f, 7.8f)));
    REQUIRE(!city::contains(volume, site_point(site, 5.8f, 0.2f, 4.0f)));
    apricot_test::pass("rotated floor footprint matches authored yaw");
}

void indoor_presentation_radius_is_horizontal_and_optional() {
    const glm::vec3 focus{10.0f, 2.0f, 20.0f};
    REQUIRE(city::within_presentation_radius({1000.0f, 50.0f, 1000.0f},
                                              focus, 0.0f));
    REQUIRE(city::within_presentation_radius({64.0f, 200.0f, 20.0f},
                                              focus,
                                              city::kInteriorNpcPresentationRadiusM));
    REQUIRE(!city::within_presentation_radius({66.0f, 2.0f, 20.0f},
                                               focus,
                                               city::kInteriorNpcPresentationRadiusM));
    REQUIRE(city::within_presentation_radius({100.0f, -50.0f, 20.0f},
                                              focus,
                                              city::kInteriorTrafficPresentationRadiusM));
    apricot_test::pass("indoor traffic and NPC presentation radii are bounded in XZ");
}

}  // namespace

int main() {
    authored_floor_drives_interior_state();
    local_floor_yaw_is_respected();
    indoor_presentation_radius_is_horizontal_and_optional();
    return apricot_test::done("interior_streaming_tests");
}

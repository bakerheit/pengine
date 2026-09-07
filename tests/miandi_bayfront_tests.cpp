#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "city/miandi_bayfront.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool named(const city::BuildingPiece& p, std::string_view needle) {
    return p.name && std::string_view(p.name).find(needle) != std::string_view::npos;
}

void site_and_skyline_are_bounded() {
    const auto& s = city::kMiandiBayfrontSite;
    REQUIRE_NEAR(s.origin.x, 7600.f, 1e-5f);
    REQUIRE_NEAR(s.origin.z, 8300.f, 1e-5f);
    REQUIRE_NEAR(s.ground_m, 8.f, 1e-5f);
    REQUIRE_NEAR(s.lot_width_m, 160.f, 1e-5f);
    REQUIRE_NEAR(s.lot_depth_m, 150.f, 1e-5f);
    REQUIRE(s.cos_yaw == 1.f && s.sin_yaw == 0.f);
    REQUIRE(city::valid_building_plan(city::kMiandiBayfrontPlan));
    const auto parts = city::bake_miandi_bayfront();
    REQUIRE(parts.size() >= 55u && parts.size() <= 110u);
    float top = 0.f;
    for (const auto& p : parts) top = std::max(top, p.bottom_m + p.height_m);
    REQUIRE_NEAR(top, 116.f, 1e-4f);
    apricot_test::pass("BF-2 has the fixed parcel, 8 m ground and 116 m skyline cap");
}

void solids_stay_inside_and_access_meets_sidewalks() {
    const auto parts = city::bake_miandi_bayfront();
    const city::BuildingPiece* forecourt = nullptr;
    const city::BuildingPiece* service = nullptr;
    for (const auto& p : parts) {
        const float yaw = p.yaw_deg * 0.01745329251994329577f;
        const float half_x = std::fabs(std::cos(yaw)) * p.width_m * .5f +
                             std::fabs(std::sin(yaw)) * p.depth_m * .5f;
        const float half_z = std::fabs(std::sin(yaw)) * p.width_m * .5f +
                             std::fabs(std::cos(yaw)) * p.depth_m * .5f;
        if (p.solid) {
            REQUIRE(p.centre.x - half_x >= -80.001f);
            REQUIRE(p.centre.x + half_x <= 80.001f);
            REQUIRE(p.centre.z - half_z >= -75.001f);
            REQUIRE(p.centre.z + half_z <= 75.001f);
        }
        if (std::strcmp(p.name, "crown forecourt walk") == 0) forecourt = &p;
        if (std::strcmp(p.name, "crown service drive") == 0) service = &p;
    }
    REQUIRE(forecourt && service);
    REQUIRE_NEAR(forecourt->centre.z + forecourt->depth_m * .5f, 86.0f, 1e-5f);
    REQUIRE_NEAR(service->centre.x + service->width_m * .5f, 90.0f, 1e-5f);
    REQUIRE(!forecourt->solid && !service->solid);
    apricot_test::pass("BF-2 solids stay in parcel while public and service access meet sidewalks");
}

void entrance_is_a_real_gap_with_a_reachable_walk() {
    const auto parts = city::bake_miandi_bayfront();
    bool walk = false;
    bool glazing = false;
    for (const auto& p : parts) {
        if (named(p, "forecourt walk")) walk = true;
        if (named(p, "lobby glazing")) {
            glazing = true;
            REQUIRE(!p.solid);
        }
        if (p.solid) {
            const float x0 = p.centre.x - p.width_m * .5f;
            const float x1 = p.centre.x + p.width_m * .5f;
            const float z0 = p.centre.z - p.depth_m * .5f;
            const float z1 = p.centre.z + p.depth_m * .5f;
            // The south approach and doorway corridor stay open through the
            // podium. The floor is support, so only elevated blockers count.
            if (p.bottom_m > .25f && p.bottom_m < 3.4f &&
                p.bottom_m + p.height_m > .2f)
                REQUIRE(!(x0 < 4.f && x1 > -4.f && z0 < 46.f && z1 > 42.f));
        }
    }
    REQUIRE(walk && glazing);
    apricot_test::pass("BF-2 keeps a collision-open lobby gap and continuous forecourt route");
}

void tower_has_setbacks_crown_plant_and_semantics() {
    const auto parts = city::bake_miandi_bayfront();
    int bands = 0;
    bool first_setback = false;
    bool second_setback = false;
    bool lantern = false;
    bool plant = false;
    bool solid = false;
    bool non_solid = false;
    for (const auto& p : parts) {
        if (named(p, "paving") || named(p, "floor") || named(p, "forecourt") ||
            named(p, "threshold") || named(p, "service apron"))
            REQUIRE(!p.solid);
    }
    for (const auto& p : parts) {
        bands += named(p, "horizontal balcony band");
        first_setback |= named(p, "first shaft setback");
        second_setback |= named(p, "second shaft setback");
        lantern |= named(p, "lantern");
        plant |= named(p, "plant") || named(p, "HVAC");
        if (p.finish == city::BuildingFinish::Glass) REQUIRE(!p.solid);
        solid |= p.solid;
        non_solid |= !p.solid;
    }
    REQUIRE(bands >= 20);
    REQUIRE(first_setback && second_setback && lantern && plant);
    REQUIRE(solid && non_solid);
    apricot_test::pass("BF-2 has two shaft setbacks, a distinct lantern crown, roof plant and mixed collision semantics");
}

void bake_is_deterministic() {
    const auto a = city::bake_miandi_bayfront();
    const auto b = city::bake_miandi_bayfront();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-6f);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-6f);
        REQUIRE_NEAR(a[i].bottom_m, b[i].bottom_m, 1e-6f);
        REQUIRE(a[i].solid == b[i].solid);
    }
    apricot_test::pass("BF-2 bake output is deterministic");
}

}  // namespace

int main() {
    site_and_skyline_are_bounded();
    solids_stay_inside_and_access_meets_sidewalks();
    entrance_is_a_real_gap_with_a_reachable_walk();
    tower_has_setbacks_crown_plant_and_semantics();
    bake_is_deterministic();
    return apricot_test::done("miandi_bayfront_tests");
}

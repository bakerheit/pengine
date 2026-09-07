#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/miandi_sunwave_hotel.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool named(const city::BuildingPiece& p, const char* text) {
    return p.name && std::strcmp(p.name, text) == 0;
}

float extent_x(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * 0.01745329251994329577f;
    return (std::fabs(std::cos(yaw)) * p.width_m +
            std::fabs(std::sin(yaw)) * p.depth_m) * .5f;
}

float extent_z(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * 0.01745329251994329577f;
    return (std::fabs(std::sin(yaw)) * p.width_m +
            std::fabs(std::cos(yaw)) * p.depth_m) * .5f;
}

bool blocks(const city::BuildingPiece& p, float x0, float x1, float z0,
            float z1) {
    return p.solid && p.bottom_m < 2.2f && p.bottom_m + p.height_m > .3f &&
           p.centre.x - extent_x(p) < x1 && p.centre.x + extent_x(p) > x0 &&
           p.centre.z - extent_z(p) < z1 && p.centre.z + extent_z(p) > z0;
}

void site_plan_and_budget_are_pinned() {
    const auto& site = city::kMiandiSunwaveHotelSite;
    REQUIRE_NEAR(site.origin.x, 8000.0f, 1e-5f);
    REQUIRE_NEAR(site.origin.z, 8500.0f, 1e-5f);
    REQUIRE_NEAR(site.ground_m, 8.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_width_m, 160.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_depth_m, 150.0f, 1e-5f);
    REQUIRE(city::valid_building_plan(city::kMiandiSunwaveHotelPlan));
    const auto parts = city::bake_miandi_sunwave_hotel();
    REQUIRE(parts.size() >= 100u);
    REQUIRE(parts.size() <= 500u);  // includes the two-layer resort name tubes
    float top = 0.0f;
    for (const auto& p : parts) top = std::max(top, p.bottom_m + p.height_m);
    REQUIRE(top < 26.0f);
    apricot_test::pass("H2 pins the parcel, deterministic plan, part budget, and roof cap");
}

void routes_and_door_gaps_stay_open() {
    const auto parts = city::bake_miandi_sunwave_hotel();
    const city::BuildingPiece* public_route = nullptr;
    const city::BuildingPiece* club_route = nullptr;
    const city::BuildingPiece* service_lane = nullptr;
    for (const auto& p : parts) {
        if (named(p, "Palmera public route to Ocean Drive")) public_route = &p;
        if (named(p, "Palmera club route to Ocean Drive")) club_route = &p;
        if (named(p, "Palmera seven metre Seabreeze service lane")) service_lane = &p;
        REQUIRE_MSG(!blocks(p, 19.8f, 90.1f, -16.2f, -11.8f),
                    "solid closes recessed lobby approach", p.name);
        REQUIRE_MSG(!blocks(p, 29.8f, 90.1f, 46.8f, 51.2f),
                    "solid closes cabana club approach", p.name);
        REQUIRE_MSG(!blocks(p, -90.1f, -37.7f, 10.35f, 13.65f),
                    "solid closes west service approach", p.name);
    }
    REQUIRE(public_route && club_route && service_lane);
    REQUIRE(!public_route->solid && !club_route->solid && !service_lane->solid);
    REQUIRE_NEAR(public_route->centre.x + public_route->width_m * .5f, 86.0f, 1e-5f);
    REQUIRE_NEAR(club_route->centre.x + club_route->width_m * .5f, 86.0f, 1e-5f);
    REQUIRE_NEAR(service_lane->centre.x - service_lane->width_m * .5f, -90.0f, 1e-5f);
    REQUIRE_NEAR(service_lane->depth_m, 7.0f, 1e-5f);
    apricot_test::pass("H2 keeps Ocean Drive public routes and the 7 m Seabreeze lane clear");
}

void deco_language_pool_and_neon_are_explicit() {
    const auto parts = city::bake_miandi_sunwave_hotel();
    int eyebrows = 0;
    int rails = 0;
    int windows = 0;
    int cabanas = 0;
    int vents = 0;
    int rounded = 0;
    int resort_detail = 0;
    bool pool = false;
    bool terrazzo = false;
    bool center_bay = false;
    bool pink = false;
    bool blue = false;
    bool warm_white = false;
    for (const auto& p : parts) {
        if (p.name && std::strstr(p.name, "horizontal eyebrow")) ++eyebrows;
        if (p.name && std::strstr(p.name, "balcony rail")) ++rails;
        if (p.name && std::strstr(p.name, "narrow window rhythm")) ++windows;
        if (p.name && std::strstr(p.name, "cabana roof")) ++cabanas;
        if (p.name && std::strstr(p.name, "rooftop vent")) ++vents;
        if (p.name && std::strstr(p.name, "rounded")) ++rounded;
        if (p.name && (std::strstr(p.name, "vertical Deco rib") ||
                       std::strstr(p.name, "pool chaise") ||
                       std::strstr(p.name, "pool umbrella") ||
                       std::strstr(p.name, "cabana bar")))
            ++resort_detail;
        if (named(p, "Palmera pool water")) pool = !p.solid;
        if (named(p, "Palmera terrazzo-like entry slab")) terrazzo = !p.solid;
        if (p.name && std::strstr(p.name, "stepped center bay")) center_bay = true;
        if (p.name && std::strstr(p.name, "miandi neon")) {
            REQUIRE(!p.solid);
            pink |= std::strstr(p.name, "pink") != nullptr;
            blue |= std::strstr(p.name, "blue") != nullptr;
            warm_white |= std::strstr(p.name, "warm-white") != nullptr;
        }
    }
    REQUIRE(eyebrows >= 5);
    REQUIRE(rails >= 8);
    REQUIRE(windows >= 40);
    REQUIRE(cabanas == 2);
    REQUIRE(vents == 4);
    REQUIRE(rounded >= 6);
    REQUIRE(resort_detail >= 17);
    REQUIRE(pool && terrazzo && center_bay);
    REQUIRE(pink && blue && warm_white);
    apricot_test::pass("H2 has Tropical Deco ribs, furnished cabana pool court, and non-solid neon keys");
}

void bake_is_deterministic_and_solids_stay_on_parcel() {
    const auto a = city::bake_miandi_sunwave_hotel();
    const auto b = city::bake_miandi_sunwave_hotel();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-5f);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-5f);
        REQUIRE_NEAR(a[i].height_m, b[i].height_m, 1e-5f);
        REQUIRE(a[i].solid == b[i].solid);
        if (a[i].solid) {
            REQUIRE(a[i].centre.x - extent_x(a[i]) >= -80.01f);
            REQUIRE(a[i].centre.x + extent_x(a[i]) <= 80.01f);
            REQUIRE(a[i].centre.z - extent_z(a[i]) >= -75.01f);
            REQUIRE(a[i].centre.z + extent_z(a[i]) <= 75.01f);
        }
    }
    apricot_test::pass("H2 uses one repeatable baked source for visible and collision geometry");
}

}  // namespace

int main() {
    site_plan_and_budget_are_pinned();
    routes_and_door_gaps_stay_open();
    deco_language_pool_and_neon_are_explicit();
    bake_is_deterministic_and_solids_stay_on_parcel();
    return apricot_test::done("miandi_sunwave_hotel_tests");
}

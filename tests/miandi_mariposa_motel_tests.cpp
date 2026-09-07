#include <algorithm>
#include <cmath>
#include <cstring>

#include "city/miandi_mariposa_motel.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool named(const city::BuildingPiece& part, const char* text) {
    return part.name && std::strcmp(part.name, text) == 0;
}

bool overlaps(const city::BuildingPiece& part, float x0, float x1, float z0,
              float z1) {
    const float yaw = part.yaw_deg * .0174532925199433f;
    const float ex = (std::fabs(std::cos(yaw)) * part.width_m +
                      std::fabs(std::sin(yaw)) * part.depth_m) * .5f;
    const float ez = (std::fabs(std::sin(yaw)) * part.width_m +
                      std::fabs(std::cos(yaw)) * part.depth_m) * .5f;
    return part.centre.x - ex < x1 && part.centre.x + ex > x0 &&
           part.centre.z - ez < z1 && part.centre.z + ez > z0;
}

void site_plan_and_bake_are_deterministic() {
    const auto a = city::bake_miandi_mariposa_motel();
    const auto b = city::bake_miandi_mariposa_motel();
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.origin.x, 7800.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.origin.z, 8500.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.ground_m, 8.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.lot_width_m, 160.0f, 1e-5f);
    REQUIRE_NEAR(city::kMiandiMariposaMotelSite.lot_depth_m, 150.0f, 1e-5f);
    REQUIRE(city::valid_building_plan(city::kMiandiMariposaMotelPlan));
    REQUIRE(a.size() >= 90u && a.size() <= 260u);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE(a[i].solid == b[i].solid);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-5f);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-5f);
    }
    apricot_test::pass("H1 site, plan, and authored bake are deterministic");
}

void lodge_routes_doors_and_pool_are_real() {
    const auto parts = city::bake_miandi_mariposa_motel();
    int room_doors = 0;
    bool lobby_gap = false;
    bool pool = false;
    bool fence = false;
    for (const auto& part : parts) {
        if (part.name && std::strstr(part.name, "room door")) ++room_doors;
        if (named(part, "mariposa lobby door")) lobby_gap = true;
        if (named(part, "mariposa pool water")) pool = !part.solid;
        if (part.name && std::strstr(part.name, "pool fence")) fence |= part.solid;
        if (!part.solid || part.height_m < .2f || part.bottom_m > 2.2f) continue;
        REQUIRE_MSG(!overlaps(part, -.5f, .5f, -75.0f, -48.0f),
                    "solid part blocks Bayfront lobby walk", part.name);
        REQUIRE_MSG(!overlaps(part, 27.0f, 34.0f, 47.5f, 56.5f),
                    "solid part blocks motor-court driveway turn", part.name);
        REQUIRE_MSG(!overlaps(part, 33.0f, 90.0f, 51.5f, 58.5f),
                    "solid part blocks seven metre Seabreeze driveway", part.name);
    }
    const auto lobby_walk = std::find_if(parts.begin(), parts.end(),
        [](const auto& p) { return named(p, "mariposa Bayfront lobby walk"); });
    const auto driveway = std::find_if(parts.begin(), parts.end(),
        [](const auto& p) { return named(p, "mariposa Seabreeze driveway"); });
    REQUIRE(lobby_walk != parts.end() && driveway != parts.end());
    REQUIRE_NEAR(lobby_walk->centre.z - lobby_walk->depth_m * .5f, -86.0f,
                 1e-5f);
    REQUIRE_NEAR(driveway->centre.x + driveway->width_m * .5f, 90.0f, 1e-5f);
    REQUIRE_NEAR(driveway->depth_m, 7.0f, 1e-5f);
    REQUIRE(lobby_gap);
    REQUIRE(room_doors >= 24);  // jambs and lintels from eight true openings.
    REQUIRE(pool && fence);
    apricot_test::pass("H1 keeps lobby and east drive clear, with real door gaps and fenced pool");
}

void mimo_language_neon_and_height_contract_hold() {
    const auto parts = city::bake_miandi_mariposa_motel();
    int galleries = 0, rails = 0, screens = 0, neon = 0, resort_detail = 0;
    float pylon_top = 0.0f;
    for (const auto& part : parts) {
        if (part.name && std::strstr(part.name, "gallery")) ++galleries;
        if (part.name && std::strstr(part.name, "rail")) ++rails;
        if (part.name && std::strstr(part.name, "breeze-block")) ++screens;
        if (part.name && (std::strstr(part.name, "porte cochere") ||
                          std::strstr(part.name, "room number fin") ||
                          std::strstr(part.name, "pool chaise") ||
                          std::strstr(part.name, "pool umbrella")))
            ++resort_detail;
        const float top = part.bottom_m + part.height_m;
        if (part.name && std::strstr(part.name, "pylon")) pylon_top = std::max(pylon_top, top);
        else REQUIRE(top < 14.0f);
        if (part.name && std::strstr(part.name, "miandi neon")) {
            ++neon;
            REQUIRE(!part.solid);
        }
    }
    REQUIRE(galleries >= 16 && rails >= 20 && screens >= 16);
    REQUIRE(neon >= 6);
    REQUIRE(resort_detail >= 16);
    REQUIRE(pylon_top < 24.0f);
    apricot_test::pass("H1 has MiMo galleries, shaded arrival, pool furniture, non-solid neon, and bounded pylon");
}

}  // namespace

int main() {
    site_plan_and_bake_are_deterministic();
    lodge_routes_doors_and_pool_are_real();
    mimo_language_neon_and_height_contract_hold();
    return apricot_test::done("miandi_mariposa_motel_tests");
}

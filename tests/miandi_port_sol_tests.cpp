#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/miandi_port_sol.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool named(const city::BuildingPiece& p, const char* text) {
    return p.name != nullptr && std::strcmp(p.name, text) == 0;
}

bool overlaps_xy(const city::BuildingPiece& p, float x0, float x1, float z0,
                 float z1) {
    const float yaw = p.yaw_deg * 0.0174532925199433f;
    const float ex = (std::fabs(std::cos(yaw)) * p.width_m +
                      std::fabs(std::sin(yaw)) * p.depth_m) * 0.5f;
    const float ez = (std::fabs(std::sin(yaw)) * p.width_m +
                      std::fabs(std::cos(yaw)) * p.depth_m) * 0.5f;
    return p.centre.x - ex < x1 && p.centre.x + ex > x0 &&
           p.centre.z - ez < z1 && p.centre.z + ez > z0;
}

void site_and_plan_are_valid() {
    REQUIRE_NEAR(city::kMiandiPortSolSite.origin.x, 7400.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiPortSolSite.origin.z, 8700.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiPortSolSite.ground_m, 8.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiPortSolSite.lot_width_m, 160.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiPortSolSite.lot_depth_m, 150.0f, 1e-5);
    REQUIRE(city::valid_building_plan(city::kMiandiPortSolPlan));
    apricot_test::pass("PS-3 exposes the contracted site and valid plan");
}

void bake_is_deterministic_and_bounded() {
    const auto first = city::bake_miandi_port_sol();
    const auto second = city::bake_miandi_port_sol();
    REQUIRE(first.size() >= 45u);
    REQUIRE(first.size() <= 90u);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(first[i].name != nullptr);
        REQUIRE(std::strcmp(first[i].name, second[i].name) == 0);
        REQUIRE_NEAR(first[i].centre.x, second[i].centre.x, 1e-5);
        REQUIRE_NEAR(first[i].centre.z, second[i].centre.z, 1e-5);
        REQUIRE(first[i].solid == second[i].solid);
    }
    apricot_test::pass("PS-3 bake is deterministic and bounded");
}

void site_bounds_and_route_clearances_hold() {
    const auto parts = city::bake_miandi_port_sol();
    for (const auto& p : parts) {
        const float yaw = p.yaw_deg * 0.0174532925199433f;
        const float ex = (std::fabs(std::cos(yaw)) * p.width_m +
                          std::fabs(std::sin(yaw)) * p.depth_m) * 0.5f;
        const float ez = (std::fabs(std::sin(yaw)) * p.width_m +
                          std::fabs(std::cos(yaw)) * p.depth_m) * 0.5f;
        REQUIRE_MSG(p.centre.x - ex >= -80.1f, "part outside west bound", p.name);
        REQUIRE_MSG(p.centre.x + ex <= 80.1f, "part outside east bound", p.name);
        const bool coral_access = named(p, "public market walk") ||
                                  named(p, "rear truck lane");
        REQUIRE_MSG(p.centre.z - ez >= (coral_access ? -90.1f : -75.1f),
                    "part outside north/access bound", p.name);
        REQUIRE_MSG(p.centre.z + ez <= 75.1f, "part outside south bound", p.name);
    }

    // The public walk runs north from Coral Way to the 4.5 m entrance.
    // Ignore the floor slab: it supports the route and is below the walker.
    for (const auto& p : parts) {
        if (!p.solid || p.height_m < 0.2f || p.bottom_m > 2.2f) continue;
        REQUIRE_MSG(!overlaps_xy(p, -24.0f, -20.0f, -90.0f, -30.0f),
                    "solid part blocks public walk", p.name);
    }
    // The 9 m truck corridor stays on the east side of the market and reaches
    // the rear apron without crossing the barriers or cold-store equipment.
    for (const auto& p : parts) {
        if (!p.solid || p.height_m < 0.2f || p.bottom_m > 2.2f) continue;
        REQUIRE_MSG(!overlaps_xy(p, 40.0f, 49.0f, -90.0f, 65.0f),
                    "solid part blocks truck corridor", p.name);
    }
    apricot_test::pass("PS-3 bounds and pedestrian/truck corridors stay open");
}

void paved_routes_cover_their_full_contract() {
    const auto parts = city::bake_miandi_port_sol();
    const city::BuildingPiece* public_walk = nullptr;
    const city::BuildingPiece* truck_lane = nullptr;
    for (const auto& p : parts) {
        if (named(p, "public market walk")) public_walk = &p;
        if (named(p, "rear truck lane")) truck_lane = &p;
    }
    REQUIRE(public_walk != nullptr);
    REQUIRE(truck_lane != nullptr);
    REQUIRE(!public_walk->solid);
    REQUIRE(!truck_lane->solid);
    REQUIRE(public_walk->finish == city::BuildingFinish::Concrete);
    REQUIRE(truck_lane->finish == city::BuildingFinish::Concrete);
    REQUIRE_NEAR(public_walk->width_m, 4.5f, 1e-5);
    REQUIRE_NEAR(public_walk->depth_m, 60.0f, 1e-5);
    REQUIRE_NEAR(public_walk->centre.x, -22.0f, 1e-5);
    REQUIRE_NEAR(public_walk->centre.z - public_walk->depth_m * 0.5f, -90.0f,
                 1e-5);
    REQUIRE_NEAR(public_walk->centre.z + public_walk->depth_m * 0.5f, -30.0f,
                 1e-5);
    REQUIRE_NEAR(truck_lane->width_m, 9.0f, 1e-5);
    REQUIRE_NEAR(truck_lane->depth_m, 150.0f, 1e-5);
    REQUIRE_NEAR(truck_lane->centre.x, 44.5f, 1e-5);
    REQUIRE_NEAR(truck_lane->centre.z - truck_lane->depth_m * 0.5f, -90.0f,
                 1e-5);
    REQUIRE_NEAR(truck_lane->centre.z + truck_lane->depth_m * 0.5f, 60.0f,
                 1e-5);
    // Check endpoints and interior segments, not just the aggregate length.
    for (const float z : {-89.99f, -75.0f, -60.0f, -45.0f, -30.01f, 0.0f, 30.0f,
                          59.9f}) {
        REQUIRE(overlaps_xy(*public_walk, -22.0f, -22.0f, z, z + 0.01f) ||
                z > -30.0f);
        REQUIRE(overlaps_xy(*truck_lane, 44.5f, 44.5f, z, z + 0.01f));
    }
    apricot_test::pass("PS-3 paved public and truck routes cover every endpoint and segment");
}

void openings_and_service_language_are_present() {
    const auto parts = city::bake_miandi_port_sol();
    int loading_frames = 0;
    int open_market_frame = 0;
    int vents = 0;
    int crane_legs = 0;
    bool has_solid = false;
    bool has_non_solid = false;
    for (const auto& p : parts) {
        if (p.solid) has_solid = true;
        else has_non_solid = true;
        if (named(p, "loading bay west") || named(p, "loading bay centre") ||
            named(p, "loading bay east")) {
            // The creator emits three steel jamb/head pieces per open bay.
            ++loading_frames;
        }
        if (named(p, "market entrance")) ++open_market_frame;
        if (named(p, "roof vent west") || named(p, "roof vent east")) ++vents;
        if (named(p, "gantry west leg") || named(p, "gantry east leg"))
            ++crane_legs;
    }
    REQUIRE(loading_frames >= 9);
    REQUIRE(open_market_frame >= 3);
    for (const float bay_x : {-38.0f, -16.0f, 6.0f}) {
        for (const auto& p : parts) {
            if (!p.solid || p.height_m < 0.2f || p.bottom_m >= 4.0f) continue;
            REQUIRE_MSG(!overlaps_xy(p, bay_x - 3.8f, bay_x + 3.8f, 21.7f,
                                     22.3f),
                        "solid piece refilled a loading bay", p.name);
        }
    }
    for (const auto& p : parts) {
        if (!p.solid || p.height_m < 0.2f || p.bottom_m >= 3.4f) continue;
        REQUIRE_MSG(!overlaps_xy(p, -24.0f, -20.0f, -30.3f, -29.7f),
                    "solid piece refilled the market entrance", p.name);
    }
    REQUIRE(vents == 2);
    REQUIRE(crane_legs == 2);
    int seafood_canopies = 0;
    for (const auto& p : parts) {
        if (named(p, "seafood shed north canopy") ||
            named(p, "seafood shed south canopy")) {
            ++seafood_canopies;
            REQUIRE(!p.solid);
        }
    }
    REQUIRE(seafood_canopies == 2);
    REQUIRE(has_solid);
    REQUIRE(has_non_solid);
    apricot_test::pass("PS-3 has real loading/entry gaps, roof service detail, and a crane");
}

}  // namespace

int main() {
    site_and_plan_are_valid();
    bake_is_deterministic_and_bounded();
    site_bounds_and_route_clearances_hold();
    paved_routes_cover_their_full_contract();
    openings_and_service_language_are_present();
    return apricot_test::done("miandi_port_sol_tests");
}

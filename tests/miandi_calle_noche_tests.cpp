#include <algorithm>
#include <cmath>
#include <cstring>

#include "city/miandi_calle_noche.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool overlaps(const city::BuildingPiece& p, float x0, float x1, float z0,
              float z1) {
    const float yaw = p.yaw_deg * 0.01745329251994329577f;
    const float ex = (std::fabs(std::cos(yaw)) * p.width_m +
                      std::fabs(std::sin(yaw)) * p.depth_m) * 0.5f;
    const float ez = (std::fabs(std::sin(yaw)) * p.width_m +
                      std::fabs(std::cos(yaw)) * p.depth_m) * 0.5f;
    return p.centre.x - ex < x1 && p.centre.x + ex > x0 &&
           p.centre.z - ez < z1 && p.centre.z + ez > z0;
}

float half_extent_x(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * 0.01745329251994329577f;
    return (std::fabs(std::cos(yaw)) * p.width_m +
            std::fabs(std::sin(yaw)) * p.depth_m) * 0.5f;
}

float half_extent_z(const city::BuildingPiece& p) {
    const float yaw = p.yaw_deg * 0.01745329251994329577f;
    return (std::fabs(std::sin(yaw)) * p.width_m +
            std::fabs(std::cos(yaw)) * p.depth_m) * 0.5f;
}

bool named(const city::BuildingPiece& p, const char* name) {
    return p.name != nullptr && std::strcmp(p.name, name) == 0;
}

void site_plan_and_bake_are_pinned() {
    const auto& site = city::kMiandiCalleNocheSite;
    REQUIRE_NEAR(site.origin.x, 7000.0f, 1e-5f);
    REQUIRE_NEAR(site.origin.z, 8500.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_width_m, 160.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_depth_m, 150.0f, 1e-5f);
    REQUIRE_NEAR(site.ground_m, 8.0f, 1e-5f);
    REQUIRE(city::valid_building_plan(city::kMiandiSolSocialClubPlan));
    REQUIRE(city::valid_building_plan(city::kMiandiPalmaDanceHallPlan));
    REQUIRE(city::valid_building_plan(city::kMiandiLateNightCafecitoPlan));

    const auto first = city::bake_miandi_calle_noche();
    const auto second = city::bake_miandi_calle_noche();
    REQUIRE(first.size() >= 70u);
    REQUIRE(first.size() <= 700u);  // two readable, two-layer venue names
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(std::strcmp(first[i].name, second[i].name) == 0);
        REQUIRE_NEAR(first[i].centre.x, second[i].centre.x, 1e-5f);
        REQUIRE_NEAR(first[i].centre.z, second[i].centre.z, 1e-5f);
        REQUIRE(first[i].solid == second[i].solid);
    }
    apricot_test::pass("N1 site, plans, and deterministic part budget are pinned");
}

void doors_and_program_are_real() {
    const auto parts = city::bake_miandi_calle_noche();
    int authored_doors = 0;
    for (const auto* walls : {city::kMiandiSolSocialWalls,
                              city::kMiandiPalmaWalls,
                              city::kMiandiCafecitoWalls}) {
        for (int wall_index = 0; wall_index < 4; ++wall_index)
            for (std::size_t i = 0; i < walls[wall_index].opening_count; ++i) {
                const auto& opening = walls[wall_index].openings[i];
                if (opening.kind == city::OpeningKind::Door && opening.sill_m == 0.0f) {
                    ++authored_doors;
                    REQUIRE(!opening.leaf);
                }
            }
    }
    int door_frame_sets = 0;
    int service_gate_frames = 0;
    int neon = 0;
    int roof_plant = 0;
    int facade_detail = 0;
    int patio_furniture = 0;
    bool patio = false;
    bool court = false;
    bool stage = false;
    bool dance_floor = false;
    bool mural = false;
    for (const auto& p : parts) {
        if (std::strstr(p.name, "entry") || std::strstr(p.name, "door") ||
            std::strstr(p.name, "counter"))
            ++door_frame_sets;
        if (std::strstr(p.name, "service gate")) ++service_gate_frames;
        if (std::strstr(p.name, "miandi neon")) {
            ++neon;
            REQUIRE(!p.solid);
            REQUIRE(p.finish == city::BuildingFinish::TealDoor ||
                    p.finish == city::BuildingFinish::RedTrim ||
                    p.finish == city::BuildingFinish::Yellow);
        }
        if (std::strstr(p.name, "HVAC") || std::strstr(p.name, "fan")) ++roof_plant;
        if (std::strstr(p.name, "facade pilaster") ||
            std::strstr(p.name, "facade fin") ||
            std::strstr(p.name, "facade tile pier")) ++facade_detail;
        if (std::strstr(p.name, "domino patio chair") ||
            std::strstr(p.name, "domino patio bench")) ++patio_furniture;
        patio = patio || named(p, "domino patio canopy");
        court = court || named(p, "open music court paving");
        stage = stage || named(p, "music court stage shell back");
        dance_floor = dance_floor || named(p, "open music court dance floor");
        mural = mural || std::strstr(p.name, "geometric mural relief");
    }
    REQUIRE(authored_doors >= 6);
    // Leafless doors bake three jamb/head pieces; this catches a fake facade.
    REQUIRE(door_frame_sets >= 15);
    REQUIRE(service_gate_frames >= 3);
    REQUIRE(neon >= 6);
    REQUIRE(roof_plant >= 6);
    REQUIRE(facade_detail >= 12 && patio_furniture >= 6);
    REQUIRE(patio && court && stage && dance_floor && mural);
    apricot_test::pass("N1 has real thresholds, detailed facades, furnished patio, court, mural, neon, and roof plant");
}

void parcel_and_routes_stay_clear() {
    const auto parts = city::bake_miandi_calle_noche();
    float max_height = 0.0f;
    for (const auto& p : parts) {
        const bool west_access = named(p, "Calle Noche west service route");
        const bool north_access = named(p, "Calle Noche north public walk");
        REQUIRE(p.centre.x - half_extent_x(p) >=
                (west_access ? -90.1f : -80.1f));
        REQUIRE(p.centre.x + half_extent_x(p) <= 80.1f);
        REQUIRE(p.centre.z - half_extent_z(p) >=
                (north_access ? -86.1f : -75.1f));
        REQUIRE(p.centre.z + half_extent_z(p) <= 75.1f);
        max_height = std::max(max_height, p.bottom_m + p.height_m);
        if (!p.solid || p.height_m < 0.2f || p.bottom_m > 2.2f) continue;
        REQUIRE_MSG(!overlaps(p, -73.0f, 73.0f, -75.0f, -44.0f),
                    "solid pinches north public walk", p.name);
        REQUIRE_MSG(!overlaps(p, -90.0f, -62.2f, -31.9f, -26.1f),
                    "solid pinches six metre west service route", p.name);
    }
    const auto public_walk = std::find_if(parts.begin(), parts.end(),
        [](const auto& p) { return named(p, "Calle Noche north public walk"); });
    const auto service_route = std::find_if(parts.begin(), parts.end(),
        [](const auto& p) { return named(p, "Calle Noche west service route"); });
    REQUIRE(public_walk != parts.end() && service_route != parts.end());
    REQUIRE_NEAR(public_walk->centre.z - half_extent_z(*public_walk), -86.0f,
                 1e-5f);
    REQUIRE_NEAR(service_route->centre.x - half_extent_x(*service_route),
                 -90.0f, 1e-5f);
    REQUIRE(max_height <= 16.0f);
    apricot_test::pass("N1 solids stay in parcel, below sixteen metres, and clear both routes");
}

}  // namespace

int main() {
    site_plan_and_bake_are_pinned();
    doors_and_program_are_real();
    parcel_and_routes_stay_clear();
    return apricot_test::done("miandi_calle_noche_tests");
}

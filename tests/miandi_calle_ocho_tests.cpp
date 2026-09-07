#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include "city/miandi_calle_ocho.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool solid_overlaps(const city::BuildingPiece& p, float x0, float x1,
                    float z0, float z1) {
    return p.solid && p.centre.x - p.width_m * .5f < x1 &&
           p.centre.x + p.width_m * .5f > x0 &&
           p.centre.z - p.depth_m * .5f < z1 &&
           p.centre.z + p.depth_m * .5f > z0;
}

float half_extent_x(const city::BuildingPiece& p) {
    const float radians = p.yaw_deg * 0.01745329251994329577f;
    return std::fabs(std::cos(radians)) * p.width_m * .5f +
           std::fabs(std::sin(radians)) * p.depth_m * .5f;
}

float half_extent_z(const city::BuildingPiece& p) {
    const float radians = p.yaw_deg * 0.01745329251994329577f;
    return std::fabs(std::sin(radians)) * p.width_m * .5f +
           std::fabs(std::cos(radians)) * p.depth_m * .5f;
}

bool is_walk_support(const city::BuildingPiece& p) {
    return std::strstr(p.name, "walk") != nullptr ||
           std::strstr(p.name, "paving") != nullptr ||
           std::strstr(p.name, "pavement") != nullptr ||
           std::strstr(p.name, "connector") != nullptr ||
           std::strstr(p.name, "service lane") != nullptr;
}

template <std::size_t N>
void openings_fit_inside_walls(const city::BuildingWall (&walls)[N]) {
    for (const auto& wall : walls) {
        const float length = std::hypot(wall.b.x - wall.a.x, wall.b.z - wall.a.z);
        if (wall.opening_count == 0u) {
            REQUIRE(wall.openings == nullptr);
            continue;
        }
        REQUIRE(wall.openings != nullptr);
        for (std::size_t i = 0; i < wall.opening_count; ++i) {
            const auto& opening = wall.openings[i];
            REQUIRE(opening.width_m > 0.0f);
            REQUIRE(opening.center_m - opening.width_m * .5f >= -1e-4f);
            REQUIRE(opening.center_m + opening.width_m * .5f <= length + 1e-4f);
        }
    }
}

struct Bounds {
    float x0 = std::numeric_limits<float>::max();
    float x1 = std::numeric_limits<float>::lowest();
    float z0 = std::numeric_limits<float>::max();
    float z1 = std::numeric_limits<float>::lowest();
};

Bounds solid_plan_bounds(const city::BuildingPlan& plan) {
    Bounds bounds;
    for (const auto& piece : city::bake_building(plan)) {
        if (!piece.solid) continue;
        bounds.x0 = std::min(bounds.x0, piece.centre.x - half_extent_x(piece));
        bounds.x1 = std::max(bounds.x1, piece.centre.x + half_extent_x(piece));
        bounds.z0 = std::min(bounds.z0, piece.centre.z - half_extent_z(piece));
        bounds.z1 = std::max(bounds.z1, piece.centre.z + half_extent_z(piece));
    }
    return bounds;
}

bool overlap(const Bounds& a, const Bounds& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.z0 < b.z1 && b.z0 < a.z1;
}

void site_and_plans_are_pinned() {
    const auto& site = city::kMiandiCalleOchoSite;
    REQUIRE_NEAR(site.origin.x, 7000.0f, 1e-5f);
    REQUIRE_NEAR(site.origin.z, 8300.0f, 1e-5f);
    REQUIRE_NEAR(site.ground_m, 8.0f, 1e-5f);
    REQUIRE_NEAR(site.cos_yaw, 1.0f, 1e-5f);
    REQUIRE_NEAR(site.sin_yaw, 0.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_width_m, 160.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_depth_m, 150.0f, 1e-5f);
    REQUIRE(city::valid_building_plan(city::kMiandiSolCafePlan));
    REQUIRE(city::valid_building_plan(city::kMiandiMercadoPlan));
    REQUIRE(city::valid_building_plan(city::kMiandiCigarWorkshopPlan));
    REQUIRE(city::valid_building_plan(city::kMiandiCornerMusicBarPlan));
    openings_fit_inside_walls(city::kMiandiCafeWalls);
    openings_fit_inside_walls(city::kMiandiMercadoWalls);
    openings_fit_inside_walls(city::kMiandiCigarWalls);
    openings_fit_inside_walls(city::kMiandiMusicWalls);
    apricot_test::pass("CO-1 site and four building plans are valid");
}

void storefront_shells_do_not_overlap() {
    const city::BuildingPlan plans[] = {
        city::kMiandiSolCafePlan, city::kMiandiMercadoPlan,
        city::kMiandiCigarWorkshopPlan, city::kMiandiCornerMusicBarPlan};
    const Bounds bounds[] = {solid_plan_bounds(plans[0]), solid_plan_bounds(plans[1]),
                             solid_plan_bounds(plans[2]), solid_plan_bounds(plans[3])};
    for (std::size_t i = 0; i < std::size(bounds); ++i)
        for (std::size_t j = i + 1; j < std::size(bounds); ++j)
            REQUIRE_MSG(!overlap(bounds[i], bounds[j]),
                        "solid storefront shells overlap", plans[j].name);
    REQUIRE(bounds[0].x1 <= bounds[1].x0 + 1e-4f);
    REQUIRE(bounds[1].x1 <= bounds[2].x0 + 1e-4f);
    REQUIRE(bounds[2].x1 <= bounds[3].x0 + 1e-4f);
    apricot_test::pass("CO-1 storefront shell footprints are separate or party-wall adjacent");
}

void bake_is_deterministic_and_bounded() {
    const auto first = city::bake_miandi_calle_ocho();
    const auto second = city::bake_miandi_calle_ocho();
    REQUIRE(first.size() > 50u);
    REQUIRE(first.size() < 240u);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        REQUIRE(std::strcmp(first[i].name, second[i].name) == 0);
        REQUIRE_NEAR(first[i].centre.x, second[i].centre.x, 1e-5f);
        REQUIRE_NEAR(first[i].centre.z, second[i].centre.z, 1e-5f);
        REQUIRE_NEAR(first[i].height_m, second[i].height_m, 1e-5f);
        REQUIRE(first[i].solid == second[i].solid);
    }
    apricot_test::pass("CO-1 bake is deterministic and stays within its part budget");
}

void facade_palette_silhouettes_and_openings_are_distinct() {
    const auto parts = city::bake_miandi_calle_ocho();
    std::set<int> facade_finishes;
    std::set<int> facade_heights;
    for (const auto* wall_array : {city::kMiandiCafeWalls, city::kMiandiMercadoWalls,
                                   city::kMiandiCigarWalls, city::kMiandiMusicWalls}) {
        const city::BuildingWall& front = wall_array[0];
        facade_finishes.insert(static_cast<int>(front.finish));
        facade_heights.insert(static_cast<int>(front.height_m * 10.0f));
    }
    REQUIRE(facade_finishes.size() >= 4u);
    REQUIRE(facade_heights.size() >= 4u);

    int doors = 0;
    int windows = 0;
    for (const auto* wall_array : {city::kMiandiCafeWalls, city::kMiandiMercadoWalls,
                                   city::kMiandiCigarWalls, city::kMiandiMusicWalls})
        for (int wall_index = 0; wall_index < 4; ++wall_index)
            for (std::size_t i = 0; i < wall_array[wall_index].opening_count; ++i)
                if (wall_array[wall_index].openings[i].kind == city::OpeningKind::Door)
                    ++doors;
                else
                    ++windows;
    REQUIRE(doors >= 6);
    REQUIRE(windows >= 6);
    int awnings = 0;
    int roof_clutter = 0;
    for (const auto& p : parts) {
        if (std::strstr(p.name, "awning") != nullptr) ++awnings;
        if (std::strstr(p.name, "HVAC") != nullptr ||
            std::strstr(p.name, "vent") != nullptr)
            ++roof_clutter;
    }
    REQUIRE(awnings >= 3);
    REQUIRE(roof_clutter >= 5);
    apricot_test::pass("CO-1 has four facade palettes, silhouettes, real doors, windows, awnings, and roof clutter");
}

void clear_public_and_service_routes_and_collision_parity() {
    const auto parts = city::bake_miandi_calle_ocho();
    for (const auto& p : parts) {
        REQUIRE(p.width_m > 0.0f);
        REQUIRE(p.depth_m > 0.0f);
        REQUIRE(p.height_m > 0.0f);
        REQUIRE_MSG(std::fabs(p.centre.x) + half_extent_x(p) <= 80.01f,
                    "piece exceeds site width", p.name);
        const bool sidewalk_link =
            std::strcmp(p.name, "Calle Ocho continuous public pavement") == 0;
        REQUIRE_MSG(std::fabs(p.centre.z) + half_extent_z(p) <=
                        (sidewalk_link ? 90.01f : 75.01f),
                    "piece exceeds site/access depth", p.name);

        // Nothing solid may pinch the four-metre Calle Ocho walk or the six-
        // metre rear service passage.  The corridors are intentionally broad
        // enough to catch an accidental wall or HVAC placement.
        if (!is_walk_support(p))
            REQUIRE_MSG(!solid_overlaps(p, -76.0f, 76.0f, -75.0f, -59.0f),
                        "solid piece pinches public route", p.name);
        REQUIRE_MSG(!solid_overlaps(p, -76.0f, 76.0f, 42.0f, 75.0f),
                    "solid piece pinches service route", p.name);
    }
    bool solid = false;
    bool non_solid = false;
    for (const auto& p : parts) {
        if (p.solid) solid = true;
        else non_solid = true;
    }
    REQUIRE(solid && non_solid);
    apricot_test::pass("CO-1 keeps public/rear routes clear and preserves visible/collision flags");
}

void pavement_covers_public_and_rear_routes() {
    const auto parts = city::bake_miandi_calle_ocho();
    const city::BuildingPiece* public_strip = nullptr;
    const city::BuildingPiece* service_lane = nullptr;
    int connectors = 0;
    for (const auto& p : parts) {
        if (std::strstr(p.name, "paving") != nullptr ||
            std::strstr(p.name, "pavement") != nullptr ||
            std::strstr(p.name, "connector") != nullptr ||
            std::strstr(p.name, "service lane") != nullptr) {
            REQUIRE_MSG(!p.solid, "pavement must not be collision-solid", p.name);
        }
        if (std::strcmp(p.name, "Calle Ocho continuous public pavement") == 0)
            public_strip = &p;
        if (std::strcmp(p.name, "CO-1 rear service lane") == 0)
            service_lane = &p;
        if (std::strstr(p.name, "door connector") != nullptr) ++connectors;
    }
    REQUIRE(public_strip != nullptr);
    REQUIRE(service_lane != nullptr);
    REQUIRE(connectors == 7);
    REQUIRE(public_strip->centre.x - public_strip->width_m * .5f <= -67.9f);
    REQUIRE(public_strip->centre.x + public_strip->width_m * .5f >= 67.9f);
    REQUIRE_NEAR(public_strip->centre.z - public_strip->depth_m * .5f,
                 -90.0f, 1e-5f);
    REQUIRE_NEAR(public_strip->centre.z + public_strip->depth_m * .5f,
                 -55.0f, 1e-5f);
    REQUIRE(public_strip->depth_m >= 4.0f);
    REQUIRE(service_lane->centre.x - service_lane->width_m * .5f <= -67.9f);
    REQUIRE(service_lane->centre.x + service_lane->width_m * .5f >= 67.9f);
    REQUIRE(service_lane->depth_m >= 6.0f);
    apricot_test::pass("CO-1 has continuous non-solid Calle frontage, door connectors, and rear service pavement");
}

void height_and_road_clearance_are_bounded() {
    const auto parts = city::bake_miandi_calle_ocho();
    float tallest = 0.0f;
    for (const auto& p : parts) {
        tallest = std::max(tallest, p.bottom_m + p.height_m);
        if (!p.solid) continue;
        // Site-local road centerlines are Calle Ocho z=-100 and Solana x=100.
        REQUIRE(p.centre.z - half_extent_z(p) >= -86.0f);
        REQUIRE(p.centre.x + half_extent_x(p) <= 86.0f);
    }
    REQUIRE(tallest >= 9.0f);
    REQUIRE(tallest <= 14.0f);
    apricot_test::pass("CO-1 stays low-rise and solid geometry clears the road-centerline envelopes");
}

}  // namespace

int main() {
    site_and_plans_are_pinned();
    storefront_shells_do_not_overlap();
    bake_is_deterministic_and_bounded();
    facade_palette_silhouettes_and_openings_are_distinct();
    clear_public_and_service_routes_and_collision_parity();
    pavement_covers_public_and_rear_routes();
    height_and_road_clearance_are_bounded();
    return apricot_test::done("miandi_calle_ocho_tests");
}

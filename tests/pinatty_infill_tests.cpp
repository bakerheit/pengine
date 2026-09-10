#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "city/roads.h"
#include "city/pinatty_infill.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec2 world(const city::StartSite& site, city::Vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x +
                site.sin_yaw * local.z,
            site.origin.z - site.sin_yaw * local.x +
                site.cos_yaw * local.z};
}

float road_and_sidewalk_clearance(glm::vec2 point) {
    float clearance = 1e9f;
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const glm::vec2 a{road.path[i - 1].x, road.path[i - 1].z};
            const glm::vec2 b{road.path[i].x, road.path[i].z};
            const glm::vec2 delta = b - a;
            const float length_sq = glm::dot(delta, delta);
            if (length_sq <= 1e-5f) continue;
            const float t = std::clamp(glm::dot(point - a, delta) / length_sq,
                                       0.0f, 1.0f);
            const float road_half = road.width_m > 0.0f
                                        ? road.width_m * 0.5f
                                        : city::road_width_m(road.cls) * 0.5f;
            const float walk = city::road_has_sidewalks(road.cls)
                                   ? city::kWalkWidthM
                                   : 0.0f;
            clearance = std::min(
                clearance,
                glm::length(point - (a + delta * t)) - road_half - walk);
        }
    }
    return clearance;
}

bool sites_are_separate(const city::StartSite& a, const city::StartSite& b,
                        float margin_m = 2.0f) {
    const glm::vec2 delta{b.origin.x - a.origin.x,
                          b.origin.z - a.origin.z};
    const glm::vec2 local{a.cos_yaw * delta.x - a.sin_yaw * delta.y,
                          a.sin_yaw * delta.x + a.cos_yaw * delta.y};
    return std::fabs(local.x) >
               (a.lot_width_m + b.lot_width_m) * 0.5f + margin_m ||
           std::fabs(local.y) >
               (a.lot_depth_m + b.lot_depth_m) * 0.5f + margin_m;
}

void exact_perimeter_and_central_infill_is_authored() {
    REQUIRE(city::kPinattyInfillParcels.size() == 12u);
    const std::array<city::Vec2, 12> expected{{
        {-322.0f, -279.0f}, {-322.0f, -217.0f}, {-322.0f, -155.0f},
        {-322.0f, -93.0f}, {-322.0f, 31.0f}, {-322.0f, 93.0f},
        {-230.0f, 155.0f}, {-138.0f, 155.0f}, {-46.0f, 155.0f},
        {46.0f, 155.0f}, {-230.0f, -155.0f}, {-230.0f, 93.0f},
    }};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& site = city::kPinattyInfillParcels[i].site;
        REQUIRE(site.name != nullptr);
        REQUIRE_NEAR(site.cos_yaw, city::kGridCos, 1e-6f);
        REQUIRE_NEAR(site.sin_yaw, city::kGridSin, 1e-6f);
        REQUIRE(site.lot_width_m <= 54.0f);
        REQUIRE(site.lot_depth_m <= 38.0f);
        const city::Vec2 origin =
            city::pinatty_infill_grid_point(expected[i].x, expected[i].z);
        REQUIRE_NEAR(site.origin.x, origin.x, 1e-4f);
        REQUIRE_NEAR(site.origin.z, origin.z, 1e-4f);
    }
    for (std::size_t i = 0; i < city::kPinattyInfillParcels.size(); ++i) {
        for (std::size_t j = i + 1;
             j < city::kPinattyInfillParcels.size(); ++j) {
            REQUIRE_MSG(sites_are_separate(
                            city::kPinattyInfillParcels[i].site,
                            city::kPinattyInfillParcels[j].site),
                        "Pinatty infill lots overlap",
                        city::kPinattyInfillParcels[i].site.name);
        }
    }
    apricot_test::pass("twelve infill lots fill the intended perimeter and central gaps");
}

void parcels_are_flat_and_clear_of_road_ribbons() {
    for (const auto& parcel : city::kPinattyInfillParcels) {
        const auto& site = parcel.site;
        for (int xi = -2; xi <= 2; ++xi) {
            for (int zi = -2; zi <= 2; ++zi) {
                const city::Vec2 local{
                    static_cast<float>(xi) * site.lot_width_m * 0.25f,
                    static_cast<float>(zi) * site.lot_depth_m * 0.25f};
                const glm::vec2 point = world(site, local);
                REQUIRE_NEAR(mesh_height_at(city::kMapSeed, point.x, point.y),
                             site.ground_m, 0.02f);
                REQUIRE_MSG(road_and_sidewalk_clearance(point) > 1.5f,
                            "infill lot reaches a road or sidewalk",
                            site.name);
            }
        }
    }
    apricot_test::pass("all infill lots stay on the flat datum and clear road ribbons");
}

void require_low_part_inside_lot(const city::StartSite& site,
                                 const city::StartPart& part) {
    const float yaw = part.yaw_deg * 0.017453292519943f;
    const float cos_yaw = std::cos(yaw);
    const float sin_yaw = std::sin(yaw);
    for (float x_sign : {-1.0f, 1.0f}) {
        for (float z_sign : {-1.0f, 1.0f}) {
            const float x = x_sign * part.width_m * 0.5f;
            const float z = z_sign * part.depth_m * 0.5f;
            const float corner_x = part.centre.x + cos_yaw * x + sin_yaw * z;
            const float corner_z = part.centre.z - sin_yaw * x + cos_yaw * z;
            REQUIRE_MSG(std::fabs(corner_x - site.lot_centre.x) <=
                            site.lot_width_m * 0.5f + 0.02f,
                        "low infill piece leaves lot width", part.name);
            REQUIRE_MSG(std::fabs(corner_z - site.lot_centre.z) <=
                            site.lot_depth_m * 0.5f + 0.02f,
                        "low infill piece leaves lot depth", part.name);
        }
    }
}

void baked_parts_are_valid_bounded_and_collidable() {
    std::size_t total_parts = 0;
    std::size_t total_solids = 0;
    for (std::size_t i = 0; i < city::kPinattyInfillParcels.size(); ++i) {
        const auto& parcel = city::kPinattyInfillParcels[i];
        const auto parts = city::bake_pinatty_infill(i);
        REQUIRE(!parts.empty());
        REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
        REQUIRE(parts.size() >= 55u);
        REQUIRE(parts.size() <= 150u);
        total_parts += parts.size();

        std::size_t lots = 0;
        std::size_t walks = 0;
        std::size_t lobbies = 0;
        std::size_t solid_doors = 0;
        std::size_t structural_solids = 0;
        for (const auto& part : parts) {
            REQUIRE(std::strncmp(part.name, "infill ", 7u) == 0);
            lots += std::strcmp(part.name, "infill lot") == 0;
            walks += std::strcmp(part.name, "infill entrance walk") == 0;
            lobbies += std::strcmp(part.name, "infill lobby floor") == 0;
            solid_doors += part.solid &&
                std::strcmp(part.name, "infill entrance door") == 0;
            structural_solids += part.solid;
            if (part.bottom_m < 3.0f)
                require_low_part_inside_lot(parcel.site, part);
        }
        REQUIRE(lots == 1u);
        REQUIRE(walks == 1u);
        REQUIRE(lobbies == 1u);
        REQUIRE(solid_doors == 1u);
        REQUIRE(structural_solids >= 10u);
        total_solids += structural_solids;
        std::printf("  %s: %d floors, %zu pieces, %zu collision pieces\n",
                    parcel.site.name, parcel.floors, parts.size(),
                    structural_solids);
    }
    REQUIRE(total_parts < 1200u);
    REQUIRE(total_solids >= 100u);
    apricot_test::pass("baked infill geometry and collision share bounded StartPart sets");
}

bool has_part(const std::vector<city::StartPart>& parts, const char* name) {
    return std::any_of(parts.begin(), parts.end(), [&](const auto& part) {
        return std::strcmp(part.name, name) == 0;
    });
}

void facades_massing_and_roofs_really_vary() {
    std::set<int> floor_counts;
    std::set<int> piece_counts;
    std::set<unsigned int> finish_pairs;
    std::array<std::size_t, 3> roofline_counts{};
    float shortest = 1e9f;
    float tallest = 0.0f;
    for (std::size_t i = 0; i < city::kPinattyInfillParcels.size(); ++i) {
        const auto& parcel = city::kPinattyInfillParcels[i];
        const auto parts = city::bake_pinatty_infill(i);
        floor_counts.insert(parcel.floors);
        piece_counts.insert(static_cast<int>(parts.size()));
        const auto facade = static_cast<unsigned int>(parcel.facade_finish);
        const auto trim = static_cast<unsigned int>(parcel.trim_finish);
        finish_pairs.insert(facade * 32u + trim);
        const auto roofline = static_cast<std::size_t>(parcel.roofline);
        REQUIRE(roofline < roofline_counts.size());
        ++roofline_counts[roofline];
        shortest = std::min(shortest, city::pinatty_infill_eave_height(parcel));
        tallest = std::max(tallest, city::pinatty_infill_eave_height(parcel));

        REQUIRE(has_part(parts, "infill shopfront glass"));
        REQUIRE(has_part(parts, "infill apartment window"));
        REQUIRE(has_part(parts, "infill rear service door"));
        REQUIRE(has_part(parts, "infill rear utility cabinet"));
        REQUIRE(has_part(parts, "infill rooftop water tank") ||
                has_part(parts, "infill rooftop hvac") ||
                has_part(parts, "infill rooftop stair house"));
        if (parcel.roofline == city::PinattyInfillRoofline::Stepped)
            REQUIRE(has_part(parts, "infill upper setback mass"));
        if (parcel.roofline == city::PinattyInfillRoofline::Gabled)
            REQUIRE(has_part(parts, "infill gable end wall"));
    }
    REQUIRE(floor_counts.size() >= 6u);
    REQUIRE(piece_counts.size() >= 6u);
    REQUIRE(finish_pairs.size() >= 6u);
    REQUIRE(roofline_counts[0] >= 3u);
    REQUIRE(roofline_counts[1] >= 3u);
    REQUIRE(roofline_counts[2] >= 3u);
    REQUIRE(shortest < 13.0f);
    REQUIRE(tallest > 28.0f);
    apricot_test::pass("infill has varied heights, finishes, massing and roof silhouettes");
}

}  // namespace

int main() {
    exact_perimeter_and_central_infill_is_authored();
    parcels_are_flat_and_clear_of_road_ribbons();
    baked_parts_are_valid_bounded_and_collidable();
    facades_massing_and_roofs_really_vary();
    return apricot_test::done("pinatty_infill_tests");
}

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "city/miandi_context.h"
#include "test_assert.h"

using namespace apricot;
using city::Vec2;

namespace {

bool same_point(Vec2 a, Vec2 b) { return a.x == b.x && a.z == b.z; }

float segment_distance(Vec2 point, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float length2 = dx * dx + dz * dz;
    const float t = length2 > 0.0f
                        ? std::clamp(((point.x - a.x) * dx +
                                      (point.z - a.z) * dz) /
                                         length2,
                                     0.0f, 1.0f)
                        : 0.0f;
    const float nearest_x = a.x + t * dx;
    const float nearest_z = a.z + t * dz;
    const float ex = point.x - nearest_x;
    const float ez = point.z - nearest_z;
    return std::sqrt(ex * ex + ez * ez);
}

bool is_visual_only(std::string_view name) {
    for (const std::string_view token : {
             "awning", "balcony", "coping", "crown", "decorative fin",
             "downpipe", "fountain", "gantry", "glazing", "lot",
             "parapet", "plant", "plaza", "roof", "room window",
             "sealed storefront", "service yard", "sill", "storefront",
             "valance", "vent", "window frame", "window head", "barrier"}) {
        if (name.find(token) != std::string_view::npos) return true;
    }
    return false;
}

bool is_low_rise_detail(std::string_view name) {
    return name.find("miandi infill low-rise") != std::string_view::npos &&
           (name.find("window") != std::string_view::npos ||
            name.find("storefront") != std::string_view::npos ||
            name.find("valance") != std::string_view::npos ||
            name.find("coping") != std::string_view::npos ||
            name.find("vent cap") != std::string_view::npos ||
            name.find("downpipe") != std::string_view::npos ||
            name.find("decorative fin") != std::string_view::npos ||
            name.find("balcony") != std::string_view::npos);
}

std::size_t nearest_block(const city::BuildingPiece& piece) {
    std::size_t best = 0;
    float best_d2 = 1.0e30f;
    for (std::size_t i = 0; i < city::kMiandiContextBlockCount; ++i) {
        const float dx = piece.centre.x - city::kMiandiContextBlockCenters[i].x;
        const float dz = piece.centre.z - city::kMiandiContextBlockCenters[i].z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

void occupied_centres_are_exact() {
    constexpr Vec2 expected[] = {
        {-300.0f, -100.0f}, {-100.0f, -100.0f}, {300.0f, -100.0f},
        {-500.0f, 100.0f},  {-300.0f, 100.0f},  {-100.0f, 100.0f},
        {100.0f, 100.0f},   {300.0f, 100.0f},   {500.0f, 100.0f},
        {-500.0f, 300.0f},  {-300.0f, 300.0f},  {300.0f, 300.0f},
    };
    REQUIRE(city::kMiandiContextBlockCount == 12u);
    for (std::size_t i = 0; i < 12u; ++i) {
        REQUIRE(same_point(city::kMiandiContextBlockCenters[i], expected[i]));
        REQUIRE(city::miandi_context_block_is_replaced(i) ==
                (i == 3u || i == 4u || i == 7u || i == 8u));
        for (std::size_t j = i + 1u; j < 12u; ++j)
            REQUIRE(!same_point(city::kMiandiContextBlockCenters[i],
                                city::kMiandiContextBlockCenters[j]));
    }
    constexpr Vec2 heroes[] = {
        {-500.0f, -100.0f}, {100.0f, -100.0f},
        {500.0f, -100.0f}, {-100.0f, 300.0f},
    };
    for (const Vec2 hero : heroes) {
        for (const Vec2 centre : city::kMiandiContextBlockCenters)
            REQUIRE(!same_point(hero, centre));
    }
    apricot_test::pass("Miandi context occupies the exact twelve non-hero blocks");
}

void pieces_are_deterministic_bounded_and_varied() {
    const std::vector<city::BuildingPiece> a = city::bake_miandi_context();
    const std::vector<city::BuildingPiece> b = city::bake_miandi_context();
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() >= 360u && a.size() < 650u);
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].name != nullptr);
        REQUIRE(std::string_view(a[i].name) == std::string_view(b[i].name));
        REQUIRE(a[i].centre.x == b[i].centre.x);
        REQUIRE(a[i].centre.z == b[i].centre.z);
        REQUIRE(a[i].bottom_m == b[i].bottom_m);
        REQUIRE(a[i].width_m == b[i].width_m);
        REQUIRE(a[i].height_m == b[i].height_m);
        REQUIRE(a[i].depth_m == b[i].depth_m);
        REQUIRE(a[i].finish == b[i].finish);
        REQUIRE(a[i].solid == b[i].solid);
    }

    int lot_or_paving = 0;
    int solid_count = 0;
    float min_solid_height = 1.0e30f;
    float max_solid_height = 0.0f;
    bool has_glass = false;
    bool has_concrete = false;
    bool has_steel = false;
    bool has_warm = false;
    for (const city::BuildingPiece& piece : a) {
        const std::string_view name = piece.name;
        if (name.find("lot") != std::string_view::npos ||
            name.find("plaza") != std::string_view::npos ||
            name.find("yard") != std::string_view::npos)
            ++lot_or_paving;
        if (piece.solid) {
            ++solid_count;
            min_solid_height = std::min(min_solid_height, piece.height_m);
            max_solid_height = std::max(max_solid_height, piece.height_m);
        }
        has_glass |= piece.finish == city::BuildingFinish::Glass;
        has_concrete |= piece.finish == city::BuildingFinish::Concrete;
        has_steel |= piece.finish == city::BuildingFinish::Steel;
        has_warm |= piece.finish == city::BuildingFinish::WarmWall;
    }
    REQUIRE(lot_or_paving >= 8);
    REQUIRE(solid_count >= 20);
    REQUIRE(min_solid_height < 8.0f && max_solid_height > 40.0f);
    REQUIRE(has_glass && has_concrete && has_steel && has_warm);
    apricot_test::pass("Miandi context bake is deterministic, bounded, and varied");
}

void low_rise_facades_are_complete_closed_and_clear_service_space() {
    const auto pieces = city::bake_miandi_context();
    for (std::size_t block : {0u, 1u, 2u}) {
        std::size_t detail_count = 0;
        std::size_t room_glass = 0;
        std::size_t window_depth_parts = 0;
        std::size_t storefront_glass = 0;
        std::size_t valances = 0;
        std::size_t roof_service = 0;
        std::size_t apartment_detail = 0;
        std::size_t east_front_room_glass = 0;
        std::size_t east_front_storefront_glass = 0;
        std::size_t east_front_window_frames = 0;
        std::size_t exterior_downpipes = 0;
        for (const city::BuildingPiece& piece : pieces) {
            if (nearest_block(piece) != block) continue;
            const std::string_view name = piece.name;
            REQUIRE(name.find("door") == std::string_view::npos);
            if (!is_low_rise_detail(name)) continue;
            ++detail_count;
            REQUIRE(!piece.solid);
            room_glass += name.find("room window glass") != std::string_view::npos;
            window_depth_parts += name.find("window sill") != std::string_view::npos ||
                                  name.find("window head") != std::string_view::npos ||
                                  name.find("window frame") != std::string_view::npos;
            storefront_glass += name.find("sealed storefront glazing") !=
                               std::string_view::npos;
            valances += name.find("valance") != std::string_view::npos;
            roof_service += name.find("coping") != std::string_view::npos ||
                            name.find("vent cap") != std::string_view::npos ||
                            name.find("downpipe") != std::string_view::npos;
            apartment_detail += name.find("decorative fin") != std::string_view::npos ||
                                name.find("balcony") != std::string_view::npos;

            const Vec2 centre = city::kMiandiContextBlockCenters[block];
            if (name.find("east front room window glass") !=
                std::string_view::npos) {
                ++east_front_room_glass;
                REQUIRE(piece.centre.z < centre.z - 26.0f);
            }
            if (name.find("east front sealed storefront glazing") !=
                std::string_view::npos) {
                ++east_front_storefront_glass;
                REQUIRE(piece.centre.z < centre.z - 26.0f);
            }
            east_front_window_frames +=
                name.find("east front window frame") != std::string_view::npos;
            if (name == "miandi infill low-rise downpipe west") {
                ++exterior_downpipes;
                REQUIRE(piece.centre.x < centre.x - 55.0f);
            }
            if (name == "miandi infill low-rise downpipe east") {
                ++exterior_downpipes;
                REQUIRE(piece.centre.x > centre.x + 55.0f);
            }

            // The old rear service strip is central to each block. New facade
            // detail may wrap an outer edge, but cannot intrude into this lane.
            const float left = piece.centre.x - piece.width_m * 0.5f;
            const float right = piece.centre.x + piece.width_m * 0.5f;
            const float front = piece.centre.z - piece.depth_m * 0.5f;
            const float rear = piece.centre.z + piece.depth_m * 0.5f;
            const bool overlaps_rear_lane = left < centre.x + 45.0f &&
                                            right > centre.x - 45.0f &&
                                            front < centre.z + 56.0f &&
                                            rear > centre.z + 36.0f;
            REQUIRE(!overlaps_rear_lane);
        }
        REQUIRE(detail_count >= 100u && detail_count <= 150u);
        REQUIRE(room_glass >= 19u);
        REQUIRE(window_depth_parts >= 40u);
        REQUIRE(storefront_glass == 9u);
        REQUIRE(valances == 9u);
        REQUIRE(roof_service >= 8u);
        REQUIRE(apartment_detail >= 8u);
        REQUIRE(east_front_room_glass == 5u);
        REQUIRE(east_front_storefront_glass == 5u);
        REQUIRE(east_front_window_frames == 10u);
        REQUIRE(exterior_downpipes == 2u);
    }
    apricot_test::pass("low-rise blocks have closed layered facades and clear rear service lanes");
}

void solids_fit_parcels_clear_roads_and_heroes() {
    const auto pieces = city::bake_miandi_context();
    constexpr float grid_x[] = {-600.0f, -400.0f, -200.0f, 0.0f,
                                200.0f, 400.0f, 600.0f};
    constexpr float grid_z[] = {-200.0f, 0.0f, 200.0f, 400.0f};
    constexpr Vec2 diagonal_a[] = {
        {-500.0f, -400.0f}, {-400.0f, -360.0f}, {-200.0f, -280.0f},
        {0.0f, 200.0f},     {200.0f, 400.0f},   {600.0f, -200.0f},
        {600.0f, 200.0f},
    };
    constexpr Vec2 diagonal_b[] = {
        {-400.0f, -360.0f}, {-200.0f, -280.0f}, {0.0f, -200.0f},
        {200.0f, 400.0f},   {500.0f, 400.0f},   {600.0f, 200.0f},
        {500.0f, 400.0f},
    };
    constexpr Vec2 heroes[] = {
        {-500.0f, -100.0f}, {100.0f, -100.0f},
        {500.0f, -100.0f},  {-100.0f, 300.0f},
    };

    for (const city::BuildingPiece& piece : pieces) {
        const std::size_t block = nearest_block(piece);
        REQUIRE_MSG(!city::miandi_context_block_is_replaced(block),
                    "finished nightlife parcel still has context massing",
                    piece.name);
        const Vec2 centre = city::kMiandiContextBlockCenters[block];
        const float left = piece.centre.x - piece.width_m * 0.5f;
        const float right = piece.centre.x + piece.width_m * 0.5f;
        const float top = piece.centre.z - piece.depth_m * 0.5f;
        const float bottom = piece.centre.z + piece.depth_m * 0.5f;
        REQUIRE(left >= centre.x - 80.0f && right <= centre.x + 80.0f);
        REQUIRE(top >= centre.z - 75.0f && bottom <= centre.z + 75.0f);
        if (piece.solid) {
            for (float x : grid_x) {
                REQUIRE(std::fabs(piece.centre.x - x) - piece.width_m * 0.5f >
                        14.0f);
            }
            for (float z : grid_z) {
                REQUIRE(std::fabs(piece.centre.z - z) - piece.depth_m * 0.5f >
                        14.0f);
            }
            for (std::size_t i = 0; i < sizeof(diagonal_a) / sizeof(diagonal_a[0]);
                 ++i) {
                REQUIRE(segment_distance(piece.centre, diagonal_a[i],
                                         diagonal_b[i]) -
                            std::max(piece.width_m, piece.depth_m) * 0.5f >
                        14.0f);
            }
            for (const Vec2 hero : heroes) {
                const bool overlap = left < hero.x + 80.0f &&
                                     right > hero.x - 80.0f &&
                                     top < hero.z + 75.0f &&
                                     bottom > hero.z - 75.0f;
                REQUIRE(!overlap);
            }
        }
        if (is_visual_only(piece.name)) REQUIRE(!piece.solid);
    }
    apricot_test::pass("Miandi context solids fit parcels, clear roads, and avoid heroes");
}

}  // namespace

int main() {
    occupied_centres_are_exact();
    pieces_are_deterministic_bounded_and_varied();
    low_rise_facades_are_complete_closed_and_clear_service_space();
    solids_fit_parcels_clear_roads_and_heroes();
    return apricot_test::done("miandi_context_tests");
}

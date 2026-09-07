#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "city/miandi_ocean_drive.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool has(const std::vector<city::StartPart>& parts, const char* needle) {
    return std::any_of(parts.begin(), parts.end(), [needle](const auto& p) {
        return p.name && std::strstr(p.name, needle) != nullptr;
    });
}

const city::StartPart* named(const std::vector<city::StartPart>& parts,
                             const char* needle) {
    for (const auto& p : parts)
        if (p.name && std::strcmp(p.name, needle) == 0) return &p;
    return nullptr;
}

bool covers_route(const city::StartPart& p, float start_x, float end_x,
                  float z) {
    const float x0 = p.centre.x - p.width_m * .5f;
    const float x1 = p.centre.x + p.width_m * .5f;
    const float z0 = p.centre.z - p.depth_m * .5f;
    const float z1 = p.centre.z + p.depth_m * .5f;
    return !p.solid && x0 <= start_x + .01f && x1 >= end_x - .01f &&
           z0 <= z && z1 >= z;
}

std::size_t count_named(const std::vector<city::StartPart>& parts,
                        const char* needle) {
    return static_cast<std::size_t>(std::count_if(
        parts.begin(), parts.end(), [needle](const auto& p) {
            return p.name && std::strstr(p.name, needle) != nullptr;
        }));
}

bool overlaps_xz(const city::StartPart& p, float min_x, float max_x,
                 float min_z, float max_z) {
    return p.centre.x + p.width_m * .5f > min_x &&
           p.centre.x - p.width_m * .5f < max_x &&
           p.centre.z + p.depth_m * .5f > min_z &&
           p.centre.z - p.depth_m * .5f < max_z;
}

void require_side_window_layer(const std::vector<city::StartPart>& parts,
                               const char* name, std::size_t expected_count,
                               float min_x, float max_x, float min_z,
                               float max_z, float outward_offset,
                               float roof_top) {
    std::size_t low_end = 0, high_end = 0;
    for (const auto& p : parts) {
        if (!p.name || std::strcmp(p.name, name) != 0) continue;
        REQUIRE(!p.solid);
        REQUIRE(p.centre.x - p.width_m * .5f >= min_x - .01f);
        REQUIRE(p.centre.x + p.width_m * .5f <= max_x + .01f);
        REQUIRE(p.bottom_m + p.height_m <= roof_top + .01f);
        if (p.centre.z < (min_z + max_z) * .5f) {
            REQUIRE_NEAR(p.centre.z, min_z - outward_offset, 1e-4f);
            ++low_end;
        } else {
            REQUIRE_NEAR(p.centre.z, max_z + outward_offset, 1e-4f);
            ++high_end;
        }
    }
    REQUIRE(low_end == expected_count / 2);
    REQUIRE(high_end == expected_count / 2);
}

void site_contract_and_identity() {
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.origin.x, 8000.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.origin.z, 8300.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.ground_m, 8.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.lot_width_m, 166.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.lot_centre.x, 3.0f, 1e-5);
    REQUIRE_NEAR(city::kMiandiOceanDriveSite.lot_depth_m, 150.0f, 1e-5);
    REQUIRE(std::strcmp(city::kCoralCrownHotelPlan.name,
                        city::kBlueHeronHotelPlan.name) != 0);
    REQUIRE(city::kMiandiNorthPromenadeSite.origin.x >
            city::kMiandiOceanDriveSite.origin.x);
    REQUIRE_NEAR(city::kMiandiNorthPromenadeSite.origin.x -
                     city::kMiandiNorthPromenadeSite.lot_width_m * .5f,
                 8114.0f, 1e-5f);
    REQUIRE(city::kMiandiNorthPromenadeSite.origin.x +
                city::kMiandiNorthPromenadeSite.lot_width_m * .5f <=
            8400.0f);
    apricot_test::pass("OD-1 and promenade have separated authored sites");
}

void hotel_bakes_have_doors_paths_and_budget() {
    const auto parts = city::bake_miandi_ocean_drive();
    REQUIRE(parts.size() > 30u);
    // Two layered tube-letter hotel names and double-sided HOTEL blades.
    REQUIRE(parts.size() < 1600u);
    REQUIRE(has(parts, "Bellmar lobby door"));
    REQUIRE(has(parts, "Maravelle lobby door"));
    REQUIRE(has(parts, "Bellmar lobby floor"));
    REQUIRE(has(parts, "Maravelle lobby floor"));
    REQUIRE(has(parts, "Bellmar lobby walk"));
    REQUIRE(has(parts, "Maravelle lobby walk"));
    REQUIRE(has(parts, "Bellmar rooftop HVAC"));
    REQUIRE(has(parts, "Maravelle rooftop HVAC"));
    REQUIRE(has(parts, "Ocean Drive cafe court"));
    REQUIRE(has(parts, "Bellmar lettering"));
    REQUIRE(has(parts, "Maravelle lettering"));

    const auto* coral_walk = named(parts, "Bellmar lobby walk");
    const auto* heron_walk = named(parts, "Maravelle lobby walk");
    const auto* coral_floor = named(parts, "Bellmar lobby floor");
    const auto* heron_floor = named(parts, "Maravelle lobby floor");
    REQUIRE(coral_walk && heron_walk && coral_floor && heron_floor);
    REQUIRE(!coral_floor->solid && !heron_floor->solid);
    REQUIRE_NEAR(coral_floor->centre.x, 53.0f, 1e-5f);
    REQUIRE_NEAR(coral_floor->centre.z, -29.0f, 1e-5f);
    REQUIRE_NEAR(heron_floor->centre.x, 57.0f, 1e-5f);
    REQUIRE_NEAR(heron_floor->centre.z, 28.0f, 1e-5f);
    REQUIRE_NEAR(coral_floor->bottom_m + coral_floor->height_m, .20f, 1e-5f);
    REQUIRE_NEAR(heron_floor->bottom_m + heron_floor->height_m, .20f, 1e-5f);
    // Floor extents stay inside the actual shell wall centres, while spanning
    // each lobby threshold so the door no longer opens onto grass.
    REQUIRE(coral_floor->centre.x - coral_floor->width_m * .5f > 28.0f);
    REQUIRE(coral_floor->centre.x + coral_floor->width_m * .5f < 78.0f);
    REQUIRE(coral_floor->centre.z - coral_floor->depth_m * .5f > -54.0f);
    REQUIRE(coral_floor->centre.z + coral_floor->depth_m * .5f < -4.0f);
    REQUIRE(heron_floor->centre.x - heron_floor->width_m * .5f > 36.0f);
    REQUIRE(heron_floor->centre.x + heron_floor->width_m * .5f < 78.0f);
    REQUIRE(heron_floor->centre.z - heron_floor->depth_m * .5f > 4.0f);
    REQUIRE(heron_floor->centre.z + heron_floor->depth_m * .5f < 52.0f);
    REQUIRE(covers_route(*coral_floor, 77.7f, 77.7f, -29.0f));
    REQUIRE(covers_route(*heron_floor, 77.7f, 77.7f, 28.0f));
    REQUIRE(covers_route(*coral_walk, 78.0f, 86.0f, -29.0f));
    REQUIRE(covers_route(*heron_walk, 78.0f, 86.0f, 28.0f));
    REQUIRE_NEAR(coral_walk->width_m, 8.0f, 1e-5f);
    REQUIRE_NEAR(heron_walk->width_m, 8.0f, 1e-5f);
    REQUIRE(!coral_walk->solid && !heron_walk->solid);
    REQUIRE(coral_walk->centre.z + coral_walk->depth_m * .5f <
            heron_walk->centre.z - heron_walk->depth_m * .5f);
    for (const auto& p : parts) {
        if (p.name && (std::strstr(p.name, "lobby walk") ||
                       std::strstr(p.name, "threshold") ||
                       std::strstr(p.name, "cafe court") ||
                       std::strstr(p.name, "service lane")))
            REQUIRE(!p.solid);
    }

    bool coral = false, heron = false;
    for (const auto& p : parts) {
        if (p.name && std::strstr(p.name, "Bellmar")) coral = true;
        if (p.name && std::strstr(p.name, "Maravelle")) heron = true;
        // Neon strokes rotate a vertical primitive into the facade plane.
        // Measure its transformed bounds, not the unrotated length.
        const float pitch = p.pitch_deg * .01745329252f;
        const float centre_y = p.bottom_m + p.height_m * .5f;
        const float extent_y = .5f * (std::fabs(std::cos(pitch)) * p.height_m +
                                      std::fabs(std::sin(pitch)) * p.depth_m);
        REQUIRE(centre_y - extent_y >= -1e-4f);
        REQUIRE(centre_y + extent_y < 25.0f);
        if (p.solid) {
            REQUIRE(p.centre.x > -80.0f && p.centre.x < 80.0f);
            REQUIRE(p.centre.z > -75.0f && p.centre.z < 75.0f);
        }
    }
    REQUIRE(coral && heron);
    std::size_t neon = 0;
    for (const auto& p : parts) {
        if (!p.name || !std::strstr(p.name, "miandi neon")) continue;
        REQUIRE(!p.solid);
        ++neon;
    }
    REQUIRE(neon >= 6u);
    apricot_test::pass("hotel identities, circulation pieces, height and count are bounded");
}

void hotel_room_rhythm_balconies_and_clear_entries() {
    const auto parts = city::bake_miandi_ocean_drive();
    // Each bay is four separate layers: reveal, recessed glass, sill and
    // mullion. These counts prove a room rhythm over several storeys, rather
    // than a few decorative panes near the sign.
    REQUIRE(count_named(parts, "Bellmar front room window bay") >= 64u);
    REQUIRE(count_named(parts, "Maravelle front room window bay") >= 36u);
    REQUIRE(count_named(parts, "Bellmar side room window") >= 64u);
    REQUIRE(count_named(parts, "Maravelle side room window") >= 48u);
    REQUIRE(count_named(parts, "Bellmar balcony assembly") >= 24u);
    REQUIRE(count_named(parts, "Maravelle balcony assembly") >= 24u);
    REQUIRE(has(parts, "Bellmar Deco centre spine"));
    REQUIRE(has(parts, "Maravelle Deco corner pilaster"));

    std::size_t coral_floors = 0, heron_floors = 0;
    for (const auto& p : parts) {
        if (!p.name || !std::strstr(p.name, "balcony assembly")) continue;
        REQUIRE(!p.solid);
        REQUIRE(p.bottom_m >= 4.5f);  // visual-only detail stays overhead
        // Balcony slabs and rails are long in Z, shallow in X: no more axes-
        // swapped seven-metre projections into the hotel approach.
        REQUIRE(p.width_m <= 2.4f);
        REQUIRE(p.height_m <= .8f);  // rails are open members, not a slab
        if (std::strstr(p.name, "Bellmar")) ++coral_floors;
        if (std::strstr(p.name, "Maravelle")) ++heron_floors;
    }
    REQUIRE(coral_floors >= 24u && heron_floors >= 24u);

    for (const auto& p : parts) {
        if (!p.name || (!std::strstr(p.name, "room window") &&
                        !std::strstr(p.name, "balcony assembly")))
            continue;
        REQUIRE(!p.solid);
        // Decorative hotel work cannot invade either 4.8 m-wide lobby route
        // below canopy height, even though it is visual-only today.
        if (overlaps_xz(p, 78.0f, 86.0f, -31.4f, -26.6f) ||
            overlaps_xz(p, 78.0f, 86.0f, 25.6f, 30.4f))
            REQUIRE(p.bottom_m >= 4.3f);
    }

    // Both end walls use the actual shell extents. Backing starts outside the
    // 30 cm wall; glass, sill and mullion then step farther outward in order.
    for (const auto& layer : std::array<std::pair<const char*, float>, 4>{{
             {"Bellmar side room window backing", .24f},
             {"Bellmar side room window glass", .36f},
             {"Bellmar side room window sill", .48f},
             {"Bellmar side room window mullion", .52f}}})
        require_side_window_layer(parts, layer.first, 16u, 28.0f, 78.0f,
                                  -54.0f, -4.0f, layer.second, 18.0f);
    for (const auto& layer : std::array<std::pair<const char*, float>, 4>{{
             {"Maravelle side room window backing", .24f},
             {"Maravelle side room window glass", .36f},
             {"Maravelle side room window sill", .48f},
             {"Maravelle side room window mullion", .52f}}})
        require_side_window_layer(parts, layer.first, 12u, 36.0f, 78.0f,
                                  4.0f, 52.0f, layer.second, 14.8f);
    apricot_test::pass("hotel room bays are layered, balconies face the facade, and entries stay clear");
}

void both_hotels_form_a_close_street_wall() {
    // Pin the wall planes as well as the walkway length: a short fake path
    // must not pass while the actual shell/map footprint remains in the lawn.
    for (const auto* facade : {&city::kCoralCrownWalls[1],
                               &city::kBlueHeronWalls[1]}) {
        REQUIRE_NEAR(facade->a.x, city::kMiandiOceanHotelFrontX, 1e-5f);
        REQUIRE_NEAR(facade->b.x, city::kMiandiOceanHotelFrontX, 1e-5f);
    }
    REQUIRE_NEAR(city::kMiandiOceanSidewalkX - city::kMiandiOceanHotelFrontX,
                 8.0f, 1e-5f);
    for (const auto* roof : {&city::kCoralCrownRoofs[0],
                            &city::kBlueHeronRoofs[0]})
        REQUIRE_NEAR(roof->centre.x + roof->width_m * .5f, 79.0f, 1e-5f);

    const auto parts = city::bake_miandi_ocean_drive();
    for (const auto& p : parts) {
        // Full transformed east bounds include rotated HOTEL blade strokes.
        REQUIRE(p.roll_deg == 0.0f);
        const float yaw = p.yaw_deg * .01745329252f;
        const float pitch = p.pitch_deg * .01745329252f;
        const float z_extent = (std::fabs(std::sin(pitch)) * p.height_m +
                                std::fabs(std::cos(pitch)) * p.depth_m) * .5f;
        const float x_extent = std::fabs(std::cos(yaw)) * p.width_m * .5f +
                                std::fabs(std::sin(yaw)) * z_extent;
        REQUIRE_MSG(p.centre.x + x_extent <= 86.01f,
                    "moved hotel detail enters public sidewalk", p.name);
        if (std::strstr(p.name, "name lettering")) {
            const float sign_z = std::strstr(p.name, "Bellmar") ? -29.f : 28.f;
            REQUIRE_MSG(std::fabs(p.centre.z - sign_z) + z_extent < 12.3f,
                        "hotel name spills off its 25 m backing", p.name);
        }
    }
    const auto* cafe = named(parts, "Ocean Drive cafe court");
    REQUIRE(cafe);
    REQUIRE(cafe->centre.z + cafe->depth_m * .5f < -54.15f);
    REQUIRE_NEAR(cafe->centre.x + cafe->width_m * .5f, 86.0f, 1e-5f);
    apricot_test::pass("both hotel shells and map roofs align 8 m from sidewalk with clear cafe and overhangs");
}

void promenade_is_low_open_and_nonbuilding() {
    const auto parts = city::bake_miandi_north_promenade();
    REQUIRE(parts.size() > 20u);
    REQUIRE(parts.size() < 70u);
    REQUIRE(has(parts, "broad clear walk"));
    REQUIRE(has(parts, "Ocean Drive connector"));
    REQUIRE(has(parts, "shade pavilion roof"));
    REQUIRE(has(parts, "low seawall"));
    REQUIRE(has(parts, "palm crown"));
    const auto* connector = named(parts, "north promenade Ocean Drive connector");
    const auto* broad = named(parts, "north promenade broad clear walk");
    REQUIRE(connector && broad);
    REQUIRE(covers_route(*connector, -108.0f, -12.0f, 0.0f));
    REQUIRE(connector->centre.x + connector->width_m * .5f >=
            broad->centre.x - broad->width_m * .5f);
    REQUIRE(!connector->solid && !broad->solid);
    REQUIRE_NEAR(city::kMiandiNorthPromenadeSite.origin.x +
                     connector->centre.x - connector->width_m * .5f,
                 8114.0f, 1e-5f);

    const auto hotels = city::bake_miandi_ocean_drive();
    const auto* service = named(hotels, "Ocean Drive hotel service lane");
    REQUIRE(service);
    REQUIRE(covers_route(*service, -90.0f, -54.0f, 0.0f));
    REQUIRE(!service->solid);

    std::vector<std::pair<float, float>> seawall_ranges;
    for (const auto& p : parts) {
        if (p.name && std::strstr(p.name, "low seawall")) {
            REQUIRE(p.width_m < p.depth_m);
            REQUIRE(p.centre.x > 100.0f);
            seawall_ranges.push_back({p.centre.z - p.depth_m * .5f,
                                      p.centre.z + p.depth_m * .5f});
        }
    }
    REQUIRE(seawall_ranges.size() == 4u);
    std::sort(seawall_ranges.begin(), seawall_ranges.end());
    int gaps = 0;
    for (std::size_t i = 1; i < seawall_ranges.size(); ++i)
        if (seawall_ranges[i].first - seawall_ranges[i - 1].second > 4.0f)
            ++gaps;
    REQUIRE(gaps == 3);
    for (const auto& p : parts) {
        REQUIRE(p.bottom_m + p.height_m < 5.0f);
        if (p.name && (std::strstr(p.name, "palm") ||
                       std::strstr(p.name, "shade pavilion roof") ||
                       std::strstr(p.name, "bench back")))
            REQUIRE(!p.solid);
    }
    apricot_test::pass("promenade stays low, open, and keeps foliage/trim non-solid");
}

void bake_is_deterministic_and_collision_parity_is_explicit() {
    const auto a = city::bake_miandi_ocean_drive();
    const auto b = city::bake_miandi_ocean_drive();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE(a[i].solid == b[i].solid);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-5);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-5);
        REQUIRE_NEAR(a[i].height_m, b[i].height_m, 1e-5);
    }
    REQUIRE(city::valid_building_plan(city::kCoralCrownHotelPlan));
    REQUIRE(city::valid_building_plan(city::kBlueHeronHotelPlan));
    apricot_test::pass("visible and solid flags come from one deterministic bake");
}

}  // namespace

int main() {
    site_contract_and_identity();
    hotel_bakes_have_doors_paths_and_budget();
    hotel_room_rhythm_balconies_and_clear_entries();
    both_hotels_form_a_close_street_wall();
    promenade_is_low_open_and_nonbuilding();
    bake_is_deterministic_and_collision_parity_is_explicit();
    return 0;
}

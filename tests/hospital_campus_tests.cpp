#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/building_access.h"
#include "city/hospital_exterior.h"
#include "city/roads.h"
#include "city/spines.h"
#include "test_assert.h"

using namespace apricot;

namespace {

std::size_t named_count(const std::vector<city::StartPart>& parts,
                        const char* name) {
    std::size_t count = 0;
    for (const auto& part : parts)
        count += std::strcmp(part.name, name) == 0;
    return count;
}

bool horizontal_overlap(const city::StartPart& part, float min_x, float max_x,
                        float min_z, float max_z) {
    return part.centre.x + part.width_m * 0.5f > min_x &&
           part.centre.x - part.width_m * 0.5f < max_x &&
           part.centre.z + part.depth_m * 0.5f > min_z &&
           part.centre.z - part.depth_m * 0.5f < max_z;
}

bool named_wall_blocks_point(const std::vector<city::StartPart>& parts,
                             const char* name, float x, float z, float y) {
    constexpr float kPi = 3.14159265358979323846f;
    for (const auto& part : parts) {
        if (!part.solid || std::strcmp(part.name, name) != 0 ||
            y < part.bottom_m || y > part.bottom_m + part.height_m)
            continue;
        const float yaw = part.yaw_deg * kPi / 180.0f;
        const float dx = x - part.centre.x;
        const float dz = z - part.centre.z;
        const float local_x = std::cos(yaw) * dx - std::sin(yaw) * dz;
        const float local_z = std::sin(yaw) * dx + std::cos(yaw) * dz;
        if (std::fabs(local_x) < part.width_m * 0.5f &&
            std::fabs(local_z) < part.depth_m * 0.5f)
            return true;
    }
    return false;
}

float nearest_road_centreline(city::Vec2 point) {
    float nearest = 1e9f;
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const auto& a = road.path[i - 1];
            const auto& b = road.path[i];
            const float dx = b.x - a.x;
            const float dz = b.z - a.z;
            const float length2 = dx * dx + dz * dz;
            const float t = std::clamp(((point.x - a.x) * dx +
                                        (point.z - a.z) * dz) / length2,
                                       0.0f, 1.0f);
            const float ex = point.x - (a.x + dx * t);
            const float ez = point.z - (a.z + dz * t);
            nearest = std::min(nearest, std::sqrt(ex * ex + ez * ez));
        }
    }
    return nearest;
}

void layout_matches_requested_grid() {
    REQUIRE_NEAR(city::hospital_block_centre(0, 0).x, 0.0f, 0.001f);
    REQUIRE_NEAR(city::hospital_block_centre(2, 2).x, 184.0f, 0.001f);
    REQUIRE_NEAR(city::hospital_block_centre(2, 2).z, 124.0f, 0.001f);
    REQUIRE_NEAR(city::kHospitalGarageCentre.x, 46.0f, 0.001f);
    REQUIRE_NEAR(city::kHospitalGarageCentre.z, 186.0f, 0.001f);
    REQUIRE_NEAR(city::kHospitalCampusWidthM, 238.0f, 0.001f);
    REQUIRE_NEAR(city::kHospitalCampusDepthM, 162.0f, 0.001f);
    REQUIRE_NEAR(city::kHospitalGarageWidthM, 146.0f, 0.001f);
    REQUIRE(city::kHospitalFloorCount == 4);
    REQUIRE_NEAR(city::kHospitalHeightM, 13.6f, 0.001f);
    const city::StartSite replaced_tower{
        "former garage-block tower", city::hospital_grid_point(-138.0f, -93.0f),
        city::kGridCos, city::kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f};
    const city::StartSite east_neighbor{
        "east neighbor", city::hospital_grid_point(138.0f, -93.0f),
        city::kGridCos, city::kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f};
    REQUIRE(city::hospital_campus_replaces(replaced_tower));
    REQUIRE(!city::hospital_campus_replaces(east_neighbor));
    apricot_test::pass(
        "hospital overhaul keeps the approved campus and garage envelopes");
}

void internal_roads_are_clipped_to_the_superblock() {
    const city::Vec2 removed[] = {
        city::hospital_grid_point(-92.0f, -217.0f),
        city::hospital_grid_point(0.0f, -217.0f),
        city::hospital_grid_point(-46.0f, -248.0f),
        city::hospital_grid_point(-46.0f, -186.0f),
        city::hospital_grid_point(-92.0f, -124.0f),
    };
    for (const auto point : removed)
        REQUIRE(nearest_road_centreline(point) > 20.0f);

    const city::Vec2 perimeter[] = {
        city::hospital_grid_point(-184.0f, -217.0f),
        city::hospital_grid_point(92.0f, -217.0f),
        city::hospital_grid_point(-46.0f, -310.0f),
        city::hospital_grid_point(-46.0f, -62.0f),
        city::hospital_grid_point(46.0f, -124.0f),
    };
    for (const auto point : perimeter)
        REQUIRE(nearest_road_centreline(point) < 1.0f);
    apricot_test::pass("five internal road runs are gone while the campus perimeter stays served");
}

void campus_has_professional_hospital_layout() {
    const auto parts = city::bake_polished_hospital_campus();
    const auto north_parking = city::bake_hospital_north_parking();
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
    REQUIRE(city::valid_start_parts(north_parking.data(),
                                    north_parking.size()));
    REQUIRE(parts.size() > 1000u);
    REQUIRE(city::kHospitalOverhaulFloorCount == 4);
    REQUIRE(named_count(parts,
                        "hospital overhaul north public diagnostic floor") ==
            4u);
    REQUIRE(named_count(parts, "hospital overhaul west inpatient floor") ==
            4u);
    REQUIRE(named_count(parts, "hospital overhaul east surgery ED floor") ==
            4u);
    REQUIRE(named_count(parts, "hospital overhaul south support floor") ==
            4u);
    REQUIRE(named_count(parts,
                        "hospital overhaul central clinical spine floor") ==
            4u);
    REQUIRE(named_count(parts, "hospital wing floor") == 0u);
    REQUIRE(named_count(parts, "hospital east west connector core") == 0u);
    REQUIRE(named_count(parts, "hospital garage lot") == 1u);
    REQUIRE(named_count(parts, "hospital garage upper deck") == 6u);
    REQUIRE(named_count(parts, "hospital overhaul east ED helipad deck") ==
            1u);
    REQUIRE(named_count(parts, "hospital garage column") == 24u);
    REQUIRE(named_count(parts, "hospital garage entry pay station body") == 1u);
    REQUIRE(named_count(parts,
                        "hospital garage entry pay station control face") == 1u);
    REQUIRE(named_count(parts, "hospital facade wall light lens") == 12u);
    REQUIRE(named_count(parts,
                        "hospital northwest healing art glass face") == 1u);
    REQUIRE(city::bake_hospital_overhaul_logistics().size() == 253u);
    REQUIRE(city::bake_hospital_overhaul_mobility().size() == 176u);
    REQUIRE(city::bake_hospital_overhaul_public_realm().size() == 442u);
    REQUIRE(named_count(parts, "hospital arrival canopy light lens") >= 20u);
    REQUIRE(named_count(parts, "hospital grounds path light lens") >= 25u);
    REQUIRE(named_count(
                parts,
                "hospital arrival emergency shore power cabinet fitted face") ==
            1u);
    REQUIRE(named_count(parts, "hospital grounds healing mosaic face") == 1u);
    REQUIRE(city::bake_hospital_exterior_garage().size() == 150u);
    REQUIRE(named_count(parts, "hospital garage ceiling light lens") == 6u);
    REQUIRE(named_count(parts, "hospital garage west entry control face") == 1u);
    REQUIRE(named_count(parts, "hospital garage skybridge floor") == 1u);
    REQUIRE(named_count(parts,
                        "hospital overhaul ambulance bay hardstand") == 1u);
    REQUIRE(named_count(parts,
                        "hospital overhaul ambulance entry throat") == 1u);
    REQUIRE(named_count(parts,
                        "hospital overhaul ambulance exit throat") == 1u);
    REQUIRE(named_count(parts,
                        "hospital overhaul ambulance bay boundary") == 8u);
    REQUIRE(named_count(parts,
                        "hospital overhaul ambulance bay stop bar") == 4u);
    REQUIRE(named_count(parts,
                        "hospital overhaul service loading dock platform") ==
            3u);
    REQUIRE(named_count(parts,
                        "hospital mobility public arrival asphalt lane") ==
            1u);
    REQUIRE(named_count(parts,
                        "hospital mobility curved arrival apron") == 9u);
    REQUIRE(named_count(parts,
                        "hospital mobility public arrival turn apron") == 0u);
    REQUIRE(named_count(parts, "hospital mobility protected lobby walk") ==
            1u);
    REQUIRE(named_count(parts, "hospital mobility transit shelter roof") ==
            1u);
    REQUIRE(north_parking.size() > 130u);
    REQUIRE(named_count(north_parking,
                        "hospital north visitor parking lot") == 1u);
    REQUIRE(named_count(north_parking,
                        "hospital north parking stall stripe") >= 90u);
    REQUIRE(named_count(north_parking,
                        "hospital parking lot light lens") == 8u);
    REQUIRE(named_count(
                north_parking,
                "hospital north parking wayfinding fitted face") == 1u);
    std::printf("  %s: %zu authored pieces, %zu floors, two healing courts\n",
                city::kHospitalSite.name, parts.size(),
                static_cast<std::size_t>(city::kHospitalOverhaulFloorCount));
    apricot_test::pass(
        "four-story ring-and-spine hospital, separated arrivals and garage are authored");
}

void courtyards_and_portals_are_real_openings() {
    const auto massing = city::bake_hospital_overhaul_massing();
    const char* floor_names[] = {
        "hospital overhaul north public diagnostic floor",
        "hospital overhaul west inpatient floor",
        "hospital overhaul east surgery ED floor",
        "hospital overhaul south support floor",
        "hospital overhaul central clinical spine floor",
    };
    for (const auto& part : massing) {
        bool is_floor = false;
        for (const char* name : floor_names)
            is_floor = is_floor || std::strcmp(part.name, name) == 0;
        if (!is_floor) continue;
        REQUIRE(!horizontal_overlap(part,
                                    city::kHospitalOverhaulNorthCourt.min_x,
                                    city::kHospitalOverhaulNorthCourt.max_x,
                                    city::kHospitalOverhaulNorthCourt.min_z,
                                    city::kHospitalOverhaulNorthCourt.max_z));
        REQUIRE(!horizontal_overlap(part,
                                    city::kHospitalOverhaulSouthCourt.min_x,
                                    city::kHospitalOverhaulSouthCourt.max_x,
                                    city::kHospitalOverhaulSouthCourt.min_z,
                                    city::kHospitalOverhaulSouthCourt.max_z));
    }

    REQUIRE(!named_wall_blocks_point(
        massing, "hospital overhaul north public diagnostic wall", 0.0f,
        -8.0f, 1.5f));
    REQUIRE(named_wall_blocks_point(
        massing, "hospital overhaul north public diagnostic wall", 30.0f,
        -8.0f, 1.5f));
    REQUIRE(!named_wall_blocks_point(
        massing, "hospital overhaul east surgery ED wall", 204.0f,
        city::kHospitalOverhaulEdTraumaZ, 1.5f));
    REQUIRE(named_wall_blocks_point(
        massing, "hospital overhaul east surgery ED wall", 204.0f, 68.0f,
        1.5f));
    for (float dock_x : {158.0f, 174.0f, 190.0f}) {
        REQUIRE(!named_wall_blocks_point(
            massing, "hospital overhaul southeast clinical wall", dock_x,
            138.0f, 2.0f));
    }
    apricot_test::pass(
        "healing courts stay open and lobby, trauma and loading portals are real wall cuts");
}

void operational_vehicle_sweeps_are_clear() {
    const auto parts = city::bake_polished_hospital_campus();
    for (const auto& part : parts) {
        if (!part.solid || part.bottom_m >= 4.5f) continue;
        REQUIRE(!horizontal_overlap(
            part, city::kHospitalAmbulanceBypassMinX,
            city::kHospitalAmbulanceBypassMaxX,
            city::kHospitalAmbulanceBypassMinZ,
            city::kHospitalAmbulanceBypassMaxZ));
        REQUIRE(!horizontal_overlap(
            part, city::kHospitalServiceTruckSweepMinX,
            city::kHospitalServiceTruckSweepMaxX,
            city::kHospitalServiceTruckSweepMinZ,
            city::kHospitalServiceTruckSweepMaxZ));
        const bool in_public_lane = horizontal_overlap(
            part, city::kHospitalOverhaulArrivalLaneMinX,
            city::kHospitalOverhaulArrivalLaneMaxX,
            city::kHospitalOverhaulArrivalLaneMinZ,
            city::kHospitalOverhaulArrivalLaneMaxZ);
        const bool in_public_entry = horizontal_overlap(
            part, city::kHospitalOverhaulArrivalEntryMinX,
            city::kHospitalOverhaulArrivalEntryMaxX,
            city::kHospitalOverhaulArrivalEntryMinZ,
            city::kHospitalOverhaulArrivalEntryMaxZ);
        const bool in_public_exit = horizontal_overlap(
            part, city::kHospitalOverhaulArrivalExitMinX,
            city::kHospitalOverhaulArrivalExitMaxX,
            city::kHospitalOverhaulArrivalExitMinZ,
            city::kHospitalOverhaulArrivalExitMaxZ);
        if (in_public_lane || in_public_entry || in_public_exit) {
            std::fprintf(stderr,
                         "vehicle sweep obstruction: %s at (%.2f, %.2f)\n",
                         part.name, part.centre.x, part.centre.z);
        }
        REQUIRE(!in_public_lane);
        REQUIRE(!in_public_entry);
        REQUIRE(!in_public_exit);
    }
    REQUIRE_NEAR(city::kHospitalEmergencyServiceClearGapM, 16.0f, 0.001f);
    apricot_test::pass(
        "public, ambulance and service vehicle sweeps have no low solid obstructions");
}

void north_lot_is_across_tenth_street() {
    constexpr float tenth_street_local_z = -31.0f;
    constexpr float street_half_width_with_walks = 10.0f;
    const float lot_south_edge = city::kHospitalNorthParkingCentre.z +
        city::kHospitalNorthParkingDepthM * 0.5f;
    REQUIRE_NEAR(city::kHospitalNorthParkingWidthM,
                 city::kHospitalGarageWidthM, 0.001f);
    REQUIRE(lot_south_edge < tenth_street_local_z -
                                 street_half_width_with_walks);
    REQUIRE_NEAR(lot_south_edge, -43.0f, 0.001f);
    apricot_test::pass(
        "large north visitor lot stays across Tenth Street from the hospital");
}

void perimeter_accesses_cut_real_curbs() {
    std::vector<city::BuildingAccessLot> hospital_accesses;
    for (const auto& lot : city::authored_building_access_lots()) {
        if (city::access_same_site(lot.site, city::kHospitalSite))
            hospital_accesses.push_back(lot);
    }
    REQUIRE(hospital_accesses.size() == 5u);

    const TerrainGround ground{city::kMapSeed};
    RoadGraph graph;
    graph.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    auto ribbons = bake_ribbons(graph, ground.sampler());
    const auto access = city::bake_building_access(
        graph, ribbons, ground.sampler(), hospital_accesses);
    REQUIRE(access.lots.size() == 5u);
    for (const auto& lot : access.lots) {
        if (!lot.connected) {
            std::fprintf(stderr, "hospital curb cut not connected: %s\n",
                         lot.name);
        }
        REQUIRE(lot.connected);
        REQUIRE(!lot.driveway.empty());
        REQUIRE(lot.frontage.empty());
        REQUIRE(lot.entrance.sidewalk);
    }
    city::append_building_access(ribbons, access);
    REQUIRE(!ribbons.layer(RoadLayer::Walk).empty());
    REQUIRE(!ribbons.layer(RoadLayer::Kerb).empty());
    apricot_test::pass(
        "five hospital throats cut the real perimeter sidewalks and curbs");
}

}  // namespace

int main() {
    layout_matches_requested_grid();
    internal_roads_are_clipped_to_the_superblock();
    campus_has_professional_hospital_layout();
    courtyards_and_portals_are_real_openings();
    operational_vehicle_sweeps_are_clear();
    north_lot_is_across_tenth_street();
    perimeter_accesses_cut_real_curbs();
    return apricot_test::done("hospital_campus_tests");
}

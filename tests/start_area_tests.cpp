#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "city/airport.h"
#include "city/billboards.h"
#include "city/roads.h"
#include "city/start_area.h"
#include "city/tacomaco.h"
#include "city/neighborhood_shops.h"
#include "physics/vehicle.h"
#include "road/road_class.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

city::Vec2 world_point(const city::StartSite& site, city::Vec2 local) {
    return {
        site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.z,
        site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.z,
    };
}

city::Vec2 local_point(const city::StartSite& site, city::Vec2 world) {
    const float dx = world.x - site.origin.x;
    const float dz = world.z - site.origin.z;
    return {
        site.cos_yaw * dx - site.sin_yaw * dz,
        site.sin_yaw * dx + site.cos_yaw * dz,
    };
}

city::Vec2 vellum_grid_point(city::Vec2 world) {
    constexpr city::Vec2 kGridCentre{70.0f, -40.0f};
    const float dx = world.x - kGridCentre.x;
    const float dz = world.z - kGridCentre.z;
    return {
        city::kGridCos * dx - city::kGridSin * dz,
        city::kGridSin * dx + city::kGridCos * dz,
    };
}

city::Vec2 part_corner_world(const city::StartSite& site,
                            const city::StartPart& part, float sx, float sz) {
    const float yaw = part.yaw_deg * 0.0174532925199f;
    const float cs = std::cos(yaw);
    const float sn = std::sin(yaw);
    const float x = sx * part.width_m * 0.5f;
    const float z = sz * part.depth_m * 0.5f;
    const city::Vec2 local{
        part.centre.x + cs * x + sn * z,
        part.centre.z - sn * x + cs * z,
    };
    return world_point(site, local);
}

float world_aabb_gap_xz(const city::StartSite& site,
                        const city::StartPart& part, city::Vec2 point) {
    float min_x = 1e9f;
    float max_x = -1e9f;
    float min_z = 1e9f;
    float max_z = -1e9f;
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const city::Vec2 corner = part_corner_world(site, part, sx, sz);
            min_x = std::min(min_x, corner.x);
            max_x = std::max(max_x, corner.x);
            min_z = std::min(min_z, corner.z);
            max_z = std::max(max_z, corner.z);
        }
    }

    const float dx = std::max({min_x - point.x, 0.0f, point.x - max_x});
    const float dz = std::max({min_z - point.z, 0.0f, point.z - max_z});
    return std::sqrt(dx * dx + dz * dz);
}

bool contains_xz(const city::StartPart& p, city::Vec2 q) {
    const float yaw = p.yaw_deg * 0.0174532925199f;
    const float cs = std::cos(yaw);
    const float sn = std::sin(yaw);
    const float dx = q.x - p.centre.x;
    const float dz = q.z - p.centre.z;
    const float lx = cs * dx - sn * dz;
    const float lz = sn * dx + cs * dz;
    return std::fabs(lx) <= p.width_m * 0.5f &&
           std::fabs(lz) <= p.depth_m * 0.5f;
}

const city::StartPart& named_part(const std::vector<city::StartPart>& parts,
                                  const char* name) {
    for (const city::StartPart& part : parts) {
        if (std::strcmp(part.name, name) == 0) return part;
    }
    REQUIRE_MSG(false, "authored part is missing", name);
    return parts.front();
}

const city::Road& road_with_id(uint32_t id) {
    for (int i = 0; i < city::kRoadCount; ++i) {
        if (city::kRoads[i].id == id) return city::kRoads[i];
    }
    REQUIRE_MSG(false, "authored road is missing", "road id");
    return city::kRoads[0];
}

void every_site_follows_the_downtown_grid() {
    const city::Vec2 local{10.0f, 0.0f};
    const city::StartSite sites[] = {
        city::kGasStationSite,
        city::kCarWashSite,
        city::kMotelSite,
        city::kApartmentSite,
        city::kFastFoodSite,
        city::kTacomacoSite,
        city::kBankSite,
        city::kNessBillboardSite,
        city::kPinnatyTaxiBillboardSite,
    };
    for (const city::StartSite& site : sites) {
        const city::Vec2 east = world_point(site, local);
        REQUIRE_MSG(east.x > site.origin.x, "local east did not point east",
                    site.name);
        REQUIRE_MSG(east.z > site.origin.z,
                    "the six-degree downtown rotation was lost", site.name);

        const city::Vec2 round_trip = local_point(site, east);
        REQUIRE_NEAR(round_trip.x, local.x, 1e-4);
        REQUIRE_NEAR(round_trip.z, local.z, 1e-4);
    }
    apricot_test::pass("every starting site uses the six-degree downtown grid");
}

void the_motel_is_a_real_u() {
    REQUIRE(city::valid_building_plan(city::kMotelPlan));
    REQUIRE(city::kMotelPlan.wall_count == 16u);

    const city::BuildingWall& east = city::kMotelWalls[3];
    const city::BuildingWall& back = city::kMotelWalls[4];
    const city::BuildingWall& west = city::kMotelWalls[5];
    REQUIRE_NEAR(east.a.x, 17.0f, 1e-5);
    REQUIRE_NEAR(east.b.x, 17.0f, 1e-5);
    REQUIRE_NEAR(west.a.x, -17.0f, 1e-5);
    REQUIRE_NEAR(west.b.x, -17.0f, 1e-5);
    REQUIRE_NEAR(back.a.z, -15.0f, 1e-5);
    REQUIRE_NEAR(back.b.z, -15.0f, 1e-5);

    // The court is enclosed on west/east/north and genuinely open to the
    // south. The creator bake has narrow wall pieces around its openings,
    // rather than three solid wing-sized boxes.
    const std::vector<city::StartPart> baked =
        city::bake_building(city::kMotelPlan);
    std::size_t solid = 0;
    for (const city::StartPart& p : baked) {
        if (p.solid) ++solid;
        REQUIRE_MSG(!(p.solid && contains_xz(p, {0.0f, 10.0f})),
                    "the U courtyard was filled in", p.name);
    }
    REQUIRE_MSG(solid >= 50u, "motel shell lost its repeated room walls",
                "motel shell");
    REQUIRE_MSG(baked.size() >= 150u,
                "the motel lost its room, balcony or pool detail",
                "motel detail");
    apricot_test::pass("creator walls form a detailed open-south U court");
}

void each_site_is_one_complete_creator_document() {
    REQUIRE(city::valid_building_plan(city::kGasStationPlan));
    REQUIRE(city::valid_building_plan(city::kCarWashPlan));
    REQUIRE(city::valid_building_plan(city::kMotelPlan));
    REQUIRE(city::valid_building_plan(city::kApartmentPlan));
    REQUIRE(city::valid_building_plan(city::kFastFoodPlan));
    REQUIRE(city::valid_building_plan(city::kTacomacoPlan));
    REQUIRE(city::valid_building_plan(city::kBankPlan));
    REQUIRE(city::valid_building_plan(city::kAirportPlan));
    REQUIRE(city::valid_building_plan(city::kAutoRepairPlan));
    REQUIRE(city::valid_building_plan(city::kLaundromatPlan));
    REQUIRE(city::kGasStationPlan.fixture_count == city::kGasStationPartCount);
    REQUIRE(city::kCarWashPlan.fixture_count == city::kCarWashPartCount);
    REQUIRE(city::kMotelPlan.fixture_count == city::kMotelPartCount);
    REQUIRE(city::kApartmentPlan.fixture_count == city::kApartmentPartCount);
    REQUIRE(city::kFastFoodPlan.fixture_count == city::kFastFoodPartCount);
    REQUIRE(city::kTacomacoPlan.fixture_count == city::kFastFoodPartCount);
    REQUIRE(city::kBankPlan.fixture_count == city::kBankPartCount);
    REQUIRE(city::kAirportPlan.fixture_count == city::kAirportCorePartCount);
    REQUIRE(city::kMotelPlan.stair_count == 2u);

    const std::vector<city::StartPart> motel =
        city::bake_building(city::kMotelPlan);
    int west_steps = 0;
    int east_steps = 0;
    float stair_top = 0.0f;
    for (const city::StartPart& p : motel) {
        if (std::strcmp(p.name, "west exterior stairs") == 0) ++west_steps;
        if (std::strcmp(p.name, "east exterior stairs") == 0) ++east_steps;
        if (std::strstr(p.name, "exterior stairs") != nullptr) {
            stair_top = std::max(stair_top, p.bottom_m + p.height_m);
        }
    }
    REQUIRE(west_steps == 14);
    REQUIRE(east_steps == 14);
    REQUIRE_NEAR(stair_top, 3.87f, 1e-4);
    apricot_test::pass("each district site bakes from one complete creator plan");
}

void the_bank_is_really_enterable_and_furnished() {
    const std::vector<city::StartPart> bank =
        city::bake_building(city::kBankPlan);
    REQUIRE_MSG(bank.size() >= 110u,
                "bank lost its shell, interior rooms, or lobby detail",
                city::kBankSite.name);

    // Walk the same centre line a player uses: from the parking lot, over the
    // low threshold, through the south wall, and into the lobby. Floor slabs
    // are support surfaces below step height; anything taller is an obstacle.
    const city::Vec2 entry_path[] = {
        {0.0f, -10.0f}, {0.0f, -8.0f}, {0.0f, -7.0f},
        {0.0f, -6.0f}, {0.0f, -3.0f}, {0.0f, 1.5f},
    };
    for (const city::Vec2 point : entry_path) {
        for (const city::StartPart& part : bank) {
            const float top = part.bottom_m + part.height_m;
            const bool blocks_body = part.solid && top > 0.55f &&
                                     part.bottom_m < 2.0f;
            if (blocks_body) {
                REQUIRE_MSG(!contains_xz(part, point),
                            "bank entrance path is blocked", part.name);
            }
        }
    }

    const city::StartPart& floor = named_part(bank, "bank interior floor");
    const city::StartPart& walk = named_part(bank, "bank entrance walk");
    const city::StartPart& sign = named_part(bank, "bank sign face");
    const city::StartPart& sign_backing =
        named_part(bank, "bank roof sign backing");
    REQUIRE(floor.solid);
    REQUIRE(walk.solid);
    REQUIRE_NEAR(floor.bottom_m + floor.height_m,
                 walk.bottom_m + walk.height_m, 0.05f);
    REQUIRE(!sign.solid);
    REQUIRE(sign_backing.solid);
    REQUIRE(sign.bottom_m > city::kBankRoofs[0].bottom_m +
            city::kBankRoofs[0].thickness_m + city::kBankRoofs[0].parapet_m);
    REQUIRE_NEAR(sign.centre.x, 0.0f, 1e-4);
    REQUIRE_NEAR(sign.width_m / sign.height_m, 3.0f, 1e-4);
    REQUIRE_NEAR(std::fabs(sign.yaw_deg), 180.0f, 1e-4);
    REQUIRE(sign.centre.z <
            sign_backing.centre.z - sign_backing.depth_m * 0.5f);
    const city::StartPart& atm_face = named_part(bank, "bank atm face west");
    const city::StartPart& atm_body = named_part(bank, "bank atm west body");
    REQUIRE_NEAR(atm_face.width_m, atm_face.height_m, 1e-4);
    REQUIRE_NEAR(atm_face.yaw_deg, 0.0f, 1e-4);
    REQUIRE(atm_face.centre.z > atm_body.centre.z + atm_body.depth_m * 0.5f);
    REQUIRE(!atm_face.solid);
    for (const char* side : {"west", "east"}) {
        const std::string prefix = std::string("bank atm ") + side;
        const auto& body = named_part(bank, (prefix + " body").c_str());
        const auto& bezel = named_part(bank, (prefix + " bezel").c_str());
        const auto& panel = named_part(bank, (std::string("bank atm face ") + side).c_str());
        const auto& service = named_part(bank, (prefix + " service door").c_str());
        REQUIRE(body.solid);
        REQUIRE(body.width_m <= 0.90f);
        REQUIRE(body.height_m <= 1.65f);
        REQUIRE_NEAR(body.bottom_m, floor.bottom_m + floor.height_m, 0.01f);
        REQUIRE_NEAR(panel.width_m, panel.height_m, 1e-4);
        REQUIRE(panel.width_m < bezel.width_m);
        REQUIRE(bezel.width_m < body.width_m);
        REQUIRE(panel.bottom_m > body.bottom_m + body.height_m * 0.45f);
        REQUIRE(panel.bottom_m + panel.height_m < body.bottom_m + body.height_m);
        REQUIRE(service.bottom_m + service.height_m < panel.bottom_m);
        REQUIRE(panel.centre.z + panel.depth_m * 0.5f >
                bezel.centre.z + bezel.depth_m * 0.5f + 0.005f);
    }
    REQUIRE(named_part(bank, "bank interior ceiling").bottom_m >
            named_part(bank, "bank ceiling light 1").bottom_m +
            named_part(bank, "bank ceiling light 1").height_m);

    int open_entry_dressing = 0;
    int ceiling_lights = 0;
    int teller_parts = 0;
    int atm_parts = 0;
    int vault_parts = 0;
    int manager_parts = 0;
    for (const city::StartPart& part : bank) {
        REQUIRE(std::strstr(part.name, "bank monument") == nullptr);
        open_entry_dressing +=
            std::strcmp(part.name, "bank open entrance") == 0;
        ceiling_lights += std::strstr(part.name, "bank ceiling light") != nullptr;
        teller_parts += std::strstr(part.name, "bank teller") != nullptr;
        atm_parts += std::strstr(part.name, "bank atm") != nullptr;
        vault_parts += std::strstr(part.name, "bank vault") != nullptr;
        manager_parts += std::strstr(part.name, "bank manager") != nullptr;
    }
    REQUIRE_MSG(open_entry_dressing == 6,
                "exterior and interior frames must each keep the entry open",
                "bank entrance");
    REQUIRE(ceiling_lights == 6);
    REQUIRE(teller_parts >= 9);
    REQUIRE(atm_parts == 10);
    REQUIRE(vault_parts >= 8);
    REQUIRE(manager_parts >= 6);

    // The internal room doorways are actual holes too, not painted panels.
    for (const city::Vec2 doorway :
         {city::Vec2{7.0f, 11.5f}, city::Vec2{-7.0f, 11.0f}}) {
        for (const city::StartPart& part : bank) {
            const float top = part.bottom_m + part.height_m;
            if (part.solid && top > 0.55f && part.bottom_m < 2.0f) {
                REQUIRE_MSG(!contains_xz(part, doorway),
                            "bank interior doorway is blocked", part.name);
            }
        }
    }
    apricot_test::pass(
        "bank has a walkable lobby, teller line, office, and open vault room");
}

void camber_point_has_a_real_international_airport() {
    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::StartPart& runway = named_part(airport, "runway 09-27");
    const city::StartPart& terminal_west =
        named_part(airport, "terminal facade west wing");
    const city::StartPart& terminal_centre =
        named_part(airport, "terminal facade centre hall");
    const city::StartPart& terminal_east =
        named_part(airport, "terminal facade east wing");
    const city::StartPart& tower = named_part(airport, "control tower beacon");

    REQUIRE_NEAR(city::kAirportSite.ground_m, 6.0f, 1e-5);
    REQUIRE(runway.width_m >= 900.0f);
    REQUIRE(runway.depth_m >= 45.0f);
    const float terminal_west_edge = terminal_west.centre.x -
                                     terminal_west.width_m * 0.5f;
    const float terminal_east_edge = terminal_east.centre.x +
                                     terminal_east.width_m * 0.5f;
    REQUIRE(terminal_east_edge - terminal_west_edge >= 230.0f);
    REQUIRE(terminal_centre.height_m > terminal_west.height_m);
    REQUIRE(terminal_centre.height_m > terminal_east.height_m);
    REQUIRE_NEAR(tower.bottom_m + tower.height_m, 30.2f, 1e-5);
    REQUIRE_MSG(airport.size() >= 280u,
                "airport lost its runway cadence, gates, or support buildings",
                city::kAirportSite.name);

    std::size_t centre_dashes = 0;
    std::size_t edge_lights = 0;
    std::size_t gates = 0;
    std::size_t taxiway_centrelines = 0;
    std::size_t terminal_entrances = 0;
    std::size_t terminal_canopies = 0;
    std::size_t terminal_crosswalk_stripes = 0;
    std::size_t terminal_parking_stripes = 0;
    std::size_t terminal_parking_islands = 0;
    std::size_t hold_short_bars = 0;
    std::size_t gate_stand_lines = 0;
    std::size_t pedestrian_connectors = 0;
    std::size_t perimeter_posts = 0;
    bool has_hangar = false;
    for (const city::StartPart& part : airport) {
        centre_dashes += std::strstr(part.name, "runway centreline") != nullptr;
        edge_lights += std::strstr(part.name, "runway edge light") != nullptr;
        gates += std::strstr(part.name, "jet bridge gate") != nullptr;
        taxiway_centrelines +=
            std::strstr(part.name, "taxiway alpha centreline") != nullptr ||
            std::strstr(part.name, "taxiway bravo centreline") != nullptr;
        terminal_entrances +=
            std::strstr(part.name, "terminal entrance") != nullptr &&
            std::strstr(part.name, "mullion") == nullptr;
        terminal_canopies +=
            std::strstr(part.name, "terminal arrivals canopy") != nullptr;
        const bool terminal_zebra =
            std::strcmp(part.name, "terminal zebra stripe") == 0;
        terminal_crosswalk_stripes += terminal_zebra;
        if (terminal_zebra) {
            REQUIRE(!part.solid);
            REQUIRE(part.height_m <= 0.05f);
            REQUIRE_NEAR(part.bottom_m + part.height_m,
                         DRAPE_EPS_M + city::kAirportCrosswalkLiftM, 1e-5);
        }
        const bool terminal_parking_stripe =
            std::strcmp(part.name, "terminal parking stripe") == 0;
        terminal_parking_stripes += terminal_parking_stripe;
        if (terminal_parking_stripe) {
            REQUIRE(!part.solid);
            REQUIRE(part.height_m <= 0.05f);
            REQUIRE_NEAR(part.bottom_m + part.height_m,
                         city::kAirportPavingTopM +
                             city::kAirportPaintLiftM,
                         1e-5);
        }
        terminal_parking_islands +=
            std::strstr(part.name, "terminal parking island") != nullptr;
        hold_short_bars +=
            std::strstr(part.name, "taxiway hold short") != nullptr;
        gate_stand_lines +=
            std::strcmp(part.name, "gate stand lead line") == 0;
        pedestrian_connectors +=
            std::strstr(part.name, "terminal pedestrian connector") != nullptr;
        perimeter_posts +=
            std::strstr(part.name, "airport perimeter fence post") != nullptr;
        has_hangar |= std::strcmp(part.name, "airport hangar") == 0;
        REQUIRE_MSG(std::strncmp(part.name, "airliner ", 9) != 0,
                    "old box aircraft duplicates the cooked Aster", part.name);
    }
    REQUIRE(centre_dashes == 17u);
    REQUIRE(edge_lights == 38u);
    REQUIRE(gates == 3u);
    REQUIRE(taxiway_centrelines == 12u);
    REQUIRE(terminal_entrances == 3u);
    REQUIRE(terminal_canopies == 3u);
    REQUIRE(terminal_crosswalk_stripes == 21u);
    // Door-aligned walks and planting islands reserve spaces in both rows.
    REQUIRE(terminal_parking_stripes >= 60u);
    REQUIRE(terminal_parking_stripes < 85u);
    REQUIRE(terminal_parking_islands == 5u);
    REQUIRE(hold_short_bars == 4u);
    REQUIRE(gate_stand_lines == 3u);
    REQUIRE(pedestrian_connectors == 6u);
    REQUIRE(perimeter_posts == 21u);
    REQUIRE(has_hangar);
    REQUIRE(std::strcmp(city::kAirportAircraftName, "Aster A-80") == 0);
    const auto& aircraft = city::kAirportAircraftFootprint;
    REQUIRE(aircraft.centre.x - aircraft.width_m*.5f >= -340.0f);
    REQUIRE(aircraft.centre.x + aircraft.width_m*.5f <= 160.0f);
    REQUIRE(aircraft.centre.z - aircraft.depth_m*.5f >= -29.0f);
    REQUIRE(aircraft.centre.z + aircraft.depth_m*.5f <= 25.0f);

    const city::Vec2 west = world_point(city::kAirportSite, {-420.0f, -94.0f});
    const city::Vec2 east = world_point(city::kAirportSite, {420.0f, -94.0f});
    REQUIRE(city::district_at(west.x, west.z) == city::DistrictId::CamberPoint);
    REQUIRE(city::district_at(east.x, east.z) == city::DistrictId::CamberPoint);
    apricot_test::pass("Camber Point has a full international airport campus");
}

void airport_airside_and_landside_clearances_make_sense() {
    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::StartPart& runway = named_part(airport, "runway 09-27");
    const city::StartPart& apron = named_part(airport, "airport apron");
    const city::StartPart& hangar_apron = named_part(airport, "hangar apron");
    const city::StartPart& alpha = named_part(airport, "taxiway alpha");
    const city::StartPart& bravo = named_part(airport, "taxiway bravo");
    const city::StartPart& terminal =
        named_part(airport, "terminal facade centre hall");
    const city::StartPart& parking = named_part(airport, "terminal parking");
    const city::StartPart& tower = named_part(airport, "control tower base");

    const float runway_south = runway.centre.z + runway.depth_m * 0.5f;
    const float apron_north = apron.centre.z - apron.depth_m * 0.5f;
    REQUIRE_MSG(apron_north - runway_south >= 35.0f,
                "runway safety gap became too tight", runway.name);
    for (const city::StartPart* taxiway : {&alpha, &bravo}) {
        const float north = taxiway->centre.z - taxiway->depth_m * 0.5f;
        REQUIRE_MSG(north < runway_south,
                    "taxiway does not overlap the runway shoulder",
                    taxiway->name);
        REQUIRE_MSG(taxiway->width_m >= 20.0f,
                    "taxiway is too narrow for the escape plane", taxiway->name);
    }
    const auto overlaps_xz = [](const city::StartPart& a,
                                const city::StartPart& b) {
        return std::fabs(a.centre.x - b.centre.x) <
                   (a.width_m + b.width_m) * 0.5f &&
               std::fabs(a.centre.z - b.centre.z) <
                   (a.depth_m + b.depth_m) * 0.5f;
    };
    REQUIRE_MSG(overlaps_xz(bravo, apron),
                "Taxiway Bravo does not reach the passenger apron", bravo.name);
    REQUIRE_MSG(overlaps_xz(alpha, hangar_apron),
                "Taxiway Alpha does not reach the hangar apron", alpha.name);

    const city::StartPart& gate_two = named_part(airport, "jet bridge gate 2");
    REQUIRE_MSG(!overlaps_xz(tower, gate_two),
                "control tower blocks Gate 2", tower.name);

    const city::Road& apron_road = road_with_id(151u);
    const city::RoadPoint apron_end = apron_road.path[apron_road.count - 1];
    const city::Vec2 tower_world = world_point(city::kAirportSite, tower.centre);
    const float tower_dx = apron_end.x - tower_world.x;
    const float tower_dz = apron_end.z - tower_world.z;
    REQUIRE_MSG(std::sqrt(tower_dx * tower_dx + tower_dz * tower_dz) > 40.0f,
                "apron service road ends inside the control tower", tower.name);

    const city::Road& loop = road_with_id(152u);
    REQUIRE(std::strcmp(loop.name, "the Terminal Loop") == 0);
    REQUIRE(loop.count == 10);
    REQUIRE_NEAR(loop.width_m, 12.0f, 1e-5);
    const float curb_world_z = loop.path[4].z;
    REQUIRE_NEAR(loop.path[5].z, curb_world_z, 1e-5);
    const float terminal_south_world = city::kAirportSite.origin.z +
                                       terminal.centre.z + terminal.depth_m * 0.5f;
    const float parking_north_world = city::kAirportSite.origin.z +
                                      parking.centre.z - parking.depth_m * 0.5f;
    REQUIRE_MSG(curb_world_z - terminal_south_world >= 12.0f,
                "pickup lane crowds the terminal wall", loop.name);
    REQUIRE_MSG(parking_north_world - curb_world_z >= 25.0f,
                "pickup lane crowds the parking field", loop.name);
    const float parking_south_world = city::kAirportSite.origin.z +
                                      parking.centre.z + parking.depth_m * 0.5f;
    REQUIRE_MSG(loop.path[0].z > parking_south_world &&
                    loop.path[loop.count - 1].z > parking_south_world,
                "terminal loop junctions are not behind the parking field",
                loop.name);

    // No abrupt corner may fall back to the road baker's giant plate path.
    // Gentle polyline bends miter cleanly and keep sidewalks on their edge.
    for (int i = 1; i + 1 < loop.count; ++i) {
        const glm::vec2 in{loop.path[i].x - loop.path[i - 1].x,
                           loop.path[i].z - loop.path[i - 1].z};
        const glm::vec2 out{loop.path[i + 1].x - loop.path[i].x,
                            loop.path[i + 1].z - loop.path[i].z};
        REQUIRE_MSG(glm::dot(glm::normalize(in), glm::normalize(out)) > 0.50f,
                    "terminal loop corner is too sharp for a clean miter",
                    loop.name);
    }

    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
            const city::Vec2 corner = world_point(
                city::kAirportSite,
                {parking.centre.x + sx * parking.width_m * 0.5f,
                 parking.centre.z + sz * parking.depth_m * 0.5f});
            REQUIRE_NEAR(height_at(city::kMapSeed, corner.x, corner.z),
                         city::kAirportSite.ground_m, 0.15f);
            REQUIRE_NEAR(city::wild_scatter_at(corner.x, corner.z), 0.0f, 1e-6);
        }
    }
    apricot_test::pass("airport airside and pickup loop have usable clearances");
}

void airport_service_entry_is_secured_and_reroutable() {
    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::Road& service = road_with_id(151u);
    const city::Road& visitor_return = road_with_id(156u);
    const city::Road& frontage = road_with_id(150u);
    REQUIRE(std::strcmp(service.name, "Secure Apron Access") == 0);
    REQUIRE(std::strcmp(visitor_return.name, "Visitor Return") == 0);
    REQUIRE(visitor_return.cls == city::RoadClass::Alley);

    // The visitor split lands on the service-road centreline, before the
    // checkpoint. The far end rejoins the south perimeter toward the terminal.
    const city::RoadPoint split = visitor_return.path[0];
    const float service_t =
        (split.x - service.path[0].x) /
        (service.path[1].x - service.path[0].x);
    const float service_z = service.path[0].z +
        service_t * (service.path[1].z - service.path[0].z);
    REQUIRE_NEAR(split.z, service_z, 0.01f);
    const city::RoadPoint return_end =
        visitor_return.path[visitor_return.count - 1];
    REQUIRE_NEAR(return_end.z, 2390.0f, 1e-5);
    REQUIRE(return_end.x >= frontage.path[0].x);
    REQUIRE(return_end.x <= frontage.path[1].x);

    // The secured road used to run straight through the solid west concourse.
    // Sample every segment with the road's full half-width and also keep it off
    // the parked aircraft at Gate 1.
    const city::StartPart& west_concourse =
        named_part(airport, "concourse west");
    const city::StartPart& parked_wing = city::kAirportAircraftFootprint;
    const float service_half = service.width_m * 0.5f;
    const auto clears_part = [service_half](city::Vec2 p,
                                             const city::StartPart& part) {
        return std::fabs(p.x - part.centre.x) >
                   part.width_m * 0.5f + service_half ||
               std::fabs(p.z - part.centre.z) >
                   part.depth_m * 0.5f + service_half;
    };
    for (int seg = 0; seg + 1 < service.count; ++seg) {
        for (int i = 0; i <= 24; ++i) {
            const float t = static_cast<float>(i) / 24.0f;
            const city::Vec2 local = local_point(
                city::kAirportSite,
                {service.path[seg].x +
                     (service.path[seg + 1].x - service.path[seg].x) * t,
                 service.path[seg].z +
                     (service.path[seg + 1].z - service.path[seg].z) * t});
            REQUIRE_MSG(clears_part(local, west_concourse),
                        "secure service road crosses the west concourse",
                        service.name);
            REQUIRE_MSG(clears_part(local, parked_wing),
                        "secure service road crosses the parked aircraft",
                        service.name);
        }
    }

    const city::StartPart& gatehouse =
        named_part(airport, "airport security gatehouse");
    const city::StartPart& inbound =
        named_part(airport, "airport security barrier inbound");
    const city::StartPart& outbound =
        named_part(airport, "airport security barrier outbound");
    const city::StartPart& north_fence =
        named_part(airport, "airport security fence north lower");
    REQUIRE(gatehouse.solid);
    REQUIRE(inbound.centre.x >
            split.x - city::kAirportSite.origin.x + 40.0f);
    REQUIRE(outbound.centre.x >
            split.x - city::kAirportSite.origin.x + 40.0f);
    REQUIRE(inbound.depth_m >= service.width_m * 0.5f);
    REQUIRE(outbound.depth_m >= service.width_m * 0.5f);

    const city::StartPart& runway = named_part(airport, "runway 09-27");
    const float fence_south =
        north_fence.centre.z + north_fence.depth_m * 0.5f;
    const float runway_south = runway.centre.z + runway.depth_m * 0.5f;
    REQUIRE_MSG(fence_south - runway_south >= 35.0f,
                "security fence crowds the runway safety area",
                north_fence.name);

    // The return road clears the hotel's full planned parcel, including its
    // 4 m carriageway half-width, before joining the public south ring.
    const city::AirportDevelopmentParcel& hotel =
        city::kAirportDevelopmentParcels[0];
    const float hotel_west = hotel.centre.x - hotel.width_m * 0.5f;
    const float hotel_south = hotel.centre.z + hotel.depth_m * 0.5f;
    const float road_half = visitor_return.width_m * 0.5f;
    for (int i = 0; i < visitor_return.count; ++i) {
        const city::Vec2 local = local_point(
            city::kAirportSite,
            {visitor_return.path[i].x, visitor_return.path[i].z});
        const bool clears_west = local.x + road_half <= hotel_west;
        const bool clears_south = local.z - road_half >= hotel_south;
        REQUIRE_MSG(clears_west || clears_south,
                    "visitor return road clips the hotel parcel",
                    visitor_return.name);
    }
    apricot_test::pass(
        "west apron entry gates airside and returns visitors to the front");
}

void airport_parking_has_vehicle_and_pedestrian_access() {
    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::Road& parking_access = road_with_id(158u);
    const city::Road& frontage = road_with_id(150u);
    const city::StartPart& parking = named_part(airport, "terminal parking");
    const city::StartPart& north_connector =
        named_part(airport, "terminal pedestrian connector north");
    const city::StartPart& south_connector =
        named_part(airport, "terminal pedestrian connector south");

    REQUIRE(std::strcmp(parking_access.name, "Terminal Parking Access") == 0);
    REQUIRE_NEAR(parking_access.path[0].z, frontage.path[0].z, 1e-5);
    REQUIRE_NEAR(parking_access.path[parking_access.count - 1].z,
                 frontage.path[0].z, 1e-5);
    const float parking_west = city::kAirportSite.origin.x + parking.centre.x -
                               parking.width_m * 0.5f;
    const float parking_east = city::kAirportSite.origin.x + parking.centre.x +
                               parking.width_m * 0.5f;
    REQUIRE(parking_access.path[0].x >= parking_west);
    REQUIRE(parking_access.path[parking_access.count - 1].x <= parking_east);
    REQUIRE_NEAR(parking_access.path[3].z,
                 city::kAirportSite.origin.z + 215.0f, 1e-5);
    REQUIRE_NEAR(parking_access.path[4].z,
                 city::kAirportSite.origin.z + 215.0f, 1e-5);

    const float north_end = north_connector.centre.z +
                            north_connector.depth_m * 0.5f;
    const float south_start = south_connector.centre.z -
                              south_connector.depth_m * 0.5f;
    const float south_end = south_connector.centre.z +
                            south_connector.depth_m * 0.5f;
    const float parking_north = parking.centre.z - parking.depth_m * 0.5f;
    REQUIRE_NEAR(north_end, 175.0f, 1e-5);
    REQUIRE_NEAR(south_start, 181.0f, 1e-5);
    REQUIRE_NEAR(south_end, parking_north, 1e-5);

    const city::StartPart& pylon =
        named_part(airport, "airport arrival pylon body");
    const city::AirportDevelopmentParcel& cargo =
        city::kAirportDevelopmentParcels[2];
    REQUIRE(pylon.centre.x - pylon.width_m * 0.5f >=
            cargo.centre.x + cargo.width_m * 0.5f + 10.0f);
    apricot_test::pass(
        "terminal parking has two kerb cuts and a continuous walking route");
}

void airport_main_approach_reaches_landside_not_the_field() {
    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::Road& causeway = road_with_id(10u);
    const city::Road& parkway = road_with_id(157u);
    const city::Road& frontage = road_with_id(150u);
    const city::StartPart& runway = named_part(airport, "runway 09-27");
    const city::StartPart& apron = named_part(airport, "airport apron");

    REQUIRE(std::strcmp(parkway.name, "O'Haven Airport Parkway") == 0);
    REQUIRE(std::strcmp(frontage.name, "Airport Frontage Road") == 0);
    REQUIRE(parkway.cls == city::RoadClass::Arterial);
    REQUIRE(frontage.cls == city::RoadClass::Street);

    // The two public roads meet on dry ground north of the airport. The other
    // end meets the frontage street, south-east of every flight surface.
    const city::RoadPoint causeway_end = causeway.path[causeway.count - 1];
    REQUIRE_NEAR(parkway.path[0].x, causeway_end.x, 1e-5);
    REQUIRE_NEAR(parkway.path[0].z, causeway_end.z, 1e-5);
    const city::RoadPoint parkway_end = parkway.path[parkway.count - 1];
    const city::RoadPoint frontage_end = frontage.path[frontage.count - 1];
    REQUIRE_NEAR(parkway_end.x, frontage_end.x, 1e-5);
    REQUIRE_NEAR(parkway_end.z, frontage_end.z, 1e-5);

    // Sample the whole public approach, not just its authored vertices. A long
    // segment can cross a runway even when both endpoints look innocent.
    const float public_half = parkway.ribbon_half_m();
    for (int seg = 0; seg + 1 < parkway.count; ++seg) {
        for (int i = 0; i <= 32; ++i) {
            const float t = static_cast<float>(i) / 32.0f;
            const city::Vec2 world{
                parkway.path[seg].x +
                    (parkway.path[seg + 1].x - parkway.path[seg].x) * t,
                parkway.path[seg].z +
                    (parkway.path[seg + 1].z - parkway.path[seg].z) * t,
            };
            const city::Vec2 local = local_point(city::kAirportSite, world);
            const auto clears = [public_half](const city::StartPart& part,
                                              city::Vec2 p) {
                return std::fabs(p.x - part.centre.x) >
                           part.width_m * 0.5f + public_half ||
                       std::fabs(p.z - part.centre.z) >
                           part.depth_m * 0.5f + public_half;
            };
            REQUIRE_MSG(clears(runway, local),
                        "public airport approach crosses the runway",
                        parkway.name);
            REQUIRE_MSG(clears(apron, local),
                        "public airport approach crosses the apron",
                        parkway.name);
        }
    }

    // The frontage road is open and wholly landside. It cannot sneak a final
    // closing segment back into the airfield like the old perimeter loop did.
    REQUIRE(frontage.path[0].x != frontage.path[frontage.count - 1].x ||
            frontage.path[0].z != frontage.path[frontage.count - 1].z);
    for (int i = 0; i < frontage.count; ++i) {
        REQUIRE_MSG(frontage.path[i].z >= 2390.0f,
                    "airport frontage road leaves the landside edge",
                    frontage.name);
    }
    apricot_test::pass(
        "main airport approach wraps to landside instead of the field");
}

void airport_edge_development_is_zoned_and_reachable() {
    REQUIRE(city::kAirportDevelopmentParcelCount == 3u);
    REQUIRE(city::kAirportDevelopmentPartCount >= 40u);

    const std::vector<city::StartPart> airport = city::bake_airport();
    const city::StartPart& runway = named_part(airport, "runway 09-27");
    const city::StartPart& hotel =
        named_part(airport, "airport hotel facade main wing");
    const city::StartPart& rental =
        named_part(airport, "rental centre customer hall");
    const city::StartPart& cargo =
        named_part(airport, "air cargo warehouse");

    const auto parcel_contains = [](
        const city::AirportDevelopmentParcel& parcel,
        const city::StartPart& part) {
        return part.centre.x - part.width_m * 0.5f >=
                   parcel.centre.x - parcel.width_m * 0.5f - 0.01f &&
               part.centre.x + part.width_m * 0.5f <=
                   parcel.centre.x + parcel.width_m * 0.5f + 0.01f &&
               part.centre.z - part.depth_m * 0.5f >=
                   parcel.centre.z - parcel.depth_m * 0.5f - 0.01f &&
               part.centre.z + part.depth_m * 0.5f <=
                   parcel.centre.z + parcel.depth_m * 0.5f + 0.01f &&
               part.bottom_m + part.height_m <= parcel.max_height_m + 1.0f;
    };

    for (const city::StartPart& part : airport) {
        const city::AirportDevelopmentParcel* parcel = nullptr;
        if (std::strstr(part.name, "airport hotel") != nullptr) {
            parcel = &city::kAirportDevelopmentParcels[0];
        } else if (std::strstr(part.name, "rental ") != nullptr) {
            parcel = &city::kAirportDevelopmentParcels[1];
        } else if (std::strstr(part.name, "air cargo") != nullptr) {
            parcel = &city::kAirportDevelopmentParcels[2];
        }
        if (parcel != nullptr) {
            REQUIRE_MSG(parcel_contains(*parcel, part),
                        "airport use escaped its planned parcel", part.name);
        }
    }

    // Passenger uses stay between the runway thresholds and behind the
    // terminal frontage. Cargo is low and beside the hangar, not on airside.
    const float runway_west = runway.centre.x - runway.width_m * 0.5f;
    const float runway_east = runway.centre.x + runway.width_m * 0.5f;
    const city::AirportDevelopmentParcel& hotel_parcel =
        city::kAirportDevelopmentParcels[0];
    const city::AirportDevelopmentParcel& rental_parcel =
        city::kAirportDevelopmentParcels[1];
    const city::AirportDevelopmentParcel& cargo_parcel =
        city::kAirportDevelopmentParcels[2];
    REQUIRE(hotel_parcel.centre.x - hotel_parcel.width_m * 0.5f >=
            runway_west + 20.0f);
    REQUIRE(cargo_parcel.centre.x + cargo_parcel.width_m * 0.5f <=
            runway_east - 20.0f);
    for (const city::AirportDevelopmentParcel* parcel :
         {&hotel_parcel, &rental_parcel, &cargo_parcel}) {
        REQUIRE(parcel->centre.z - parcel->depth_m * 0.5f >= 90.0f);
        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 corner = world_point(
                    city::kAirportSite,
                    {parcel->centre.x + sx * parcel->width_m * 0.5f,
                     parcel->centre.z + sz * parcel->depth_m * 0.5f});
                REQUIRE(city::district_at(corner.x, corner.z) ==
                        city::DistrictId::CamberPoint);
                REQUIRE_NEAR(height_at(city::kMapSeed, corner.x, corner.z),
                             city::kAirportSite.ground_m, 0.15f);
                REQUIRE_NEAR(city::wild_scatter_at(corner.x, corner.z),
                             0.0f, 1e-6);
            }
        }
    }

    REQUIRE(hotel.height_m <= hotel_parcel.max_height_m);
    REQUIRE(rental.height_m <= rental_parcel.max_height_m);
    REQUIRE(cargo.height_m <= cargo_parcel.max_height_m);

    const city::Road& hotel_road = road_with_id(153u);
    const city::Road& rental_road = road_with_id(154u);
    const city::Road& cargo_road = road_with_id(155u);
    const city::Road& parkway = road_with_id(157u);
    const city::Road& terminal_loop = road_with_id(152u);
    REQUIRE(std::strcmp(hotel_road.name, "Camber Gateway") == 0);
    REQUIRE(std::strcmp(rental_road.name, "Rental Row") == 0);
    REQUIRE(std::strcmp(cargo_road.name, "Cargo Service") == 0);
    REQUIRE_NEAR(hotel_road.path[0].z, 2390.0f, 1e-5);
    REQUIRE_NEAR(rental_road.path[0].z, 2390.0f, 1e-5);
    REQUIRE_NEAR(cargo_road.path[0].x, parkway.path[8].x, 1e-5);
    REQUIRE_NEAR(cargo_road.path[0].z, parkway.path[8].z, 1e-5);
    REQUIRE_NEAR(cargo_road.path[cargo_road.count - 1].x, 560.0f, 1e-5);
    REQUIRE_NEAR(cargo_road.path[cargo_road.count - 1].z, 2310.0f, 1e-5);
    REQUIRE(cargo_road.cls == city::RoadClass::Alley);

    // Parcel edges respect the terminal loop's full carriageway + sidewalk,
    // and Rental Row fits between the hall and deck with a metre to spare.
    const float loop_edge = terminal_loop.width_m * 0.5f + kSidewalkWidthM;
    const float hotel_east_world = city::kAirportSite.origin.x +
        hotel_parcel.centre.x + hotel_parcel.width_m * 0.5f;
    const float rental_west_world = city::kAirportSite.origin.x +
        rental_parcel.centre.x - rental_parcel.width_m * 0.5f;
    REQUIRE(hotel_east_world <= terminal_loop.path[0].x - loop_edge);
    REQUIRE(rental_west_world >=
            terminal_loop.path[terminal_loop.count - 1].x + loop_edge);

    const city::StartPart& rental_deck =
        named_part(airport, "rental centre parking deck");
    const float rental_road_edge =
        rental_road.width_m * 0.5f + kSidewalkWidthM;
    const float hall_east_world = city::kAirportSite.origin.x +
        rental.centre.x + rental.width_m * 0.5f;
    const float deck_west_world = city::kAirportSite.origin.x +
        rental_deck.centre.x - rental_deck.width_m * 0.5f;
    const float rental_road_x = rental_road.path[rental_road.count - 1].x;
    REQUIRE(rental_road_x - rental_road_edge >= hall_east_world + 1.0f);
    REQUIRE(rental_road_x + rental_road_edge <= deck_west_world - 1.0f);
    apricot_test::pass(
        "airport edge has separate public, rental, and cargo zones");
}

void canopy_faces_do_not_fight_for_the_same_depth() {
    const std::vector<city::StartPart> gas =
        city::bake_building(city::kGasStationPlan);
    const city::StartPart& roof = named_part(gas, "canopy roof");
    const city::StartPart& north = named_part(gas, "canopy north fascia");
    const city::StartPart& west = named_part(gas, "canopy west fascia");
    const city::StartPart& column = named_part(gas, "canopy column nw");

    const float roof_top = roof.bottom_m + roof.height_m;
    REQUIRE_MSG(north.bottom_m + north.height_m < roof_top,
                "north fascia top is coplanar with the roof", north.name);
    REQUIRE_MSG(west.bottom_m + west.height_m < roof_top,
                "west fascia top is coplanar with the roof", west.name);
    const float north_distance =
        std::fabs(north.centre.z - roof.centre.z);
    REQUIRE_MSG(roof.depth_m * 0.5f >
                        north_distance - north.depth_m * 0.5f &&
                    roof.depth_m * 0.5f <
                        north_distance + north.depth_m * 0.5f,
                "roof edge is not buried inside north fascia", roof.name);
    const float west_distance = std::fabs(west.centre.x - roof.centre.x);
    REQUIRE_MSG(roof.width_m * 0.5f >
                        west_distance - west.width_m * 0.5f &&
                    roof.width_m * 0.5f <
                        west_distance + west.width_m * 0.5f,
                "roof edge is not buried inside west fascia", roof.name);
    REQUIRE_MSG(column.bottom_m + column.height_m > roof.bottom_m,
                "column cap is coplanar with the roof underside", column.name);
    apricot_test::pass("canopy layers have stable, non-coplanar depth planes");
}

void the_new_buildings_keep_their_identity_and_detail() {
    // The richer prop pass started from 123 baked gas-station details. A 45%
    // increase rounds up to 179. The integrated pump-face texture deliberately
    // replaces six tiny box details on each of four dispensers.
    constexpr std::size_t kGasDetailBaselinePieces = 123u;
    constexpr std::size_t kGasDetailTargetPieces =
        (kGasDetailBaselinePieces * 145u + 99u) / 100u;
    constexpr std::size_t kIntegratedPumpFaceDetails = 24u;
    const std::vector<city::StartPart> gas =
        city::bake_building(city::kGasStationPlan);
    const std::vector<city::StartPart> apartments =
        city::bake_building(city::kApartmentPlan);
    const std::vector<city::StartPart> restaurant =
        city::bake_building(city::kFastFoodPlan);
    const std::vector<city::StartPart> tacomaco =
        city::bake_building(city::kTacomacoPlan);

    REQUIRE(city::kApartmentPlan.wall_count == 12u);
    REQUIRE(city::kFastFoodPlan.wall_count == 4u);
    REQUIRE_MSG(apartments.size() >= 110u,
                "apartments lost windows, balconies, or site detail",
                "Halloway Flats");
    REQUIRE_MSG(restaurant.size() >= 45u,
                "restaurant lost drive-through, sign, or parking detail",
                "Quickbite Grill");
    REQUIRE(tacomaco.size() == restaurant.size());
    REQUIRE_MSG(gas.size() + kIntegratedPumpFaceDetails >=
                    kGasDetailTargetPieces,
                "gas station fell below its 45 percent modeling increase",
                "Halloway Gas");

    std::size_t canopy_lights = 0;
    std::size_t pump_details = 0;
    std::size_t pump_control_faces = 0;
    std::size_t obsolete_pump_control_boxes = 0;
    std::size_t apartment_glass = 0;
    float apartment_top = 0.0f;
    bool has_drive_window = false;
    bool has_menu_board = false;
    bool has_pylon = false;
    bool has_halloway_sign = false;
    bool has_air_water = false;
    bool has_service_bin = false;
    bool has_rooftop_hvac = false;
    std::size_t canopy_hardware = 0;
    std::size_t service_machine_details = 0;
    std::size_t service_bin_details = 0;
    std::size_t hvac_louvers = 0;
    for (const city::StartPart& part : gas) {
        canopy_lights += std::strstr(part.name, "canopy light") != nullptr;
        pump_details += std::strstr(part.name, "pump ") != nullptr;
        pump_control_faces += std::strstr(part.name, "pump ") != nullptr &&
                              std::strstr(part.name, " face") != nullptr;
        obsolete_pump_control_boxes +=
            std::strstr(part.name, "pump ") != nullptr &&
            (std::strstr(part.name, " display") != nullptr ||
             std::strstr(part.name, " keypad") != nullptr ||
             std::strstr(part.name, " card reader") != nullptr ||
             std::strstr(part.name, " receipt slot") != nullptr ||
             std::strstr(part.name, " emergency stop") != nullptr ||
             std::strstr(part.name, " lower panel") != nullptr);
        canopy_hardware +=
            std::strstr(part.name, "canopy column") != nullptr ||
            std::strstr(part.name, "canopy drain") != nullptr;
        service_machine_details +=
            std::strstr(part.name, "air water") != nullptr;
        service_bin_details +=
            std::strstr(part.name, "east service bin") != nullptr;
        hvac_louvers += std::strstr(part.name, "hvac louver") != nullptr;
        has_halloway_sign |=
            std::strcmp(part.name, "Halloway sign board") == 0;
        has_air_water |= std::strcmp(part.name, "air water station") == 0;
        has_service_bin |= std::strcmp(part.name, "east service bin") == 0;
        has_rooftop_hvac |=
            std::strcmp(part.name, "store rooftop hvac") == 0;
    }
    for (const city::StartPart& part : apartments) {
        apartment_top = std::max(apartment_top, part.bottom_m + part.height_m);
        if (part.finish == city::StartFinish::Glass) ++apartment_glass;
    }
    for (const city::StartPart& part : restaurant) {
        has_drive_window |=
            std::strcmp(part.name, "drive through window") == 0;
        has_menu_board |=
            std::strcmp(part.name, "drive through menu board") == 0;
        has_pylon |= std::strcmp(part.name, "restaurant pylon") == 0;
    }
    REQUIRE_MSG(apartment_glass >= 20u, "apartment facade lost its windows",
                "Halloway Flats");
    REQUIRE_MSG(apartment_top >= 9.5f, "apartment block is no longer three storeys",
                "Halloway Flats");
    REQUIRE_MSG(has_drive_window && has_menu_board && has_pylon,
                "restaurant no longer reads as a drive-through",
                "Quickbite Grill");
    REQUIRE_MSG(canopy_lights == 6u, "canopy lost its underside lighting",
                "Halloway Gas");
    REQUIRE_MSG(pump_details >= 28u, "fuel pumps lost their silhouette hardware",
                "Halloway Gas");
    REQUIRE_MSG(pump_control_faces == 8u,
                "fuel pumps lost a front or rear textured control face",
                "Halloway Gas");
    REQUIRE_MSG(obsolete_pump_control_boxes == 0u,
                "textured pump controls were duplicated by old box geometry",
                "Halloway Gas");
    REQUIRE_MSG(canopy_hardware >= 16u,
                "canopy lost its bases, safety bands, or drain leaders",
                "Halloway Gas");
    REQUIRE_MSG(service_machine_details >= 9u && service_bin_details >= 8u,
                "service props lost their small-scale hardware",
                "Halloway Gas");
    REQUIRE_MSG(hvac_louvers == 4u,
                "rooftop unit lost its modeled vent face", "Halloway Gas");
    REQUIRE_MSG(has_halloway_sign && has_air_water && has_service_bin &&
                    has_rooftop_hvac,
                "gas station no longer reads as a complete roadside business",
                "Halloway Gas");
    apricot_test::pass("district businesses retain authored identity and detail");
}

void halloway_wash_is_a_clear_drive_through() {
    const std::vector<city::StartPart> wash =
        city::bake_building(city::kCarWashPlan);
    std::size_t arches = 0;
    std::size_t brushes = 0;
    std::size_t ceiling_lights = 0;
    std::size_t blower_parts = 0;
    std::size_t vacuum_parts = 0;
    bool has_payment_kiosk = false;
    bool has_equipment_room = false;
    float wash_max_grid_x = -1e9f;

    for (const city::StartPart& part : wash) {
        arches += std::strstr(part.name, "wash arch") != nullptr;
        brushes += std::strstr(part.name, "wash brush") != nullptr;
        ceiling_lights +=
            std::strstr(part.name, "canopy light wash") != nullptr;
        blower_parts += std::strstr(part.name, "wash blower") != nullptr;
        vacuum_parts += std::strstr(part.name, "wash vacuum") != nullptr;
        has_payment_kiosk |=
            std::strcmp(part.name, "wash payment kiosk") == 0;
        has_equipment_room |=
            std::strcmp(part.name, "wash equipment west wall") == 0;

        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 grid = vellum_grid_point(part_corner_world(
                    city::kCarWashSite, part, sx, sz));
                REQUIRE_MSG(grid.x >= -82.0f && grid.x <= -57.0f,
                            "car wash leaves its west half-block", part.name);
                REQUIRE_MSG(grid.z >= -51.75f && grid.z <= -14.25f,
                            "car wash overlaps a street or sidewalk", part.name);
                wash_max_grid_x = std::max(wash_max_grid_x, grid.x);
            }
        }

        // Check the conservative world-axis boxes used by TerrainCollider,
        // not just the visible rotated mesh. The car needs its full collision
        // radius plus a little steering room across the entire wash lane.
        if (part.solid) {
            constexpr float kSteeringSlackM = 0.05f;
            const float required_gap =
                VehicleTuning{}.chassis_collision_radius + kSteeringSlackM;
            for (const float x : {-0.6f, 0.0f, 0.6f}) {
                for (int z_quarters = -56; z_quarters <= 56; ++z_quarters) {
                    const float z = static_cast<float>(z_quarters) * 0.25f;
                    const city::Vec2 lane_point = world_point(
                        city::kCarWashSite, {x, z});
                    REQUIRE_MSG(
                        world_aabb_gap_xz(city::kCarWashSite, part,
                                          lane_point) >= required_gap,
                        "conservative wash collider intrudes into the usable lane",
                        part.name);
                }
            }
        }
    }

    float billboard_min_grid_x = 1e9f;
    for (const city::StartPart& part : city::kNessBillboardParts) {
        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 grid = vellum_grid_point(part_corner_world(
                    city::kNessBillboardSite, part, sx, sz));
                billboard_min_grid_x = std::min(billboard_min_grid_x, grid.x);
            }
        }
    }

    const float tunnel_clear_width =
        (city::kCarWashWalls[5].a.x - city::kCarWashWalls[5].thickness_m * 0.5f) -
        (city::kCarWashWalls[0].a.x + city::kCarWashWalls[0].thickness_m * 0.5f);
    const float queue_depth =
        (city::kCarWashSite.lot_depth_m - city::kCarWashRoofs[0].depth_m) *
        0.5f;

    REQUIRE(tunnel_clear_width >= 5.0f);
    REQUIRE(city::kCarWashPlan.wall_count == 13u);
    REQUIRE(queue_depth >= 5.5f);
    REQUIRE(city::kCarWashParts[3].bottom_m >= 3.0f);
    REQUIRE(arches == 15u);
    REQUIRE(brushes == 8u);
    REQUIRE(ceiling_lights == 4u);
    REQUIRE(blower_parts >= 2u);
    REQUIRE(vacuum_parts >= 6u);
    REQUIRE(has_payment_kiosk);
    REQUIRE(has_equipment_room);
    REQUIRE(wash_max_grid_x + 2.0f <= billboard_min_grid_x);
    apricot_test::pass(
        "Halloway Wash has a clear tunnel, queues, machinery, and billboard gap");
}

void every_plot_fits_between_the_road_sidewalks() {
    const auto check_plot = [](const city::StartSite& site,
                               const city::BuildingPlan& plan,
                               float west_clear, float east_clear,
                               float south_clear, float north_clear) {
        const std::vector<city::StartPart> parts = city::bake_building(plan);
        const city::StartPart* lot = nullptr;
        for (const city::StartPart& part : parts) {
            if (part.finish == city::StartFinish::Asphalt) {
                lot = &part;
                break;
            }
        }
        REQUIRE_MSG(lot != nullptr, "creator plan has no plotted lot", site.name);

        float min_x = 1e9f;
        float max_x = -1e9f;
        float min_z = 1e9f;
        float max_z = -1e9f;
        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 grid =
                    vellum_grid_point(part_corner_world(site, *lot, sx, sz));
                min_x = std::min(min_x, grid.x);
                max_x = std::max(max_x, grid.x);
                min_z = std::min(min_z, grid.z);
                max_z = std::max(max_z, grid.z);
            }
        }

        REQUIRE_MSG(min_z >= south_clear, "lot overlaps its south street", site.name);
        REQUIRE_MSG(max_z <= north_clear, "lot overlaps its north street", site.name);
        REQUIRE_MSG(min_x >= west_clear, "lot overlaps its west street", site.name);
        REQUIRE_MSG(max_x <= east_clear, "lot overlaps its east street", site.name);

        // The plot fitting is not enough if a roof, sign or stair still hangs
        // over it. Check every creator-emitted footprint against the road
        // ribbon boundaries too; roofs may use the smaller zero-margin edge.
        for (const city::StartPart& part : parts) {
            float part_min_x = 1e9f;
            float part_max_x = -1e9f;
            float part_min_z = 1e9f;
            float part_max_z = -1e9f;
            for (const float sx : {-1.0f, 1.0f}) {
                for (const float sz : {-1.0f, 1.0f}) {
                    const city::Vec2 grid = vellum_grid_point(
                        part_corner_world(site, part, sx, sz));
                    part_min_x = std::min(part_min_x, grid.x);
                    part_max_x = std::max(part_max_x, grid.x);
                    part_min_z = std::min(part_min_z, grid.z);
                    part_max_z = std::max(part_max_z, grid.z);
                }
            }
            REQUIRE_MSG(part_min_z >= south_clear - 0.25f,
                        "building piece overlaps its south street", part.name);
            REQUIRE_MSG(part_max_z <= north_clear + 0.25f,
                        "building piece overlaps its north street", part.name);
            REQUIRE_MSG(part_min_x >= west_clear,
                        "building piece overlaps its west street", part.name);
            REQUIRE_MSG(part_max_x <= east_clear,
                        "building piece overlaps its east street", part.name);
        }
    };

    check_plot(city::kGasStationSite, city::kGasStationPlan,
               -82.0f, -14.0f, 14.25f, 51.75f);
    check_plot(city::kCarWashSite, city::kCarWashPlan,
               -82.0f, -57.0f, -51.75f, -14.25f);
    check_plot(city::kMotelSite, city::kMotelPlan,
               -174.0f, -102.0f, 14.25f, 51.75f);
    check_plot(city::kApartmentSite, city::kApartmentPlan,
               14.0f, 82.0f, 14.25f, 51.75f);
    check_plot(city::kFastFoodSite, city::kFastFoodPlan,
               -82.0f, -14.0f, 72.25f, 113.75f);
    check_plot(city::kTacomacoSite, city::kTacomacoPlan,
               102.0f, 170.0f, 134.25f, 175.75f);
    check_plot(city::kBankSite, city::kBankPlan,
               14.0f, 82.0f, 72.25f, 113.75f);
    apricot_test::pass("every district plot stays off roads and sidewalks");
}

void town_billboard_is_readable_structural_and_off_the_road() {
    const city::StartPart* face = nullptr;
    const city::StartPart* backing = nullptr;
    int solid_posts = 0;
    int solid_footings = 0;
    for (const city::StartPart& part : city::kNessBillboardParts) {
        if (std::strcmp(part.name, "billboard face ness and ness") == 0) {
            face = &part;
        }
        if (std::strcmp(part.name, "billboard backing") == 0) {
            backing = &part;
        }
        solid_posts += part.solid && std::strstr(part.name, "billboard post");
        solid_footings +=
            part.solid && std::strstr(part.name, "billboard footing");

        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 grid = vellum_grid_point(part_corner_world(
                    city::kNessBillboardSite, part, sx, sz));
                REQUIRE_MSG(grid.x >= -82.0f && grid.x <= -14.0f,
                            "billboard overlaps an east/west street",
                            part.name);
                REQUIRE_MSG(grid.z >= -51.75f && grid.z <= -14.25f,
                            "billboard overlaps Halloway or its south street",
                            part.name);
            }
        }
    }

    REQUIRE(face != nullptr);
    REQUIRE(backing != nullptr);
    REQUIRE(!face->solid);
    REQUIRE(backing->solid);
    REQUIRE(solid_posts == 2);
    REQUIRE(solid_footings == 2);
    REQUIRE_NEAR(face->width_m / face->height_m, 1280.0f / 588.0f, 1e-4);
    REQUIRE(face->bottom_m >= 4.5f);
    REQUIRE(face->bottom_m >= backing->bottom_m);
    REQUIRE(face->bottom_m + face->height_m <=
            backing->bottom_m + backing->height_m);
    REQUIRE(face->centre.z + face->depth_m * 0.5f >
            backing->centre.z + backing->depth_m * 0.5f);
    apricot_test::pass(
        "town billboard keeps the supplied aspect, structure, and road clearance");
}

void taxi_billboard_has_a_distinct_monopole_fixture() {
    const city::StartPart* face = nullptr;
    const city::StartPart* backing = nullptr;
    int solid_monopoles = 0;
    int solid_footings = 0;
    int yokes = 0;
    int catwalks = 0;
    for (const city::StartPart& part : city::kPinnatyTaxiBillboardParts) {
        if (std::strcmp(part.name, "billboard face pinnaty taxi") == 0) {
            face = &part;
        }
        if (std::strcmp(part.name, "taxi billboard backing") == 0) {
            backing = &part;
        }
        solid_monopoles +=
            part.solid && std::strstr(part.name, "monopole") != nullptr;
        solid_footings +=
            part.solid && std::strstr(part.name, "footing") != nullptr;
        yokes += std::strstr(part.name, "yoke") != nullptr;
        catwalks += std::strstr(part.name, "catwalk") != nullptr;

        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sz : {-1.0f, 1.0f}) {
                const city::Vec2 grid = vellum_grid_point(part_corner_world(
                    city::kPinnatyTaxiBillboardSite, part, sx, sz));
                REQUIRE_MSG(grid.x >= 14.0f && grid.x <= 82.0f,
                            "taxi billboard overlaps an east/west street",
                            part.name);
                REQUIRE_MSG(grid.z >= -51.75f && grid.z <= -14.25f,
                            "taxi billboard overlaps a north/south street",
                            part.name);
            }
        }
    }

    REQUIRE(face != nullptr);
    REQUIRE(backing != nullptr);
    REQUIRE(!face->solid);
    REQUIRE(backing->solid);
    REQUIRE(solid_monopoles == 1);
    REQUIRE(solid_footings == 1);
    REQUIRE(yokes == 2);
    REQUIRE(catwalks == 0);
    REQUIRE_NEAR(face->width_m / face->height_m, 1850.0f / 850.0f, 1e-4);
    REQUIRE(face->bottom_m >= backing->bottom_m);
    REQUIRE(face->bottom_m + face->height_m <=
            backing->bottom_m + backing->height_m);
    REQUIRE(face->centre.z + face->depth_m * 0.5f >
            backing->centre.z + backing->depth_m * 0.5f);
    apricot_test::pass(
        "taxi billboard uses a road-clear single-pole fixture and exact art ratio");
}

void the_player_does_not_spawn_inside_a_building() {
    constexpr city::Vec2 kSpawn{0.0f, 0.0f};

    struct SitePlan {
        const city::StartSite* site;
        const city::BuildingPlan* plan;
    };
    const SitePlan sites[] = {
        {&city::kGasStationSite, &city::kGasStationPlan},
        {&city::kCarWashSite, &city::kCarWashPlan},
        {&city::kMotelSite, &city::kMotelPlan},
        {&city::kApartmentSite, &city::kApartmentPlan},
        {&city::kFastFoodSite, &city::kFastFoodPlan},
        {&city::kTacomacoSite, &city::kTacomacoPlan},
        {&city::kBankSite, &city::kBankPlan},
        {&city::kAirportSite, &city::kAirportPlan},
        {&city::kAutoRepairSite, &city::kAutoRepairPlan},
        {&city::kLaundromatSite, &city::kLaundromatPlan},
    };
    for (const SitePlan& entry : sites) {
        const city::Vec2 local = local_point(*entry.site, kSpawn);
        const std::vector<city::StartPart> parts =
            city::bake_building(*entry.plan);
        for (const city::StartPart& part : parts) {
            if (part.solid) {
                REQUIRE_MSG(!contains_xz(part, local),
                            "spawn overlaps a creator-baked building",
                            entry.site->name);
            }
        }
    }
    const std::vector<city::StartPart> gas_shell =
        city::bake_building(city::kGasStationPlan);
    REQUIRE_MSG(gas_shell.size() >= 110u,
                "the gas station lost its canopy, pumps or sign detail",
                "gas detail");
    apricot_test::pass("the player starts clear of every solid building part");
}

}  // namespace

void neighborhood_shops_have_clear_entries_and_real_fixtures() {
    const auto repair=city::bake_auto_repair();
    const auto laundry=city::bake_laundromat();
    const auto count=[](const auto& parts,const char* name) {
        return std::count_if(parts.begin(),parts.end(),[&](const auto& p) {
            return std::strcmp(p.name,name)==0;
        });
    };
    REQUIRE(count(repair,"repair lift column")==4);
    REQUIRE(count(repair,"repair tire stack")==9);
    REQUIRE(count(laundry,"laundry washer body")==8);
    REQUIRE(count(laundry,"laundry washer drum glass")==8);
    REQUIRE(count(repair,"repair shop sign face")==1);
    REQUIRE(count(laundry,"laundry shop sign face")==1);
    const auto clear=[](const auto& parts,city::Vec2 point) {
        for (const auto& p:parts) {
            if (p.solid && p.bottom_m<2.5f && p.bottom_m+p.height_m>.25f &&
                contains_xz(p,point)) return false;
        }
        return true;
    };
    for (float z=7;z>=-9;z-=.25f) {
        REQUIRE(clear(repair,{-9,z}));
        REQUIRE(clear(repair,{-1,z}));
    }
    for (float z=7;z>=-7;z-=.25f) REQUIRE(clear(laundry,{-8.2f,z}));
    REQUIRE(city::kAutoRepairSite.ground_m==12);
    REQUIRE(city::kLaundromatSite.ground_m==12);
    apricot_test::pass("new shops retain fixtures and unobstructed bay and walking entries");
}

int main() {
    neighborhood_shops_have_clear_entries_and_real_fixtures();
    every_site_follows_the_downtown_grid();
    the_motel_is_a_real_u();
    each_site_is_one_complete_creator_document();
    the_bank_is_really_enterable_and_furnished();
    camber_point_has_a_real_international_airport();
    airport_airside_and_landside_clearances_make_sense();
    airport_service_entry_is_secured_and_reroutable();
    airport_main_approach_reaches_landside_not_the_field();
    airport_parking_has_vehicle_and_pedestrian_access();
    airport_edge_development_is_zoned_and_reachable();
    canopy_faces_do_not_fight_for_the_same_depth();
    the_new_buildings_keep_their_identity_and_detail();
    halloway_wash_is_a_clear_drive_through();
    every_plot_fits_between_the_road_sidewalks();
    town_billboard_is_readable_structural_and_off_the_road();
    taxi_billboard_has_a_distinct_monopole_fixture();
    the_player_does_not_spawn_inside_a_building();
    return apricot_test::done("start_area_tests");
}

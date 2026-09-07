#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "city/airport.h"
#include "city/billboards.h"
#include "city/construction_expansion.h"
#include "city/construction_neighbor_equipment.h"
#include "city/construction_neighbor_materials.h"
#include "city/construction_street_detail.h"
#include "city/emergency_stations.h"
#include "city/east_arm_plaza.h"
#include "city/gun_store.h"
#include "city/graffiti.h"
#include "city/hospital_north_parking.h"
#include "city/hospital_overhaul_logistics.h"
#include "city/marina.h"
#include "city/neighborhood_bar.h"
#include "city/neighborhood_shops.h"
#include "city/neighborhood_towers.h"
#include "city/pawn_shop.h"
#include "city/residential_neighborhood.h"
#include "city/tacomaco.h"
#include "city/tidewater_farm.h"
#include "city/vellum_infill.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

struct AuthoredLot {
    const char* name = nullptr;
    const city::StartSite* site = nullptr;
    city::Vec2 centre{};
    float width_m = 0.0f;
    float depth_m = 0.0f;
};

AuthoredLot lot(const city::StartSite& site) {
    return {site.name, &site, site.lot_centre, site.lot_width_m,
            site.lot_depth_m};
}

AuthoredLot lot(const char* name, const city::StartSite& site,
                city::Vec2 centre, float width_m, float depth_m) {
    return {name, &site, centre, width_m, depth_m};
}

glm::vec2 world_point(const AuthoredLot& item, glm::vec2 local) {
    const auto& site = *item.site;
    const city::Vec2 p{item.centre.x + local.x, item.centre.z + local.y};
    return {site.origin.x + site.cos_yaw * p.x + site.sin_yaw * p.z,
            site.origin.z - site.sin_yaw * p.x + site.cos_yaw * p.z};
}

std::array<glm::vec2, 4> corners(const AuthoredLot& item) {
    const float x = item.width_m * 0.5f;
    const float z = item.depth_m * 0.5f;
    return {world_point(item, {-x, -z}), world_point(item, {x, -z}),
            world_point(item, {x, z}), world_point(item, {-x, z})};
}

bool separated_on(const std::array<glm::vec2, 4>& a,
                  const std::array<glm::vec2, 4>& b, glm::vec2 axis,
                  float margin_m) {
    const float length = glm::length(axis);
    if (length < 1e-5f) return false;
    axis /= length;
    float a_lo = 1e9f;
    float a_hi = -1e9f;
    float b_lo = 1e9f;
    float b_hi = -1e9f;
    for (const auto& p : a) {
        const float d = glm::dot(p, axis);
        a_lo = std::min(a_lo, d);
        a_hi = std::max(a_hi, d);
    }
    for (const auto& p : b) {
        const float d = glm::dot(p, axis);
        b_lo = std::min(b_lo, d);
        b_hi = std::max(b_hi, d);
    }
    return a_hi + margin_m <= b_lo || b_hi + margin_m <= a_lo;
}

bool lots_are_separate(const AuthoredLot& a, const AuthoredLot& b,
                       float margin_m = 0.25f) {
    const auto ca = corners(a);
    const auto cb = corners(b);
    const glm::vec2 axes[] = {
        ca[1] - ca[0], ca[3] - ca[0], cb[1] - cb[0], cb[3] - cb[0],
    };
    for (const auto& edge : axes) {
        const glm::vec2 axis{-edge.y, edge.x};
        if (separated_on(ca, cb, axis, margin_m)) return true;
    }
    return false;
}

std::vector<AuthoredLot> active_lots() {
    std::vector<AuthoredLot> out{
        lot(city::kGasStationSite),
        lot(city::kCarWashSite),
        lot(city::kMotelSite),
        lot(city::kApartmentSite),
        lot(city::kFastFoodSite),
        lot(city::kTacomacoSite),
        lot(city::kBankSite),
        lot(city::kAutoRepairSite),
        lot(city::kLaundromatSite),
        lot(city::kPawnShopSite),
        lot(city::kGunStoreSite),
        lot(city::kEastArmPlazaSite),
        lot(city::kNeighborhoodBarSite),
        lot(city::kFireStationSite),
        lot(city::kPoliceStationSite),
        lot(city::kConstructionSite.site),
        lot(city::kConstructionNeighborMaterialsSite),
        lot(city::kConstructionNeighborEquipment.site),
        lot(city::kConstructionStreetDetailSite),
        lot(city::kHospitalSite),
        lot("Vellum Regional Hospital parking garage", city::kHospitalSite,
            city::kHospitalGarageCentre, city::kHospitalGarageWidthM,
            city::kHospitalBlockDepthM),
        lot("Vellum Regional Hospital service yard", city::kHospitalSite,
            {(city::kHospitalServiceYardMinX +
              city::kHospitalServiceYardMaxX) * .5f,
             (city::kHospitalServiceYardMinZ +
              city::kHospitalServiceYardMaxZ) * .5f},
            city::kHospitalServiceYardMaxX -
                city::kHospitalServiceYardMinX,
            city::kHospitalServiceYardMaxZ -
                city::kHospitalServiceYardMinZ),
        lot(city::kHospitalNorthParkingSite),
        lot(city::kNessBillboardSite),
        lot(city::kPinnatyTaxiBillboardSite),
        lot(city::kAirportSite),
        lot(city::kMarlinDockSite),
        lot(city::kTidewaterFarmSite),
    };
    for (const auto& tower : city::kNeighborhoodTowers) {
        if (!city::hospital_campus_replaces(tower.site))
            out.push_back(lot(tower.site));
    }
    for (const auto& expansion : city::kAdditionalConstructionSites)
        out.push_back(lot(expansion.site));
    for (const auto& twin : city::kTwinSkyscraperBlockSites)
        out.push_back(lot(twin));
    for (const auto& parcel : city::kVellumInfillParcels)
        out.push_back(lot(parcel.site));
    for (const auto& house : city::kResidentialHouses)
        out.push_back(lot(house.site));
    return out;
}

void no_active_authored_lots_overlap() {
    const auto lots = active_lots();
    std::size_t comparisons = 0;
    std::size_t overlaps = 0;
    for (std::size_t i = 0; i < lots.size(); ++i) {
        REQUIRE(lots[i].name != nullptr);
        REQUIRE(lots[i].site != nullptr);
        REQUIRE(lots[i].width_m > 0.0f && lots[i].depth_m > 0.0f);
        for (std::size_t j = i + 1; j < lots.size(); ++j) {
            ++comparisons;
            if (!lots_are_separate(lots[i], lots[j])) {
                ++overlaps;
                std::printf("  overlap: %s <> %s\n", lots[i].name,
                            lots[j].name);
            }
        }
    }
    std::printf("  checked %zu active lots across %zu pairs\n", lots.size(),
                comparisons);
    REQUIRE_MSG(overlaps == 0u, "active authored lots overlap",
                "see pairs above");
    apricot_test::pass(
        "whole-city active lot inventory has no accidental overlaps");
}

void vellum_lots_have_flat_ground_support() {
    const auto lots = active_lots();
    std::size_t checked = 0;
    for (const auto& item : lots) {
        const auto& site = *item.site;
        const bool vellum_basis =
            std::fabs(site.cos_yaw - city::kGridCos) < 1e-5f &&
            std::fabs(site.sin_yaw - city::kGridSin) < 1e-5f &&
            std::fabs(site.ground_m - city::kStartAreaGroundM) < 1e-5f;
        if (!vellum_basis) continue;
        const auto points = corners(item);
        for (const auto& p : points) {
            const float height = mesh_height_at(city::kMapSeed, p.x, p.y);
            if (std::fabs(height - site.ground_m) > .02f)
                std::printf("  unsupported: %s terrain %.3f at %.1f %.1f\n",
                            item.name, height, p.x, p.y);
            REQUIRE_NEAR(height, site.ground_m, .02f);
        }
        ++checked;
    }
    std::printf("  checked flat terrain under %zu Vellum lots\n", checked);
    apricot_test::pass("Vellum lots sit on their authored 12 metre datum");
}

void graffiti_is_a_complete_rear_wall_set() {
    std::array<bool, city::kGraffitiTagCount> seen{};
    std::size_t distinct_sites = 0;
    const city::StartSite* previous_site = nullptr;
    for (const auto& tag : city::kGraffitiPlacements) {
        REQUIRE(tag.site != nullptr);
        REQUIRE(tag.tag_index < city::kGraffitiTagCount);
        REQUIRE(!seen[tag.tag_index]);
        seen[tag.tag_index] = true;
        REQUIRE(tag.width_m > 2.0f && tag.height_m > 2.0f);
        REQUIRE(tag.centre_height_m > 1.0f && tag.centre_height_m < 4.0f);
        // Decals face one of the building's four cardinal local wall planes.
        const float quarter_turns = tag.local_yaw_deg / 90.0f;
        REQUIRE_NEAR(quarter_turns, std::round(quarter_turns), 1e-4f);
        if (tag.site != previous_site) ++distinct_sites;
        previous_site = tag.site;
    }
    for (bool present : seen) REQUIRE(present);
    REQUIRE(distinct_sites >= 8u);
    apricot_test::pass("ten unique graffiti decals cover rear and alley walls");
}

}  // namespace

int main() {
    no_active_authored_lots_overlap();
    vellum_lots_have_flat_ground_support();
    graffiti_is_a_complete_rear_wall_set();
    return apricot_test::done("authored_city_layout_tests");
}

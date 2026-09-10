#pragma once

#include <array>
#include <cstddef>

#include "city/start_area.h"

namespace apricot {
namespace city {

// Miandi's city frame. Local coordinates keep the block layout readable; the
// site anchor places the whole city southeast of Florangia Regional Airport at
// the rounded southeast tip of Florangia. Detailed district sites replace the
// coarse massing below one block at a time.
inline constexpr Vec2 kMiandiWorldOrigin{7500.0f, 8400.0f};

// states.h carries Miandi's map label anchor as a literal so the city table
// stays free of the layout headers. This is the check that stops the copy
// drifting: move the city and the label follows, or the build stops.
static_assert(city_at(CityId::Miandi).map_label_anchor.x == kMiandiWorldOrigin.x &&
                  city_at(CityId::Miandi).map_label_anchor.z == kMiandiWorldOrigin.z,
              "Miandi's map label anchor must track kMiandiWorldOrigin");
inline constexpr float kMiandiGroundM = 8.0f;
inline constexpr float kMiandiHalfWidthM = 900.0f;
inline constexpr float kMiandiHalfDepthM = 700.0f;

inline constexpr StartSite kMiandiSite{
    "Miandi", kMiandiWorldOrigin, 1.0f, 0.0f, {0.0f, 0.0f},
    kMiandiHalfWidthM * 2.0f, kMiandiHalfDepthM * 2.0f,
    kMiandiGroundM, 2600.0f};

constexpr Vec2 miandi_world_point(Vec2 local) {
    return {kMiandiWorldOrigin.x + local.x, kMiandiWorldOrigin.z + local.z};
}

constexpr bool miandi_city_contains(float world_x, float world_z,
                                    float margin_m = 0.0f) {
    return world_x >= kMiandiWorldOrigin.x - kMiandiHalfWidthM - margin_m &&
           world_x <= kMiandiWorldOrigin.x + kMiandiHalfWidthM + margin_m &&
           world_z >= kMiandiWorldOrigin.z - kMiandiHalfDepthM - margin_m &&
           world_z <= kMiandiWorldOrigin.z + kMiandiHalfDepthM + margin_m;
}

enum class MiandiDistrictId : unsigned char {
    BayfrontCore = 0,
    CalleOcho,
    PortSol,
    AirportGateway,
    OceanDrive,
    MangroveEdge,
    CausewayIslands,
    Count,
};

struct MiandiDistrict {
    MiandiDistrictId id = MiandiDistrictId::Count;
    const char* name = nullptr;
    const char* chase_role = nullptr;
    Boundary boundary{};
    Vec2 landmark{};
};

// Coarse local jurisdictions. Gaps are street, coast, and canal reserves, not
// forgotten land. Landmarks point at the first signature block in each area.
inline constexpr std::array<MiandiDistrict, 7> kMiandiDistricts{{
    {MiandiDistrictId::BayfrontCore, "Bayfront Core",
     "tight high-rise grid with fast four-way choices",
     {{{-180.0f, -180.0f}, {180.0f, -180.0f}, {180.0f, 180.0f},
       {-180.0f, 180.0f}}, 4}, {100.0f, -100.0f}},
    {MiandiDistrictId::CalleOcho, "Calle Ocho",
     "low-rise commercial blocks and short back-lane cuts",
     {{{-580.0f, -180.0f}, {-220.0f, -180.0f}, {-220.0f, 180.0f},
       {-580.0f, 180.0f}}, 4}, {-500.0f, -100.0f}},
    {MiandiDistrictId::PortSol, "Port Sol",
     "warehouse runs that build speed before hard waterfront turns",
     {{{-580.0f, 250.0f}, {180.0f, 250.0f}, {180.0f, 550.0f},
       {-580.0f, 550.0f}}, 4}, {-100.0f, 300.0f}},
    {MiandiDistrictId::AirportGateway, "Airport Gateway",
     "broad hotel and office approach from Florangia Highway",
     {{{-250.0f, -650.0f}, {250.0f, -650.0f}, {250.0f, -250.0f},
       {-250.0f, -250.0f}}, 4}, {0.0f, -430.0f}},
    {MiandiDistrictId::OceanDrive, "Ocean Drive",
     "bright coastal hotels beside the cleanest straight in town",
     {{{420.0f, -180.0f}, {850.0f, -180.0f}, {850.0f, 180.0f},
       {420.0f, 180.0f}}, 4}, {500.0f, -100.0f}},
    {MiandiDistrictId::MangroveEdge, "Mangrove Edge",
     "low western fringe with poor sightlines and quiet exits",
     {{{-850.0f, -500.0f}, {-620.0f, -500.0f}, {-620.0f, 180.0f},
       {-850.0f, 180.0f}}, 4}, {-740.0f, -360.0f}},
    {MiandiDistrictId::CausewayIslands, "Causeway Islands",
     "resort blocks reached through two obvious chokepoints",
     {{{250.0f, 380.0f}, {850.0f, 380.0f}, {850.0f, 600.0f},
       {250.0f, 600.0f}}, 4}, {400.0f, 460.0f}},
}};

// Asphalt pads establish readable city blocks between the actual road
// ribbons. Buildings are simple collision-backed massing for this pass.
inline constexpr StartPart kMiandiBuildingParts[] = {
    {"Calle Ocho west lot", {-725.0f, -100.0f}, 0.00f, 210.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Calle Ocho market", {-725.0f, -100.0f}, 0.12f, 170.0f, 11.0f, 112.0f, StartFinish::WarmWall, true},
    {"Calle Ocho centre lot", {-430.0f, -100.0f}, 0.00f, 270.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Calle Ocho shops west", {-500.0f, -100.0f}, 0.12f, 105.0f, 13.0f, 112.0f, StartFinish::Brick, true},
    {"Calle Ocho shops east", {-360.0f, -100.0f}, 0.12f, 105.0f, 9.0f, 112.0f, StartFinish::TealDoor, true},

    {"Bayfront northwest lot", {-155.0f, -100.0f}, 0.00f, 260.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Bayfront Tower podium", {-155.0f, -100.0f}, 0.12f, 210.0f, 9.0f, 118.0f, StartFinish::Concrete, true},
    {"Bayfront Tower", {-155.0f, -100.0f}, 9.12f, 118.0f, 52.0f, 86.0f, StartFinish::Glass, true},
    {"Bayfront Tower roof", {-155.0f, -100.0f}, 61.12f, 124.0f, 1.8f, 92.0f, StartFinish::White, false},
    {"Miandi Crown lot", {155.0f, -100.0f}, 0.00f, 260.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Miandi Crown podium", {155.0f, -100.0f}, 0.12f, 210.0f, 11.0f, 118.0f, StartFinish::White, true},
    {"Miandi Crown tower", {155.0f, -100.0f}, 11.12f, 104.0f, 68.0f, 82.0f, StartFinish::Glass, true},
    {"Miandi Crown cap", {155.0f, -100.0f}, 79.12f, 116.0f, 8.0f, 94.0f, StartFinish::Yellow, false},

    {"Brickell west lot", {-155.0f, 100.0f}, 0.00f, 260.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Brickell House podium", {-155.0f, 100.0f}, 0.12f, 210.0f, 8.0f, 118.0f, StartFinish::WarmWall, true},
    {"Brickell House tower", {-155.0f, 100.0f}, 8.12f, 126.0f, 44.0f, 92.0f, StartFinish::White, true},
    {"Brickell east lot", {155.0f, 100.0f}, 0.00f, 260.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Sol Financial podium", {155.0f, 100.0f}, 0.12f, 210.0f, 10.0f, 118.0f, StartFinish::Concrete, true},
    {"Sol Financial tower", {155.0f, 100.0f}, 10.12f, 112.0f, 58.0f, 88.0f, StartFinish::Glass, true},

    {"Ocean Drive north lot", {430.0f, -100.0f}, 0.00f, 270.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Ocean Drive Coral Hotel", {430.0f, -100.0f}, 0.12f, 220.0f, 28.0f, 116.0f, StartFinish::WarmWall, true},
    {"Ocean Drive east lot", {725.0f, -100.0f}, 0.00f, 210.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Ocean Drive Surf Hotel", {725.0f, -100.0f}, 0.12f, 170.0f, 36.0f, 116.0f, StartFinish::White, true},
    {"Ocean Drive Surf Hotel roof", {725.0f, -100.0f}, 36.12f, 176.0f, 1.4f, 122.0f, StartFinish::TealDoor, false},

    {"Bayfront south lot", {430.0f, 100.0f}, 0.00f, 270.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Bayfront Terraces", {430.0f, 100.0f}, 0.12f, 220.0f, 22.0f, 116.0f, StartFinish::Concrete, true},
    {"Ocean south lot", {725.0f, 100.0f}, 0.00f, 210.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Ocean Court", {725.0f, 100.0f}, 0.12f, 170.0f, 18.0f, 116.0f, StartFinish::TealDoor, true},

    {"Port Sol west lot", {-500.0f, 300.0f}, 0.00f, 210.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Port Sol cold warehouse", {-500.0f, 300.0f}, 0.12f, 170.0f, 10.0f, 112.0f, StartFinish::Steel, true},
    {"Port Sol east lot", {-430.0f, 300.0f}, 0.00f, 270.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Port Sol freight shed", {-430.0f, 300.0f}, 0.12f, 230.0f, 12.0f, 112.0f, StartFinish::Brick, true},

    {"Airport Gateway lot", {-155.0f, -300.0f}, 0.00f, 260.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Airport Gateway offices", {-155.0f, -300.0f}, 0.12f, 210.0f, 24.0f, 116.0f, StartFinish::Glass, true},
    {"Mangrove Edge lot", {-725.0f, -300.0f}, 0.00f, 210.0f, 0.12f, 150.0f, StartFinish::Asphalt, false},
    {"Mangrove Edge apartments", {-725.0f, -300.0f}, 0.12f, 170.0f, 16.0f, 112.0f, StartFinish::WarmWall, true},

    {"Causeway resort lot", {300.0f, 400.0f}, 0.00f, 250.0f, 0.12f, 160.0f, StartFinish::Asphalt, false},
    {"Causeway Palms Hotel", {300.0f, 400.0f}, 0.12f, 200.0f, 24.0f, 120.0f, StartFinish::White, true},
    {"Causeway east lot", {500.0f, 400.0f}, 0.00f, 130.0f, 0.12f, 160.0f, StartFinish::Asphalt, false},
    {"Causeway Motor Court", {500.0f, 400.0f}, 0.12f, 100.0f, 12.0f, 120.0f, StartFinish::TealDoor, true},
};

inline constexpr std::size_t kMiandiBuildingPartCount =
    sizeof(kMiandiBuildingParts) / sizeof(kMiandiBuildingParts[0]);

// The detailed 200 m grid runs through most of the original broad placeholder
// pads. Keep only the two far-west context blocks; drawing the others would put
// buildings and asphalt directly across Solana, Mango, Royal Palm, Seabreeze,
// and the finished signature parcels.
constexpr bool miandi_keeps_rough_part(const StartPart& part) {
    return part.centre.x <= -700.0f;
}

inline constexpr const MiandiDistrict& miandi_district(MiandiDistrictId id) {
    return kMiandiDistricts[static_cast<std::size_t>(id)];
}

constexpr bool miandi_layout_is_well_formed() {
    for (std::size_t i = 0; i < kMiandiDistricts.size(); ++i) {
        const auto& d = kMiandiDistricts[i];
        if (static_cast<std::size_t>(d.id) != i || d.name == nullptr ||
            d.chase_role == nullptr || d.boundary.count != 4 ||
            d.boundary.area_m2() <= 0.0f ||
            !d.boundary.contains(d.landmark.x, d.landmark.z))
            return false;
    }
    return kMiandiBuildingPartCount >= 30;
}

static_assert(miandi_layout_is_well_formed(),
              "Miandi districts and first-pass massing must be complete");

}  // namespace city
}  // namespace apricot

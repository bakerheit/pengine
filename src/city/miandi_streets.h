#pragma once

#include <cstddef>
#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"

namespace apricot {
namespace city {

// Stable Miandi road identities. Keep 235..239 free for the later alley,
// marina-loop, and bridge packages described by docs/design/miandi.md.
inline constexpr uint32_t kMiandiBiscayneBoulevardRoadId = 222;
inline constexpr uint32_t kMiandiCalleOchoRoadId = 223;
inline constexpr uint32_t kMiandiBayfrontAvenueRoadId = 224;
inline constexpr uint32_t kMiandiCoralWayRoadId = 225;
inline constexpr uint32_t kMiandiPortSolDriveRoadId = 226;
inline constexpr uint32_t kMiandiPalmAvenueRoadId = 227;
inline constexpr uint32_t kMiandiOceanDriveRoadId = 228;
inline constexpr uint32_t kMiandiCausewayBoulevardRoadId = 229;
inline constexpr uint32_t kMiandiGatewayDriveRoadId = 230;
inline constexpr uint32_t kMiandiSolanaAvenueRoadId = 231;
inline constexpr uint32_t kMiandiMangoAvenueRoadId = 232;
inline constexpr uint32_t kMiandiRoyalPalmAvenueRoadId = 233;
inline constexpr uint32_t kMiandiSeabreezeAvenueRoadId = 234;

inline constexpr StartSite kMiandiStreetFixtureSite{
    "Miandi street fixtures", {7500.0f, 8400.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 1800.0f, 1400.0f, 8.0f, 900.0f};

// These site-local pieces sit on the broad Bayfront sidewalk edge, with two
// Biscayne palms to give the arrival spine a readable rhythm. Trunks, poles,
// benches, planters, and shelter posts carry collision; foliage, lamp heads,
// and shelter panels remain visual-only. All geometry comes through the same
// BuildingPlan bake.
inline constexpr BuildingPiece kMiandiStreetFixtures[] = {
    {"miandi bayfront palm 01", {-460.0f, -24.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi bayfront palm 01 crown", {-460.0f, -24.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
    {"miandi bayfront palm 02", {-260.0f, -24.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi bayfront palm 02 crown", {-260.0f, -24.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
    {"miandi bayfront palm 03", {-60.0f, -24.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi bayfront palm 03 crown", {-60.0f, -24.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
    {"miandi bayfront palm 04", {140.0f, -24.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi bayfront palm 04 crown", {140.0f, -24.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
    {"miandi bayfront light pole 01", {-360.0f, -24.0f}, 0.0f, 0.35f, 4.8f, 0.35f,
     BuildingFinish::Steel, true},
    {"miandi bayfront light head 01", {-360.0f, -24.0f}, 4.8f, 0.8f, 0.3f, 0.55f,
     BuildingFinish::Yellow, false},
    {"miandi bayfront light pole 02", {40.0f, -24.0f}, 0.0f, 0.35f, 4.8f, 0.35f,
     BuildingFinish::Steel, true},
    {"miandi bayfront light head 02", {40.0f, -24.0f}, 4.8f, 0.8f, 0.3f, 0.55f,
     BuildingFinish::Yellow, false},
    {"miandi bayfront bench", {-160.0f, -24.0f}, 0.0f, 2.4f, 0.45f, 0.55f,
     BuildingFinish::Steel, true},
    {"miandi bayfront planter", {240.0f, -24.0f}, 0.0f, 1.8f, 0.55f, 1.0f,
     BuildingFinish::Concrete, true},
    {"miandi bayfront bus shelter post near", {520.0f, -24.0f}, 0.0f, 0.25f, 2.6f,
     0.25f, BuildingFinish::Steel, true},
    {"miandi bayfront bus shelter post far", {560.0f, -24.0f}, 0.0f, 0.25f, 2.6f,
     0.25f, BuildingFinish::Steel, true},
    {"miandi bayfront bus shelter roof", {540.0f, -24.0f}, 2.6f, 5.0f, 0.18f,
     1.8f, BuildingFinish::Steel, false},
    {"miandi bayfront bus shelter back", {540.0f, -24.75f}, 0.0f, 5.0f, 2.5f,
     0.06f, BuildingFinish::Glass, false},
    {"miandi biscayne palm north", {-22.0f, -100.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi biscayne palm north crown", {-22.0f, -100.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
    {"miandi biscayne palm south", {-22.0f, 100.0f}, 0.0f, 1.2f, 5.5f, 1.2f,
     BuildingFinish::WarmWall, true},
    {"miandi biscayne palm south crown", {-22.0f, 100.0f}, 5.5f, 2.8f, 0.8f, 2.8f,
     BuildingFinish::WarmWall, false},
};

inline constexpr std::size_t kMiandiStreetFixtureCount =
    sizeof(kMiandiStreetFixtures) / sizeof(kMiandiStreetFixtures[0]);

inline constexpr BuildingPlan kMiandiStreetFixturePlan{
    "Miandi street fixtures", nullptr, 0, nullptr, 0,
    kMiandiStreetFixtures, kMiandiStreetFixtureCount, nullptr, 0};

inline std::vector<BuildingPiece> bake_miandi_street_fixtures() {
    return bake_building(kMiandiStreetFixturePlan);
}

static_assert(kMiandiStreetFixtureCount >= 8 && kMiandiStreetFixtureCount <= 24,
              "Miandi street fixture count must stay modest");

}  // namespace city
}  // namespace apricot

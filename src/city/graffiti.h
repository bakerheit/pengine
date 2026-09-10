#pragma once

#include <array>
#include <cstddef>

#include "city/gun_store.h"
#include "city/neighborhood_bar.h"
#include "city/neighborhood_shops.h"
#include "city/pawn_shop.h"
#include "city/tacomaco.h"

namespace apricot::city {

// Tags are authored as exterior, non-solid decals. Centres sit just beyond the
// named rear/service wall; yaw is local to the owning building site.
struct GraffitiPlacement {
    const StartSite* site;
    Vec2 local_centre;
    float centre_height_m;
    float width_m;
    float height_m;
    float local_yaw_deg;
    std::size_t tag_index;
};

inline constexpr std::size_t kGraffitiTagCount = 10u;
inline constexpr std::array<GraffitiPlacement, kGraffitiTagCount>
    kGraffitiPlacements{{
        // Halloway service lane: store rear and blind side wall.
        {&kGasStationSite, {  2.0f, -18.86f}, 2.35f, 3.4f, 2.5f, 180.0f, 0u},
        {&kGasStationSite, { -5.16f, -13.4f }, 2.20f, 2.8f, 2.3f, -90.0f, 1u},
        // Shop backs and the real alley behind The Bent Elbow.
        {&kAutoRepairSite, { -8.4f, -15.16f}, 2.65f, 4.1f, 3.1f, 180.0f, 2u},
        {&kLaundromatSite, {  3.4f, -14.14f}, 2.25f, 3.0f, 2.5f, 180.0f, 3u},
        {&kFastFoodSite,   { -8.5f,  11.16f}, 2.15f, 3.7f, 2.6f,   0.0f, 4u},
        {&kPawnShopSite,   { -4.4f, -12.66f}, 2.85f, 3.6f, 3.1f, 180.0f, 5u},
        {&kGunStoreSite,   {  3.8f, -10.16f}, 2.45f, 3.1f, 2.8f, 180.0f, 6u},
        {&kNeighborhoodBarSite, {-6.6f, -8.02f}, 2.35f, 3.2f, 2.7f, 180.0f, 7u},
        {&kBankSite,       { 10.2f,  15.16f}, 2.70f, 3.8f, 2.8f,   0.0f, 8u},
        {&kTacomacoSite,   {  2.6f, -14.08f}, 2.10f, 3.5f, 2.5f, 180.0f, 9u},
    }};

}  // namespace apricot::city

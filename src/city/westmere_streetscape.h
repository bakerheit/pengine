#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "city/luxury_neighborhood.h"

namespace apricot::city {

// Small, deliberately chunky street props for Westmere. The layered boxes,
// limited material palette and strong silhouettes keep the furniture readable
// at the draw distances used by an early-2000s console-style open world. No
// face carries words, a logo, or a borrowed brand mark.
enum class WestmereStreetscapeKind : std::uint8_t {
    Entry,
    Community,
    Court,
    Estate,
};

struct WestmereStreetscapeSite {
    StartSite site{};
    WestmereStreetscapeKind kind = WestmereStreetscapeKind::Estate;
    std::uint8_t variant = 0;
};

// Integration order is stable: neighborhood entry, community club, three
// court greens, then the nine house frontages in kLuxuryEstates order. Keeping
// the source StartSite in each record means the parent can append every bake
// with the existing StartSite/StartPart path and no coordinate conversion.
inline constexpr std::array<WestmereStreetscapeSite, 14>
    kWestmereStreetscapeSites{{
        {kWestmereGateSite, WestmereStreetscapeKind::Entry, 0},
        {kWestmereCommonSite, WestmereStreetscapeKind::Community, 0},
        {kWestmereCourtGreens[0], WestmereStreetscapeKind::Court, 0},
        {kWestmereCourtGreens[1], WestmereStreetscapeKind::Court, 1},
        {kWestmereCourtGreens[2], WestmereStreetscapeKind::Court, 2},
        {kLuxuryEstates[0].site, WestmereStreetscapeKind::Estate, 0},
        {kLuxuryEstates[1].site, WestmereStreetscapeKind::Estate, 1},
        {kLuxuryEstates[2].site, WestmereStreetscapeKind::Estate, 2},
        {kLuxuryEstates[3].site, WestmereStreetscapeKind::Estate, 3},
        {kLuxuryEstates[4].site, WestmereStreetscapeKind::Estate, 4},
        {kLuxuryEstates[5].site, WestmereStreetscapeKind::Estate, 5},
        {kLuxuryEstates[6].site, WestmereStreetscapeKind::Estate, 6},
        {kLuxuryEstates[7].site, WestmereStreetscapeKind::Estate, 7},
        {kLuxuryEstates[8].site, WestmereStreetscapeKind::Estate, 8},
    }};

inline Vec2 westmere_streetscape_world(const StartSite& site, Vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.z,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.z};
}

inline float westmere_streetscape_height(const StartSite& site,
                                         GroundSampler ground, float x,
                                         float z) {
    const Vec2 world = westmere_streetscape_world(site, {x, z});
    return ground.at(world.x, world.z) - site.ground_m;
}

inline std::vector<StartPart> bake_westmere_streetscape_site(
    std::size_t index, GroundSampler ground) {
    const WestmereStreetscapeSite& record =
        kWestmereStreetscapeSites.at(index);
    const StartSite& site = record.site;
    std::vector<StartPart> out;
    out.reserve(32);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };
    const auto at = [&](float x, float z) {
        return westmere_streetscape_height(site, ground, x, z);
    };
    const auto add_shrub = [&](float x, float z, float width, float height,
                               float depth, float yaw_deg = 0.0f,
                               float lift = 0.0f) {
        add("westmere streetscape ornamental shrub", x, z,
            at(x, z) + lift, width, height, depth, StartFinish::TealDoor,
            false, yaw_deg);
    };
    const auto add_trash_bin = [&](float x, float z, float yaw_deg) {
        const float y = at(x, z);
        // The body owns the collision. Lid and opening are visual layers and
        // do not grow the physical footprint past the visible bin.
        add("westmere streetscape trash bin body", x, z, y, 0.72f, 1.02f,
            0.68f, StartFinish::Steel, true, yaw_deg);
        add("westmere streetscape trash bin lid", x, z, y + 1.02f, 0.80f,
            0.12f, 0.76f, StartFinish::DarkRoof, false, yaw_deg);
        add("westmere streetscape trash bin slot", x, z + 0.351f,
            y + 0.70f, 0.42f, 0.17f, 0.03f, StartFinish::DarkRoof, false,
            yaw_deg);
    };
    const auto add_utility = [&](float x, float z, float yaw_deg) {
        const float y = at(x, z);
        add("westmere streetscape utility cabinet body", x, z, y, 1.25f,
            1.35f, 0.62f, StartFinish::TealDoor, true, yaw_deg);
        add("westmere streetscape utility cabinet door", x, z + 0.326f,
            y + 0.16f, 1.02f, 1.04f, 0.03f, StartFinish::Steel, false,
            yaw_deg);
        add("westmere streetscape utility cabinet sill", x, z + 0.346f,
            y + 0.10f, 1.08f, 0.08f, 0.07f, StartFinish::White, false,
            yaw_deg);
    };
    const auto add_palm = [&](float x, float z, float yaw_deg) {
        const float y = at(x, z);
        // A square trunk and four crossed frond slabs are intentionally low
        // poly. Only the trunk is solid; broad foliage must not snag traffic.
        add("westmere streetscape palm trunk", x, z, y, 0.52f, 4.85f,
            0.52f, StartFinish::WarmWall, true, yaw_deg);
        add("westmere streetscape palm crown hub", x, z, y + 4.62f, 0.78f,
            0.62f, 0.78f, StartFinish::TealDoor, false, yaw_deg);
        for (int blade = 0; blade < 4; ++blade) {
            add("westmere streetscape palm frond", x, z,
                y + 4.70f + static_cast<float>(blade % 2) * 0.13f, 0.30f,
                0.15f, 5.60f, StartFinish::TealDoor, false,
                yaw_deg + 22.5f + static_cast<float>(blade) * 45.0f);
        }
    };

    switch (record.kind) {
        case WestmereStreetscapeKind::Entry: {
            const float x = -13.2f;
            const float z = -4.5f;
            const float y = at(x, z);
            // The planter is the low physical boundary. Its sign uses a broad
            // blank panel, cap and two color bars instead of text or branding.
            add("westmere streetscape entry planter curb", x, z, y, 6.5f,
                0.28f, 3.2f, StartFinish::Concrete, true);
            add("westmere streetscape entry planter soil", x, z, y + 0.28f,
                6.0f, 0.07f, 2.7f, StartFinish::DarkRoof);
            add("westmere streetscape entry sign plinth", x, z, y + 0.35f,
                5.8f, 0.52f, 0.86f, StartFinish::Brick, true);
            add("westmere streetscape entry sign backing", x, z,
                y + 0.87f, 5.3f, 1.62f, 0.30f, StartFinish::White);
            // Face the public approach (local -Z), so WESTMERE reads before
            // the player passes the gate instead of only in the rear-view.
            add("westmere streetscape entry sign face", x, z - 0.166f,
                y + 1.08f, 4.78f, 1.08f, 0.032f,
                StartFinish::TealDoor, false, 180.0f);
            add("westmere streetscape entry sign cap", x, z, y + 2.49f,
                5.7f, 0.15f, 0.52f, StartFinish::White);
            for (float stripe_x : {-1.65f, 1.65f}) {
                add("westmere streetscape entry sign accent", x + stripe_x,
                    z + 0.186f, y + 1.31f, 0.24f, 0.62f, 0.026f,
                    StartFinish::Yellow);
            }
            for (const Vec2 shrub : std::array<Vec2, 5>{{
                     {-15.4f, -4.9f}, {-14.2f, -3.8f}, {-12.9f, -5.1f},
                     {-11.7f, -3.9f}, {-10.9f, -5.0f}}}) {
                add("westmere streetscape ornamental shrub", shrub.x,
                    shrub.z, y + 0.35f, 1.05f, 0.72f, 0.76f,
                    StartFinish::TealDoor, false,
                    (shrub.x - x) * 8.0f);
            }
            add_palm(-13.0f, 7.2f, -8.0f);
            add_palm(13.0f, 8.0f, 12.0f);
            break;
        }

        case WestmereStreetscapeKind::Community: {
            const float board_x = 7.0f;
            const float board_z = -38.0f;
            const float board_y = at(board_x, board_z);
            // A chunky roofed bulletin board and nearby bench form a small
            // resident notice spot. The six cards are plain color blocks.
            for (float post_x : {-2.05f, 2.05f}) {
                add("westmere streetscape bulletin post", board_x + post_x,
                    board_z, at(board_x + post_x, board_z), 0.18f, 2.55f,
                    0.18f, StartFinish::Steel, true);
            }
            add("westmere streetscape bulletin backing", board_x, board_z,
                board_y + 0.88f, 5.0f, 1.72f, 0.22f,
                StartFinish::WarmWall, true);
            add("westmere streetscape bulletin face", board_x,
                board_z + 0.126f, board_y + 1.05f, 4.56f, 1.34f, 0.032f,
                StartFinish::DarkRoof);
            add("westmere streetscape bulletin roof", board_x, board_z,
                board_y + 2.60f, 5.65f, 0.18f, 0.92f,
                StartFinish::DarkRoof);
            constexpr std::array<StartFinish, 3> kCardFinishes{{
                StartFinish::White, StartFinish::Yellow,
                StartFinish::TealDoor}};
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 3; ++column) {
                    add("westmere streetscape bulletin card",
                        board_x - 1.45f + static_cast<float>(column) * 1.45f,
                        board_z + 0.146f,
                        board_y + 1.20f + static_cast<float>(row) * 0.63f,
                        0.82f, 0.46f, 0.022f,
                        kCardFinishes[static_cast<std::size_t>(
                            (row + column) % 3)]);
                }
            }

            const float bench_x = 7.0f;
            const float bench_z = -33.5f;
            const float bench_y = at(bench_x, bench_z);
            for (float leg_x : {-1.25f, 1.25f}) {
                add("westmere streetscape bulletin bench leg",
                    bench_x + leg_x, bench_z, bench_y, 0.18f, 0.48f,
                    0.55f, StartFinish::Steel);
            }
            add("westmere streetscape bulletin bench seat", bench_x,
                bench_z, bench_y + 0.44f, 3.35f, 0.16f, 0.72f,
                StartFinish::WarmWall, true);
            add("westmere streetscape bulletin bench back", bench_x,
                bench_z + 0.31f, bench_y + 0.57f, 3.35f, 0.68f, 0.13f,
                StartFinish::WarmWall);
            add_trash_bin(11.0f, -33.8f, 0.0f);
            add_utility(54.0f, -24.0f, 0.0f);
            for (const Vec2 shrub : std::array<Vec2, 4>{{
                     {2.8f, -39.5f}, {11.2f, -39.5f},
                     {2.8f, -34.2f}, {12.3f, -37.4f}}}) {
                add_shrub(shrub.x, shrub.z, 1.35f, 0.78f, 0.88f,
                          (shrub.x - board_x) * 5.0f);
            }
            break;
        }

        case WestmereStreetscapeKind::Court: {
            const float sign_x = 0.0f;
            const float sign_z = 18.5f;
            const float sign_y = at(sign_x, sign_z);
            for (float post_x : {-1.5f, 1.5f}) {
                add("westmere streetscape court sign post", post_x, sign_z,
                    at(post_x, sign_z), 0.15f, 1.55f, 0.15f,
                    StartFinish::Steel, true);
            }
            add("westmere streetscape court sign backing", sign_x, sign_z,
                sign_y + 1.15f, 4.15f, 1.20f, 0.22f,
                StartFinish::White, true);
            add("westmere streetscape court sign face", sign_x,
                sign_z + 0.126f, sign_y + 1.33f, 3.75f, 0.82f, 0.032f,
                StartFinish::TealDoor);
            add("westmere streetscape court sign cap", sign_x, sign_z,
                sign_y + 2.35f, 4.45f, 0.14f, 0.42f,
                StartFinish::DarkRoof);
            constexpr std::array<StartFinish, 3> kCourtAccents{{
                StartFinish::Yellow, StartFinish::White,
                StartFinish::RedTrim}};
            for (int tile = 0; tile < 3; ++tile) {
                add("westmere streetscape court sign route tile",
                    -1.1f + static_cast<float>(tile) * 1.1f,
                    sign_z + 0.146f, sign_y + 1.53f, 0.52f, 0.40f,
                    0.022f,
                    kCourtAccents[(static_cast<std::size_t>(tile) +
                                   record.variant) %
                                  kCourtAccents.size()]);
            }

            constexpr std::array<Vec2, 3> kHydrantAnchors{{
                {18.0f, 0.0f}, {-18.0f, 0.0f}, {0.0f, -18.0f}}};
            const Vec2 hydrant = kHydrantAnchors[record.variant];
            const float hydrant_y = at(hydrant.x, hydrant.z);
            add("westmere streetscape fire hydrant flange", hydrant.x,
                hydrant.z, hydrant_y, 0.68f, 0.16f, 0.68f,
                StartFinish::White);
            add("westmere streetscape fire hydrant body", hydrant.x,
                hydrant.z, hydrant_y + 0.08f, 0.44f, 0.92f, 0.44f,
                StartFinish::Yellow, true);
            add("westmere streetscape fire hydrant cap", hydrant.x,
                hydrant.z, hydrant_y + 1.00f, 0.58f, 0.20f, 0.58f,
                StartFinish::Yellow);
            for (float nozzle_x : {-0.30f, 0.30f}) {
                add("westmere streetscape fire hydrant nozzle",
                    hydrant.x + nozzle_x, hydrant.z, hydrant_y + 0.58f,
                    0.20f, 0.28f, 0.30f, StartFinish::White);
            }
            for (float shrub_x : {-5.2f, 0.0f, 5.2f}) {
                add_shrub(shrub_x, 16.0f, 1.25f, 0.68f, 0.82f,
                          shrub_x * 4.0f);
            }
            break;
        }

        case WestmereStreetscapeKind::Estate: {
            const float mailbox_x = -8.6f;
            const float mailbox_z = 27.0f;
            const float mailbox_y = at(mailbox_x, mailbox_z);
            const StartFinish trim =
                kLuxuryEstates[record.variant].trim_finish;
            add("westmere streetscape mailbox post", mailbox_x, mailbox_z,
                mailbox_y, 0.16f, 1.12f, 0.16f, StartFinish::Steel, true);
            add("westmere streetscape mailbox crossarm", mailbox_x + 0.22f,
                mailbox_z, mailbox_y + 0.88f, 0.60f, 0.13f, 0.18f,
                StartFinish::Steel);
            add("westmere streetscape mailbox body", mailbox_x,
                mailbox_z, mailbox_y + 1.02f, 0.56f, 0.42f, 0.72f,
                trim, true);
            add("westmere streetscape mailbox roof", mailbox_x, mailbox_z,
                mailbox_y + 1.44f, 0.64f, 0.11f, 0.78f,
                StartFinish::DarkRoof);
            add("westmere streetscape mailbox door", mailbox_x,
                mailbox_z + 0.376f, mailbox_y + 1.08f, 0.46f, 0.30f,
                0.032f, StartFinish::White);
            add("westmere streetscape mailbox flag", mailbox_x + 0.34f,
                mailbox_z, mailbox_y + 1.28f, 0.08f, 0.42f, 0.08f,
                StartFinish::RedTrim);

            const float garden_shift =
                record.variant % 2 == 0 ? 0.0f : 0.65f;
            for (float shrub_x : {-18.2f, -14.2f, -10.2f}) {
                add_shrub(shrub_x, 23.4f + garden_shift, 1.55f, 0.76f,
                          0.95f, shrub_x * 2.0f + record.variant * 7.0f);
            }
            // Service-day clutter rotates between neighboring houses instead
            // of stamping the same bin/cabinet onto every frontage.
            if (record.variant % 3 == 0) {
                add_trash_bin(24.0f, 23.0f, 0.0f);
            } else if (record.variant % 3 == 1) {
                add_utility(26.0f, 23.0f, 0.0f);
            }
            break;
        }
    }

    return out;
}

constexpr bool westmere_streetscape_site_table_is_stable() {
    if (kWestmereStreetscapeSites.size() != 14u) return false;
    if (kWestmereStreetscapeSites[0].kind !=
            WestmereStreetscapeKind::Entry ||
        kWestmereStreetscapeSites[1].kind !=
            WestmereStreetscapeKind::Community) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (kWestmereStreetscapeSites[i + 2].kind !=
                WestmereStreetscapeKind::Court ||
            kWestmereStreetscapeSites[i + 2].variant != i) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 9; ++i) {
        if (kWestmereStreetscapeSites[i + 5].kind !=
                WestmereStreetscapeKind::Estate ||
            kWestmereStreetscapeSites[i + 5].variant != i) {
            return false;
        }
    }
    return true;
}

static_assert(westmere_streetscape_site_table_is_stable(),
              "Westmere streetscape integration order drifted");

}  // namespace apricot::city

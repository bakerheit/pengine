#pragma once

#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>

#include "city/kyjhi_phonebooth_asset.h"

namespace apricot {

// Every payphone in the world as one list: the ten district booths, then the
// Halloway Gas pedestal and wall handset. The props (world.cpp) and their "T"
// map markers (game_ui.cpp) read these same site tables, so a phone the player
// can see is a phone the player can call from, and there is no second list of
// payphones to fall out of step with the first.
inline constexpr std::size_t kPayphoneCount = city::kKyjhiPhoneboothSites.size() +
                                              city::kKyjhiPayphoneSites.size() +
                                              city::kKyjhiWallPhoneSites.size();

inline const city::StartSite& payphone_site(std::size_t i) {
    constexpr std::size_t booths = city::kKyjhiPhoneboothSites.size();
    constexpr std::size_t pedestals = city::kKyjhiPayphoneSites.size();
    if (i < booths) return city::kKyjhiPhoneboothSites[i];
    if (i < booths + pedestals) return city::kKyjhiPayphoneSites[i - booths];
    return city::kKyjhiWallPhoneSites[i - booths - pedestals];
}

// The phone's ground point: the site origin, where each cooked model's
// footprint was recentred (docs/assets/kyjhi-payphones.md).
inline glm::vec3 payphone_position(std::size_t i) {
    return city::kyjhi_prop_world({0.0f, 0.0f, 0.0f}, payphone_site(i));
}

// How near the player's feet must be to use a phone. The booth, the widest of
// the three models, is 0.87 x 0.89 m, so the 0.32 m character pressed against
// it stands 0.76 m from its centre face-on and 0.94 m at a corner; 1.5 m
// reaches it from every side. The height window keeps a player on a roof or a
// bridge above a phone from calling on it.
inline constexpr float kPayphoneReachM = 1.5f;
inline constexpr float kPayphoneReachHeightM = 1.2f;

// The phone within reach of a player on foot, nearest first, or -1.
inline int payphone_in_reach(const glm::vec3& feet, bool on_foot) {
    if (!on_foot) return -1;
    int best = -1;
    float best_d2 = kPayphoneReachM * kPayphoneReachM;
    for (std::size_t i = 0; i < kPayphoneCount; ++i) {
        const glm::vec3 p = payphone_position(i);
        const float dx = feet.x - p.x;
        const float dz = feet.z - p.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 <= best_d2 && std::fabs(feet.y - p.y) <= kPayphoneReachHeightM) {
            best = static_cast<int>(i);
            best_d2 = d2;
        }
    }
    return best;
}

// A marked phone keeps the marker until another is this much nearer, so the
// marker cannot flick between two phones as the player crosses the line
// halfway between them.
inline constexpr float kPayphoneRetargetMarginM = 25.0f;

// The payphone to point the player at: the nearest in a straight line, unless
// `current` (the one already marked, or -1) is within the margin of it. There
// is no routing in this game, only a bearing (docs/design/README.md), so
// straight-line distance is the honest measure.
inline int nearest_payphone(glm::vec2 from, int current = -1) {
    const auto distance = [&](std::size_t i) {
        const glm::vec3 p = payphone_position(i);
        return glm::length(glm::vec2{p.x, p.z} - from);
    };
    std::size_t best = 0;
    float best_d = distance(0);
    for (std::size_t i = 1; i < kPayphoneCount; ++i) {
        const float d = distance(i);
        if (d < best_d) {
            best = i;
            best_d = d;
        }
    }
    if (current >= 0 && static_cast<std::size_t>(current) < kPayphoneCount &&
        distance(static_cast<std::size_t>(current)) <= best_d + kPayphoneRetargetMarginM)
        return current;
    return static_cast<int>(best);
}

}  // namespace apricot

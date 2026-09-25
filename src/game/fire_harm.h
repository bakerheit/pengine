#pragma once
// Fire burns people. Everybody standing in the flames takes a bite: the
// player, pedestrians and police officers alike, whichever fire it is — a
// molotov's or a car bomb's, because both are the same FireField.
//
// The player's bite is paced by their own timer in the host (it starts when
// they step in). Everybody else shares ONE clock: on the step it comes round,
// whoever is standing in fire is bitten. A timer per person would need state
// on every pedestrian for the few seconds a fire lasts, and a shared beat is
// indistinguishable from the street — nobody watches two strangers burn and
// checks they are out of phase.
//
// The crowd does not learn what a fire is (traffic/ does not depend on game/),
// and the fire does not learn what a crowd is: this file is where they meet,
// through Crowd::standing_within() and Crowd::blast_ped(), the same pair the
// car bomb uses. A bitten survivor panics and runs, as somebody shot at does;
// a body that goes down drops where it stood, not thrown.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "game/fire.h"
#include "traffic/crowd.h"

namespace apricot {

// A whole bite every kFireBiteSeconds rather than a trickle every step,
// because health that slides continuously reads as a bar bug and a discrete
// hit reads as being on fire. Four bites kills from full, which is about three
// and a half seconds in the flames — long enough to run out of.
inline constexpr float kFireBiteSeconds = 0.85f;
inline constexpr float kFireBiteDamage = 26.0f;
// Below this much heat a point is at the fire's edge, not in it.
inline constexpr float kFireBiteMinHeat = 0.05f;

// The shared beat. Held still while nothing burns, so the first bite of the
// next fire lands on the step somebody is first found standing in it.
struct FireBiteClock {
    float until_s = 0.0f;
    bool due(float dt, bool burning) {
        if (!burning) {
            until_s = 0.0f;
            return false;
        }
        until_s -= std::max(dt, 0.0f);
        if (until_s > 0.0f) return false;
        until_s = kFireBiteSeconds;
        return true;
    }
};

// Bites everybody standing in `fire`, once. Returns the hits so the caller can
// charge the heat for them; a person already on the floor is not a hit.
inline std::vector<PedShotHit> burn_standing_people(Crowd& crowd, const FireField& fire,
                                                    int64_t step) {
    std::vector<PedShotHit> hits;
    // One query around everything that is alight, not one per cell: the
    // bounds of the burning cells, padded by a cell's reach and a body's
    // height, then the exact heat under each person's feet.
    constexpr float inf = std::numeric_limits<float>::infinity();
    glm::vec3 lo{inf}, hi{-inf};
    bool any = false;
    for (std::size_t i = 0; i < FireField::kCapacity; ++i) {
        const FireCellDraw cell = fire.draw(i);
        if (!cell.visible) continue;
        lo = glm::min(lo, cell.position);
        hi = glm::max(hi, cell.position);
        any = true;
    }
    if (!any) return hits;
    const glm::vec3 chest{0.0f, 1.2f, 0.0f};
    const glm::vec3 middle = (lo + hi) * 0.5f + chest;
    const float radius = glm::length(hi - lo) * 0.5f + FireField::kCellSizeM + 2.5f;
    for (const auto& person : crowd.standing_within(middle, radius)) {
        const glm::vec3 feet = person.body - chest;
        const float heat = fire.heat_at(feet);
        if (heat <= kFireBiteMinHeat) continue;
        // `from` is the patch they are standing on, so a body falls away from
        // the middle of the fire rather than toward it.
        const PedShotHit hit = crowd.blast_ped(person, glm::vec3{middle.x, feet.y, middle.z},
                                               kFireBiteDamage * heat, 0.0f, step);
        if (hit.hit) hits.push_back(hit);
    }
    return hits;
}

}  // namespace apricot

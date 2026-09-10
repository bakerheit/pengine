#pragma once

#include "app/character_animation.h"

namespace apricot {

inline CharacterAnimInput pedestrian_impact_input(bool downed, bool bullet,
                                                    bool from_front) {
    CharacterAnimInput input;
    // 'dead' selects the authored fall in the animator, not simulation death.
    // Clearing it on Rising enters the existing get-up path. A bullet does
    // not inherit the car's tumbling ragdoll or its large angular impulse.
    input.dead = downed && bullet;
    input.downed = downed && !bullet;
    input.impact_from_front = from_front;
    return input;
}

inline CharacterAnimSample pedestrian_impact_sample(CharacterAnimSample sample,
                                                      bool bullet) {
    // Crowd owns the small shot displacement. Keep the authored fall's Y and
    // rotation, but pin its horizontal root through fall AND recovery so the
    // clip cannot send a victim through a nearby wall or pop back on get-up.
    if (bullet) sample.root = ClipRoot::Strip;
    return sample;
}

} // namespace apricot

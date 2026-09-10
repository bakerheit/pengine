#pragma once

#include <algorithm>
#include "game/weapon.h"

namespace apricot {

// Metre offsets in the authored pistol's palm space (-Z forward). Effects
// use the shot clock, never the trigger, so blocked shots cannot flash.
struct WeaponVisualPose {
    float slide_back = 0.f;
    float magazine_drop = 0.f;
    float magazine_pitch = 0.f;
    float flash_scale = 0.f;
    float impact_scale = 0.f;
};

inline WeaponVisualPose weapon_visual_pose(const WeaponUseState& use) {
    WeaponVisualPose pose;
    if (use.equipped != WeaponId::Pistol) return pose;
    const float reload = use.reload_progress();
    const bool slide_locked = use.magazine == 0 && (!use.reloading || reload < .85f);
    pose.slide_back = slide_locked ? .025f
        : .025f * std::clamp(1.f - use.shot_age / .10f, 0.f, 1.f);
    if (use.reloading) {
        const float withdraw = std::clamp((reload - .12f) / .18f, 0.f, 1.f);
        const float insert = std::clamp((reload - .53f) / .23f, 0.f, 1.f);
        pose.magazine_drop = .14f * withdraw * (1.f - insert);
        pose.magazine_pitch = 12.f * withdraw * (1.f - insert);
    }
    if (use.shot_age >= 0.f && use.shot_age < .05f)
        pose.flash_scale = 1.f - .65f * (use.shot_age / .05f);
    if (use.shot_age >= 0.f && use.shot_age < .14f)
        pose.impact_scale = 1.f - use.shot_age / .14f;
    return pose;
}

}  // namespace apricot

#pragma once

#include <cstddef>

#include "audio/synth.h"

namespace apricot {

// How many takes tools/prepare_footsteps.py cooked for each footstep family,
// in FootstepSurface order. The supplied pack has an uneven number per family,
// so this cannot be derived from kFootstepSurfaceCount * kFootstepVariantCount
// — and it lives here rather than in the .cpp because the asset regression
// tests pin the total number of files that ship, and a second copy of these
// numbers in a test is a second copy that can disagree.
struct ShippedFootstepFamily { const char* name; std::size_t takes; };
inline constexpr ShippedFootstepFamily
    kShippedFootsteps[kFootstepSurfaceCount] = {
        {"concrete", 5}, {"stone", 4}, {"gravel", 5}, {"dirt", 5}, {"grass", 6},
    };

constexpr std::size_t shipped_footstep_take_count() {
    std::size_t total = 0;
    for (const ShippedFootstepFamily& family : kShippedFootsteps)
        total += family.takes;
    return total;
}

// Checked-in recorded runtime paths. Centralised so the game and the asset
// regression tests cannot silently disagree about which files ship live.
SfxOverridePaths player_car_audio_overrides();

}  // namespace apricot

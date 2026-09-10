#pragma once

#include <algorithm>
#include <cmath>

#include "game/snowpack.h"

namespace apricot {

// A cleared swath rejoins the surrounding snow when its depth catches the
// CURRENT main pack. The ordinary 10 cm full-cover threshold would hide the
// clearing far too early in a deep storm. This keeps shallow-weather opacity
// while showing refill progress over the whole remaining depth difference.
inline float snow_clearance_visual_cover(double local_depth_m,
                                         double main_depth_m) {
    // Invalid metadata must not create a bare road through valid snow.
    if (!std::isfinite(main_depth_m)) return 1.f;
    if (main_depth_m <= 0.0) return 0.f;
    const float surrounding = visual_snow_cover_from_depth(main_depth_m);
    if (!std::isfinite(local_depth_m) || local_depth_m < 0.0)
        return surrounding;
    if (local_depth_m >= main_depth_m) return surrounding;
    const double progress = std::clamp(local_depth_m / main_depth_m, 0.0, 1.0);
    return surrounding * static_cast<float>(progress * progress *
                                             (3.0 - 2.0 * progress));
}

} // namespace apricot

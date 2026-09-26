#pragma once
#include "city/map.h"

namespace apricot::city {
// Compact, level town off Ferrone Road. Shared by terrain and host placement.
inline constexpr Vec2 kBellwetherOrigin{480.0f,-700.0f};
inline constexpr float kBellwetherGroundM=11.0f;
inline constexpr uint32_t kBellwetherRoadId=245;
inline constexpr Vec2 kBellwetherPlateCentre{490.0f,-711.0f};
inline constexpr Vec2 kBellwetherPlateHalf{183.0f,103.0f};
inline constexpr Vec2 kBellwetherPlayerStart{578.0f,-720.0f};
inline constexpr Vec2 kBellwetherCarStart{566.0f,-724.0f};
inline constexpr bool in_bellwether(float x,float z) {
    return x>325 && x<677 && z>-812 && z<-608;
}
} // namespace apricot::city

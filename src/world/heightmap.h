#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace pengine {

// Procedural world heightmap. Deterministic, infinite, smooth.
// All sampling is in world-space metres.
class Heightmap {
public:
    static constexpr float AMPLITUDE     = 18.f;   // peak height above sea level
    static constexpr float FEATURE_SIZE  = 380.f;  // ~one hill per N metres

    static void  set_seed(std::uint32_t seed);

    // Height at world (wx, wz), in metres.
    static float sample(float wx, float wz);

    // Outward-pointing surface normal at (wx, wz). Computed by central differences.
    static glm::vec3 normal(float wx, float wz);
};

} // namespace pengine

#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/glm.hpp>

namespace pengine {

// World heightmap, sampled bilinearly from an authored 8-bit grayscale PNG.
//
// The PNG is a square image covering the full world, oriented so that pixel
// (0, 0) maps to world (0, 0) and the +x / +z axes go right / down. Pixel
// values map linearly to elevation:
//
//   height_m = HEIGHT_MIN + (pixel / 255) * (HEIGHT_MAX - HEIGHT_MIN)
//
// On first run (PNG missing) the analytic Cincinnati starter field is baked
// to disk and used as the world. Edit the PNG in any image editor; the
// next run picks up the change.
class Heightmap {
public:
    static constexpr float HEIGHT_MIN_M = -10.f;  // river floor
    static constexpr float HEIGHT_MAX_M = 120.f;  // bluff plateau

    // Initialise. Returns false only if generation/save fails outright.
    // After this call, sample()/normal() return values for the configured world.
    static bool init(float world_size_m, const std::filesystem::path& png_path);

    // Carved heightmap sample. Within a road band (lateral distance to the
    // nearest centerline on the ROAD_PITCH grid is <= STREET_WIDTH/2) the
    // returned value is the raw heightmap sampled on the centerline, so the
    // road band reads as a flat platform across its width and the terrain
    // mesh, road slabs, and physics all agree. Off-road, identical to
    // raw_sample.
    static float     sample(float wx, float wz);
    static glm::vec3 normal(float wx, float wz);

    // Bilinear sample of the underlying PNG with no road carving applied.
    // Use for tooling/debug or when you specifically want the "natural"
    // landscape value (e.g. river bed elevation regardless of any road bridge
    // that might cross it).
    static float     raw_sample(float wx, float wz);

    static int   resolution()  ;
    static float world_size_m();
};

}  // namespace pengine

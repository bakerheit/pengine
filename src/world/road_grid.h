#pragma once

#include <cmath>

namespace pengine {

// Global road grid constants. Roads run along world lines x = i * ROAD_PITCH
// (NS roads) and z = j * ROAD_PITCH (EW roads). Intersections at the
// integer grid points (i, j). Each streaming cell at (cx, cz) owns the 4×4
// roads indexed by i ∈ [cx*4, cx*4+4), j ∈ [cz*4, cz*4+4).
//
// Lives in its own header so Heightmap (which carves the terrain along these
// road bands) and CityLayout (which emits road slabs at these lines) can
// share constants without a circular include.
constexpr float ROAD_PITCH      = 64.f;
constexpr float STREET_WIDTH    = 8.f;
constexpr int   ROADS_PER_CELL  = 4;     // = cell_size / ROAD_PITCH

// Half-width of a road's lateral footprint. The carved heightmap is flat
// across [centerline ± ROAD_HALF_WIDTH].
constexpr float ROAD_HALF_WIDTH = STREET_WIDTH * 0.5f;

// Width of the sidewalk strip that runs between the road edge and the
// building plot's interior. Mirrors the SIDEWALK constant used in
// city_layout.cpp during plot generation. Anything outside (road +
// sidewalk) is plot interior or terrain.
constexpr float SIDEWALK_WIDTH = 3.f;

// True when (wx, wz) lies on a paved surface — road carriageway OR the
// sidewalk strip immediately bordering it. Used by VFX (sparks) and any
// other code that wants to gate effects to "the street."
inline bool is_paved_surface(float wx, float wz) {
    constexpr float PAVED_HALF = ROAD_HALF_WIDTH + SIDEWALK_WIDTH;
    float ns_x = std::round(wx / ROAD_PITCH) * ROAD_PITCH;
    float ew_z = std::round(wz / ROAD_PITCH) * ROAD_PITCH;
    return std::abs(wx - ns_x) <= PAVED_HALF
        || std::abs(wz - ew_z) <= PAVED_HALF;
}

// Classify a world-space (wx, wz) point against the road grid. Returns the
// nearest centerline coordinates and band-membership flags. The caller is
// responsible for combining these into a carved heightmap value (see
// Heightmap::sample); when both flags are set (intersection), the carving
// is a smooth bilinear blend of the two centerline profiles rather than a
// hard plateau, so vehicles don't bump as they cross band edges.
struct RoadBandClass {
    float ns_x;       // nearest NS road centerline x
    float ew_z;       // nearest EW road centerline z
    bool  in_ns;      // |wx - ns_x| <= ROAD_HALF_WIDTH
    bool  in_ew;      // |wz - ew_z| <= ROAD_HALF_WIDTH
};

inline RoadBandClass classify_road_band(float wx, float wz) {
    RoadBandClass c;
    c.ns_x  = std::round(wx / ROAD_PITCH) * ROAD_PITCH;
    c.ew_z  = std::round(wz / ROAD_PITCH) * ROAD_PITCH;
    c.in_ns = std::abs(wx - c.ns_x) <= ROAD_HALF_WIDTH;
    c.in_ew = std::abs(wz - c.ew_z) <= ROAD_HALF_WIDTH;
    return c;
}

}  // namespace pengine

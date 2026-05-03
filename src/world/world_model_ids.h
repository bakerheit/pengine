#pragma once

#include <cstdint>

namespace pengine {

// Hard-coded model ids referenced by the procedural city exporter. These MUST
// stay in sync with assets/world/streets.ide. Runtime code should look up
// models through ModelRegistry and not reference these constants directly —
// they exist only so the procedural generator can name what it places.
namespace world_ids {

constexpr uint32_t RoadSlab     = 20;
constexpr uint32_t SidewalkSlab = 30;

// Building variants. Each is a bldg_cube with a different baked tint.
// Picked at random by the exporter to give visible plot-to-plot variety.
constexpr uint32_t BuildingFirst = 11;
constexpr uint32_t BuildingLast  = 15;
constexpr uint32_t BuildingCount = BuildingLast - BuildingFirst + 1;

}  // namespace world_ids
}  // namespace pengine

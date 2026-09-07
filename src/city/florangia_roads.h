#pragma once

#include <array>
#include <cstdint>

#include "city/states.h"

namespace apricot {
namespace city {

// Stable seam between Florangia's state road and the airport parcel. The
// airport owns everything north of this node; the road network owns the
// approach and lands exactly here, so neither side guesses a nearest ribbon.
inline constexpr uint32_t kFlorangiaHighwayRoadId = 220;
inline constexpr uint32_t kFlorangiaAirportSpurRoadId = 221;
inline constexpr Vec2 kFlorangiaAirportAccessNode{4800.0f, 4700.0f};
inline constexpr Vec2 kFlorangiaAirportSpurEnd{4800.0f, 4580.0f};
inline constexpr float kFlorangiaHighwayBedM = 8.0f;
inline constexpr float kFlorangiaAirportRoadBedM = 6.5f;
inline constexpr float kFlorangiaAirportApproachBedM = 7.0f;

// Panhandle to peninsula. This centreline feeds road/lane graphs, collision,
// ribbons and terrain grading; it is not a map-only stroke.
inline constexpr std::array<Vec2, 9> kFlorangiaHighwayPath{{
    {2800.0f, 3500.0f},
    {3500.0f, 3600.0f},
    {3950.0f, 3900.0f},
    {4050.0f, 4650.0f},
    kFlorangiaAirportAccessNode,
    {5050.0f, 5200.0f},
    {5300.0f, 5750.0f},
    {5600.0f, 6250.0f},
    {5900.0f, 6650.0f},
}};

static_assert(kFlorangiaHighwayPath[4].x == kFlorangiaAirportAccessNode.x &&
                  kFlorangiaHighwayPath[4].z == kFlorangiaAirportAccessNode.z,
              "Florangia Highway must pass through the airport access node");

}  // namespace city
}  // namespace apricot

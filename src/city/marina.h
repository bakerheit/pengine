#pragma once
#include <array>
#include <vector>
#include "city/start_area.h"
#include "road/ribbon.h"

namespace apricot::city {
// Speedboat home berth. Separate from road traffic and the car picker.
inline constexpr const char* kMarlinBody="models/vehicles/marlin_sprint/body.emesh";
inline constexpr const char* kMarlinTexture="textures/vehicles/marlin_sprint/body.png";
inline constexpr Vec2 kMarlinMooring{-2046.0f,-603.4f};
inline constexpr float kMarlinYaw=-1.57079632679f;
inline constexpr StartSite kMarlinDockSite{
    "Ostend Bait & Tackle",{-2041.0f,-600.0f},1,0,{30,-12},130,50,0,900};
inline constexpr float kMarinaDeckTop=.66f;
inline constexpr Vec2 kMarinaLampPositions[]={{-22,0},{-13,9},{-3,2.4f},{5,3}};
// Keep the ramp/dock edge fixed at local x=32 and pull only the road-facing
// edge eight metres back. Boatworks Road now comes from world -Z (north) and
// holds a centreline ten metres clear of both the north and road-facing edges.
inline constexpr Vec2 kMarinaParkingCentre{47.f,-20.f};
inline constexpr float kMarinaParkingWidth=30.f;
inline constexpr float kMarinaParkingDepth=30.f;
inline constexpr float kMarinaParkingTop=5.02f;
inline constexpr float kMarinaParkingLastStripeX=59.f;
inline constexpr float kMarinaNorthStopZ=-34.55f;
inline constexpr float kMarinaSouthStopZ=-5.45f;
inline constexpr Vec2 kMarinaParkingEntrance{62.f,-20.f};
inline constexpr float kMarinaRoadIntoLotM=2.f;
inline constexpr float kMarinaEntranceApproachLocalX=72.f;
inline constexpr float kMarinaParkingNorthEdgeLocalZ=-35.f;
inline constexpr float kMarinaEntranceApproachLocalZ=-45.f;
inline constexpr float kMarinaRampWestX=-17.f;
inline constexpr float kMarinaRampEastX=32.f;
inline constexpr float kMarinaRampZ=-20.f;
inline constexpr float kMarinaRampWidth=7.5f;
inline constexpr float kMarinaRampWaterTop=-.5f;
inline constexpr float kMarinaRampMidX=(kMarinaRampWestX+kMarinaRampEastX)*.5f;
inline constexpr float kMarinaRampMidTop=
    (kMarinaRampWaterTop+kMarinaParkingTop)*.5f+.42f;
constexpr float marina_ramp_height(float x) {
    return x<=kMarinaRampMidX
        ? kMarinaRampWaterTop+(x-kMarinaRampWestX)*
            (kMarinaRampMidTop-kMarinaRampWaterTop)/(kMarinaRampMidX-kMarinaRampWestX)
        : kMarinaRampMidTop+(x-kMarinaRampMidX)*
            (kMarinaParkingTop-kMarinaRampMidTop)/(kMarinaRampEastX-kMarinaRampMidX);
}
// One source drives visible pieces and collision. All detail sits outside
// the 1.4 m clear walking route; the Marlin berth stays at its original pose.
std::vector<StartPart> bake_marina();
struct MarinaRampQuad {
    std::array<glm::vec3,4> corners;
};
struct MarinaAccessBake {
    std::vector<StartPart> parts;
    std::vector<MarinaRampQuad> ramp_quads;
    RoadCollision surfaces;
};
MarinaAccessBake bake_marina_access();
void append_marina_access_collision(RoadCollision& target,
                                     const MarinaAccessBake& access);
struct MarinaMapFootprint {
    const char* name;
    Vec2 centre;
    float width_m,depth_m;
    bool lot,dock;
};
// Project actual decks, shop, parking and ramp, never the old site lot.
std::vector<MarinaMapFootprint> marina_map_footprints();
} // namespace apricot::city

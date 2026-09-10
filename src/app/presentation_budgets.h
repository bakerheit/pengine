#pragma once

#include <cmath>

#include "terrain/heightmap.h"

namespace apricot {

// How far the two ambient actor classes are presented, and the one policy that
// overrides both for the --overhead diagnostic camera.
//
// These three numbers live together because keeping them apart is what broke:
// the camera height sat in app.cpp, the vehicle budget in traffic_visual.cpp
// and the NPC budget in character_visual.cpp, with nothing relating any of
// them. The --overhead QA view sits far enough up that BOTH budgets fall short
// of reaching the ground, so it drew empty carriageways and empty pavements
// over a junction the sim was actively running cars and pedestrians through.
// Roads, buildings and signal heads carry longer budgets, so the frame looked
// like an ordinary quiet junction rather than a broken render, and it was read
// as evidence. Anything added here should keep that relationship checkable in
// one file.

// Traffic cars in normal play. A per-node cull (SceneNode::max_draw_distance),
// so it can only ever hide a car the global streaming budget would have drawn.
inline constexpr float kTrafficVehicleDrawDistanceM = 420.0f;

// Ambient pedestrians, staff and police characters in normal play. Not a scene
// node: CharacterVisual::draw skips rigs past this range itself.
inline constexpr float kAmbientNpcDrawDistanceM = 175.0f;

// The fixed eye height of the --overhead QA camera, in WORLD space rather than
// above ground. Documented reproduce-lines across docs/design/ frame districts
// and interchanges from this height, so it is load-bearing, not a free knob.
inline constexpr float kOverheadQaCameraHeightM = 542.0f;

// The distance the --overhead view must present an actor class to, so that
// culling cannot hide one the sim is still keeping alive.
//
// The camera looks straight down from a fixed world height, so the farthest
// actor it can contain sits at that class's RETIRE radius horizontally - past
// that the crowd has despawned it - and at the LOWEST GROUND the terrain
// generator can produce vertically, which is below sea level. Pass
// kMinHeightMetres for that argument: using the camera height alone silently
// assumes no actor ever stands below y=0, and the island's analytic floor is
// 45.3 m under it.
//
// LIMIT, and it is not fixable here: "every live actor" holds around the
// CAMERA ANCHOR. The crowd activates and retires against the player focus, so
// once the player moves away from the anchor the actors near the camera stop
// existing rather than stop being drawn, and no draw distance brings them
// back. Keep the player at the anchor when reading one of these frames.
inline float overhead_qa_actor_draw_distance_m(float camera_height_m,
                                               float lowest_ground_m,
                                               float retire_radius_m) {
    const float up = camera_height_m - lowest_ground_m;
    return std::sqrt(up * up + retire_radius_m * retire_radius_m);
}

// The shipped composition. Callers pass only the thing that actually differs
// between actor classes; the camera height and the terrain floor are bound
// here so a call site cannot quietly substitute sea level for the floor and
// come up short over deep ground.
inline float overhead_qa_actor_draw_distance_m(float retire_radius_m) {
    return overhead_qa_actor_draw_distance_m(kOverheadQaCameraHeightM,
                                             kMinHeightMetres,
                                             retire_radius_m);
}

}  // namespace apricot

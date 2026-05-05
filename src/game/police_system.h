#pragma once

#include <glm/glm.hpp>

namespace pengine {

class AudioEngine;
class DebugDraw;
class Particles;
class PedestrianSystem;
class Player;
class TrafficSystem;
class WorldCollision;

// Cross-system police orchestration. Stateless — these helpers wire together
// existing subsystems (traffic, peds, world, audio, particles, debug-draw,
// player) without owning any state of their own. Lives next to the wanted
// system but kept separate so WantedSystem stays a pure heat/level counter.
namespace police {

// Spawn police officers out of any Police-driver car within range of the
// target. wanted_level <= 3 → no-op. Mutates traffic (car driver flips to
// Parked + handbrake) and pedestrians (new ped via spawn_police_officer).
void spawn_from_cars(int wanted_level, const glm::vec3& target_pos,
                     TrafficSystem& traffic, PedestrianSystem& pedestrians);

// Drain pedestrians.police_vehicle_events(): when a police ped re-enters a
// car the system parked, flip its driver back to Police and release the
// handbrake.
void promote_reentered_cars(const PedestrianSystem& pedestrians,
                             TrafficSystem& traffic);

struct ShotsResult {
    bool player_died = false;
};

// Drain pedestrians.police_shots(). For each shot: occlusion-test against
// the static world, play gunshot SFX, notify peds (panic), debug-draw the
// tracer, emit sparks at the impact, and capsule-test against the player
// torso target. On hit: damage + death check. Stops at the first shot that
// drops the player to 0 HP and reports player_died = true; the caller is
// responsible for the post-death respawn / mode transition / wanted reset.
ShotsResult resolve_shots(PedestrianSystem& pedestrians,
                          const WorldCollision& world_col,
                          const glm::vec3& player_torso_target,
                          Player& player, AudioEngine& audio,
                          Particles& particles, DebugDraw& debug_draw);

} // namespace police
} // namespace pengine

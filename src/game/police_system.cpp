#include "game/police_system.h"

#include <algorithm>
#include <cmath>

#include "audio/audio_engine.h"
#include "game/pedestrian.h"
#include "game/player.h"
#include "game/traffic.h"
#include "game/vehicle.h"
#include "physics/world_collision.h"
#include "render/debug_draw.h"
#include "render/particles.h"
#include "world/heightmap.h"

namespace pengine::police {

void spawn_from_cars(int wanted_level, const glm::vec3& target_pos,
                     TrafficSystem& traffic, PedestrianSystem& pedestrians) {
    if (wanted_level <= 3) return;

    for (const auto& cp : traffic.cars()) {
        if (!cp || cp->driver != TrafficSystem::Driver::Police) continue;
        if (pedestrians.has_police_for_car(cp.get())) continue;
        glm::vec3 car_pos = cp->vehicle.position();
        glm::vec3 d = target_pos - car_pos;
        d.y = 0.f;
        if (glm::dot(d, d) > 45.f * 45.f) continue;

        glm::vec3 exit = car_pos - cp->vehicle.right() * 2.0f;
        exit.y = Heightmap::sample(exit.x, exit.z);
        glm::vec3 to_target = target_pos - exit;
        to_target.y = 0.f;
        float yaw = glm::length(to_target) > 1e-4f
            ? glm::degrees(std::atan2(to_target.z, to_target.x))
            : 0.f;
        if (pedestrians.spawn_police_officer(exit, yaw, cp.get())) {
            cp->driver = TrafficSystem::Driver::Parked;
            cp->vehicle.set_inputs(0.f, 0.f, 0.f, true);
        }
    }
}

void promote_reentered_cars(const PedestrianSystem& pedestrians,
                             TrafficSystem& traffic) {
    for (const auto& ev : pedestrians.police_vehicle_events()) {
        for (const auto& cp : traffic.cars()) {
            if (!cp || cp.get() != ev.car_id) continue;
            if (cp->driver == TrafficSystem::Driver::Parked) {
                cp->driver = TrafficSystem::Driver::Police;
                cp->vehicle.set_inputs(0.f, 0.f, 0.f, false);
            }
            break;
        }
    }
}

ShotsResult resolve_shots(PedestrianSystem& pedestrians,
                          const WorldCollision& world_col,
                          const glm::vec3& player_torso_target,
                          Player& player, AudioEngine& audio,
                          Particles& particles, DebugDraw& debug_draw) {
    ShotsResult result;
    for (const auto& shot : pedestrians.police_shots()) {
        glm::vec3 to_player = shot.target - shot.origin;
        float dist = glm::length(to_player);
        if (dist < 1e-4f) continue;
        glm::vec3 dir = to_player / dist;
        RayHit block = world_col.raycast(shot.origin, dir, dist);
        if (block.hit && block.t < dist - 0.6f) continue;

        audio.play_gunshot();
        pedestrians.notify_gunshot(shot.origin);
        debug_draw.line(shot.origin, shot.target);
        particles.emit_sparks(shot.target, -dir * 2.f, 4);

        // Distance-spread aim (pedestrian.cpp:advance_police) means the
        // shot ray may now miss the player entirely. Treat the player as
        // a vertical capsule of radius PLAYER_HIT_RADIUS_M centred on
        // the same torso point the police aimed for, and only deduct HP
        // if the ray's closest approach is inside that radius.
        constexpr float PLAYER_HIT_RADIUS_M = 0.45f;
        float t_close = glm::clamp(
            glm::dot(player_torso_target - shot.origin, dir), 0.f, dist);
        glm::vec3 closest = shot.origin + dir * t_close;
        if (glm::length(player_torso_target - closest) > PLAYER_HIT_RADIUS_M)
            continue;

        player.apply_damage(8.f);
        if (player.is_dead()) {
            result.player_died = true;
            return result;
        }
    }
    return result;
}

} // namespace pengine::police

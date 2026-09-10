#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {

// Pure on-foot state. Input stays device-neutral and the world query is the
// same collider used by the car, so a recorded input sequence walks the same
// route on every machine.
struct PlayerCharacterState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float facing_yaw = 0.0f;
    float view_yaw = 0.0f;
    float view_pitch = -0.16f;
    float distance_walked_m = 0.0f;
    bool sprinting = false;
    bool grounded = true;
};

struct CharacterTuning {
    float walk_speed_mps = 2.35f;
    float sprint_speed_mps = 6.25f;
    float turn_speed_rad_s = 12.0f;
    float radius_m = 0.32f;
    float height_m = 1.76f;
    float max_step_m = 0.34f;
    float max_drop_m = 1.25f;
    float jump_speed_mps = 5.4f;
    float gravity_mps2 = 16.0f;
};

PlayerCharacterState spawn_character(const TerrainCollider& collider,
                                     float x, float z,
                                     float facing_yaw = 0.0f);

PlayerCharacterState step_character(const PlayerCharacterState& current,
                                    const CharacterTuning& tuning,
                                    const InputFrame& input,
                                    const TerrainCollider& collider,
                                    float dt);

// Used by the exit-car placement path before it commits to one side. Low
// slabs within step height are walkable; walls and props through the body are
// blockers.
bool character_position_clear(const TerrainCollider& collider,
                              glm::vec3 feet,
                              const CharacterTuning& tuning);

glm::vec3 character_forward(float yaw);

// Visual roots use -Z as their authored forward. Keep the yaw-to-quaternion
// mapping beside character_forward so movement and presentation cannot choose
// opposite signs again.
glm::quat character_root_rotation(float yaw);

}  // namespace apricot

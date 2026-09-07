#include <cstdio>

#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.0f / 120.0f;

void spawn_uses_the_visible_ground() {
    TerrainCollider collider{0xC0FFEEu};
    const PlayerCharacterState player = spawn_character(collider, 12.0f, -9.0f);
    REQUIRE_NEAR(player.position.y, collider.height(12.0f, -9.0f), 1e-5);
    apricot_test::pass("character spawn rests on the meshed terrain");
}

void movement_is_camera_relative_and_sprint_is_faster() {
    TerrainCollider collider{0xC0FFEEu};
    const PlayerCharacterState start = spawn_character(collider, 0.0f, 0.0f);
    CharacterTuning tuning;
    InputFrame walk_input;
    walk_input.throttle = 1.0f;
    const PlayerCharacterState walk =
        step_character(start, tuning, walk_input, collider, 1.0f / 60.0f);
    REQUIRE(walk.position.z < start.position.z);

    InputFrame sprint_input = walk_input;
    sprint_input.held = kBtnShiftUp;
    const PlayerCharacterState sprint =
        step_character(start, tuning, sprint_input, collider, 1.0f / 60.0f);
    REQUIRE(glm::distance(sprint.position, start.position) >
            glm::distance(walk.position, start.position));

    InputFrame turned = walk_input;
    turned.look_dx = 1.57079632679f;
    const PlayerCharacterState east =
        step_character(start, tuning, turned, collider, 1.0f / 60.0f);
    REQUIRE(east.position.x > start.position.x);
    apricot_test::pass("walk, sprint, and camera-relative movement share InputFrame");
}

void visual_facing_matches_sideways_movement() {
    TerrainCollider collider{0xC0FFEEu};
    CharacterTuning tuning;
    PlayerCharacterState state = spawn_character(collider, 0.0f, 0.0f);
    InputFrame move_right;
    move_right.steer = 1.0f;
    for (int i = 0; i < 180; ++i) {
        state = step_character(state, tuning, move_right, collider, kDt);
    }

    const glm::vec3 movement = glm::normalize(glm::vec3{
        state.velocity.x, 0.0f, state.velocity.z});
    const glm::vec3 visible_forward = character_root_rotation(
        state.facing_yaw) * glm::vec3{0.0f, 0.0f, -1.0f};
    REQUIRE(glm::dot(movement, visible_forward) > 0.999f);
    apricot_test::pass("visual root faces the same way the character moves");
}

void tall_props_block_but_low_steps_do_not() {
    TerrainCollider collider{0xC0FFEEu};
    CharacterTuning tuning;
    const float ground = collider.height(0.0f, 0.0f);
    collider.add_static_box(
        AABB{{0.36f, ground, -0.7f}, {1.0f, ground + 2.5f, 0.7f}});
    collider.add_static_box(
        AABB{{-1.0f, ground, -0.7f}, {-0.36f, ground + 0.12f, 0.7f}});

    REQUIRE(!character_position_clear(collider, {0.12f, ground, 0.0f}, tuning));
    REQUIRE(character_position_clear(collider, {-0.12f, ground, 0.0f}, tuning));

    PlayerCharacterState state = spawn_character(collider, 0.0f, 0.0f);
    InputFrame into_wall;
    into_wall.steer = 1.0f;
    for (int i = 0; i < 240; ++i)
        state = step_character(state, tuning, into_wall, collider, kDt);
    REQUIRE(state.position.x < 0.05f);
    apricot_test::pass("body circle stops at walls and accepts kerb-height slabs");
}

void raised_rotated_plot_pavement_supports_feet() {
    TerrainCollider collider{0xC0FFEEu};
    const glm::vec2 centre{18.0f, -12.0f};
    const float terrain = collider.height(centre.x, centre.y);
    const float pavement_top = terrain + 0.10f;
    collider.add_static_ground_rect(centre, pavement_top, {5.0f, 2.0f},
                                    glm::radians(-35.0f), Surface::Rock);

    PlayerCharacterState sunk = spawn_character(
        TerrainCollider{0xC0FFEEu}, centre.x, centre.y);
    REQUIRE_NEAR(sunk.position.y, terrain, 1e-5);
    const PlayerCharacterState planted = step_character(
        sunk, CharacterTuning{}, InputFrame{}, collider, kDt);
    REQUIRE_NEAR(planted.position.y, pavement_top, 1e-5);

    // This point is inside the rectangle's world AABB but outside the rotated
    // visible slab. It must not gain an invisible floor at pavement height.
    const glm::vec2 outside = centre + glm::vec2{3.9f, 0.0f};
    const TerrainCollider::GroundHit miss = collider.probe_down(
        {outside.x, collider.height(outside.x, outside.y) + 0.4f, outside.y},
        1.0f);
    REQUIRE_NEAR(miss.point.y, collider.height(outside.x, outside.y), 1e-5);
    apricot_test::pass(
        "raised rotated plot pavement plants feet on its visible top only");
}

}  // namespace

int main() {
    std::printf("character_controller_tests\n");
    spawn_uses_the_visible_ground();
    movement_is_camera_relative_and_sprint_is_faster();
    visual_facing_matches_sideways_movement();
    tall_props_block_but_low_steps_do_not();
    raised_rotated_plot_pavement_supports_feet();
    return apricot_test::done("character_controller_tests");
}

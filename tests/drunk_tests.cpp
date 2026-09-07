#include <cmath>

#include "game/drunk.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    DrunkState state;
    REQUIRE(!state.active());
    start_drunk(state);
    REQUIRE(state.active());
    REQUIRE(state.drinks == 1);
    REQUIRE_NEAR(state.intensity(), 0.25f, 1e-5f);
    REQUIRE_NEAR(kDrunkEffectScale, 1.25f, 1e-5f);
    REQUIRE_NEAR(kDrunkDurationSeconds, 180.0f, 1e-5f);

    InputFrame straight;
    straight.throttle = 1.0f;
    step_drunk(state, 1.5f);
    const InputFrame one_drink = apply_drunk_input(straight, state);
    REQUIRE(std::fabs(one_drink.steer) > 0.01f);

    DrunkState maximum = state;
    for (int i = 1; i < kMaxDrinks; ++i) start_drunk(maximum);
    REQUIRE(maximum.drinks == kMaxDrinks);
    REQUIRE_NEAR(maximum.intensity(), 1.0f, 1e-5f);
    const InputFrame five_drinks = apply_drunk_input(straight, maximum);
    REQUIRE(std::fabs(five_drinks.steer) > std::fabs(one_drink.steer));
    REQUIRE(five_drinks.throttle >= 0.0f && five_drinks.throttle <= 1.0f);

    ChaseCameraPose mild_camera;
    ChaseCameraPose strong_camera;
    apply_drunk_camera(mild_camera, state);
    apply_drunk_camera(strong_camera, maximum);
    REQUIRE(glm::length(strong_camera.target) > glm::length(mild_camera.target));
    REQUIRE(std::fabs(strong_camera.fov_y - glm::radians(62.0f)) >
            std::fabs(mild_camera.fov_y - glm::radians(62.0f)));

    state = maximum;
    step_drunk(state, 30.0f);
    start_drunk(state);
    REQUIRE(state.drinks == kMaxDrinks);
    REQUIRE_NEAR(state.remaining_seconds, kDrunkDurationSeconds, 1e-5f);
    step_drunk(state, kDrunkDurationSeconds - 0.5f);
    REQUIRE(state.active());
    step_drunk(state, 0.5f);
    REQUIRE(!state.active());
    REQUIRE(state.drinks == 0);
    REQUIRE_NEAR(state.remaining_seconds, 0.0f, 1e-5f);
    apricot_test::pass("Bent Elbow drinks stack from mild to strong and refresh a 1.5-hour timeout");
    return 0;
}

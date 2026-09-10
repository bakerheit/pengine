#include <limits>

#include "app/weapon_aim.h"
#include "test_assert.h"

using namespace apricot;

namespace {
void layout() {
    const glm::vec3 feet{16, 8, -21};
    const auto free = weapon_aim_camera(feet, 0, 0, 0);
    REQUIRE_NEAR(free.pivot.y, feet.y + 1.38f, .00001f);
    REQUIRE_NEAR(free.target.x, feet.x + kWeaponAimIdleShoulderM, .00001f);
    REQUIRE_NEAR(glm::distance(free.eye, free.target), 4.8f, .00001f);
    // Idle must clear the player's own silhouette, or the crosshair is drawn
    // on his back and the feature reads as broken however well it traces.
    REQUIRE(kWeaponAimIdleShoulderM > .45f);
    const auto aim = weapon_aim_camera(feet, 0, 0, 1);
    REQUIRE_NEAR(aim.pivot.y, feet.y + 1.48f, .00001f);
    REQUIRE_NEAR(aim.target.x, feet.x + .72f, .00001f);
    REQUIRE_NEAR(glm::distance(aim.eye, aim.target), 2.25f, .00001f);
    REQUIRE_NEAR(aim.fov_y, glm::radians(48.0f), .000001f);
    for (int step = 0; step <= 100; ++step) {
        const float blend = static_cast<float>(step) / 100.0f;
        const auto pose = weapon_aim_camera(feet, .73f, -.21f, blend);
        REQUIRE_NEAR(glm::distance(pose.eye, pose.target), glm::mix(4.8f, 2.25f, blend), .00001f);
        REQUIRE_NEAR(glm::distance(pose.target, pose.pivot),
                     glm::mix(kWeaponAimIdleShoulderM, kWeaponAimShoulderM, blend), .00001f);
        REQUIRE_NEAR(glm::dot(glm::normalize(pose.target - pose.eye), pose.direction), 1, .000001f);
    }
    apricot_test::pass("camera smoothly reaches the authored shoulder layout");
}

void convergence() {
    for (glm::vec3 feet : {glm::vec3{0}, glm::vec3{815, 32, -591}}) {
        for (float blend : {0.0f, .4f, 1.0f}) {
            const float shoulder = weapon_aim_shoulder(blend);
            const float height = glm::mix(1.38f, 1.48f, blend);
            for (float yaw : {-3.1415f, -2.1f, -.01f, 0.0f, .9f, 3.1415f}) {
                for (float radius : {.8f, 1.5f, 10.0f, 100.0f}) {
                    for (float elevation : {-5.0f, -.1f, 0.0f, .1f, 7.0f}) {
                        const glm::vec3 target = feet + glm::vec3{
                            std::sin(yaw) * radius, height + elevation, -std::cos(yaw) * radius};
                        const auto angles = weapon_aim_toward(feet, target, blend);
                        REQUIRE(angles.reachable);
                        const auto camera = weapon_aim_camera(feet, angles.yaw, angles.pitch, blend);
                        const auto to_target = target - camera.eye;
                        const float distance = glm::dot(to_target, camera.direction);
                        REQUIRE(distance > 0);
                        REQUIRE(glm::distance(camera.eye + camera.direction * distance, target) < .0004f);
                        REQUIRE(glm::distance(angles.direction, camera.direction) < .000001f);
                        REQUIRE_NEAR(glm::distance(camera.target, camera.pivot), shoulder, .00005f);
                    }
                }
            }
        }
    }
    apricot_test::pass("sight rays converge near/far and high/low across yaw wrap and aim blends");
}

void finite_and_unreachable() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float angle : {nan, inf, -inf, 100000.0f}) {
        const auto camera = weapon_aim_camera({nan, 0, inf}, angle, angle, angle);
        REQUIRE(weapon_aim_detail::finite(camera.eye));
        REQUIRE(weapon_aim_detail::finite(camera.target));
        REQUIRE(weapon_aim_detail::finite(camera.direction));
        REQUIRE_NEAR(glm::length(camera.direction), 1, .000001f);
    }
    const auto inside = weapon_aim_toward({0, 0, 0}, {.1f, 1.48f, -.1f});
    REQUIRE(!inside.reachable);
    REQUIRE(weapon_aim_detail::finite(inside.direction));
    const auto vertical = weapon_aim_toward({0, 0, 0}, {0, 100, 0}, 0);
    REQUIRE(!vertical.reachable);
    REQUIRE_NEAR(vertical.pitch, kWeaponAimPitchLimit, .000001f);
    REQUIRE(!weapon_aim_toward({nan, 0, 0}, {0, 0, 0}).reachable);
    REQUIRE(!weapon_aim_toward({0, 0, 0}, {inf, 0, 0}).reachable);
    // Dead ahead is still solvable at idle, but the offset axis means a point
    // ON the player's centre line no longer is: it sits inside the orbit.
    const auto ahead = weapon_aim_toward({0, 0, 0}, {0, 1.38f, -6}, 0);
    REQUIRE(ahead.reachable);
    REQUIRE(weapon_aim_detail::finite(ahead.direction));
    REQUIRE(!weapon_aim_toward({0, 0, 0}, {0, 1.38f, 0}, 0).reachable);
    apricot_test::pass("invalid and unreachable targets stay finite without claiming convergence");
}
} // namespace

int main() {
    layout();
    convergence();
    finite_and_unreachable();
    return apricot_test::done("weapon_aim_tests");
}

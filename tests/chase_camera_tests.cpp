#include <cmath>
#include <cstdio>

#include "gfx/chase_camera.h"
#include "test_assert.h"
#include "game/character.h"
#include "city/map.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.0f / 60.0f;

void mouse_orbit_crosses_wrapped_yaw_without_a_camera_jump() {
    TerrainCollider ground(city::kMapSeed);
    constexpr float pi=3.14159265358979323846f;
    for(float sign:{-1.f,1.f}) {
        auto previous=spawn_character(ground,0.f,0.f,0.f);
        previous.view_yaw=sign*(pi-.01f);
        InputFrame mouse;mouse.look_dx=sign*.02f;
        const auto current=step_character(previous,CharacterTuning{},mouse,ground,1.f/120.f);
        REQUIRE(previous.view_yaw*current.view_yaw<0.f);
        const auto eye=[](float yaw) { return glm::vec3{-4.8f*std::sin(yaw),0.f,4.8f*std::cos(yaw)}; };
        const auto expected=eye(sign*pi);
        const auto old_mid=eye(glm::mix(previous.view_yaw,current.view_yaw,.5f));
        REQUIRE(glm::distance(old_mid,expected)>9.5f);
        glm::vec3 last=eye(previous.view_yaw);
        for(int frame=0;frame<=100;++frame) {
            const float alpha=static_cast<float>(frame)/100.f;
            const auto now=eye(interpolate_camera_yaw(previous.view_yaw,current.view_yaw,alpha));
            REQUIRE(glm::distance(now,expected)<.05f);
            REQUIRE(glm::distance(now,last)<.002f);
            last=now;
        }
        REQUIRE(glm::distance(last,eye(current.view_yaw))<1e-5f);
    }
    REQUIRE_NEAR(interpolate_camera_yaw(.2f,.6f,.25f),.3f,1e-6f);
    apricot_test::pass("mouse orbit stays on the short arc across both yaw wrap directions");
}

void speed_pulls_back_and_widens_view() {
    ChaseCameraRig slow_rig;
    ChaseCameraRig fast_rig;
    const glm::vec3 pos{0.0f, 0.0f, 0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    const ChaseCameraPose slow =
        slow_rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f,
                        false, kDt);
    const ChaseCameraPose fast =
        fast_rig.update(pos, forward, glm::vec3{0.0f, 0.0f, -42.0f}, 0.0f,
                        0.0f, 0.0f, false, kDt);

    REQUIRE(glm::distance(fast.target, fast.desired_eye) >
            glm::distance(slow.target, slow.desired_eye));
    REQUIRE(fast.fov_y > slow.fov_y);
    apricot_test::pass("speed pulls the chase camera back and widens its view");
}

void focus_stays_on_the_vehicle_center() {
    ChaseCameraRig rig;
    const glm::vec3 pos{18.0f, 4.0f, -32.0f};
    ChaseCameraPose pose;
    for (int i = 0; i < 180; ++i) {
        pose = rig.update(pos, glm::vec3{0.0f, 0.0f, -1.0f},
                          glm::vec3{17.0f, 0.0f, -38.0f}, 1.2f,
                          0.0f, 0.0f, false, kDt);
    }

    REQUIRE_NEAR(pose.target.x, pos.x, 1e-6);
    REQUIRE_NEAR(pose.target.z, pos.z, 1e-6);
    REQUIRE_NEAR(pose.target.y, pos.y + 0.75f, 1e-6);
    apricot_test::pass("speed and turning keep focus on the chassis center");
}

void look_back_is_instant_and_reversible() {
    ChaseCameraRig rig;
    const glm::vec3 pos{0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    const ChaseCameraPose behind =
        rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f, false, kDt);
    const ChaseCameraPose ahead =
        rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f, true, kDt);
    const ChaseCameraPose released =
        rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f, false, kDt);

    REQUIRE(behind.desired_eye.z > behind.target.z);
    REQUIRE(ahead.desired_eye.z < ahead.target.z);
    REQUIRE(released.desired_eye.z > released.target.z);
    apricot_test::pass("look-back flips immediately and releases cleanly");
}

void orbit_recentres_after_input_stops() {
    ChaseCameraRig rig;
    const glm::vec3 pos{0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    ChaseCameraPose pose =
        rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 1.0f, 0.0f, false, kDt);
    const float side_after_input = std::fabs(pose.desired_eye.x - pose.target.x);
    for (int i = 0; i < 360; ++i) {
        pose = rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f,
                          false, kDt);
    }
    const float side_after_idle = std::fabs(pose.desired_eye.x - pose.target.x);
    REQUIRE(side_after_input > 2.0f);
    REQUIRE(side_after_idle < 0.05f);
    apricot_test::pass("manual orbit recentres after the player lets go");
}

void orbit_stays_put_when_auto_recenter_is_disabled() {
    ChaseCameraRig rig;
    rig.set_auto_recenter(false);
    REQUIRE(!rig.auto_recenter());
    const glm::vec3 pos{0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    ChaseCameraPose pose =
        rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 1.0f, 0.0f, false, kDt);
    const float side_after_input = std::fabs(pose.desired_eye.x - pose.target.x);
    for (int i = 0; i < 360; ++i) {
        pose = rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f,
                          false, kDt);
    }
    const float side_after_idle = std::fabs(pose.desired_eye.x - pose.target.x);
    REQUIRE(side_after_input > 2.0f);
    REQUIRE(side_after_idle > side_after_input * 0.95f);
    apricot_test::pass("manual orbit stays put when auto recenter is disabled");
}

void camera_cycle_has_three_distinct_distances() {
    ChaseCameraRig rig;
    const glm::vec3 pos{0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float distances[3]{};
    for (int i = 0; i < 3; ++i) {
        rig.reset();
        const ChaseCameraPose pose =
            rig.update(pos, forward, glm::vec3{0.0f}, 0.0f, 0.0f, 0.0f,
                       false, kDt);
        distances[i] = glm::distance(pose.target, pose.desired_eye);
        rig.cycle();
    }
    REQUIRE(distances[0] < distances[1]);
    REQUIRE(distances[1] > distances[2]);
    REQUIRE(distances[2] < distances[0]);
    apricot_test::pass("camera cycle exposes far, near and chase distances");
}

}  // namespace

int main() {
    std::printf("chase_camera_tests\n");
    mouse_orbit_crosses_wrapped_yaw_without_a_camera_jump();
    speed_pulls_back_and_widens_view();
    focus_stays_on_the_vehicle_center();
    look_back_is_instant_and_reversible();
    orbit_recentres_after_input_stops();
    orbit_stays_put_when_auto_recenter_is_disabled();
    camera_cycle_has_three_distinct_distances();
    return apricot_test::done("chase_camera_tests");
}

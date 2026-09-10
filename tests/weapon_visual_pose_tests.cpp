#include "app/weapon_visual_pose.h"
#include "app/weapon_audio.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    constexpr float dt = 1.f / 120.f;
    const WeaponUseInput idle{true, false, false, false};
    const WeaponUseInput fire{true, true, true, false};
    const WeaponUseInput reload{true, false, false, true};
    WeaponUseState use;
    REQUIRE(weapon_visual_pose(use).flash_scale == 0.f);
    REQUIRE(!use.step(WeaponId::Pistol, fire, .30f));
    REQUIRE(weapon_visual_pose(use).flash_scale == 0.f);
    use.step(WeaponId::Pistol, idle, dt);
    REQUIRE(use.step(WeaponId::Pistol, fire, dt));
    auto pose = weapon_visual_pose(use);
    REQUIRE(pose.flash_scale > 0.f);
    REQUIRE(pose.slide_back > .02f);
    REQUIRE(pose.impact_scale > 0.f);
    use.step(WeaponId::Pistol, idle, .06f);
    pose = weapon_visual_pose(use);
    REQUIRE(pose.flash_scale == 0.f);
    REQUIRE(pose.slide_back > 0.f);
    REQUIRE(pose.impact_scale > 0.f);
    use.step(WeaponId::Pistol, idle, .17f);
    pose = weapon_visual_pose(use);
    REQUIRE(pose.slide_back == 0.f);
    REQUIRE(pose.impact_scale == 0.f);
    apricot_test::pass("shot clock drives bounded flash and slide return");

    use.step(WeaponId::Pistol, reload, dt);
    use.step(WeaponId::Pistol, idle, WeaponUseState::kReloadSeconds * .4f);
    pose = weapon_visual_pose(use);
    REQUIRE(pose.flash_scale == 0.f);
    REQUIRE(pose.magazine_drop > .13f);
    REQUIRE(!use.step(WeaponId::Pistol, fire, dt));
    REQUIRE(weapon_visual_pose(use).flash_scale == 0.f);
    use.step(WeaponId::Pistol, idle, WeaponUseState::kReloadSeconds * .4f);
    REQUIRE(weapon_visual_pose(use).magazine_drop == 0.f);
    use.step(WeaponId::Pistol, idle, WeaponUseState::kReloadSeconds);
    REQUIRE(use.magazine == WeaponUseState::kMagazineCapacity);
    apricot_test::pass("reload withdraws and reseats magazine without a false shot");

    for (int i = 0; i < WeaponUseState::kMagazineCapacity; ++i) {
        use.step(WeaponId::Pistol, idle, .23f);
        REQUIRE(use.step(WeaponId::Pistol, fire, dt));
    }
    use.step(WeaponId::Pistol, idle, .23f);
    REQUIRE(!use.step(WeaponId::Pistol, fire, dt));
    pose = weapon_visual_pose(use);
    REQUIRE(pose.flash_scale == 0.f);
    REQUIRE_NEAR(pose.slide_back, .025f, .00001f);
    use.step(WeaponId::Unarmed, idle, dt);
    pose = weapon_visual_pose(use);
    REQUIRE(pose.slide_back == 0.f);
    REQUIRE(pose.flash_scale == 0.f);
    apricot_test::pass("empty magazine locks slide and dry fire cannot flash");

    const auto shot = synth_pistol_shot();
    const auto shot_again = synth_pistol_shot();
    const auto click = synth_pistol_reload();
    REQUIRE(shot.samples == shot_again.samples);
    REQUIRE(!shot.empty());
    REQUIRE(!click.empty());
    REQUIRE(shot.samples.front() == 0.f && shot.samples.back() == 0.f);
    REQUIRE(click.samples.front() == 0.f && click.samples.back() == 0.f);
    for (const auto* clip : {&shot, &click}) {
        float peak = 0.f;
        for (const float value : clip->samples) {
            REQUIRE(std::isfinite(value));
            REQUIRE(std::abs(value) <= 1.f);
            peak = std::max(peak, std::abs(value));
        }
        REQUIRE(peak > .2f);
    }
    REQUIRE(synth_pistol_shot(0).empty());
    apricot_test::pass("feedback clips are deterministic non-clipping PCM with silent endpoints");
    return apricot_test::done("weapon_visual_pose_tests");
}

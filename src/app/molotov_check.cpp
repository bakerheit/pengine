#include "app/app.h"

#include <SDL.h>

#include "core/log.h"

namespace apricot {

// --molotov-check. Equips the molotov off the wheel, throws one, watches the
// fire it lights spread, walks into it, and watches it die back.
//
// THIS EXISTS BECAUSE NO HEADLESS SUITE CAN SEE A FLAME. tests/fire_tests.cpp
// proves the field spreads, stays bounded and goes out; tests/molotov_tests.cpp
// proves the throw arcs and the stock spends. Neither can tell you the bottle
// is visible in the hand, that the sprite sheet is the right way up, that the
// billboards face the camera, or that the fire on the ground looks like fire —
// and every one of those has exactly one symptom, which is that it looks
// wrong. So the check drives the real controls through the real app and leaves
// screenshots a human looks at.
//
// What it ASSERTS is the set of things a machine can honestly judge, and each
// of them has already been wrong once: the bottle came out of the wheel; it
// left the hand and spent a bottle; something caught; what caught is on the
// ground the player is standing on rather than on the roof behind the wall the
// bottle broke against; all three recorded takes reached the mixer, because a
// clip that failed to load is silent and silence is the one bug a screenshot
// cannot show; standing in the flames costs health; and the fire grew and then
// shrank rather than sitting there as a decal.
//
// Frames are 1/60 s exactly (see the fixed step in App::run), so the numbers
// below are sixtieths of a second and mean the same thing on every machine.
namespace {
constexpr int kOpenWheel = 90;
constexpr int kPickMolotov = 95;
constexpr int kCaptureWheel = 116;
constexpr int kCloseWheel = 120;
constexpr int kCheckEquipped = 132;
constexpr int kFaceStreet = 190;
constexpr int kCaptureHeld = 200;
constexpr int kAimDown = 210;
constexpr int kCaptureAim = 262;
constexpr int kThrowDown = 270;
constexpr int kThrowUp = 272;
constexpr int kCheckThrown = 278;
constexpr int kCaptureFlight = 280;
constexpr int kCheckLit = 400;        // ~2 s: long enough for any arc to land
constexpr int kCaptureLit = 404;
constexpr int kCaptureSpread = 560;
constexpr int kStandInIt = 620;
constexpr int kCaptureBurning = 700;
constexpr int kCheckBurned = 760;
constexpr int kStepOut = 762;
constexpr int kCaptureEstablished = 860;
constexpr int kCheckDone = 1250;
}  // namespace

void App::tick_molotov_check() {
    if (molotov_check_failed_ || molotov_check_done_) return;
    const int frame = frames_rendered_;
    const auto key = [&](SDL_Keycode code, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto mouse = [&](uint8_t button, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button = button;
        SDL_PushEvent(&event);
    };
    const auto fail = [&](const char* reason) {
        molotov_check_failed_ = true;
        AP_ERROR("molotov check: %s", reason);
    };

    // FACE THE OPEN STREET. The spawn looks at a shop front four metres away,
    // and a bottle that breaks on a wall at point blank is a worse test of a
    // SPREADING fire than one thrown across open tarmac — the flames have
    // nowhere to walk. A half turn puts the parking bays in front of the
    // player; the camera follows, so the screenshots show the fire too.
    if (frame == kFaceStreet) {
        // World XZ, not XY: this is the spot the player walks back out to
        // after standing in the flames.
        fire_check_start_ = {player_character_.position.x,
                             player_character_.position.z};
        player_character_.view_yaw = 3.14159265f;
        player_character_.facing_yaw = player_character_.view_yaw;
        prev_player_character_ = player_character_;
        update_camera(0.f);
    }

    if (frame == kOpenWheel) key(SDLK_TAB, true);
    if (frame == kPickMolotov) key(SDLK_3, true);
    if (frame == kPickMolotov + 1) key(SDLK_3, false);
    if (frame == kCloseWheel) key(SDLK_TAB, false);

    if (frame == kCheckEquipped) {
        if (weapon_wheel_.equipped != WeaponId::Molotov) {
            fail("the wheel did not equip the molotov");
            return;
        }
        AP_INFO("molotov check: equipped from the wheel");
    }

    // Right button holds aim AND captures the cursor, which the left button
    // needs before it counts as a throw rather than as a click to focus.
    if (frame == kAimDown) mouse(SDL_BUTTON_RIGHT, true);
    if (frame == kThrowDown) mouse(SDL_BUTTON_LEFT, true);
    if (frame == kThrowUp) mouse(SDL_BUTTON_LEFT, false);

    if (frame == kCheckThrown) {
        if (molotov_throws_ != 1u) { fail("the throw did not leave the hand"); return; }
        if (molotov_use_.stock != MolotovUseState::kInitialStock - 1) {
            fail("the throw did not spend a bottle");
            return;
        }
        if (molotov_shots_.live_count() != 1u) {
            fail("no bottle in the air after the throw");
            return;
        }
        AP_INFO("molotov check: thrown, %d left, 1 in the air", molotov_use_.stock);
    }

    molotov_check_peak_cells_ = std::max(molotov_check_peak_cells_, fire_.live_count());

    if (frame == kCheckLit) {
        if (!fire_.burning()) { fail("the bottle landed without lighting a fire"); return; }
        if (molotov_fires_lit_ != 1u) { fail("the impact did not report an ignition"); return; }
        // ON THE GROUND THE PLAYER IS STANDING ON, not on a roof above it.
        // A bottle that breaks against a wall used to ignite on the far side
        // of that wall, and a fire alone on a shop roof neither spreads nor
        // looks like anything — see the spill step in molotov_gameplay.cpp.
        const float lift = fire_.centre().y - player_character_.position.y;
        if (!(lift > -2.0f && lift < 2.0f)) {
            fail("the fire lit well above or below the street");
            return;
        }
        // THE AUDIO IS PART OF THE FEATURE AND IT IS CHECKABLE HERE. Not
        // whether it sounds right — nothing automated can say that — but
        // whether the three takes reached the mixer at all, which is the
        // failure mode of a recorded-only clip whose file went missing or
        // whose loop found no free voice. Both are silent, and silence is the
        // one bug a screenshot cannot show.
        if (molotov_glass_clip_.empty() || molotov_whoosh_clip_.empty() ||
            fire_loop_clip_.empty()) {
            fail("a molotov audio take did not load");
            return;
        }
        if (!fire_voice_.valid()) { fail("the fire opened no loop voice"); return; }
        AP_INFO("molotov check: fire lit, %zu cells burning, loop voice open",
                fire_.live_count());
    }

    if (frame == kCaptureSpread) {
        if (fire_.live_count() < 2u) { fail("the fire never spread past one cell"); return; }
        AP_INFO("molotov check: spread to %zu cells", fire_.live_count());
    }

    // WALK INTO IT. Standing in a fire is the one part of this that the
    // player will find by accident within a minute of throwing their first
    // bottle, and it is a silent failure: a heat field that never reaches the
    // health pool looks exactly like a fire that is working.
    if (frame == kStandInIt) {
        const glm::vec3 centre = fire_.centre();
        player_character_ = spawn_character(collider_, centre.x, centre.z);
        prev_player_character_ = player_character_;
        fire_check_health_ = player_vitals_.health;
        update_camera(0.f);
    }
    if (frame == kCheckBurned) {
        if (!(player_vitals_.health < fire_check_health_)) {
            fail("standing in the fire cost no health");
            return;
        }
        AP_INFO("molotov check: burned for %.0f health standing in it",
                static_cast<double>(fire_check_health_ - player_vitals_.health));
    }
    // Back out before the flames finish the job. The death path has its own
    // check; this one is about the fire, and a dead player stops walking.
    if (frame == kStepOut) {
        player_character_ = spawn_character(collider_,
            fire_check_start_.x, fire_check_start_.y);
        prev_player_character_ = player_character_;
        update_camera(0.f);
    }

    if (frame == kCheckDone) {
        // Past the peak: cells have started going out, which is the property
        // that says the fire is a fire and not a decal somebody pasted down.
        if (molotov_check_peak_cells_ < 3u) { fail("the fire barely spread"); return; }
        if (fire_.live_count() >= molotov_check_peak_cells_) {
            fail("the fire never started dying back");
            return;
        }
        if (molotov_shots_.live_count() != 0u) { fail("a bottle is still in the air"); return; }
        if (molotov_check_captures_ != 0x3fu) { fail("not every screenshot was written"); return; }
        molotov_check_done_ = true;
        AP_INFO("molotov check: PASS; peak %zu cells, now %zu",
                molotov_check_peak_cells_, fire_.live_count());
    }
}

void App::capture_molotov_check() {
    if (molotov_check_failed_ || screenshot_path_.empty()) return;
    // frames_rendered_ has not been bumped for the frame being drawn, so the
    // capture numbers are one behind the input numbers above. Same offset the
    // weapon check uses, and for the same reason.
    const int frame = frames_rendered_ + 1;
    const auto shot = [&](const char* name, unsigned bit) {
        if (save_screenshot(screenshot_path_ + name)) molotov_check_captures_ |= bit;
        else molotov_check_failed_ = true;
    };
    if (frame == kCaptureWheel) {
        if (weapon_wheel_.open && weapon_wheel_.hovered == WeaponId::Molotov)
            shot(".wheel.png", 1u);
        else molotov_check_failed_ = true;
    }
    if (frame == kCaptureHeld) {
        if (molotov_use_.equip_blend >= .99f && molotov_use_.armed())
            shot(".held.png", 2u);
        else molotov_check_failed_ = true;
    }
    if (frame == kCaptureAim) shot(".aim.png", 4u);
    if (frame == kCaptureFlight) shot(".flight.png", 8u);
    if (frame == kCaptureLit) shot(".lit.png", 16u);
    if (frame == kCaptureSpread) shot(".spread.png", 32u);
    if (frame == kCaptureBurning) save_screenshot(screenshot_path_ + ".standing.png");
    if (frame == kCaptureEstablished) save_screenshot(screenshot_path_ + ".late.png");
}

}  // namespace apricot

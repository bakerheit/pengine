#include "app/app.h"
#include "core/log.h"
#include "game/delivery_mission.h"

#include <SDL.h>

namespace apricot {

// --wallet-check: every way the wallet moves, in the real game, with the HUD
// captured after each one.
//
// wallet_rules_tests proves the amounts and the arithmetic. It cannot tell you
// that the counter is readable against a sunlit street, that the flash lands
// where the eye already is, or that the fine line fits under ARRESTED — and it
// cannot prove the payout is wired to the delivery at all. So:
//
//   0. a fresh session holds kNewGameCash                    .start.png
//   1. the delivery cutscene, started and skipped for real,
//      pays kDeliveryPayout through finish_opening()        .payout.png
//   2. an arrest at two stars charges the two-star fine      .arrested.png
//   3. a lethal blow through damage_player() charges the
//      hospital bill, shown under WASTED                    .wasted.png
//   4. after the respawn, a five-star arrest the wallet
//      cannot cover takes what is there and stops at zero   .broke.png
//
// The arrests go through arrest_player(), the same door check_police_arrest()
// uses, rather than staging an officer: --police-officer-check already walks a
// real one up to the player, and this check is about the money.
void App::tick_wallet_check() {
    if (wallet_check_done_ || wallet_check_failed_ || frames_rendered_ < 300) return;
    // The frame the run first ticks, so a stage that never acts has a clock.
    if (wallet_check_stage_frame_ == 0) wallet_check_stage_frame_ = frames_rendered_;
    const auto fail = [&](const char* reason) {
        wallet_check_failed_ = true;
        AP_ERROR("wallet check: %s (stage %d, cash $%lld)", reason, wallet_check_stage_,
                 static_cast<long long>(economy_.cash));
    };
    const auto key = [](SDL_Keycode code, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto shot = [&](const char* suffix) {
        if (!save_screenshot(screenshot_path_ + suffix)) fail("screenshot failed");
        else AP_INFO("wallet check: captured %s%s", screenshot_path_.c_str(), suffix);
    };
    const auto advance = [&]() {
        ++wallet_check_stage_;
        wallet_check_stage_frame_ = frames_rendered_;
        wallet_check_acted_ = false;
        wallet_check_capture_pending_ = true;
    };
    // Each stage's action happens once, even if a loop pass repeats a frame
    // count: charging a fine twice is the bug this check exists to catch. It
    // waits for the HUD to go quiet first (the last banner gone, the last
    // flash faded, the player alive) so each capture shows one event and the
    // flash shows that event's figure alone. `held` restarts at the action.
    const bool quiet = mission_success_feedback_s_ <= 0.0f && arrested_feedback_s_ <= 0.0f &&
                       !wallet_.flash.showing() && player_vitals_.alive() &&
                       !opening_cutscene_.active();
    const auto act_once = [&]() {
        if (wallet_check_acted_ || !quiet) return false;
        wallet_check_acted_ = true;
        wallet_check_stage_frame_ = frames_rendered_;
        wallet_check_cash_ = economy_.cash;
        return true;
    };
    const int held = frames_rendered_ - wallet_check_stage_frame_;
    if (!wallet_check_acted_ && held > 3000) {
        fail("the HUD never went quiet for the next stage"); return;
    }

    switch (wallet_check_stage_) {
    case 0:
        if (act_once() && economy_.cash != kNewGameCash) {
            fail("a fresh session is not holding kNewGameCash"); return;
        }
        if (wallet_check_acted_ && held >= 20) { shot(".start.png"); advance(); }
        return;
    case 1:
        if (act_once()) {
            mission_stage_ = MissionStage::DeliveryActive;
            on_foot_ = true;
            const auto devon = city::devon_position();
            player_character_ = spawn_character(collider_, devon.x, devon.z - 1.05f, 3.14159265f);
            prev_player_character_ = player_character_;
            if (!begin_delivery_cutscene()) { fail("the delivery cutscene did not start"); return; }
        }
        if (!wallet_check_acted_) return;
        if (held == 60) key(SDLK_ESCAPE, true);
        if (held == 61) key(SDLK_ESCAPE, false);
        // Captured once the success card has had a second to slide in; the
        // card runs on the render clock, so this waits on it, not on frames.
        if (held >= 150 && !opening_cutscene_.active() && mission_success_feedback_s_ > 0.0f &&
            mission_success_feedback_s_ < 5.0f) {
            if (opening_cutscene_.active() || mission_stage_ != MissionStage::DeliveryComplete) {
                fail("skipping the delivery did not complete it"); return;
            }
            if (economy_.cash != wallet_check_cash_ + kDeliveryPayout ||
                wallet_.delivery_paid != kDeliveryPayout) {
                fail("the delivery did not pay kDeliveryPayout"); return;
            }
            if (!wallet_.flash.showing() || wallet_.flash.delta != kDeliveryPayout) {
                fail("the counter is not flashing the payout"); return;
            }
            shot(".payout.png");
            advance();
        }
        return;
    case 2:
        if (act_once()) {
            wanted_.set_level(2);
            arrest_player();
            if (economy_.cash != wallet_check_cash_ - arrest_fine(2) || wanted_.level() != 0 ||
                arrested_feedback_s_ <= 0.0f || wallet_.arrest_fine.taken != arrest_fine(2)) {
                fail("a two-star arrest did not charge the two-star fine"); return;
            }
        }
        if (wallet_check_acted_ && arrested_feedback_s_ < GameUi::kArrestedDisplaySeconds - 1.0f) {
            shot(".arrested.png"); advance();
        }
        return;
    case 3:
        if (act_once()) {
            damage_player(1.0e6f, "the wallet check");
            if (player_vitals_.alive()) { fail("the player survived a lethal blow"); return; }
            if (economy_.cash != wallet_check_cash_ - kHospitalBill ||
                wallet_.hospital_bill.taken != kHospitalBill) {
                fail("dying did not charge the hospital bill"); return;
            }
        }
        if (!wallet_check_acted_) return;
        if (!player_vitals_.alive() && player_vitals_.dead_seconds >= 1.2f &&
            wallet_check_capture_pending_) {
            shot(".wasted.png");
            wallet_check_capture_pending_ = false;
        }
        if (!wallet_check_capture_pending_ && player_vitals_.alive()) advance();
        else if (held > 2000) fail("the player never respawned");
        return;
    case 4:
        if (act_once()) {
            const int64_t had = economy_.cash;
            if (had <= 0 || had >= arrest_fine(5)) { fail("the broke case needs a wallet short of the fine"); return; }
            wanted_.set_level(5);
            arrest_player();
            if (economy_.cash != 0 || wallet_.arrest_fine.taken != had ||
                wallet_.arrest_fine.billed != arrest_fine(5)) {
                fail("a fine the wallet cannot cover did not stop at zero"); return;
            }
        }
        if (wallet_check_acted_ && arrested_feedback_s_ < GameUi::kArrestedDisplaySeconds - 1.0f) {
            shot(".broke.png");
            wallet_check_done_ = !wallet_check_failed_;
            AP_INFO("wallet check: PASS payout, fine, hospital bill and a fine that stops at zero");
        }
        return;
    default:
        return;
    }
}

}  // namespace apricot

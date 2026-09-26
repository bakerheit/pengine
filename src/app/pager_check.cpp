#include "app/app.h"

#include <array>
#include <cmath>
#include <cstring>

#include "city/marina.h"
#include "core/log.h"
#include "game/lou_page.h"
#include "game/payphones.h"
#include "gfx/glyph_atlas.h"

#include <SDL.h>

namespace apricot {

// --pager-check: Lou's page and the call back, in the real game, with the HUD
// captured along the way.
//
// pager_tests proves the delay, the crawl, the nearest phone and the reach. It
// cannot show that the pager reads over a sunlit street, that the page is
// wired to the finished delivery at all, or that the booth the marker picks
// can be walked to from where the delivery leaves the player. So, from a
// delivery just completed at Devon's counter (set up in App::init):
//
//   1. the page arrives kLouPageDelaySeconds later and beeps once, and the
//      marker goes to the Ostend Docks booth
//   2. mid-rattle, the pager fully on screen                         .page.png
//   3. the whole of "Call me at store" on the LCD                 .message.png
//   4. the pager gone, the objective in its corner, and E at Devon's
//      counter, far from any phone, calls nobody                 .objective.png
//   5. the real character walks out of the bait shop, round the bench at
//      its door and up the bank to the booth, the marker holding on it all
//      the way
//   6. there, the call is offered                                .payphone.png
//   7. E makes it: LouCalled, the marker gone, the notice up       .called.png
//
// Each capture is taken in render(), before the swap, of the frame the stage
// asked for it on.

namespace {

// Out of the shed's front door (local x 2.0 in its z 5.5 wall) and east along
// the aisle the waiting bench leaves in front of it, to the platform's east
// end, where the bank meets the deck; the last leg is straight up the bank to
// the booth. Marina-local metres: kMarlinDockSite is unrotated, so these are
// offsets from its origin.
constexpr std::array<glm::vec2, 3> kWalkOut{{
    {2.0f, 6.6f}, {2.0f, 4.6f}, {6.0f, 4.6f},
}};
constexpr float kLegReachedM = 0.35f;

glm::vec3 marina_point(glm::vec2 local) {
    const auto& site = city::kMarlinDockSite;
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y, 0.0f,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

int ostend_payphone() {
    for (std::size_t i = 0; i < kPayphoneCount; ++i)
        if (std::strcmp(payphone_site(i).name, "Payphone: Ostend Docks") == 0)
            return static_cast<int>(i);
    return -1;
}

}  // namespace

glm::vec3 App::pager_check_goal() const {
    if (pager_check_leg_ < kWalkOut.size()) return marina_point(kWalkOut[pager_check_leg_]);
    return payphone_position(static_cast<std::size_t>(ostend_payphone()));
}

// The heist check's walker: face the goal, walk at walking pace.
InputFrame App::pager_check_input() const {
    InputFrame input;
    if (!pager_check_walking_ || pager_check_failed_) return input;
    const glm::vec3 goal = pager_check_goal();
    const glm::vec2 delta{goal.x - player_character_.position.x,
                          goal.z - player_character_.position.z};
    if (glm::length(delta) < 0.02f) return input;
    input.look_dx = std::atan2(delta.x, -delta.y) - player_character_.view_yaw;
    input.throttle = std::min(1.0f, glm::length(delta) /
        (character_tuning_.walk_speed_mps * static_cast<float>(kSimDt)));
    return input;
}

void App::tick_pager_check() {
    if (pager_check_done_ || pager_check_failed_) return;
    const auto fail = [&](const char* reason) {
        pager_check_failed_ = true;
        pager_check_walking_ = false;
        AP_ERROR("pager check: %s (stage %d, leg %zu, mission %d, player %.2f %.2f %.2f)", reason,
                 pager_check_stage_, pager_check_leg_, static_cast<int>(mission_stage_),
                 static_cast<double>(player_character_.position.x),
                 static_cast<double>(player_character_.position.y),
                 static_cast<double>(player_character_.position.z));
    };
    const auto key = [](SDL_Keycode code, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto advance = [&]() {
        ++pager_check_stage_;
        pager_check_stage_frame_ = frames_rendered_;
    };
    const int held = frames_rendered_ - pager_check_stage_frame_;
    const int ostend = ostend_payphone();
    if (ostend < 0) { fail("there is no Ostend Docks payphone"); return; }

    switch (pager_check_stage_) {
    case 0: {
        if (mission_stage_ == MissionStage::DeliveryComplete) {
            if (pager_.showing() || pager_.pages_shown() != 0)
                fail("the pager went off before the delay");
            else if (held > 3000)
                fail("the page never arrived");
            return;
        }
        if (mission_stage_ != MissionStage::CallLou) { fail("the delivery led somewhere else"); return; }
        const double waited =
            static_cast<double>(step_index_ - pager_check_start_step_) * kSimDt;
        AP_INFO("pager check: page after %.2f s of sim time", waited);
        if (waited + kSimDt < static_cast<double>(kLouPageDelaySeconds)) {
            fail("the page arrived early"); return;
        }
        if (!pager_.showing() || pager_.text() != kLouPageText || pager_.pages_shown() != 1) {
            fail("the page is not on the pager"); return;
        }
        if (pager_pages_beeped_ != 1) { fail("the page did not beep exactly once"); return; }
        if (payphone_target_ != ostend || !mission_target()) {
            fail("the marker is not on the Ostend Docks booth"); return;
        }
        advance();
        return;
    }
    case 1:
        if (pager_.shown_seconds() > kPagerSlideSeconds && pager_.buzzing()) {
            pager_check_capture_ = "page";
            advance();
        } else if (pager_.shown_seconds() > kPagerBuzzSeconds) {
            fail("the pager never rattled fully on screen");
        }
        return;
    case 2: {
        // This tick runs before the frame's two sim steps, so ask for the
        // frame whose steps will land the crawl in its first dot column past
        // a full LCD: "Call me at store" whole, nothing of it clipped.
        const float per_frame = kPagerCrawlCellsPerSecond * 2.0f * static_cast<float>(kSimDt);
        const float at_render = pager_.crawl_cells() + per_frame;
        const float full = static_cast<float>(kPagerLcdCells);
        const float one_column = 1.0f / static_cast<float>(kAdvanceUnits);
        if (at_render >= full && at_render < full + one_column) {
            pager_check_capture_ = "message";
            advance();
        } else if (!pager_.showing() || at_render > full + one_column) {
            fail("the message never filled the LCD");
        }
        return;
    }
    case 3:
        if (pager_.showing()) {
            if (held > 1200) fail("the pager never went away");
            return;
        }
        if (pager_.last_page() != kLouPageText || !mission_target()) {
            fail("the objective went with the pager"); return;
        }
        advance();
        return;
    case 4:
        if (held == 1) key(SDLK_e, true);
        if (held == 2) key(SDLK_e, false);
        if (held < 20) return;
        if (mission_stage_ != MissionStage::CallLou) {
            fail("E away from any payphone made the call"); return;
        }
        pager_check_capture_ = "objective";
        advance();
        return;
    case 5:
        pager_check_walking_ = true;
        pager_check_leg_ = 0;
        advance();
        return;
    case 6: {
        if (payphone_target_ != ostend) { fail("the marker left the Ostend booth on the way"); return; }
        if (!on_foot_ || !player_vitals_.alive()) { fail("the walk did not stay on foot"); return; }
        if (pager_check_leg_ < kWalkOut.size()) {
            const glm::vec3 goal = pager_check_goal();
            if (glm::length(glm::vec2{goal.x - player_character_.position.x,
                                      goal.z - player_character_.position.z}) < kLegReachedM) {
                AP_INFO("pager check: leg %zu reached at %.2f %.2f %.2f after %d frames",
                        pager_check_leg_, static_cast<double>(player_character_.position.x),
                        static_cast<double>(player_character_.position.y),
                        static_cast<double>(player_character_.position.z), held);
                ++pager_check_leg_;
            }
        } else if (can_call_lou(mission_stage_, player_character_.position, on_foot_)) {
            const glm::vec3 phone = payphone_position(static_cast<std::size_t>(ostend));
            AP_INFO("pager check: at the booth after %d frames, %.2f m out, feet %+.2f m",
                    held, static_cast<double>(glm::length(glm::vec2{
                        phone.x - player_character_.position.x,
                        phone.z - player_character_.position.z})),
                    static_cast<double>(player_character_.position.y - phone.y));
            pager_check_walking_ = false;
            advance();
            return;
        }
        if (held > 3000) fail("the walk from Devon to the booth got stuck");
        return;
    }
    case 7:
        // Let the walk's last steps and the chase camera settle.
        if (held < 45) return;
        if (!can_call_lou(mission_stage_, player_character_.position, on_foot_)) {
            fail("at the Ostend booth the call is not offered"); return;
        }
        pager_check_capture_ = "payphone";
        advance();
        return;
    case 8:
        if (held == 1) key(SDLK_e, true);
        if (held == 2) key(SDLK_e, false);
        if (held < 30) return;
        if (mission_stage_ != MissionStage::LouCalled) { fail("E at the booth did not call Lou"); return; }
        if (payphone_target_ != -1 || mission_target()) { fail("the marker outlived the call"); return; }
        if (step_index_ >= vehicle_notice_until_ ||
            vehicle_interaction_notice_ != "CALLED LOU AT THE STORE") {
            fail("the call left no notice"); return;
        }
        pager_check_capture_ = "called";
        pager_check_done_ = true;
        AP_INFO("pager check: PASS page, marker, the walk to the booth and the call back to Lou");
        return;
    default:
        return;
    }
}

void App::capture_pager_check() {
    if (pager_check_capture_.empty()) return;
    if (screenshot_path_.empty()) {
        pager_check_capture_.clear();
        return;
    }
    const std::string path = screenshot_path_ + "." + pager_check_capture_ + ".png";
    if (save_screenshot(path)) {
        AP_INFO("pager check captured %s", path.c_str());
    } else {
        AP_ERROR("pager check: could not write %s", path.c_str());
        pager_check_failed_ = true;
    }
    pager_check_capture_.clear();
}

}  // namespace apricot

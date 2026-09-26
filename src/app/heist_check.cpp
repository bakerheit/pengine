#include "app/app.h"

#include <SDL.h>

#include <array>
#include <cmath>

#include "city/bank_vault_layout.h"
#include "core/log.h"

namespace apricot {

// --heist-check. Robs Pinatty Savings & Trust end to end and captures it.
//
// THIS EXISTS BECAUSE NO HEADLESS SUITE CAN SEE THE CASH. bank_heist_tests
// proves the rules against a real wanted system, wallet and walking
// controller; it cannot say the piles draw on the table and the carts, that
// the E key reaches the grab from a real keyboard event, that a taken pile
// disappears, that the stars go up and real cruisers answer, or that the take
// and the payout are readable. The screenshots are for a human.
//
// Everything goes through the real controls — the keypad is typed, every
// doorway is walked, every pile is taken with E — with two liberties: the
// player is placed at the vault keypad to start, and the getaway is a teleport
// to a quiet street far from the bank (Sycamore Loop, the house check's
// drive; the airports if a patrol keeps eyes on him there). The escape itself is not scripted: the heat cools on the real
// wanted system, out of every officer's sight, and the take is banked by the
// real rule when the stars clear.
//
// Frames are 1/60 s exactly (see the fixed step in App::run): two sim steps.
namespace {

constexpr int kLookIn = -2;  // stop in the doorway and capture the stocked vault
struct Leg {
    glm::vec2 at;  // bank-local x, z
    int pile;      // the pile to take on arrival, or -1
};
constexpr glm::vec2 kKeypadSide{5.4f, 9.0f};
constexpr std::array<Leg, 12> kRoute{{
    {{5.85f, 11.5f}, kLookIn}, {{8.6f, 11.8f}, -1},
    {{11.3f, 11.8f}, 0}, {{12.5f, 11.8f}, 1}, {{13.7f, 11.8f}, 2},
    {{15.8f, 12.3f}, 3}, {{15.8f, 9.6f}, 4},
    // Out: round the table's east end, back through the vault door, down the
    // teller-side aisle and out of the front entrance.
    {{15.8f, 11.9f}, -1}, {{8.6f, 11.9f}, -1}, {{5.85f, 11.5f}, -1},
    {{5.85f, 2.65f}, -1}, {{0.0f, -10.0f}, -1},
}};
constexpr std::size_t kLastGrabLeg = 6;
// World x, z. Sycamore Loop first; if a patrol happens on the player there and
// keeps eyes on him, the next quiet place, so a chance sighting cannot stall
// the check. The heat still only cools out of sight.
constexpr std::array<glm::vec2, 3> kGetaways{{
    {1023.919f, 223.786f}, {150.0f, 2140.0f}, {4800.0f, 4400.0f}}};

glm::vec3 bank_point(glm::vec2 local) {
    const auto& s = city::kBankSite;
    return {s.origin.x + s.cos_yaw * local.x + s.sin_yaw * local.y, 0.0f,
            s.origin.z - s.sin_yaw * local.x + s.cos_yaw * local.y};
}
float yaw_toward(glm::vec3 from, glm::vec3 to) {
    return std::atan2(to.x - from.x, -(to.z - from.z));
}
void push_key(SDL_Keycode code, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = code;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(code);
    SDL_PushEvent(&e);
}

}  // namespace

bool App::heist_check_passed() const {
    return heist_check_done_ && !heist_check_failed_ && heist_check_captures_ == 0x7Fu;
}

// Walks the current leg while phase 3 or 5 is running; stands still otherwise.
InputFrame App::heist_check_input() {
    InputFrame input;
    const bool walking = heist_check_phase_ == 3 || heist_check_phase_ == 5;
    if (heist_check_failed_ || !walking || heist_check_leg_ >= kRoute.size()) return input;
    const glm::vec3 goal = bank_point(kRoute[heist_check_leg_].at);
    const glm::vec2 delta{goal.x - player_character_.position.x,
                          goal.z - player_character_.position.z};
    if (glm::length(delta) < 0.02f) return input;
    input.look_dx = std::atan2(delta.x, -delta.y) - player_character_.view_yaw;
    input.throttle = std::min(1.0f, glm::length(delta) /
        (character_tuning_.walk_speed_mps * static_cast<float>(kSimDt)));
    return input;
}

void App::tick_heist_check() {
    if (heist_check_failed_ || heist_check_done_) return;
    const int frame = frames_rendered_;
    const int in_phase = frame - heist_check_mark_;
    const auto next = [&] {
        ++heist_check_phase_;
        heist_check_mark_ = frame;
    };
    const auto fail = [&](const char* reason) {
        heist_check_failed_ = true;
        AP_ERROR("heist check: phase %d leg %zu: %s", heist_check_phase_, heist_check_leg_, reason);
    };
    const auto capture = [&](const char* name, unsigned bit) {
        heist_check_capture_ = name;
        heist_check_capture_bit_ = bit;
    };
    const auto tap = [&](SDL_Keycode key, int at) {
        if (in_phase == at) push_key(key, true);
        if (in_phase == at + 1) push_key(key, false);
    };
    const auto face = [&](glm::vec2 local) {
        player_character_.view_yaw = player_character_.facing_yaw =
            yaw_toward(player_character_.position, bank_point(local));
        prev_player_character_ = player_character_;
    };
    if (!player_vitals_.alive() && heist_check_phase_ < 8) { fail("the player died"); return; }

    switch (heist_check_phase_) {
    case 0:  // Settle, then stand at the vault keypad facing the door.
        if (frame < 40) return;
        if (!on_foot_) { fail("the player is not on foot"); return; }
        {
            const glm::vec3 at = bank_point(kKeypadSide);
            player_character_ = spawn_character(collider_, at.x, at.z, 0.0f);
            face({8.6f, 11.2f});
            camera_obstruction_distance_ = -1.0f;
            const glm::vec3 local = city::bank_local_position(player_character_.position);
            if (std::fabs(local.y - 0.2f) > 0.05f) { fail("did not land on the bank floor"); return; }
            if (bank_heist_.taken != 0 || bank_heist_.live) { fail("the vault is not stocked"); return; }
            heist_check_cash_before_ = economy_.cash;
            AP_INFO("heist check: at the vault keypad; wallet %lld",
                    static_cast<long long>(economy_.cash));
        }
        next();
        return;
    case 1:  // The closed door, then E and the code on the real keypad.
        if (in_phase == 60) capture("vault-closed", 1u);
        tap(SDLK_e, 90);
        if (in_phase == 100 && !bank_interaction_.modal()) { fail("E did not open the keypad"); return; }
        tap(SDLK_7, 110); tap(SDLK_4, 120); tap(SDLK_9, 130); tap(SDLK_1, 140);
        if (in_phase == 160) capture("keypad", 2u);
        tap(SDLK_RETURN, 180);
        if (in_phase == 190) {
            if (!bank_vault_.unlocked || bank_interaction_.modal()) { fail("the code did not open the vault"); return; }
            if (bank_heist_.live || wanted_.level() != 0) { fail("opening the door tripped the alarm"); return; }
            next();
        }
        return;
    case 2:  // The door swings open.
        if (in_phase > 600) { fail("the vault door never opened"); return; }
        if (bank_vault_.openness < 1.0f) return;
        heist_check_leg_ = 0;
        next();
        return;
    case 3: {  // Walk the route; at each pile, stop, face it and press E.
        if (in_phase > 1500) { fail("the walk to the next pile got stuck"); return; }
        const Leg& leg = kRoute[heist_check_leg_];
        const glm::vec3 goal = bank_point(leg.at);
        if (glm::length(glm::vec2{goal.x - player_character_.position.x,
                                  goal.z - player_character_.position.z}) > 0.03f) return;
        if (leg.pile == kLookIn) { heist_check_phase_ = 10; heist_check_mark_ = frame; return; }
        if (leg.pile < 0) { ++heist_check_leg_; heist_check_mark_ = frame; return; }
        next();
        return;
    }
    case 10:  // In the doorway, looking at the stocked table.
        if (in_phase < 45) { face({12.5f, 10.5f}); return; }
        if (in_phase == 45) { capture("vault-open", 4u); return; }
        if (in_phase < 50) return;
        ++heist_check_leg_;
        heist_check_phase_ = 3;
        heist_check_mark_ = frame;
        return;
    case 4: {  // The grab, through the real key.
        const Leg& leg = kRoute[heist_check_leg_];
        if (in_phase == 1) face(kBankHeistPiles[static_cast<std::size_t>(leg.pile)].centre);
        tap(SDLK_e, 10);
        if (in_phase == 20) {
            if (!bank_heist_pile_taken(bank_heist_, static_cast<std::size_t>(leg.pile))) {
                fail("E beside the pile did not take it");
                return;
            }
            if (!bank_heist_.live || wanted_.level() < 3) { fail("the grab did not trip three stars"); return; }
        }
        if (leg.pile == 0 && in_phase == 70) capture("alarm", 8u);
        if (in_phase < 80) return;
        if (heist_check_leg_ == kLastGrabLeg) {
            if (bank_heist_.carried != bank_heist_vault_total() || wanted_.level() < 4) {
                fail("the full take or four stars is missing");
                return;
            }
            capture("carrying", 16u);
            AP_INFO("heist check: vault emptied; carrying %lld at %d stars",
                    static_cast<long long>(bank_heist_.carried), wanted_.level());
        }
        ++heist_check_leg_;
        heist_check_phase_ = 3;
        heist_check_mark_ = frame;
        if (heist_check_leg_ > kLastGrabLeg) { heist_check_phase_ = 5; }
        return;
    }
    case 5: {  // Walk out of the bank.
        if (in_phase > 2400) { fail("the walk out of the bank got stuck"); return; }
        const glm::vec3 goal = bank_point(kRoute[heist_check_leg_].at);
        if (glm::length(glm::vec2{goal.x - player_character_.position.x,
                                  goal.z - player_character_.position.z}) > 0.03f) return;
        if (++heist_check_leg_ < kRoute.size()) return;
        AP_INFO("heist check: out of the bank; wanted %d, pursuers %zu", wanted_.level(),
                world_.traffic().police_pursuit_count());
        next();
        return;
    }
    case 6:  // Real cruisers answer the alarm.
        if (!bank_heist_.live) { fail("the take settled before the getaway"); return; }
        if (in_phase > 3600) { fail("no police unit answered the alarm"); return; }
        {
            // A pursuing cruiser or its officer in plain view of the door.
            const VehicleAgent* nearest = nullptr;
            // Close enough to be in the picture; after 30 s, anyone in pursuit
            // near the bank will do.
            float nearest_m = in_phase < 1800 ? 28.0f : 60.0f;
            glm::vec3 near_at{0.0f};
            for (const VehicleAgent& unit : world_.traffic().vehicles()) {
                if (!unit.police_pursuit) continue;
                const glm::vec3 eye = police_officer_eye_position(unit);
                // In the picture means in plain view, not behind the bank.
                if (collider_.line_of_sight_blocked(
                        player_character_.position + glm::vec3{0.0f, 1.6f, 0.0f}, eye)) continue;
                const float d = glm::distance(glm::vec2{eye.x, eye.z},
                    glm::vec2{player_character_.position.x, player_character_.position.z});
                if (d < nearest_m) { nearest_m = d; nearest = &unit; near_at = eye; }
            }
            if (!nearest) { heist_check_cop_frames_ = 0; return; }
            // Face them, and hold it until the chase camera has come round.
            player_character_.view_yaw = player_character_.facing_yaw =
                yaw_toward(player_character_.position, near_at);
            prev_player_character_ = player_character_;
            if (++heist_check_cop_frames_ < 50) return;
            AP_INFO("heist check: a pursuing cruiser is %.0f m away", static_cast<double>(nearest_m));
        }
        capture("cops", 32u);
        AP_INFO("heist check: %zu police units in pursuit at %d stars",
                world_.traffic().police_pursuit_count(), wanted_.level());
        next();
        return;
    case 7:  // The getaway, after the capture above has been taken.
        if (in_phase < 5) return;
        heist_check_getaway_ = 0;
        heist_check_cop_frames_ = 0;
        teleport({kGetaways[0].x, 0.0f, kGetaways[0].y}, 0.0f);
        if (!on_foot_) toggle_player_mode();
        next();
        return;
    case 8:  // The getaway: out of sight, the heat cools on its own.
        if (in_phase > 12000) { fail("the stars never cleared"); return; }
        if (bank_heist_.live) {
            if (!player_vitals_.alive()) { fail("the player died on the getaway"); return; }
            if (in_phase % 600 == 0)
                AP_INFO("heist check: waiting out the heat; wanted %d, heat %.2f", wanted_.level(),
                        static_cast<double>(wanted_.heat()));
            heist_check_cop_frames_ = police_eyes_on_ ? heist_check_cop_frames_ + 1 : 0;
            if (heist_check_cop_frames_ > 300 && heist_check_getaway_ + 1 < kGetaways.size()) {
                const glm::vec2 to = kGetaways[++heist_check_getaway_];
                AP_INFO("heist check: spotted at the hideout; moving to (%.0f, %.0f)",
                        static_cast<double>(to.x), static_cast<double>(to.y));
                teleport({to.x, 0.0f, to.y}, 0.0f);
                if (!on_foot_) toggle_player_mode();
                heist_check_cop_frames_ = 0;
            }
            return;
        }
        if (economy_.cash != heist_check_cash_before_ + bank_heist_vault_total()) {
            fail("the take did not land in the wallet");
            return;
        }
        AP_INFO("heist check: banked; wallet %lld -> %lld",
                static_cast<long long>(heist_check_cash_before_),
                static_cast<long long>(economy_.cash));
        next();
        return;
    case 9:
        if (in_phase < 45) return;
        capture("payout", 64u);
        heist_check_done_ = true;
        AP_INFO("heist check: PASS vault opened, five piles taken, alarm answered, take banked");
        return;
    default:
        return;
    }
}

void App::capture_heist_check() {
    if (heist_check_capture_.empty()) return;
    if (screenshot_path_.empty()) {
        heist_check_capture_.clear();
        return;
    }
    const std::string path = screenshot_path_ + "." + heist_check_capture_ + ".png";
    if (save_screenshot(path)) {
        heist_check_captures_ |= heist_check_capture_bit_;
        AP_INFO("heist check captured %s", path.c_str());
    } else {
        AP_ERROR("heist check: could not write %s", path.c_str());
        heist_check_failed_ = true;
    }
    heist_check_capture_.clear();
}

}  // namespace apricot

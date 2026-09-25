#include "app/app.h"

#include <SDL.h>

#include <cmath>

#include "app/vehicle_model_tuning.h"
#include "core/log.h"
#include "game/repair_shop.h"

namespace apricot {

// --car-bomb-check. Drives into Rook's bay one, fits a bomb with K, is refused
// on the lot, moves off it, gets out, walks away and sets it off; then takes a
// second car through the same bay and sets that one off from the driver's
// seat, dies, and respawns clear of the fire.
//
// THIS EXISTS BECAUSE NO HEADLESS SUITE CAN SEE A FIREBALL. car_bomb_tests
// proves the rules, the lift, the dead engine and the thrown crowd; it cannot
// say the key reaches the rules from a real keyboard event, that the prompt and
// the card draw, that the wreck is charred and on fire, or that a respawn does
// not hand the player back to the flames. The screenshots are for a human.
//
// Moving the rigged car off the lot is done by teleport, with the car's
// mechanical key put back afterwards: teleport() respawns the car, which gives
// it a new identity, and a new identity is a different car to the bomb. That
// is the one liberty taken; everything else goes through the real controls.
//
// Frames are 1/60 s exactly (see the fixed step in App::run).
namespace {

constexpr float kBayX = -9.0f;
constexpr float kForecourtZ = 12.0f;
constexpr float kStreetZ = 28.0f;      // site-local: off the lot, in the street
constexpr float kWalkAwayM = 13.0f;    // past the blast's reach

glm::vec3 site_point(float x, float z) {
    const auto& site = city::kAutoRepairSite;
    return {site.origin.x + site.cos_yaw * x + site.sin_yaw * z, 0.0f,
            site.origin.z - site.sin_yaw * x + site.cos_yaw * z};
}
float into_garage() {
    const auto& site = city::kAutoRepairSite;
    return std::atan2(site.sin_yaw, site.cos_yaw);
}
void push_key(SDL_Keycode code, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = code;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(code);
    SDL_PushEvent(&e);
}

}  // namespace

bool App::car_bomb_check_passed() const {
    return car_bomb_check_done_ && !car_bomb_check_failed_ && car_bomb_check_captures_ == 0xFFu;
}

void App::tick_car_bomb_check() {
    if (car_bomb_check_failed_ || car_bomb_check_done_) return;
    const int frame = frames_rendered_;
    const int in_phase = frame - car_bomb_check_mark_;
    const auto next = [&] {
        ++car_bomb_check_phase_;
        car_bomb_check_mark_ = frame;
    };
    const auto fail = [&](const char* reason) {
        car_bomb_check_failed_ = true;
        AP_ERROR("car bomb check: phase %d: %s", car_bomb_check_phase_, reason);
    };
    const auto capture = [&](const char* name, unsigned bit) {
        car_bomb_check_capture_ = name;
        car_bomb_check_capture_bit_ = bit;
    };
    const auto tap_k = [&](int at) {
        if (in_phase == at) push_key(SDLK_k, true);
        if (in_phase == at + 1) push_key(SDLK_k, false);
    };
    const auto move_rigged_to_street = [&](float x) {
        paint_check_driving_ = false;
        const uint64_t key = car_.mechanical_key;
        teleport(site_point(x, kStreetZ), into_garage());
        car_.mechanical_key = key;
        prev_car_ = car_;
    };
    const auto arrived = [&] {
        return respray_visit_.arrived && repair_shop_ready(car_, tuning_);
    };
    if (car_bomb_check_phase_ == 7)
        car_bomb_check_peak_y_ = std::max(car_bomb_check_peak_y_, car_.position.y);

    switch (car_bomb_check_phase_) {
    case 0:  // Settle on the forecourt, then crawl into bay one.
        if (frame < 30) return;
        if (on_foot_) { fail("the player is not seated"); return; }
        paint_check_bay_x_ = kBayX;
        paint_check_driving_ = true;
        next();
        return;
    case 1:  // Arrive and stop, then K.
        if (in_phase > 900) { fail("the car never stopped in the bay"); return; }
        if (!arrived()) return;
        next();
        return;
    case 2:
        tap_k(20);
        if (in_phase == 28) {
            if (!car_bomb_.rigged || car_bomb_fits_ != 1u ||
                car_bomb_.vehicle != respray_vehicle_identity()) {
                fail("K in the bay did not fit a bomb to this car");
                return;
            }
            AP_INFO("car bomb check: fitted in bay one");
        }
        if (in_phase == 40) capture("fitted", 1u);
        if (in_phase == 60) next();
        return;
    case 3:  // K again, still in the bay: refused, nothing goes off.
        tap_k(0);
        if (in_phase == 8) {
            if (car_bomb_detonations_ != 0u || !car_bomb_.rigged) {
                fail("K on Rook's lot set the bomb off");
                return;
            }
            AP_INFO("car bomb check: refused on the lot");
        }
        if (in_phase == 12) capture("refused", 2u);
        if (in_phase == 20) {
            move_rigged_to_street(kBayX);
            if (on_repair_lot(car_)) { fail("the street spot is still on Rook's lot"); return; }
            next();
        }
        return;
    case 4:  // Out of the car by the real exit.
        if (in_phase == 60) push_key(SDLK_e, true);
        if (in_phase == 62) push_key(SDLK_e, false);
        if (in_phase > 62 && on_foot_ && !vehicle_transition_.active()) next();
        if (in_phase > 700) fail("the player never got out of the rigged car");
        return;
    case 5: {  // Walk away past the reach along the kerb, and look back at it.
        // Along the street rather than across it: across it, the traffic that
        // queues behind the parked car stands between the camera and the blast.
        if (in_phase < 20) return;
        const glm::vec3 spot = site_point(kBayX - kWalkAwayM, kStreetZ - 7.5f);
        const glm::vec3 look = car_.position - spot;
        const float yaw = std::atan2(look.x, -look.z);
        player_character_ = spawn_character(collider_, spot.x, spot.z, yaw);
        player_character_.view_yaw = yaw;
        player_character_.view_pitch = -0.18f;
        prev_player_character_ = player_character_;
        update_camera(0.0f);
        car_bomb_check_rest_y_ = car_.position.y;
        car_bomb_check_peak_y_ = car_.position.y;
        car_bomb_check_health_ = player_vitals_.health;
        if (glm::distance(player_character_.position, car_.position) < kCarBombReachM + 1.0f) {
            fail("could not stand clear of the blast");
            return;
        }
        next();
        return;
    }
    case 6:
        if (in_phase == 40) capture("armed", 4u);
        tap_k(50);
        if (in_phase == 53) next();
        return;
    case 7:  // It went off.
        if (in_phase == 0) {
            if (car_bomb_detonations_ != 1u) { fail("K off the lot did not set it off"); return; }
            if (!vehicle_burnt(respray_vehicle_identity())) { fail("the car is not marked burnt"); return; }
            if (car_.health != 0.0f || !vehicle_engine_failed(car_.mechanical)) {
                fail("the car survived its own bomb");
                return;
            }
            if (!fire_.burning()) { fail("the blast lit no fire"); return; }
            if (wreck_blast_.live_count() == 0u) { fail("no fireball"); return; }
            if (car_bomb_.rigged) { fail("the bomb is still rigged after going off"); return; }
            if (wanted_.level() <= 0) { fail("a car bomb earned no heat"); return; }
            AP_INFO("car bomb check: detonated; wanted %d, %zu fire cells", wanted_.level(),
                    fire_.live_count());
        }
        if (in_phase == 2) capture("blast", 8u);
        if (in_phase == 20) capture("airborne", 16u);
        if (in_phase == 150) capture("burning", 32u);
        if (in_phase == 300) next();
        return;
    case 8: {
        if (!(car_bomb_check_peak_y_ > car_bomb_check_rest_y_ + 0.8f)) {
            fail("the blast did not lift the car");
            return;
        }
        if (player_vitals_.health < car_bomb_check_health_) {
            fail("a blast past its reach hurt the player");
            return;
        }
        // Standing at the wreck's door, it offers no way in.
        const PlayerCharacterState stood = player_character_;
        place_character_next_to_car();
        const VehicleEntryTarget target = nearby_vehicle();
        player_character_ = prev_player_character_ = stood;
        if (target.kind == VehicleEntryTarget::Kind::Current) {
            fail("the burnt-out car can still be entered");
            return;
        }
        AP_INFO("car bomb check: rose %.2f m; no way back into the wreck",
                static_cast<double>(car_bomb_check_peak_y_ - car_bomb_check_rest_y_));
        next();
        return;
    }
    case 9:  // A second car, and this time stay in it.
        if (in_phase < 30) return;
        clear_heat_after_respray(wanted_, police_offenses_, police_arrest_);
        world_.set_police_context(0, player_focus_position());
        park_current_vehicle();
        tuning_ = player_model_tuning(driving_mechanics_style_, PlayerCarId::VesperMistral);
        {
            const glm::vec3 at = site_point(kBayX + 8.0f, kForecourtZ);
            car_ = spawn_vehicle(tuning_, collider_, at.x, at.z, into_garage());
        }
        prev_car_ = car_;
        seen_impact_count_ = car_.impact_count;
        if (!car_visual_.select(scene_, tuning_, car_, PlayerCarId::VesperMistral)) {
            fail("could not select the second car");
            return;
        }
        car_visual_.clear_respray(scene_);
        sync_current_vehicle_obstacle();
        place_character_next_to_car();
        next();
        return;
    case 10:
        if (in_phase == 20) push_key(SDLK_e, true);
        if (in_phase == 22) push_key(SDLK_e, false);
        if (in_phase > 22 && !on_foot_ && !vehicle_transition_.active()) {
            paint_check_bay_x_ = kBayX + 8.0f;
            paint_check_driving_ = true;
            next();
        }
        if (in_phase > 700) fail("the player never got into the second car");
        return;
    case 11:
        if (in_phase > 900) { fail("the second car never stopped in the bay"); return; }
        if (!arrived()) return;
        next();
        return;
    case 12:
        tap_k(20);
        if (in_phase == 28) {
            if (car_bomb_fits_ != 2u || car_bomb_.vehicle != respray_vehicle_identity()) {
                fail("the second car took no bomb");
                return;
            }
            move_rigged_to_street(kBayX + 8.0f);
        }
        tap_k(90);
        if (in_phase == 93) next();
        return;
    case 13:  // From the driver's seat: out of the car, dead, and later clear.
        if (in_phase == 0) {
            if (car_bomb_detonations_ != 2u) { fail("K in the rigged car did not set it off"); return; }
            if (!on_foot_) { fail("the driver was left sitting in the wreck"); return; }
            if (player_vitals_.alive()) { fail("the driver survived sitting on the bomb"); return; }
        }
        if (in_phase == 12) capture("inside", 64u);
        if (in_phase > 12 && player_vitals_.alive()) {
            if (fire_.heat_at(player_character_.position) > 0.05f) {
                fail("the respawn put the player in the fire");
                return;
            }
            for (std::size_t i = 0; i < parked_vehicles_.size(); ++i)
                if (parked_vehicle_identity(i) != 0 && vehicle_burnt(parked_vehicle_identity(i)) &&
                    parked_vehicles_[i].state.health != 0.0f) {
                    fail("the first wreck healed once parked");
                    return;
                }
            next();
        }
        if (in_phase > 400) fail("the player never respawned");
        return;
    case 14:
        if (in_phase == 30) capture("respawn", 128u);
        if (in_phase == 40) {
            car_bomb_check_done_ = true;
            AP_INFO("car bomb check: PASS");
        }
        return;
    default:
        return;
    }
}

void App::capture_car_bomb_check() {
    if (car_bomb_check_capture_.empty()) return;
    if (screenshot_path_.empty()) {
        car_bomb_check_capture_.clear();
        return;
    }
    const std::string path = screenshot_path_ + "." + car_bomb_check_capture_ + ".png";
    if (save_screenshot(path)) {
        car_bomb_check_captures_ |= car_bomb_check_capture_bit_;
        AP_INFO("car bomb check captured %s", path.c_str());
    } else {
        AP_ERROR("car bomb check: could not write %s", path.c_str());
        car_bomb_check_failed_ = true;
    }
    car_bomb_check_capture_.clear();
}

}  // namespace apricot

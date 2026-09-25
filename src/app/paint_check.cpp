#include "app/app.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/traffic_paint_paths.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_paint_catalog.h"
#include "core/log.h"
#include "game/ui_canvas.h"

namespace apricot {

// --paint-check: a respray at Rook's, end to end, in the real game.
//
// The suites hold the recolour to the lab, the visit's latch, the picker's
// focus model and the save format. None of them can say that a car driven
// through Rook's door arrives, that R opens a booth a player can see past,
// that the paint on screen changed and only the paint, that a stolen car and
// the traffic it came from stay separate, or that a cop who watched you pull
// in still has you when the spray finishes. This does, through the same keys,
// mouse and driving a player uses: every car is DRIVEN in from the forecourt
// (never placed in a bay), every booth action is a pushed key or click, and
// every sighting comes from a real police unit (never a faked flag).
//
// Stages and their bits:
//     1 P0   every profiled atlas rebuilt from its PNG matches the lab's stats
//     2 S0   the forecourt stall gives the lot hint, and R does nothing there
//     4 S1   an unseen roll-in arrives, and the latch agrees with the sightings
//     8 S1b  getting out and back in inside the bay needs a fresh pull-in
//    16 S2   the booth pauses the sim, ignores key repeat, cancels from its
//            button, previews presets and custom drags; capture .picker
//    32 S3   R confirms, the spray holds the car, E does not exit in its first
//            moment, the stars clear, the paint lands; capture .palette-after
//    64 S4   rolling mid-spray cancels and restores the paint
//   128 S5   a stolen traffic car wears its shared livery, unpaintable, with
//            the right base; the parked painted car keeps its own slot
//   256 S6   the stolen car resprays into a slot of its own; traffic
//            untouched; capture .stolen-after
//   512 S9   save and load bring back the model, livery base and respray;
//            capture .loaded
//  1024 S7   a cop sees the pull-in: painted, stars kept; capture .seen-kept
//  2048 S8   police liveries respray with lamps and lightbars working;
//            captures .cruiser-lit, .cruiser-dark, .patrol29-lit
//  4096 S4b  a car swap mid-spray resets the visit and never paints the swap
//  8192 S10  the firetruck fits bay two and the bay camera frames it;
//            capture .firetruck-bay1
namespace {

constexpr unsigned kAllStageBits = 0x3FFFu;
constexpr unsigned kAllCaptureBits = 0x1FFu;
constexpr float kForecourtZ = 12.0f;  // site-local: out on the forecourt
constexpr float kBrakeAtZ = -1.5f;    // body centre: brake here, stop inside the bay

glm::vec3 site_to_world(float x, float z) {
    const auto& site = city::kAutoRepairSite;
    return {site.origin.x + site.cos_yaw * x + site.sin_yaw * z, 0.0f,
            site.origin.z - site.sin_yaw * x + site.cos_yaw * z};
}

glm::vec2 world_to_site(glm::vec3 p) {
    const auto& site = city::kAutoRepairSite;
    return repair_site_local(p - glm::vec3{site.origin.x, 0.0f, site.origin.z});
}

// Facing into the garage (site -z), in teleport()'s heading convention.
float into_garage_heading() {
    const auto& site = city::kAutoRepairSite;
    return std::atan2(site.sin_yaw, site.cos_yaw);
}

void push_key(SDL_Keycode code, bool down, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = code;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(code);
    e.key.repeat = repeat ? 1 : 0;
    SDL_PushEvent(&e);
}

void push_button(uint8_t button, bool down, glm::ivec2 at) {
    SDL_Event e{};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.button = button;
    e.button.x = at.x;
    e.button.y = at.y;
    SDL_PushEvent(&e);
}

void push_motion(glm::ivec2 at, int xrel) {
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = at.x;
    e.motion.y = at.y;
    e.motion.xrel = xrel;
    SDL_PushEvent(&e);
}

}  // namespace

InputFrame App::paint_check_input() {
    InputFrame in = input_.frame();
    if (on_foot_) return in;
    if (!paint_check_driving_) {
        in.steer = in.throttle = in.brake = 0.0f;
        in.handbrake = 1.0f;
        return in;
    }
    // Hold the bay's centreline, nose into the garage, at a crawl.
    const glm::vec2 local = world_to_site(car_.position);
    const glm::vec2 nose = repair_site_local(car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f});
    const float heading_error = std::atan2(nose.x, -nose.y);
    const float lateral = local.x - paint_check_bay_x_;
    in.steer = std::clamp(-(0.30f * lateral + 1.4f * heading_error), -1.0f, 1.0f);
    const float speed = glm::length(car_.velocity);
    // Brake once the nose is a couple of metres past the door line (site z
    // 3.5), so a firetruck stops as far inside as a hatchback does.
    const float brake_at = std::min(kBrakeAtZ, 3.5f - tuning_.car_collision_half_length - 2.0f);
    if (local.y > brake_at) {
        in.throttle = speed < 2.8f ? 0.45f : 0.0f;
        in.brake = 0.0f;
        in.handbrake = 0.0f;
    } else {
        in.throttle = 0.0f;
        in.brake = speed > 0.6f ? 1.0f : 0.0f;
        in.handbrake = 1.0f;
    }
    return in;
}

void App::capture_paint_check() {
    if (paint_check_capture_.empty()) return;
    if (screenshot_path_.empty()) {
        paint_check_capture_.clear();
        return;
    }
    const std::string path = screenshot_path_ + "." + paint_check_capture_ + ".png";
    if (save_screenshot(path)) {
        paint_check_captures_ |= paint_check_capture_bit_;
        AP_INFO("paint check captured %s", path.c_str());
    } else {
        AP_ERROR("paint check: could not write %s", path.c_str());
    }
    paint_check_capture_.clear();
}

void App::tick_paint_check() {
    if (paint_check_done_ || paint_check_failed_ || frames_rendered_ < 240) return;
    if (!paint_check_capture_.empty()) return;  // the render owes a capture first
    const int in_phase = frames_rendered_ - paint_check_mark_;

    const auto fail = [&](const char* why) {
        paint_check_failed_ = true;
        paint_check_driving_ = false;
        const glm::vec2 local = world_to_site(car_.position);
        AP_ERROR("paint check: %s (stage %d, phase %d; car at site %.2f, %.2f, %.2f m/s; "
                 "visit in_bay %d pulled_in %d arrived %d seen %d spraying %d; wanted %d)",
                 why, paint_check_stage_, paint_check_phase_, static_cast<double>(local.x),
                 static_cast<double>(local.y), static_cast<double>(glm::length(car_.velocity)),
                 respray_visit_.in_bay, respray_visit_.pulled_in, respray_visit_.arrived,
                 respray_visit_.seen, respray_visit_.spraying(), wanted_.level());
        if (step_index_ < vehicle_notice_until_)
            AP_ERROR("paint check: the game was showing \"%s\"", vehicle_interaction_notice_.c_str());
    };
    const auto next_phase = [&] {
        ++paint_check_phase_;
        // The next tick is the phase's frame 0: this frame's render still runs.
        paint_check_mark_ = frames_rendered_ + 1;
    };
    const auto pass = [&](unsigned bit, const char* what) {
        paint_check_bits_ |= bit;
        AP_INFO("paint check %s: PASS", what);
        ++paint_check_stage_;
        paint_check_retries_ = 0;
        paint_check_phase_ = 0;
        paint_check_mark_ = frames_rendered_ + 1;
        paint_check_driving_ = false;
    };
    const auto capture = [&](const char* name, unsigned bit) {
        paint_check_capture_ = name;
        paint_check_capture_bit_ = bit;
    };
    const UiCanvas canvas = UiCanvas::from_drawable(
        {static_cast<float>(window_.width()), static_cast<float>(window_.height())});
    const PaintPickerLayout layout = PaintPickerLayout::from_canvas(canvas.size);
    const auto window_at = [&](glm::vec2 canvas_point) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_.sdl(), &w, &h);
        if (!(canvas.size.x > 0.0f) || !(canvas.size.y > 0.0f)) return glm::ivec2{0};
        return glm::ivec2{static_cast<int>(std::lround(canvas_point.x * static_cast<float>(w) / canvas.size.x)),
                          static_cast<int>(std::lround(canvas_point.y * static_cast<float>(h) / canvas.size.y))};
    };
    const auto click = [&](glm::vec2 canvas_point, int frame) {
        if (frame == 0) push_button(SDL_BUTTON_LEFT, true, window_at(canvas_point));
        if (frame == 1) push_button(SDL_BUTTON_LEFT, false, window_at(canvas_point));
    };
    const auto tap = [&](SDL_Keycode key, int frame) {
        if (frame == 0) push_key(key, true);
        if (frame == 1) push_key(key, false);
    };
    const auto hint = [&] {
        ResprayHintInput in;
        in.driving = !on_foot_ && !vehicle_transition_.active() && player_vitals_.alive();
        in.on_lot = on_repair_lot(car_);
        in.fits = repair_bay_fits(car_, tuning_);
        in.ready = repair_shop_ready(car_, tuning_);
        in.pending_car = player_car_paint_pending(car_visual_.active_car());
        in.eyes_on = police_eyes_on_;
        in.wanted_level = wanted_.level();
        in.visit = &respray_visit_;
        return respray_hint(in);
    };
    const auto swap_car = [&](PlayerCarId model) {
        cancel_vehicle_transition();
        police_emergency_enabled_ = false;
        drop_trailer();
        tuning_ = player_model_tuning(driving_mechanics_style_, model);
        collider_.set_kinematic_enabled(current_vehicle_collider_, false);
        const glm::vec3 forward = car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
        car_ = spawn_vehicle(tuning_, collider_, car_.position.x, car_.position.z,
                             std::atan2(-forward.x, -forward.z));
        prev_car_ = car_;
        seen_impact_count_ = car_.impact_count;
        if (!car_visual_.select(scene_, tuning_, car_, model)) return false;
        reset_respray_state();
        dev_menu_.set_player_car(model);
        vehicle_audio_.set_model(player_car_definition(model).mesh_path);
        return true;
    };

    // Park on the forecourt in `model`, then drive into bay `bay_x` and stop.
    // Phases [base, base+2]. `go` holds the car on the forecourt until true.
    // Returns true once the car has arrived and stopped.
    const auto drive_in = [&](int base, PlayerCarId model, float bay_x, const auto& go) {
        const int p = paint_check_phase_ - base;
        if (p < 0) return false;
        if (p > 2) return true;
        if (p == 0) {
            if (on_foot_) {
                fail("the player is not seated");
                return false;
            }
            // Out onto the forecourt first, then the swap: a long car must
            // never be spawned inside a bay it does not fit.
            paint_check_bay_x_ = bay_x;
            teleport(site_to_world(bay_x, kForecourtZ), into_garage_heading());
            if (canonical_player_car_id(car_visual_.active_car()) != canonical_player_car_id(model) &&
                !swap_car(model)) {
                fail("could not select the car");
                return false;
            }
            next_phase();
            return false;
        }
        if (p == 1) {
            if (in_phase < 30) return false;
            if (!respray_visit_.outside_driving) {
                fail("a car on the forecourt did not register as driving outside the bay");
                return false;
            }
            if (!go()) {
                if (in_phase > 7200) fail("the forecourt hold never released");
                return false;
            }
            paint_check_any_visible_ = false;
            paint_check_driving_ = true;
            next_phase();
            return false;
        }
        if (respray_visit_.in_bay && !respray_visit_.arrived && police_eyes_on_)
            paint_check_any_visible_ = true;
        // Arrival latches at 0.5 m/s, but a heavy van can still be rocking on
        // its springs there, and the booth rightly refuses a car that moves.
        // Wait for it to actually settle.
        if (respray_visit_.arrived && repair_shop_ready(car_, tuning_) &&
            glm::length(car_.velocity) < 0.12f) {
            paint_check_driving_ = false;
            // Not true yet: whatever follows must start on its own frame 0,
            // not inherit this phase's frame count in the same tick.
            next_phase();
            return false;
        }
        if (in_phase > 1500) fail("the car never arrived in the bay");
        return false;
    };
    const auto always = [] { return true; };

    // Open the booth, click preset `swatch`, confirm with R. Phases
    // [base, base+2]. Returns true once the respray has completed.
    const auto respray = [&](int base, int swatch) {
        const int p = paint_check_phase_ - base;
        if (p < 0) return false;
        if (p > 2) return true;
        if (p == 0) {
            // Press R, and press it again every half second until the booth
            // is up: the gate is "stopped in the bay", read on the frame the
            // key arrives.
            if (!paint_shop_.modal()) {
                if (in_phase % 30 == 0)
                    AP_INFO("paint check: pressing R (on foot %d, bank %d, wheel %d, dev %d, transition %d)",
                            on_foot_, bank_interaction_.modal(), weapon_wheel_.open, dev_menu_.open(),
                            vehicle_transition_.active());
                tap(SDLK_r, in_phase % 30);
            }
            if (paint_shop_.modal() && in_phase >= 2) next_phase();
            else if (in_phase > 180) fail("R did not open the booth");
            return false;
        }
        if (p == 1) {
            click(layout.swatches[static_cast<std::size_t>(swatch)].centre(), in_phase);
            if (in_phase >= 24) {
                const PaintOrder preview = paint_shop_.preview();
                if (preview.factory || preview.colour != kPaintPresets[static_cast<std::size_t>(swatch)].rgb) {
                    fail("clicking a swatch did not select it");
                    return false;
                }
                paint_check_mark_count_ = respray_completions_;
                next_phase();
            }
            return false;
        }
        tap(SDLK_r, in_phase);
        if (respray_completions_ > paint_check_mark_count_ && in_phase > 10) {
            next_phase();
            return false;  // the next phase starts on its own frame 0
        }
        if (in_phase > 400) fail("the respray never completed");
        return false;
    };

    switch (paint_check_stage_) {
    case 0: {  // P0
        std::string report;
        if (!paint_pool_.check_profile_stats(report)) {
            AP_ERROR("paint check: profile drift:\n%s", report.c_str());
            fail("paint profiles no longer match their atlases");
            return;
        }
        pass(1u, "P0 profile stats");
        return;
    }
    case 1: {  // S0 + S1: Mistral, bay one, unseen
        const bool arrived = drive_in(0, PlayerCarId::VesperMistral, -9.0f, [&] {
            if (in_phase == 10) {
                if (hint() != RespraySuffix::LotPullIn) {
                    fail("the forecourt stall did not give the lot hint");
                    return false;
                }
                push_key(SDLK_r, true);
            }
            if (in_phase == 11) push_key(SDLK_r, false);
            if (in_phase < 20) return false;
            if (paint_shop_.modal()) {
                fail("R opened the booth on the forecourt");
                return false;
            }
            paint_check_bits_ |= 2u;
            return true;
        });
        if (!arrived || paint_check_failed_) return;
        if (!respray_visit_.pulled_in) {
            fail("a drive-in did not pull in");
            return;
        }
        if (respray_visit_.seen) {
            // A patrol happened past. That is the S7 case, not this one: go round.
            if (++paint_check_retries_ > 5) {
                fail("a cop saw every one of five pull-ins");
                return;
            }
            AP_INFO("paint check: a cop saw the pull-in; driving in again");
            paint_check_phase_ = 0;
            paint_check_mark_ = frames_rendered_ + 1;
            return;
        }
        if (paint_check_any_visible_) {
            fail("a cop was visible during the pull-in but the latch did not see it");
            return;
        }
        if (hint() != RespraySuffix::Ready) {
            fail("an arrived, unwanted car did not get the R / X prompt");
            return;
        }
        pass(4u, "S1 unseen drive-in");
        return;
    }
    case 2: {  // S1b: out and back in inside the bay
        switch (paint_check_phase_) {
        case 0:
            if (glm::length(car_.velocity) > 0.2f) {
                if (in_phase > 600) fail("the car never came to rest in the bay");
                return;
            }
            toggle_player_mode();
            next_phase();
            return;
        case 1:
            if (on_foot_ && !vehicle_transition_.active()) next_phase();
            else if (in_phase > 900) fail("getting out did not finish");
            else if (!vehicle_transition_.active() && in_phase % 60 == 59) toggle_player_mode();
            return;
        case 2:
            toggle_player_mode();
            next_phase();
            return;
        case 3:
            if (!on_foot_ && !vehicle_transition_.active()) next_phase();
            else if (in_phase > 900) fail("getting back in did not finish");
            else if (!vehicle_transition_.active() && in_phase % 60 == 59) toggle_player_mode();
            return;
        case 4:
            if (in_phase < 10) return;
            if (respray_visit_.arrived) {
                fail("getting out and back in inside the bay kept the arrival");
                return;
            }
            if (hint() != RespraySuffix::NeedsPullIn) {
                fail("a re-entered car in the bay was not told to pull in again");
                return;
            }
            tap(SDLK_r, in_phase - 10);
            if (in_phase < 24) return;
            if (paint_shop_.modal()) {
                fail("R opened the booth for a car that never pulled in");
                return;
            }
            next_phase();
            return;
        default:
            if (drive_in(5, PlayerCarId::VesperMistral, -9.0f, always))
                pass(8u, "S1b exit and re-entry needs a fresh pull-in");
            return;
        }
    }
    case 3: {  // S2: the booth
        switch (paint_check_phase_) {
        case 0:
            if (in_phase == 0) wanted_.set_level(3);
            tap(SDLK_r, in_phase - 2);
            if (in_phase < 10) return;
            if (!paint_shop_.modal()) {
                fail("R did not open the booth in an arrived bay");
                return;
            }
            paint_check_mark_step_ = step_index_;
            push_key(SDLK_r, true, true);  // a held key's repeat must not confirm
            next_phase();
            return;
        case 1: {
            if (in_phase == 1) push_key(SDLK_r, false);
            if (in_phase < 30) return;
            // Asserted once, before this phase cancels the booth on purpose.
            if (in_phase == 30 && step_index_ != paint_check_mark_step_) {
                fail("the sim stepped while the booth was open");
                return;
            }
            if (in_phase == 30 && !paint_shop_.modal()) {
                fail("a key repeat closed the booth");
                return;
            }
            // Walk focus down to the buttons, onto CANCEL, and press Enter.
            const int t = in_phase - 30;
            if (t < 28) tap(SDLK_DOWN, t % 4);
            else if (t < 32) tap(SDLK_LEFT, t - 28);
            else if (t < 36) {
                if (t == 32 && paint_shop_.picker().focus().kind != PaintControl::Kind::Cancel) {
                    fail("focus did not reach CANCEL");
                    return;
                }
                tap(SDLK_RETURN, t - 32);
            } else if (t >= 44) {
                if (paint_shop_.modal()) {
                    fail("Enter on CANCEL did not cancel");
                    return;
                }
                if (car_visual_.paint(scene_) != committed_body_material()) {
                    fail("cancelling did not restore the car's paint");
                    return;
                }
                next_phase();
            }
            return;
        }
        case 2:
            tap(SDLK_r, in_phase);
            if (in_phase < 10) return;
            if (!paint_shop_.modal()) {
                fail("the booth did not reopen");
                return;
            }
            paint_check_mark_count_ = paint_preview_count_;
            next_phase();
            return;
        case 3: {
            // Three presets by arrow, whichever way the grid has room.
            const int t = in_phase;
            if (t < 24 && t % 8 == 0)
                push_key(paint_shop_.picker().column() < kPaintGridColumns - 1 ? SDLK_RIGHT : SDLK_LEFT, true);
            if (t < 24 && t % 8 == 1) {
                push_key(SDLK_RIGHT, false);
                push_key(SDLK_LEFT, false);
            }
            if (t < 32) return;
            if (paint_preview_count_ < paint_check_mark_count_ + 3u) {
                fail("arrowing across the presets did not preview three of them");
                return;
            }
            paint_check_mark_count_ = paint_preview_count_;
            next_phase();
            return;
        }
        case 4: {
            // CUSTOM: drag the hue bar, then the saturation/value plane.
            const int t = in_phase;
            tap(SDLK_TAB, t);
            if (t == 6 && paint_shop_.picker().tab() != PaintTab::Custom) {
                fail("Tab did not switch to CUSTOM");
                return;
            }
            const PaintRect& hue = layout.hue_track;
            const PaintRect& plane = layout.sv_plane;
            const auto along = [](const PaintRect& r, float u, float v) {
                return r.min + glm::vec2{u, v} * r.size();
            };
            if (t == 8) push_button(SDL_BUTTON_LEFT, true, window_at(along(hue, 0.05f, 0.5f)));
            if (t > 8 && t <= 28) push_motion(window_at(along(hue, 0.05f + 0.045f * static_cast<float>(t - 8), 0.5f)), 12);
            if (t == 29) push_button(SDL_BUTTON_LEFT, false, window_at(along(hue, 0.95f, 0.5f)));
            if (t == 32) push_button(SDL_BUTTON_LEFT, true, window_at(along(plane, 0.9f, 0.2f)));
            if (t > 32 && t <= 44) push_motion(window_at(along(plane, 0.9f - 0.05f * static_cast<float>(t - 32),
                                                               0.2f + 0.04f * static_cast<float>(t - 32))), -8);
            if (t == 45) push_button(SDL_BUTTON_LEFT, false, window_at(along(plane, 0.3f, 0.68f)));
            if (t < 70) return;
            if (paint_preview_count_ < paint_check_mark_count_ + 10u) {
                fail("dragging the hue bar and plane did not preview as it went");
                return;
            }
            capture("picker", 1u);
            next_phase();
            return;
        }
        default:
            pass(16u, "S2 booth");
            return;
        }
    }
    case 4: {  // S3: unseen completion clears the stars
        switch (paint_check_phase_) {
        case 0:
            paint_check_pick_ = paint_shop_.preview().colour;
            paint_check_mark_count_ = respray_starts_;
            paint_check_mark_reveals_ = respray_reveal_count_;
            paint_check_mark_completions_ = respray_completions_;
            push_key(SDLK_r, true);
            next_phase();
            return;
        case 1:
            if (in_phase == 1) push_key(SDLK_r, false);
            if (respray_starts_ > paint_check_mark_count_) {
                if (!respray_visit_.spraying()) {
                    fail("the spray did not start with the order");
                    return;
                }
                next_phase();
            } else if (in_phase > 60) {
                fail("R did not start a spray");
            }
            return;
        case 2:
            // Two quick taps of E, the button that also gets out.
            tap(SDLK_e, in_phase);
            tap(SDLK_e, in_phase - 3);
            if (in_phase < 6) return;
            if (on_foot_ || vehicle_transition_.active()) {
                fail("a quick E during the spray threw the player out");
                return;
            }
            if (respray_completions_ <= paint_check_mark_completions_) {
                if (in_phase > 150) fail("the spray did not complete");
                return;
            }
            if (wanted_.level() != 0) {
                fail("an unseen respray did not clear the stars");
                return;
            }
            if (!car_visual_.respray() || *car_visual_.respray() != paint_check_pick_) {
                fail("the car does not wear the colour it was resprayed");
                return;
            }
            if (!renderer_.material_paintable(car_visual_.paint(scene_)) ||
                paint_pool_.is_preview(car_visual_.paint(scene_))) {
                fail("the respray did not land on an owned paint slot");
                return;
            }
            if (respray_reveal_count_ < paint_check_mark_reveals_ + 8u) {
                fail("the spray did not reveal the colour in steps");
                return;
            }
            if (!respray_camera_active()) {
                fail("the bay camera let go the moment the spray finished");
                return;
            }
            next_phase();
            return;
        case 3:
            if (in_phase < 20) return;
            capture("palette-after", 2u);
            next_phase();
            return;
        default:
            pass(32u, "S3 unseen respray clears the stars");
            return;
        }
    }
    case 5: {  // S4: moving mid-spray cancels
        switch (paint_check_phase_) {
        case 0:
            if (in_phase < 60) return;  // let the camera hold run out
            tap(SDLK_r, in_phase - 60);
            if (in_phase < 70) return;
            if (!paint_shop_.modal()) {
                fail("R did not reopen the booth");
                return;
            }
            paint_check_mark_count_ = respray_starts_;
            paint_check_mark_completions_ = respray_cancels_;
            next_phase();
            return;
        case 1:
            tap(SDLK_r, in_phase - 20);
            if (respray_starts_ > paint_check_mark_count_) {
                car_.velocity += car_.orientation * glm::vec3{0.0f, 0.0f, -2.0f};
                next_phase();
            } else if (in_phase > 80) {
                fail("the second order did not start");
            }
            return;
        case 2:
            if (respray_cancels_ <= paint_check_mark_completions_) {
                if (in_phase > 60) fail("rolling during the spray did not cancel it");
                return;
            }
            if (!car_visual_.respray() || *car_visual_.respray() != paint_check_pick_ ||
                car_visual_.paint(scene_) != committed_body_material()) {
                fail("a cancelled spray changed the paint");
                return;
            }
            pass(64u, "S4 cancel");
            return;
        }
        return;
    }
    case 6: {  // S5: steal a traffic car
        switch (paint_check_phase_) {
        case 0:
            // S4 left the car rolling; the game rightly refuses to let anyone
            // out of a moving car, so wait for it to stop first.
            if (glm::length(car_.velocity) > 0.2f) {
                if (in_phase > 600) fail("the car never came to rest after the cancelled spray");
                return;
            }
            paint_check_parked_colour_ = paint_check_pick_;
            toggle_player_mode();
            next_phase();
            return;
        case 1:
            if (on_foot_ && !vehicle_transition_.active()) next_phase();
            else if (in_phase > 900) fail("getting out of the Mistral did not finish");
            else if (!vehicle_transition_.active() && in_phase % 60 == 59) toggle_player_mode();
            return;
        default: {
            if (in_phase % 30 != 0) return;
            if (in_phase > 1800) {
                fail("no stopped traffic car could be taken");
                return;
            }
            const auto traffic = world_.traffic().vehicles();
            for (int pass_index = 0; pass_index < 2; ++pass_index) {
                for (const auto& candidate : traffic) {
                    if (candidate.speed_mps > 0.8f || glm::length(candidate.collision_velocity_xz) > 0.1f) continue;
                    const TrafficVehicleKind kind = traffic_vehicle_kind(candidate);
                    const std::size_t index = traffic_visual_.vehicle_paint_index(candidate);
                    // First look for a livery other than the stock one.
                    if (pass_index == 0 && !((kind == TrafficVehicleKind::Sedan || kind == TrafficVehicleKind::BoxTruck) && index > 0))
                        continue;
                    if (kind == TrafficVehicleKind::Snowplow) continue;
                    const auto footprint = traffic_vehicle_footprint(kind);
                    const glm::quat rotation = glm::angleAxis(std::atan2(-candidate.fwd.x, -candidate.fwd.z),
                                                              glm::vec3{0.0f, 1.0f, 0.0f});
                    const glm::vec3 door = candidate.pos + rotation *
                        glm::vec3{-(footprint.half_width_m + 0.8f), 0.0f, -footprint.half_length_m * 0.22f};
                    const auto trial = spawn_character(collider_, door.x, door.z, 0.0f);
                    if (!character_position_clear(collider_, trial.position, character_tuning_)) continue;
                    player_character_ = prev_player_character_ = trial;
                    const VehicleEntryTarget target = nearby_vehicle();
                    if (target.kind != VehicleEntryTarget::Kind::Traffic || target.lane_key != candidate.lane_key ||
                        target.slot != candidate.slot) continue;
                    const MaterialId livery = traffic_visual_.vehicle_paint(candidate);
                    const uint8_t base = paint_base_for_traffic(kind, index);
                    toggle_player_mode();
                    if (on_foot_) {
                        fail("taking the traffic car failed");
                        return;
                    }
                    if (car_visual_.paint(scene_) != livery || car_visual_.factory_material() != livery ||
                        renderer_.material_paintable(livery) || car_visual_.paint_base() != base ||
                        car_visual_.respray()) {
                        fail("the stolen car does not wear its unpaintable traffic livery at its base");
                        return;
                    }
                    paint_check_parked_index_ = parked_vehicles_.size();
                    for (std::size_t i = 0; i < parked_vehicles_.size(); ++i) {
                        const auto& parked = parked_vehicles_[i];
                        if (parked.visual.active_car() == PlayerCarId::VesperMistral && parked.visual.respray() &&
                            *parked.visual.respray() == paint_check_parked_colour_) {
                            paint_check_parked_index_ = i;
                        }
                    }
                    if (paint_check_parked_index_ >= parked_vehicles_.size()) {
                        fail("the painted Mistral did not park with its paint");
                        return;
                    }
                    paint_check_parked_material_ = parked_vehicles_[paint_check_parked_index_].visual.paint(scene_);
                    if (!paint_pool_.owns(paint_check_parked_material_)) {
                        fail("the parked Mistral does not hold its own paint slot");
                        return;
                    }
                    AP_INFO("paint check: took a %s in livery %zu (base %u)",
                            player_car_definition(car_visual_.active_car()).model, index, base);
                    pass(128u, "S5 theft keeps the traffic livery");
                    return;
                }
            }
            return;
        }
        }
    }
    case 7: {  // S6: respray the stolen car
        const PlayerCarId stolen = car_visual_.active_car();
        if (!drive_in(0, stolen, -1.0f, always)) return;
        if (paint_check_phase_ == 3 && in_phase == 0) wanted_.set_level(2);
        if (!respray(3, 2)) return;
        switch (paint_check_phase_) {
        case 6: {
            const MaterialId worn = car_visual_.paint(scene_);
            if (!car_visual_.respray() || *car_visual_.respray() != kPaintPresets[2].rgb) {
                fail("the stolen car did not take its respray");
                return;
            }
            if (worn == paint_check_parked_material_ || !paint_pool_.owns(worn)) {
                fail("the stolen car's paint shares the parked car's slot");
                return;
            }
            if (paint_check_parked_index_ >= parked_vehicles_.size() ||
                parked_vehicles_[paint_check_parked_index_].visual.paint(scene_) != paint_check_parked_material_ ||
                *parked_vehicles_[paint_check_parked_index_].visual.respray() != paint_check_parked_colour_) {
                fail("respraying the stolen car changed the parked car");
                return;
            }
            for (int k = 0; k < 9; ++k) {
                const auto kind = static_cast<TrafficVehicleKind>(k);
                for (std::size_t i = 0; i < traffic_paint_paths(kind).count; ++i) {
                    if (renderer_.material_paintable(traffic_visual_.paint_material(kind, i))) {
                        fail("a traffic livery became paintable");
                        return;
                    }
                }
            }
            if (wanted_.level() != 0 && !respray_visit_.seen) {
                fail("the unseen stolen-car respray did not clear the stars");
                return;
            }
            next_phase();
            return;
        }
        case 7:
            if (in_phase < 20) return;
            capture("stolen-after", 4u);
            next_phase();
            return;
        default:
            pass(256u, "S6 stolen respray");
            return;
        }
    }
    case 8: {  // S9: save and load the stolen, resprayed car
        switch (paint_check_phase_) {
        case 0: {
            if (in_phase < 60) return;
            const std::string original = save_path_;
            const std::string slot = screenshot_path_.empty() ? "build/paint-check.save" : screenshot_path_ + ".save";
            save_path_ = slot;
            paint_check_saved_model_ = car_visual_.active_car();
            paint_check_saved_base_ = car_visual_.paint_base();
            paint_check_saved_colour_ = car_visual_.respray().value_or(PaintColor{});
            paint_check_saved_livery_ = car_visual_.factory_material();
            const bool saved = save_game();
            if (saved) {
                car_visual_.clear_respray(scene_);
                car_.position.x += 25.0f;
            }
            const bool loaded = saved && load_game();
            save_path_ = original;
            std::remove(slot.c_str());
            if (!saved || !loaded) {
                fail("saving or loading the resprayed car failed");
                return;
            }
            if (car_visual_.active_car() != paint_check_saved_model_ ||
                car_visual_.paint_base() != paint_check_saved_base_ ||
                !car_visual_.respray() || *car_visual_.respray() != paint_check_saved_colour_ ||
                !renderer_.material_paintable(car_visual_.paint(scene_))) {
                fail("the load did not bring back the model, livery base and respray");
                return;
            }
            if (paint_check_saved_base_ > 0 && car_visual_.factory_material() != paint_check_saved_livery_) {
                fail("the load did not bring back the stolen livery as factory paint");
                return;
            }
            next_phase();
            return;
        }
        case 1:
            if (in_phase < 30) return;
            capture("loaded", 8u);
            next_phase();
            return;
        default:
            pass(512u, "S9 save and load");
            return;
        }
    }
    case 9: {  // S7: a cop sees the pull-in
        const PlayerCarId car = car_visual_.active_car();
        const bool arrived = drive_in(0, car, -1.0f, [&] {
            if (in_phase == 30) {
                wanted_.set_level(3);
                paint_check_seen_steps_ = 0;
            }
            paint_check_seen_steps_ = police_eyes_on_ ? paint_check_seen_steps_ + 1u : 0u;
            return paint_check_seen_steps_ >= 60u;
        });
        if (!arrived) return;
        switch (paint_check_phase_) {
        case 3:
            if (!respray_visit_.seen) {
                fail("a pull-in made in view of a cop was not seen");
                return;
            }
            if (hint() != RespraySuffix::ReadySeen ||
                std::string(respray_hint_text(RespraySuffix::ReadySeen)).find("STARS STAY") == std::string::npos) {
                fail("the prompt did not warn that the stars stay");
                return;
            }
            paint_check_mark_level_ = wanted_.level();
            next_phase();
            return;
        default:
            break;
        }
        if (paint_check_phase_ == 5 && in_phase == 0 &&
            std::string(paint_shop_.picker().confirm_label()) != "RESPRAY - STARS STAY") {
            fail("the booth's button did not say the stars stay");
            return;
        }
        if (!respray(4, 14)) return;
        switch (paint_check_phase_) {
        case 7:
            if (wanted_.level() < 1) {
                fail("a respray a cop watched the car pull in for cleared the stars");
                return;
            }
            if (!car_visual_.respray() || *car_visual_.respray() != kPaintPresets[14].rgb) {
                fail("the seen respray did not paint the car");
                return;
            }
            next_phase();
            return;
        case 8:
            if (in_phase < 20) return;
            capture("seen-kept", 16u);
            next_phase();
            return;
        default:
            wanted_.reset();
            police_offenses_.reset();
            police_arrest_.reset();
            world_.set_police_context(0, player_focus_position());
            pass(1024u, "S7 seen pull-in keeps the stars");
            return;
        }
    }
    case 10: {  // S8: police liveries
        if (!drive_in(0, PlayerCarId::MunicipalCruiser91C, -1.0f, always)) return;
        if (!respray(3, 13)) return;
        switch (paint_check_phase_) {
        case 6:
            police_emergency_enabled_ = true;
            next_phase();
            return;
        case 7:
            if (in_phase < 45) return;
            capture("cruiser-lit", 32u);
            next_phase();
            return;
        case 8:
            police_emergency_enabled_ = false;
            if (in_phase < 45) return;
            capture("cruiser-dark", 64u);
            next_phase();
            return;
        default:
            break;
        }
        if (!drive_in(9, PlayerCarId::LegacyCar5NextPolice, -1.0f, always)) return;
        if (!respray(12, 14)) return;
        switch (paint_check_phase_) {
        case 15:
            police_emergency_enabled_ = true;
            next_phase();
            return;
        case 16:
            if (in_phase < 45) return;
            capture("patrol29-lit", 128u);
            next_phase();
            return;
        default:
            police_emergency_enabled_ = false;
            pass(2048u, "S8 police liveries");
            return;
        }
    }
    case 11: {  // S4b: a car swap mid-spray
        switch (paint_check_phase_) {
        case 0:
            if (in_phase < 60) return;
            tap(SDLK_r, in_phase - 60);
            if (in_phase < 70) return;
            if (!paint_shop_.modal()) {
                fail("R did not open the booth for the swap test");
                return;
            }
            paint_check_mark_count_ = respray_starts_;
            paint_check_mark_completions_ = respray_completions_;
            next_phase();
            return;
        case 1:
            tap(SDLK_r, in_phase - 20);
            if (respray_starts_ > paint_check_mark_count_) {
                if (!swap_car(PlayerCarId::AlderPip)) {
                    fail("the mid-spray swap failed");
                    return;
                }
                if (car_visual_.respray() || renderer_.material_paintable(car_visual_.paint(scene_)) ||
                    car_visual_.paint(scene_) != car_visual_.factory_material()) {
                    fail("a car swapped in mid-spray wears paint it was never given");
                    return;
                }
                if (respray_visit_.arrived || respray_visit_.spraying()) {
                    fail("a car swap mid-spray kept the visit");
                    return;
                }
                next_phase();
            } else if (in_phase > 80) {
                fail("the swap test's order did not start");
            }
            return;
        default:
            if (in_phase < 240) return;
            if (respray_completions_ != paint_check_mark_completions_ || car_visual_.respray()) {
                fail("a spray completed onto the swapped-in car");
                return;
            }
            pass(4096u, "S4b swap mid-spray");
            return;
        }
    }
    case 12: {  // S10: the firetruck in bay two
        if (!drive_in(0, PlayerCarId::MunicipalFiretruck, -1.0f, always)) return;
        switch (paint_check_phase_) {
        case 3:
            if (!paint_shop_.modal()) tap(SDLK_r, in_phase % 30);
            if (!paint_shop_.modal()) {
                if (in_phase > 180) fail("the firetruck could not open the booth in bay two");
                return;
            }
            if (in_phase < 10) return;
            if (!respray_camera_active()) {
                fail("the bay camera did not take over for the firetruck");
                return;
            }
            next_phase();
            return;
        case 4:
            if (in_phase < 40) return;
            capture("firetruck-bay1", 256u);
            next_phase();
            return;
        case 5:
            tap(SDLK_ESCAPE, in_phase);
            if (in_phase < 8) return;
            if (paint_shop_.modal()) {
                fail("Esc did not close the booth");
                return;
            }
            pass(8192u, "S10 firetruck fits bay two");
            return;
        default:
            return;
        }
    }
    default:
        paint_check_done_ = true;
        if (paint_check_bits_ == kAllStageBits && paint_check_captures_ == kAllCaptureBits)
            AP_INFO("paint check PASSED: all stages, all captures");
        else
            AP_ERROR("paint check finished short: stages 0x%x of 0x%x, captures 0x%x of 0x%x",
                     paint_check_bits_, kAllStageBits, paint_check_captures_, kAllCaptureBits);
        return;
    }
}

bool App::paint_check_passed() const {
    return paint_check_done_ && !paint_check_failed_ && paint_check_bits_ == kAllStageBits &&
           paint_check_captures_ == kAllCaptureBits;
}

}  // namespace apricot

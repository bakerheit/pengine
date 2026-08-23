#include "platform/input.h"

#include <SDL.h>

#include <algorithm>

#include "core/input_shape.h"
#include "core/log.h"

namespace apricot {

namespace {

// Radians of look per pixel of mouse motion. Sensitivity lives here rather
// than in the sim so that a replay tape, which stores the resulting radians,
// reproduces the same camera regardless of the player's sensitivity setting.
constexpr float kLookRadiansPerPixel = 0.0022f;

// Radians of look per second at full stick deflection, for pad look. Chosen to
// land in the same ballpark as a mouse flick so the two do not need separate
// camera tuning downstream.
constexpr float kLookRadiansPerSecond = 3.2f;

// Longest frame delta the analogue ramps will honour. After a stall — a
// breakpoint, a shader compile, a window drag — the true delta can be seconds,
// and honouring it slams every held axis to full lock on the resume frame.
// The car then leaves the road for reasons the player was not present for.
// Clamping means the ramps simply resume; sim time is FixedStep's problem and
// it already refuses to spiral.
constexpr float kMaxRampDt = 0.25f;

bool key_held(const std::array<bool, 512>& keys, SDL_Scancode sc) {
    const std::size_t i = static_cast<std::size_t>(sc);
    return i < keys.size() && keys[i];
}

float pad_axis(SDL_GameController* pad, SDL_GameControllerAxis axis) {
    return input::normalise_axis16(SDL_GameControllerGetAxis(pad, axis));
}

float pad_trigger(SDL_GameController* pad, SDL_GameControllerAxis axis) {
    return input::apply_deadzone(
        input::normalise_trigger16(SDL_GameControllerGetAxis(pad, axis)),
        input::kTriggerDeadzone, input::kTriggerSaturation);
}

}  // namespace

InputMapper::~InputMapper() {
    // Guarded: App destroys its members after Window::shutdown() has already
    // called SDL_Quit, and closing a handle owned by a subsystem that no longer
    // exists is not something to find out about in a crash report.
    if (pad_ && SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_GameControllerClose(pad_);
    }
    pad_ = nullptr;
    pad_instance_ = -1;
}

void InputMapper::set_button(uint32_t bit, bool down) {
    // The real latch lives in core so a headless suite can drive it — see
    // tests/input_latch_tests.cpp. This is a one-line forward on purpose: a
    // second copy of the rule here is a second copy that can drift.
    input::latch_button(frame_, bit, down);
}

// --- mouse capture -----------------------------------------------------------

void InputMapper::apply_relative_mouse(bool on) {
    if (SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) != 0) {
        // Not fatal. Some environments (a remote session, a locked-down
        // desktop) refuse relative mode; the game is still playable with an
        // absolute cursor, just without mouse look.
        AP_WARN("relative mouse mode unavailable: %s", SDL_GetError());
    }
}

void InputMapper::set_mouse_look(bool on) {
    if (mouse_look_ == on) return;
    mouse_look_ = on;
    apply_relative_mouse(on);

    // Entering or leaving relative mode warps the cursor, and the warp arrives
    // as a single enormous motion delta. Fed to look_dx it whips the camera
    // through a full turn on the frame you click. Drop the deltas either side
    // of the transition, including anything already accumulated this frame.
    discard_motion_frames_ = 2;
    frame_.look_dx = 0.0f;
    frame_.look_dy = 0.0f;
}

// --- gamepad -----------------------------------------------------------------

void InputMapper::open_gamepad(int device_index) {
    if (pad_) return;  // one seat, one pad

    SDL_GameController* pad = SDL_GameControllerOpen(device_index);
    if (!pad) {
        AP_WARN("gamepad %d could not be opened: %s", device_index,
                SDL_GetError());
        return;
    }

    SDL_Joystick* js = SDL_GameControllerGetJoystick(pad);
    pad_ = pad;
    pad_instance_ = js ? SDL_JoystickInstanceID(js) : -1;
    AP_INFO("gamepad connected: %s (instance %d)",
            SDL_GameControllerName(pad) ? SDL_GameControllerName(pad)
                                        : "unnamed",
            pad_instance_);
}

void InputMapper::close_gamepad(int32_t instance_id) {
    if (!pad_ || instance_id != pad_instance_) return;

    SDL_GameControllerClose(pad_);
    pad_ = nullptr;
    pad_instance_ = -1;
    AP_INFO("gamepad disconnected (instance %d)", instance_id);

    // Buttons held on a pad that has just been yanked out never send their
    // release. Clearing the level mask stops the car driving itself into a
    // tree with nobody holding the controller. The latched EDGE mask is left
    // alone: those presses genuinely happened and the sim has not consumed
    // them yet.
    frame_.held = 0u;
}

// --- frame lifecycle ---------------------------------------------------------

void InputMapper::begin_frame() {
    // Look deltas are per-frame quantities and reset here. `held` and
    // `pressed` are NOT touched: held is level state owned by the events, and
    // pressed is latched until a sim step consumes it.
    frame_.look_dx = 0.0f;
    frame_.look_dy = 0.0f;
}

void InputMapper::handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            quit_ = true;
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = (e.type == SDL_KEYDOWN);
            const std::size_t sc = static_cast<std::size_t>(e.key.keysym.scancode);
            if (sc < kKeyCount) key_down_[sc] = down;

            // --- Esc: release the capture, or quit ---------------------------
            // Handled BEFORE anything else and returned from, so the keystroke
            // that gives the cursor back is not ALSO read as a quit, a pause,
            // or a latched edge. The matching key-up is swallowed too — a
            // release that fires a game action is the same bug arriving 30 ms
            // later, and much harder to see.
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                if (down) {
                    if (mouse_look_) {
                        set_mouse_look(false);
                        swallow_escape_ = true;
                    } else {
                        quit_ = true;
                    }
                } else if (swallow_escape_) {
                    swallow_escape_ = false;
                }
                return;
            }

            if (down) {
                const Uint16 mod = e.key.keysym.mod;
                // Cmd+Q as well as Ctrl+Q: on macOS, Cmd+Q is the quit
                // gesture users' hands already know, and a game that ignores
                // it feels broken rather than principled.
                const bool quit_combo =
                    (e.key.keysym.sym == SDLK_q) &&
                    ((mod & (KMOD_CTRL | KMOD_GUI)) != 0);
                if (quit_combo) quit_ = true;
            }

            switch (e.key.keysym.sym) {
                case SDLK_r:         set_button(kBtnRespawn, down); break;
                case SDLK_c:         set_button(kBtnCamCycle, down); break;
                case SDLK_p:         set_button(kBtnPause, down); break;
                case SDLK_b:         set_button(kBtnLookBack, down); break;
                case SDLK_BACKSPACE: set_button(kBtnBack, down); break;
                case SDLK_LSHIFT:    set_button(kBtnShiftUp, down); break;
                case SDLK_LCTRL:     set_button(kBtnShiftDown, down); break;
                case SDLK_RETURN:    set_button(kBtnAccept, down); break;
                default: break;
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
            // A click while the cursor is free CAPTURES it, and is consumed
            // doing so. Delivering it onward as well would mean the click that
            // grabs the mouse also fires whatever the newly captured mode has
            // bound to that button — the player pushes a button they could not
            // yet see the effect of.
            if (e.button.button == SDL_BUTTON_LEFT && !mouse_look_) {
                set_mouse_look(true);
                return;
            }
            break;

        case SDL_MOUSEMOTION:
            if (mouse_look_ && discard_motion_frames_ == 0) {
                frame_.look_dx +=
                    static_cast<float>(e.motion.xrel) * kLookRadiansPerPixel;
                frame_.look_dy +=
                    static_cast<float>(e.motion.yrel) * kLookRadiansPerPixel;
            }
            break;

        // --- gamepad hotplug -------------------------------------------------
        // NOTE the asymmetry, which is a genuine trap: `which` is a DEVICE
        // INDEX on ADDED and an INSTANCE ID on REMOVED. They are different
        // numbering schemes, they agree for the first pad plugged in, and they
        // stop agreeing the moment a second one is unplugged.
        case SDL_CONTROLLERDEVICEADDED:
            open_gamepad(e.cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            close_gamepad(e.cdevice.which);
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            if (e.cbutton.which != pad_instance_) break;
            const bool down = (e.type == SDL_CONTROLLERBUTTONDOWN);

            // The SAME BITS the keyboard sets. This is the point of the whole
            // exercise: downstream, and in the replay tape, there is no way to
            // tell which device produced the frame.
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                    // Also the handbrake while driving — polled as a level in
                    // end_frame(), because it feeds a ramp rather than an edge.
                    set_button(kBtnAccept, down);
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    set_button(kBtnBack, down);
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                    set_button(kBtnRespawn, down);
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    set_button(kBtnCamCycle, down);
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    set_button(kBtnPause, down);
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    set_button(kBtnShiftUp, down);
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    set_button(kBtnShiftDown, down);
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
                    set_button(kBtnLookBack, down);
                    break;
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

void InputMapper::end_frame() {
    // Self-timed, so a caller with no delta to hand can use this unchanged.
    // Reading a clock HERE is legitimate — this is the host layer, and what
    // reaches the sim is the resulting axis value, not the time it was
    // measured from. Replay never comes back through this class.
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();

    float dt = 0.0f;  // first frame owes no ramp movement
    if (last_tick_ != 0 && freq != 0 && now > last_tick_) {
        dt = static_cast<float>(static_cast<double>(now - last_tick_) /
                                static_cast<double>(freq));
    }
    last_tick_ = now;
    end_frame(dt);
}

void InputMapper::end_frame(float dt_seconds) {
    using namespace apricot::input;

    if (discard_motion_frames_ > 0) --discard_motion_frames_;

    const float dt = std::clamp(dt_seconds, 0.0f, kMaxRampDt);

    // --- digital sources, ramped ---------------------------------------------
    // A key is a switch, and a switch driven straight into an axis gives the
    // keyboard player instant full lock. That is not "responsive"; it is a
    // different and better car, and it cannot be balanced against a stick
    // without making one of the two inputs pointless. Ramping is what makes
    // keyboard and pad comparable.
    const float steer_left = key_held(key_down_, SDL_SCANCODE_A) ? 1.0f : 0.0f;
    const float steer_right = key_held(key_down_, SDL_SCANCODE_D) ? 1.0f : 0.0f;
    const float steer_target = std::clamp(steer_right - steer_left, -1.0f, 1.0f);

    key_steer_ = ramp_toward(key_steer_, steer_target, dt, kSteerRiseSeconds,
                             kSteerFallSeconds);
    key_throttle_ = ramp_toward(
        key_throttle_, key_held(key_down_, SDL_SCANCODE_W) ? 1.0f : 0.0f, dt,
        kPedalRiseSeconds, kPedalFallSeconds);
    key_brake_ = ramp_toward(
        key_brake_, key_held(key_down_, SDL_SCANCODE_S) ? 1.0f : 0.0f, dt,
        kPedalRiseSeconds, kPedalFallSeconds);

    float steer = key_steer_;
    float throttle = key_throttle_;
    float brake = key_brake_;

    // The handbrake has no analogue source on any device, so key and pad
    // button feed one shared ramp.
    bool handbrake_down = key_held(key_down_, SDL_SCANCODE_SPACE);

    // --- analogue sources, straight through ----------------------------------
    if (pad_) {
        if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_A) != 0) {
            handbrake_down = true;
        }

        // RADIAL deadzone over both stick axes, not a per-axis one. See
        // core/input_shape.h — a per-axis threshold carves a square hole out
        // of a round stick and makes small steering corrections twitch.
        const glm::vec2 stick{pad_axis(pad_, SDL_CONTROLLER_AXIS_LEFTX),
                              pad_axis(pad_, SDL_CONTROLLER_AXIS_LEFTY)};
        const glm::vec2 shaped =
            apply_stick_deadzone(stick, kStickDeadzone, kStickSaturation);

        const float pad_throttle =
            pad_trigger(pad_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        const float pad_brake =
            pad_trigger(pad_, SDL_CONTROLLER_AXIS_TRIGGERLEFT);

        // The pad wins ONLY where it is actually being moved. An idle pad left
        // plugged in must not pin every axis to zero and silently kill the
        // keyboard — "my keyboard stopped working when I plugged in a
        // controller" is a bug report nobody enjoys receiving.
        if (shaped.x != 0.0f) steer = shaped.x;
        if (pad_throttle > 0.0f) throttle = pad_throttle;
        if (pad_brake > 0.0f) brake = pad_brake;

        // Right stick looks around, in the same radians the mouse produces, so
        // the sim cannot tell them apart. Scaled by dt because a stick is a
        // RATE (held deflection = keep turning) while a mouse delta is a
        // DISPLACEMENT that already happened.
        const glm::vec2 look = apply_stick_deadzone(
            glm::vec2{pad_axis(pad_, SDL_CONTROLLER_AXIS_RIGHTX),
                      pad_axis(pad_, SDL_CONTROLLER_AXIS_RIGHTY)},
            kStickDeadzone, kStickSaturation);
        frame_.look_dx += look.x * kLookRadiansPerSecond * dt;
        frame_.look_dy += look.y * kLookRadiansPerSecond * dt;
    }

    handbrake_ramp_ =
        ramp_toward(handbrake_ramp_, handbrake_down ? 1.0f : 0.0f, dt,
                    kHandbrakeRiseSeconds, kHandbrakeFallSeconds);

    frame_.steer = std::clamp(steer, -1.0f, 1.0f);
    frame_.throttle = std::clamp(throttle, 0.0f, 1.0f);
    frame_.brake = std::clamp(brake, 0.0f, 1.0f);
    frame_.handbrake = std::clamp(handbrake_ramp_, 0.0f, 1.0f);
}

}  // namespace apricot

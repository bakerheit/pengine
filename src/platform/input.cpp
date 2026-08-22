#include "platform/input.h"

#include <SDL.h>

#include <algorithm>

namespace apricot {

namespace {

// Radians of look per pixel of mouse motion. Sensitivity lives here rather
// than in the sim so that a replay tape, which stores the resulting radians,
// reproduces the same camera regardless of the player's sensitivity setting.
constexpr float kLookRadiansPerPixel = 0.0022f;

bool key_held(const std::array<bool, 512>& keys, SDL_Scancode sc) {
    const std::size_t i = static_cast<std::size_t>(sc);
    return i < keys.size() && keys[i];
}

}  // namespace

void InputMapper::set_button(uint32_t bit, bool down) {
    if (down) {
        // Latch the edge only on a genuine transition. SDL repeats KEYDOWN
        // while a key is held; without this check the "pressed" mask would be
        // re-latched every repeat and a tap would be indistinguishable from
        // a hold.
        if ((frame_.held & bit) == 0u) frame_.pressed |= bit;
        frame_.held |= bit;
    } else {
        frame_.held &= ~bit;
    }
}

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

            if (down) {
                const Uint16 mod = e.key.keysym.mod;
                // Cmd+Q as well as Ctrl+Q: on macOS, Cmd+Q is the quit
                // gesture users' hands already know, and a game that ignores
                // it feels broken rather than principled.
                const bool quit_combo =
                    (e.key.keysym.sym == SDLK_q) &&
                    ((mod & (KMOD_CTRL | KMOD_GUI)) != 0);
                if (quit_combo || e.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
            }

            switch (e.key.keysym.sym) {
                case SDLK_r:      set_button(kBtnRespawn, down); break;
                case SDLK_c:      set_button(kBtnCamCycle, down); break;
                case SDLK_p:      set_button(kBtnPause, down); break;
                case SDLK_LSHIFT: set_button(kBtnShiftUp, down); break;
                case SDLK_LCTRL:  set_button(kBtnShiftDown, down); break;
                case SDLK_RETURN: set_button(kBtnAccept, down); break;
                default: break;
            }
            break;
        }

        case SDL_MOUSEMOTION:
            if (mouse_look_) {
                frame_.look_dx += static_cast<float>(e.motion.xrel) * kLookRadiansPerPixel;
                frame_.look_dy += static_cast<float>(e.motion.yrel) * kLookRadiansPerPixel;
            }
            break;

        default:
            break;
    }
}

void InputMapper::end_frame() {
    // Digital keys driving analogue axes. Full deflection with no ramp is
    // deliberate for now: smoothing belongs in the vehicle model, where it can
    // be frame-rate independent, not here where it would be baked into the
    // replay tape.
    // TODO(gamepad ticket): read real analogue axes from a controller and let
    // them override the keyboard when one is connected.
    const float steer_left  = key_held(key_down_, SDL_SCANCODE_A) ? 1.0f : 0.0f;
    const float steer_right = key_held(key_down_, SDL_SCANCODE_D) ? 1.0f : 0.0f;

    frame_.steer = std::clamp(steer_right - steer_left, -1.0f, 1.0f);
    frame_.throttle = key_held(key_down_, SDL_SCANCODE_W) ? 1.0f : 0.0f;
    frame_.brake = key_held(key_down_, SDL_SCANCODE_S) ? 1.0f : 0.0f;
    frame_.handbrake = key_held(key_down_, SDL_SCANCODE_SPACE) ? 1.0f : 0.0f;
}

}  // namespace apricot

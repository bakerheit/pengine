#include "platform/input.h"

#include <SDL.h>

namespace pengine {

void Input::begin_frame() {
    pressed_.fill(false);
    released_.fill(false);
    mouse_pressed_.fill(false);
    mouse_released_.fill(false);
    mouse_dx_ = 0.f;
    mouse_dy_ = 0.f;
    wheel_y_  = 0.f;
}

void Input::handle_event(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        if (e.key.repeat) return;
        int sc = static_cast<int>(e.key.keysym.scancode);
        if (sc < 0 || sc >= key_count) return;
        auto idx = static_cast<std::size_t>(sc);
        if (e.type == SDL_KEYDOWN) {
            if (!down_[idx]) pressed_[idx] = true;
            down_[idx] = true;
        } else {
            if (down_[idx]) released_[idx] = true;
            down_[idx] = false;
        }
    } else if (e.type == SDL_MOUSEMOTION) {
        mouse_dx_ += static_cast<float>(e.motion.xrel);
        mouse_dy_ += static_cast<float>(e.motion.yrel);
        // Absolute pixel position. SDL gives us window-relative coords with
        // origin at the top-left. PBD-031: consumed by the Map Builder cursor
        // path. Both dx/dy (relative) and x/y (absolute) are captured so the
        // gameplay relative-mouse path and the Map Builder absolute-mouse
        // path can share one Input.
        mouse_x_ = e.motion.x;
        mouse_y_ = e.motion.y;
    } else if (e.type == SDL_MOUSEWHEEL) {
        // SDL flips e.wheel.y when direction == FLIPPED; preciseY exists on
        // 2.0.18+ for finer-grained trackpad scroll. Use preciseY when non-zero,
        // fall back to integer y otherwise.
        float dy = e.wheel.preciseY != 0.f
                 ? e.wheel.preciseY
                 : static_cast<float>(e.wheel.y);
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
        wheel_y_ += dy;
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        int btn = static_cast<int>(e.button.button);
        if (btn < 0 || btn >= mouse_button_count) return;
        auto idx = static_cast<std::size_t>(btn);
        if (e.type == SDL_MOUSEBUTTONDOWN) {
            if (!mouse_down_[idx]) mouse_pressed_[idx] = true;
            mouse_down_[idx] = true;
        } else {
            if (mouse_down_[idx]) mouse_released_[idx] = true;
            mouse_down_[idx] = false;
        }
    }
}

} // namespace pengine

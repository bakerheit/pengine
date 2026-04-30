#include "platform/input.h"

#include <SDL.h>

namespace pengine {

void Input::begin_frame() {
    pressed_.fill(false);
    released_.fill(false);
    mouse_dx_ = 0.f;
    mouse_dy_ = 0.f;
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
    }
}

} // namespace pengine

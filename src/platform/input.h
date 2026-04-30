#pragma once

#include <array>
#include <cstdint>

union SDL_Event;

namespace pengine {

// Phase 0 keyboard layer: edge detection + held state. Mouse and gamepad
// arrive in Phase 1 / Phase 7.
class Input {
public:
    static constexpr int key_count = 512; // SDL_NUM_SCANCODES is 512

    void begin_frame();
    void handle_event(const SDL_Event& e);

    bool down(int scancode) const { return scancode >= 0 && scancode < key_count && down_[static_cast<std::size_t>(scancode)]; }
    bool pressed(int scancode) const { return scancode >= 0 && scancode < key_count && pressed_[static_cast<std::size_t>(scancode)]; }
    bool released(int scancode) const { return scancode >= 0 && scancode < key_count && released_[static_cast<std::size_t>(scancode)]; }

private:
    std::array<bool, key_count> down_{};
    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> released_{};
};

} // namespace pengine

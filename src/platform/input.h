#pragma once

#include <array>
#include <cstdint>

union SDL_Event;

namespace pengine {

class Input {
public:
    static constexpr int key_count = 512;

    void begin_frame();
    void handle_event(const SDL_Event& e);

    bool down(int scancode)     const { return scancode >= 0 && scancode < key_count && down_[static_cast<std::size_t>(scancode)]; }
    bool pressed(int scancode)  const { return scancode >= 0 && scancode < key_count && pressed_[static_cast<std::size_t>(scancode)]; }
    bool released(int scancode) const { return scancode >= 0 && scancode < key_count && released_[static_cast<std::size_t>(scancode)]; }

    static constexpr int mouse_button_count = 8;

    bool mouse_down(int button)     const { return button >= 0 && button < mouse_button_count && mouse_down_[static_cast<std::size_t>(button)]; }
    bool mouse_pressed(int button)  const { return button >= 0 && button < mouse_button_count && mouse_pressed_[static_cast<std::size_t>(button)]; }
    bool mouse_released(int button) const { return button >= 0 && button < mouse_button_count && mouse_released_[static_cast<std::size_t>(button)]; }

    float mouse_dx() const { return mouse_dx_; }
    float mouse_dy() const { return mouse_dy_; }

private:
    std::array<bool, key_count> down_{};
    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> released_{};

    std::array<bool, mouse_button_count> mouse_down_{};
    std::array<bool, mouse_button_count> mouse_pressed_{};
    std::array<bool, mouse_button_count> mouse_released_{};

    float mouse_dx_ = 0.f;
    float mouse_dy_ = 0.f;
};

} // namespace pengine

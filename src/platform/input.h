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

    // Absolute mouse position in window pixels (SDL coords: 0,0 = top-left).
    // Updated on SDL_MOUSEMOTION; stale-but-valid between motion events.
    // PBD-031: consumed by Map Builder for mouse-position cursor / placement
    // unprojection. Captured in addition to dx/dy (which only survive while
    // relative mouse mode is active in gameplay paths) because the Map Builder
    // never enters relative mode — it needs the absolute window pixel to
    // build a ray from the camera through the cursor.
    int mouse_x() const { return mouse_x_; }
    int mouse_y() const { return mouse_y_; }

    // Mouse wheel: accumulated y ticks this frame (+up / -down, in SDL's
    // "normal" direction). Cleared each begin_frame() like dx/dy.
    float wheel_y() const { return wheel_y_; }

private:
    std::array<bool, key_count> down_{};
    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> released_{};

    std::array<bool, mouse_button_count> mouse_down_{};
    std::array<bool, mouse_button_count> mouse_pressed_{};
    std::array<bool, mouse_button_count> mouse_released_{};

    float mouse_dx_ = 0.f;
    float mouse_dy_ = 0.f;
    float wheel_y_  = 0.f;
    int   mouse_x_  = 0;
    int   mouse_y_  = 0;
};

} // namespace pengine

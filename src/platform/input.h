#pragma once

#include <array>
#include <cstddef>

#include "core/input_frame.h"

union SDL_Event;

namespace apricot {

// Turns raw device events into a core::InputFrame. This is the ONLY place in
// the engine that knows what a keyboard is; everything downstream sees
// normalised axes and a bitmask.
//
// The ordering contract, which exists to protect latched edges:
//
//     mapper.begin_frame();                 // clears look deltas ONLY
//     while (poll(&e)) mapper.handle_event(e);
//     mapper.end_frame();                   // bakes held keys into axes
//
//     for (int i = 0; i < tick.steps; ++i) step(mapper.frame(), kSimDt);
//     if (tick.steps > 0) mapper.consume_edges();   // <-- note the guard
//
// consume_edges() is called on STEP COUNT, never per frame. A render frame can
// owe zero sim steps, and clearing edges on such a frame drops the press
// entirely. That bug is intermittent, frame-rate dependent, and reads to a
// player as "the button doesn't always work" — which is why the clearing is a
// separate explicit call rather than something begin_frame() does quietly.
class InputMapper {
public:
    // Start a new render frame. Clears the per-frame look deltas. Deliberately
    // does NOT clear latched edges — see above.
    void begin_frame();

    void handle_event(const SDL_Event& e);

    // Call after the event pump. Bakes currently-held keys into the analogue
    // axes, so the axes reflect the state at the END of the pump rather than
    // whatever it happened to be mid-queue.
    void end_frame();

    const InputFrame& frame() const { return frame_; }

    // Clear the latched pressed-edge mask. Call ONCE per frame in which at
    // least one sim step ran, after the last step.
    void consume_edges() { clear_edges(frame_); }

    // True once the user has asked to quit: window close, Esc, or Ctrl/Cmd+Q.
    // Latched — it never un-sets.
    bool quit_requested() const { return quit_; }

    // Mouse look is only accumulated while this is on, so moving the cursor
    // over a menu does not swing the camera.
    void set_mouse_look(bool on) { mouse_look_ = on; }
    bool mouse_look() const { return mouse_look_; }

private:
    static constexpr std::size_t kKeyCount = 512;

    void set_button(uint32_t bit, bool down);

    InputFrame frame_{};
    std::array<bool, kKeyCount> key_down_{};
    bool quit_ = false;
    bool mouse_look_ = false;
};

}  // namespace apricot

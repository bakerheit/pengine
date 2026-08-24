#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/input_frame.h"

union SDL_Event;
struct _SDL_GameController;

namespace apricot {

// Turns raw device events into a core::InputFrame. This is the ONLY place in
// the engine that knows what a keyboard or a pad is; everything downstream
// sees normalised axes and a bitmask.
//
// DEVICE-AGNOSTIC BY CONSTRUCTION. A pad and a keyboard fold into the SAME
// InputFrame fields, so a replay tape has no idea which produced it and a run
// recorded on a pad plays back identically on a machine with no pad attached.
// That is the reason gamepad support lives here and not in a parallel path:
// two input paths means two tape formats means, eventually, one of them
// desyncs.
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
    InputMapper() = default;
    ~InputMapper();

    // Holds an open device handle, so copying it would close that handle
    // twice.
    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;

    // Start a new render frame. Clears the per-frame look deltas. Deliberately
    // does NOT clear latched edges — see above.
    void begin_frame();

    void handle_event(const SDL_Event& e);

    // Call after the event pump. Bakes device state into the analogue axes, so
    // the axes reflect the state at the END of the pump rather than whatever
    // it happened to be mid-queue.
    //
    // The no-argument form measures its own elapsed time, which is what makes
    // it a drop-in for callers that have no delta to hand. Ramps are wall-clock
    // driven, and that is safe for replay: the tape stores the RESULTING axis
    // values, and playback feeds those straight to the sim without ever coming
    // back through this class.
    void end_frame();
    void end_frame(float dt_seconds);

    const InputFrame& frame() const { return frame_; }

    // Clear the latched pressed-edge mask. Call ONCE per frame in which at
    // least one sim step ran, after the last step.
    void consume_edges() { clear_edges(frame_); }

    // True once the user has asked to quit: window close, Ctrl/Cmd+Q, or Esc
    // while the mouse is NOT captured. Latched — it never un-sets.
    bool quit_requested() const { return quit_; }

    // --- mouse capture -------------------------------------------------------
    //
    // Click to capture, Esc to release. While captured the cursor is hidden and
    // motion accumulates into look_dx/look_dy; while released, moving the
    // cursor over a menu does not swing the camera.
    //
    // BOTH TRANSITIONS SWALLOW THEIR OWN INPUT. The click that captures is not
    // also delivered as a game click, and the Esc that releases is not also
    // read as quit, as pause, or as a latched edge — its key-up is swallowed
    // too. An input that both changes the input mode AND fires in the new mode
    // is how "Esc released the mouse and also quit the game" happens, and it
    // only happens to the player, never to the person who wrote it.
    void set_mouse_look(bool on);
    bool mouse_look() const { return mouse_look_; }

    // True while a pad is attached and open. Diagnostics only — never branch
    // gameplay on it, or the tape stops being device-agnostic.
    bool gamepad_connected() const { return pad_ != nullptr; }

private:
    static constexpr std::size_t kKeyCount = 512;

    void set_button(uint32_t bit, bool down);
    void open_gamepad(int device_index);
    void close_gamepad(int32_t instance_id);
    void apply_relative_mouse(bool on);

    InputFrame frame_{};
    std::array<bool, kKeyCount> key_down_{};
    bool quit_ = false;
    bool mouse_look_ = false;

    // Set when Esc released the capture, so the matching key-up is ignored
    // rather than read as a fresh game input.
    bool swallow_escape_ = false;

    // A capture transition warps the cursor, and the warp arrives as one huge
    // motion delta. Counted down over a couple of frames so that delta never
    // reaches look_dx and whips the camera round.
    int discard_motion_frames_ = 0;

    // The active pad, or null. One pad: this is a single-seat game, and
    // a second attached controller silently fighting the first for the
    // steering axis is worse than ignoring it.
    _SDL_GameController* pad_ = nullptr;
    int32_t pad_instance_ = -1;

    // Ramp state for axes synthesised from digital sources. Held across frames
    // because a ramp is, by definition, the thing a single frame cannot
    // compute on its own.
    //
    // The first three are the KEYBOARD's synthesised axes; a pad's real
    // analogue readings override them when the pad is actually being moved.
    // The handbrake ramp is SHARED, because no controller on earth has an
    // analogue handbrake — key and pad button both feed the same ramp.
    float key_steer_ = 0.0f;
    float key_throttle_ = 0.0f;
    float key_brake_ = 0.0f;
    float handbrake_ramp_ = 0.0f;

    // Performance-counter reading at the last end_frame(), for the
    // self-timing overload. 0 means "no previous frame", which yields a zero
    // delta and therefore no ramp movement — a safe first frame.
    uint64_t last_tick_ = 0;
};

}  // namespace apricot

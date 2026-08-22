#pragma once

namespace apricot {

class Window;

// The debug overlay. Wraps the immediate-mode UI backends so their headers —
// and their third-party warning profile — stay inside this one translation
// unit instead of leaking into every file that wants to print a number.
namespace overlay {

// Create the UI context and backends. Call once, after the GL context exists.
// Returns false on failure; the app must still run without an overlay.
bool init(Window& window);
void shutdown();

// Forward EVERY event here before the app handles it. Returns true when the UI
// wants to consume it. Takes void* to keep the platform event type out of this
// header; pass the address of the event.
bool process_event(const void* sdl_event);

struct Stats {
    double fps = 0.0;
    double frame_ms = 0.0;

    // Sim steps the current frame owed. Shown because it is the single most
    // useful number for spotting a frame-pacing problem: a healthy build
    // alternates between small values, and a sick one pins at the clamp.
    int sim_steps = 0;
    bool step_clamped = false;
    double alpha = 0.0;

    unsigned long long sim_step_index = 0;
};

// Build and render the overlay. Call after the 3D pass, before the swap.
void draw(Window& window, const Stats& stats);

}  // namespace overlay
}  // namespace apricot

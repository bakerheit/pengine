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

    // --- renderer ----------------------------------------------------------
    // draw_calls against instances is the whole point of the A/B toggle: with
    // batching on the two numbers should be wildly different, and if they are
    // not, the batching is not happening and the cull sort is the first
    // suspect.
    int scene_nodes = 0;
    int visible_nodes = 0;
    int batches = 0;
    int instanced_batches = 0;
    int draw_calls = 0;
    int instances = 0;
    int largest_run = 0;
    unsigned int skipped_binds = 0;

    int hud_quads = 0;
    int hud_draw_calls = 0;
    int rain_drops = 0;
    int rain_quads = 0;

    float time_of_day = 0.5f;

    // GL errors drained since startup. Anything above zero means a call failed
    // and the frame you are looking at is not the frame that was asked for.
    int gl_errors = 0;
};

// The knobs the overlay OWNS. Passed by non-const reference: the UI writes
// straight into it and the app reads it back the same frame.
//
// None of this reaches InputFrame, and it must not. InputFrame is the replay
// tape format — a debug toggle recorded into a tape would change what a replay
// does, and a replay that disagrees with the run it recorded is worse than no
// replay at all.
struct Controls {
    bool instancing = true;

    // Weather, layered onto the sky env. Each is an exact no-op at zero.
    float rain = 0.0f;
    float overcast = 0.0f;
    float fog = 0.0f;

    // Multiplier on `game/conditions.h`'s `kSecondsPerDay`. Zero freezes the sky.
    //
    // Defaults to 5 so the demo runs a 240 s day against the game's 1200 s one:
    // short enough that the sun visibly moves while you watch, which is the only
    // way to tell the sky pass is live rather than a painted backdrop. A real
    // session wants 1.
    float sky_speed = 5.0f;
};

// Build and render the overlay. Call after the 3D pass, before the swap.
void draw(Window& window, const Stats& stats, Controls& controls);

}  // namespace overlay
}  // namespace apricot

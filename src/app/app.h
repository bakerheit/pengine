#pragma once

#include <cstdint>

#include "core/fixed_step.h"
#include "game/rally.h"
#include "gfx/camera.h"
#include "physics/terrain_collider.h"
#include "platform/input.h"
#include "platform/window.h"

namespace apricot {

// The application shell: it owns the window, the frame loop and the overlay,
// and it owns the ONE wall clock in the program.
//
// That last point is the design, not an accident. FixedStep is fed a measured
// delta from here and the sim is handed a constant dt, so nothing below this
// class can observe real time. It is what makes a replay reproduce a run
// rather than approximate it.
//
// App holds wiring only. Any logic that appears here should be moved into a
// sim module where a headless test can reach it.
class App {
public:
    bool init();
    void shutdown();

    // Runs until the user quits. Returns a process exit code.
    int run();

private:
    void poll_events();
    void render();

    Window window_;
    InputMapper input_;
    FixedStep clock_;
    Camera camera_;

    // The run seed. Fixed for now so every launch is the same world, which is
    // what you want while the renderer is being built.
    // TODO(session ticket): take it from the command line / save file. It must
    // never come from the clock — see core/rng.h.
    uint64_t seed_ = 0xA5EED0FFC0FFEE11ull;

    TerrainCollider collider_{0};
    RallyState rally_;

    bool running_ = false;

    // Frame stats, smoothed for display only. Never fed back into the sim.
    double fps_ = 0.0;
    double frame_ms_ = 0.0;
    int last_steps_ = 0;
    bool last_clamped_ = false;
};

}  // namespace apricot

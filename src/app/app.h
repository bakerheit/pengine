#pragma once

#include <cstdint>

#include "app/overlay.h"
#include "app/world.h"
#include "core/fixed_step.h"
#include "game/conditions.h"
#include "gfx/camera.h"
#include "gfx/hud.h"
#include "gfx/precip.h"
#include "gfx/renderer.h"
#include "gfx/sky.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "platform/input.h"
#include "platform/window.h"
#include "scene/scene.h"

namespace apricot {

// The application shell: it owns the window, the frame loop and the overlay,
// and it owns the ONE wall clock in the program.
//
// That last point is the design, not an accident. FixedStep is fed a measured
// delta from here and the sim is handed a constant dt, so nothing below this
// class can observe real time. It is what makes a replay reproduce a run
// rather than approximate it. The SKY obeys the same rule: its time of day and
// its cloud drift are driven by the sim step index, not by the wall clock, so
// a replayed run gets the same sky it was recorded under.
//
// App holds wiring only. Any logic that appears here should be moved into a
// sim module where a headless test can reach it.
class App {
public:
    bool init();
    void shutdown();

    // Runs until the user quits. Returns a process exit code.
    int run();

    // --- dev options, set from the command line before init() ---------------

    // Quit after this many rendered frames. 0 = run until the user quits. It
    // exists so the binary can be exercised non-interactively: render a fixed
    // number of frames, report the GL error state and the draw counts, exit.
    void set_frame_limit(int frames) { frame_limit_ = frames; }

    // Start on the naive per-node path instead of the batched one, so the A/B
    // can be measured without a human clicking a checkbox.
    void set_instancing(bool on) { controls_.instancing = on; }

    // Teleport across the island every N frames. 0 = never.
    //
    // Same reasoning as --frames, applied to the hardest thing the streamer
    // does: a warp evicts the whole resident world and refills it somewhere
    // else. It is exactly the path that only gets tested when somebody
    // remembers to press the key, which is to say the path that ships broken.
    void set_warp_interval(int frames) { warp_interval_ = frames; }

private:
    void poll_events();
    void render();
    void update_camera();

    // Drain and log the GL error queue. Returns how many were found.
    int drain_gl_errors(const char* where);

    Window window_;
    InputMapper input_;
    FixedStep clock_;
    Camera camera_;

    // The RUN seed: session identity, not world identity.
    //
    // Since PENG-41 those are two different things. The terrain, and therefore
    // Pinatty itself, is keyed on city::kMapSeed, which is pinned in the map
    // tables — the city is an authored place and does not reroll. This seed
    // carries what is ALLOWED to differ between sessions: weather, ambient
    // variation, mission shuffles, and today the placeholder box field.
    //
    // Fixed for now so every launch is the same session, which is what you
    // want while the renderer is being built.
    // TODO(session ticket): take it from the command line / save file. It must
    // never come from the clock — see core/rng.h.
    uint64_t seed_ = 0xA5EED0FFC0FFEE11ull;

    TerrainCollider collider_{0};

    // The drive. There is no game layer between this and the physics yet — the
    // rally that used to sit here is gone (PENG-23) and Pinatty
    // (docs/design/pinatty.md) has no code. So App steps the vehicle directly,
    // which is exactly as much game as this build has.
    VehicleTuning tuning_;
    VehicleState car_;

    // The car one sim step ago. Render interpolates between this and the
    // current state by clock_.alpha(), which is what stops a 120 Hz sim from
    // visibly juddering on a 144 Hz panel.
    VehicleState prev_car_;

    // Sim steps elapsed. The authoritative sim clock, and the key the weather
    // is drawn from — conditions_at() is a pure function of (seed, step), so
    // the sky and the grip agree with each other on every machine.
    uint64_t step_index_ = 0;
    Conditions conditions_;

    Scene scene_;
    Renderer renderer_;
    Sky sky_;
    Precipitation rain_;
    Hud hud_;
    World world_;

    // The player's car, drawn as a box.
    //
    // The MODEL is the placeholder here, not the vehicle: physics/vehicle.cpp
    // is 910 lines of real car. There is no model loader and no asset on disk,
    // so until one exists the thing you steer is a red box, and saying so is
    // better than a comment implying a car is coming.
    MeshId car_mesh_ = kInvalidId;
    MaterialId car_material_ = kInvalidId;
    NodeId car_node_ = kInvalidId;
    glm::vec3 car_half_{0.9f, 0.6f, 2.0f};

    // Teleport the car across the island and refill before resuming. Bound to a
    // key so the cold-fill path is exercisable by hand, because it is the path
    // a mission warp will take and it must not be first run in anger.
    void teleport(glm::vec3 to);
    bool teleport_requested_ = false;
    int warp_interval_ = 0;
    int warps_done_ = 0;

    // What the last fill cost, in wall milliseconds. App owns the only clock in
    // the program, so this is measured here and not inside World.
    double last_fill_ms_ = 0.0;
    int last_fill_steps_ = 0;

    // Frame costs, measured here for the same reason. Display only; neither
    // ever reaches the sim, which sees a constant dt and nothing else.
    double cull_ms_ = 0.0;
    double mesh_ms_ = 0.0;
    double peak_cull_ms_ = 0.0;
    double peak_mesh_ms_ = 0.0;
    int stream_spikes_ = 0;

    overlay::Controls controls_;

    bool running_ = false;
    int frame_limit_ = 0;
    int frames_rendered_ = 0;
    int gl_errors_ = 0;

    // Frame stats, smoothed for display only. Never fed back into the sim.
    double fps_ = 0.0;
    double frame_ms_ = 0.0;

    // Raw totals for the end-of-session summary. fps_ and frame_ms_ above are
    // smoothed for the overlay and are the wrong thing to quote in a report.
    double frame_ms_total_ = 0.0;
    double worst_frame_ms_ = 0.0;
    int frames_timed_ = 0;
    int last_steps_ = 0;
    bool last_clamped_ = false;

    // Last frame's renderer stats, forwarded to the overlay and the exit
    // summary.
    Renderer::Stats render_stats_;
};

}  // namespace apricot

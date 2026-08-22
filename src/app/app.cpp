#include "app/app.h"

#include <SDL.h>
#include <glad/gl.h>

#include <chrono>

#include "app/overlay.h"
#include "core/log.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

// Clear colour. A flat sky is a placeholder for an actual sky pass, but it is
// a deliberately distinctive one: a black clear is indistinguishable from a
// window that failed to present anything at all.
constexpr float kSkyR = 0.42f;
constexpr float kSkyG = 0.62f;
constexpr float kSkyB = 0.86f;

using WallClock = std::chrono::steady_clock;

}  // namespace

bool App::init() {
    AP_INFO("apricot %s starting", APRICOT_VERSION);

    WindowConfig cfg;
    cfg.title = std::string("apricot ") + APRICOT_VERSION;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = true;

    if (!window_.init(cfg)) {
        AP_ERROR("window init failed; cannot continue");
        return false;
    }

    // The overlay is optional. Losing it must not lose the app.
    if (!overlay::init(window_)) {
        AP_WARN("overlay unavailable; continuing without it");
    }

    collider_ = TerrainCollider(seed_);
    rally_ = RallyState{};
    rally_.route = build_route(seed_, collider_, 8);

    // Park the car on the terrain at the origin rather than dropping it from
    // an arbitrary height, so the first second of the sim is not a fall.
    rally_.car.position =
        glm::vec3{0.0f, collider_.height(0.0f, 0.0f) + 1.0f, 0.0f};

    camera_.position = rally_.car.position + glm::vec3{0.0f, 3.0f, 8.0f};
    camera_.aspect = static_cast<float>(window_.width()) /
                     static_cast<float>(window_.height() > 0 ? window_.height() : 1);

    AP_INFO("seed 0x%016llX, %zu checkpoints, ground at origin %.2f m",
            static_cast<unsigned long long>(seed_),
            rally_.route.checkpoints.size(), static_cast<double>(rally_.car.position.y));

    running_ = true;
    return true;
}

void App::shutdown() {
    overlay::shutdown();
    window_.shutdown();
    running_ = false;
}

void App::poll_events() {
    input_.begin_frame();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        // The UI sees every event first so its own state stays coherent.
        overlay::process_event(&e);

        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            int w = 0, h = 0;
            SDL_GL_GetDrawableSize(window_.sdl(), &w, &h);
            window_.on_resize(w, h);
            camera_.aspect =
                static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1);
        }

        input_.handle_event(e);
    }

    input_.end_frame();
}

void App::render() {
    glViewport(0, 0, window_.width(), window_.height());
    glClearColor(kSkyR, kSkyG, kSkyB, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    // TODO(renderer ticket): terrain and vehicle passes go here, between the
    // clear and the overlay.

    overlay::Stats stats;
    stats.fps = fps_;
    stats.frame_ms = frame_ms_;
    stats.sim_steps = last_steps_;
    stats.step_clamped = last_clamped_;
    stats.alpha = clock_.alpha();
    stats.sim_step_index = static_cast<unsigned long long>(rally_.step_index);
    overlay::draw(window_, stats);

    window_.swap();
}

int App::run() {
    if (!running_) return 1;

    WallClock::time_point last = WallClock::now();

    // Smoothing factor for the displayed frame rate. Display only: this value
    // never reaches the sim, which sees the raw delta.
    constexpr double kFpsSmoothing = 0.08;

    // The FIRST presented frame also pays for one-time GPU work — the overlay
    // backend compiling its shaders and uploading its font atlas, the driver
    // settling the swap chain. On this machine that lands around 100 ms, which
    // is over the step clamp, so without this the app printed a "dropped sim
    // time" warning on every single launch. A warning that always fires is a
    // warning everyone learns to ignore, and then it cannot do its job on the
    // day something is genuinely wrong. Load time is not frame time: measure
    // from after the first present.
    bool first_frame = true;

    while (!input_.quit_requested()) {
        const WallClock::time_point now = WallClock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        frame_ms_ = frame_ms_ + (dt * 1000.0 - frame_ms_) * kFpsSmoothing;
        if (dt > 0.0) {
            fps_ = fps_ + (1.0 / dt - fps_) * kFpsSmoothing;
        }

        poll_events();

        const FixedStep::Tick tick = clock_.advance(dt);
        last_steps_ = tick.steps;
        last_clamped_ = tick.clamped;
        if (tick.clamped) {
            AP_WARN("frame owed more than %d sim steps; dropped the surplus",
                    kMaxStepsPerFrame);
        }

        for (int i = 0; i < tick.steps; ++i) {
            step_rally(rally_, input_.frame(), collider_,
                       static_cast<float>(kSimDt));
        }

        // Consume latched edges ONLY when a step actually ran. On a zero-step
        // frame the edges stay latched for the next one. Moving this out of
        // the guard silently drops presses at high frame rates.
        if (tick.steps > 0) input_.consume_edges();

        render();

        if (first_frame) {
            first_frame = false;
            clock_.reset();
            last = WallClock::now();
        }
    }

    AP_INFO("quit after %llu sim steps (%.2f s of sim time)",
            static_cast<unsigned long long>(rally_.step_index),
            rally_.timing.total_time);
    return 0;
}

}  // namespace apricot

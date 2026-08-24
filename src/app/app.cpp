#include "app/app.h"

#include <SDL.h>
#include <glad/gl.h>

#include <chrono>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "app/overlay.h"
#include "core/log.h"
#include "gfx/gl_state.h"
#include "gfx/sky_env.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

using WallClock = std::chrono::steady_clock;

// Placeholder world size. Deleted along with demo_scene when real terrain
// lands.
constexpr float kFieldRadius = 420.0f;
constexpr int kBoxCount = 1400;

// How far the renderer draws. The fog band is set just inside it so the
// distance cull's cutoff hides behind atmosphere instead of popping.
constexpr float kRenderDistance = 700.0f;

// Day length is NOT defined here. `game/conditions.h` owns it, because the same
// number drives weather, grip and the light the sky pass computes — and two
// copies of one constant are two numbers that eventually disagree. This file
// used to declare its own 240 s day; when the conditions system landed the two
// collided at the merge, which is the cheap way to find out.
//
// The demo wants a faster sun than a real session does, so `sky_speed` carries
// that instead. See its default in `overlay.h`.

// Chase camera placement, in the car's own frame.
constexpr float kCamBack = 9.0f;
constexpr float kCamUp = 3.6f;
constexpr float kCamLookAhead = 6.0f;

const char* gl_error_name(GLenum e) {
    switch (e) {
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_<unknown>";
    }
}

}  // namespace

bool App::init() {
    AP_INFO("apricot %s starting", APRICOT_VERSION);

    WindowConfig cfg;
    cfg.title = std::string("apricot ") + APRICOT_VERSION;
    cfg.width = 1280;
    cfg.height = 720;
    // --frames is the headless path: exercise the renderer with nobody at the
    // keyboard. Vsync there is not just pointless, it hangs. A window with no
    // active display session never gets a vblank, so SDL_GL_SwapWindow blocks
    // forever on the condition variable and the frame limit is never reached --
    // the app logs a clean startup and then sits there looking like a deadlock
    // in our own render loop. Measured: stuck in Cocoa_GL_SwapWindow, three
    // minutes, zero frames.
    cfg.vsync = (frame_limit_ <= 0);

    if (!window_.init(cfg)) {
        AP_ERROR("window init failed; cannot continue");
        return false;
    }

    // The overlay is optional. Losing it must not lose the app.
    if (!overlay::init(window_)) {
        AP_WARN("overlay unavailable; continuing without it");
    }
    // The UI backend bound its own objects while initialising, so nothing the
    // bind cache believes is true any more.
    gl_state::invalidate_all();

    collider_ = TerrainCollider(seed_);

    // spawn_vehicle settles the car on its springs and aligns it to the slope
    // it is standing on. Assigning a position by hand instead drops it in with
    // its struts at free length and the first step launches it, which looks
    // like a physics bug and is not one.
    car_ = spawn_vehicle(tuning_, collider_, 0.0f, 0.0f, 0.0f);
    prev_car_ = car_;

    // --- renderer and its passes -------------------------------------------
    // Every one of these is fatal if it fails. A renderer that starts with a
    // broken shader draws black, and black is the hardest possible symptom to
    // work backwards from; better to refuse to start and say which stage died.
    if (!renderer_.init()) {
        AP_ERROR("renderer init failed; cannot continue");
        return false;
    }
    if (!sky_.init()) {
        AP_ERROR("sky init failed; cannot continue");
        return false;
    }
    if (!rain_.init(seed_)) {
        AP_ERROR("precipitation init failed; cannot continue");
        return false;
    }
    if (!hud_.init()) {
        AP_ERROR("hud init failed; cannot continue");
        return false;
    }
    if (!build_demo_scene(scene_, renderer_, collider_, seed_, kBoxCount,
                          kFieldRadius, demo_)) {
        AP_ERROR("demo scene build failed; cannot continue");
        return false;
    }

    camera_.aspect = static_cast<float>(window_.width()) /
                     static_cast<float>(window_.height() > 0 ? window_.height() : 1);
    camera_.far_plane = kRenderDistance + 200.0f;
    update_camera();

    // A default that shows the whole feature set doing something on launch.
    controls_.rain = 0.35f;
    controls_.overcast = 0.30f;
    controls_.fog = 0.55f;

    AP_INFO("seed 0x%016llX, car spawned at %.2f m (ground %.2f m)",
            static_cast<unsigned long long>(seed_),
            static_cast<double>(car_.position.y),
            static_cast<double>(collider_.height(0.0f, 0.0f)));

    gl_errors_ += drain_gl_errors("after init");

    running_ = true;
    return true;
}

void App::shutdown() {
    // Order matters only in that the GL resources must die while the context
    // is still alive, so everything gfx goes before the window.
    hud_.destroy();
    rain_.destroy();
    sky_.destroy();
    renderer_.destroy();
    scene_.clear();

    overlay::shutdown();
    window_.shutdown();
    running_ = false;
}

int App::drain_gl_errors(const char* where) {
    int found = 0;
    // GL queues errors; one glGetError only pops one. A loop bound stops a
    // driver that returns an error forever from hanging the frame.
    for (int i = 0; i < 32; ++i) {
        const GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        AP_ERROR("GL error %s (0x%04X) %s", gl_error_name(e),
                 static_cast<unsigned>(e), where);
        ++found;
    }
    return found;
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

        // The instancing A/B toggle is handled HERE rather than in InputMapper
        // on purpose. InputFrame is the replay tape format; a debug toggle
        // recorded into a tape would change what a replay does, and a replay
        // that disagrees with the run it recorded is worse than none.
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F7) {
            controls_.instancing = !controls_.instancing;
            AP_INFO("instancing %s", controls_.instancing ? "ON" : "OFF (naive path)");
        }

        input_.handle_event(e);
    }

    input_.end_frame();
}

void App::update_camera() {
    // Interpolate the car between its previous and current sim states. Without
    // this a 120 Hz sim visibly steps on a 144 Hz panel.
    const float a = static_cast<float>(clock_.alpha());
    const glm::vec3 pos = glm::mix(prev_car_.position, car_.position, a);
    const glm::quat rot = glm::slerp(prev_car_.orientation, car_.orientation, a);

    const glm::vec3 forward = rot * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    glm::vec3 eye = pos - forward * kCamBack + up * kCamUp;

    // Never let the camera drop below the ground: a chase cam behind a car
    // climbing a hill ends up inside the hill, and the whole screen goes to
    // whatever the inside of the terrain looks like.
    const float ground = collider_.height(eye.x, eye.z) + 1.2f;
    if (eye.y < ground) eye.y = ground;

    const glm::vec3 target = pos + forward * kCamLookAhead;
    const glm::vec3 dir = target - eye;
    const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z);

    camera_.position = eye;
    camera_.yaw = std::atan2(dir.x, -dir.z);
    camera_.pitch = std::atan2(dir.y, flat > 1e-4f ? flat : 1e-4f);
}

void App::render() {
    glViewport(0, 0, window_.width(), window_.height());

    // Sim time, not wall time. See the note in app.h.
    const float sim_seconds =
        static_cast<float>(static_cast<double>(step_index_) * kSimDt);
    const float time_of_day =
        0.28f + sim_seconds * controls_.sky_speed /
                    static_cast<float>(kSecondsPerDay);

    WeatherParams weather;
    weather.rain = controls_.rain;
    weather.overcast = controls_.overcast;
    weather.fog = controls_.fog;
    weather.fog_start_m = kRenderDistance * 0.25f;
    weather.fog_end_m = kRenderDistance;
    const SkyEnv env = compute_sky_env(time_of_day, weather);

    update_camera();

    // Clear to the fog colour, so anything the sky pass somehow misses blends
    // with the horizon instead of showing as a hard black band.
    glClearColor(env.fog_color.r, env.fog_color.g, env.fog_color.b, 1.0f);
    glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    // 1. Sky first: it writes no depth, so opaque geometry covers it and no
    //    sky pixel is shaded twice.
    sky_.render(camera_, env, sim_seconds);

    // 2. Opaque world, batched.
    const Scene::CullResult& culled =
        scene_.cull(camera_.frustum(), camera_.position, kRenderDistance);

    Renderer::Options opts;
    opts.instancing = controls_.instancing;
    render_stats_ = renderer_.render(scene_, culled.visible, camera_, env, opts);

    // 3. Rain over the world, blended.
    rain_.render(camera_, env);

    // 4. HUD last, one draw.
    {
        const glm::vec2 vp{static_cast<float>(window_.width()),
                           static_cast<float>(window_.height())};
        hud_.begin(vp);

        const float speed_kmh = glm::length(car_.velocity) * 3.6f;
        char line[64];

        hud_.rect({20.0f, vp.y - 118.0f}, {330.0f, vp.y - 20.0f},
                  {0.0f, 0.0f, 0.0f, 0.45f});
        hud_.outline({20.0f, vp.y - 118.0f}, {330.0f, vp.y - 20.0f}, 2.0f,
                     {1.0f, 1.0f, 1.0f, 0.20f});

        std::snprintf(line, sizeof(line), "%3.0f KM/H", static_cast<double>(speed_kmh));
        hud_.text(line, {36.0f, vp.y - 104.0f}, 34.0f, {1.0f, 0.94f, 0.80f, 1.0f});

        // Everything below is read straight off the car and the conditions —
        // no game layer computes it, because there is not one. A real HUD data
        // model comes back with the pilot game, in apricot_sim where a headless
        // test can reach it.
        std::snprintf(line, sizeof(line), "GEAR %d   %4.0f RPM", car_.gear,
                      static_cast<double>(car_.engine_rpm));
        hud_.text(line, {36.0f, vp.y - 62.0f}, 18.0f, {0.85f, 0.90f, 1.0f, 1.0f});

        std::snprintf(line, sizeof(line), "%s / %s   GRIP %.2f",
                      daylight_name(conditions_.daylight),
                      weather_name(conditions_.weather),
                      static_cast<double>(conditions_.grip));
        hud_.text(line, {36.0f, vp.y - 40.0f}, 18.0f, {0.80f, 1.0f, 0.85f, 1.0f});

        std::snprintf(line, sizeof(line), "%d DRAWS  %d INST", render_stats_.draw_calls,
                      render_stats_.instances);
        hud_.text_centered(line, vp.x * 0.5f, 18.0f, 16.0f,
                           controls_.instancing ? glm::vec4{0.75f, 1.0f, 0.80f, 0.9f}
                                                : glm::vec4{1.0f, 0.78f, 0.35f, 0.95f});

        hud_.end();
    }

    // 5. Debug UI on top of everything.
    overlay::Stats stats;
    stats.fps = fps_;
    stats.frame_ms = frame_ms_;
    stats.sim_steps = last_steps_;
    stats.step_clamped = last_clamped_;
    stats.alpha = clock_.alpha();
    stats.sim_step_index = static_cast<unsigned long long>(step_index_);
    stats.scene_nodes = static_cast<int>(scene_.size());
    stats.visible_nodes = render_stats_.visible_nodes;
    stats.batches = render_stats_.batches;
    stats.instanced_batches = render_stats_.instanced_batches;
    stats.draw_calls = render_stats_.draw_calls;
    stats.instances = render_stats_.instances;
    stats.largest_run = render_stats_.largest_run;
    stats.skipped_binds = render_stats_.skipped_binds;
    stats.hud_quads = hud_.last_quad_count();
    stats.hud_draw_calls = hud_.last_draw_calls();
    stats.rain_drops = rain_.live_drops();
    stats.rain_quads = rain_.drawn_quads();
    stats.time_of_day = env.time_of_day;
    stats.gl_errors = gl_errors_;
    overlay::draw(window_, stats, controls_);

    // Check the error queue for the first stretch of frames. Every frame
    // forever would be a needless driver round trip; never checking at all is
    // how a broken call ships.
    if (frames_rendered_ < 8) {
        gl_errors_ += drain_gl_errors("during the first frames");
    }

    window_.swap();
    ++frames_rendered_;
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
        if (frame_limit_ > 0 && frames_rendered_ >= frame_limit_) break;

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
            // Snapshot before EACH step, not before the batch: prev_car_ has to
            // be exactly one step behind or the render interpolation covers the
            // wrong span on a multi-step frame.
            prev_car_ = car_;

            // Conditions are a pure function of (seed, ABSOLUTE step), never an
            // accumulator, so a tape replayed from any point in the session
            // gets its own weather back. See game/conditions.h.
            conditions_ = conditions_at(seed_, step_index_);
            car_ = step_vehicle(car_, conditioned_tuning(tuning_, conditions_),
                                input_.frame(), collider_,
                                static_cast<float>(kSimDt));
            ++step_index_;
        }

        // Consume latched edges ONLY when a step actually ran. On a zero-step
        // frame the edges stay latched for the next one. Moving this out of
        // the guard silently drops presses at high frame rates.
        if (tick.steps > 0) input_.consume_edges();

        // The car node is the only transform that moves; the scenery is static,
        // so Scene::update only ever walks one dirty node per frame.
        if (demo_.car_node != kInvalidId) {
            const float a = static_cast<float>(clock_.alpha());
            Transform t;
            t.position = glm::mix(prev_car_.position, car_.position, a);
            t.rotation = glm::slerp(prev_car_.orientation, car_.orientation, a);
            t.scale = demo_.car_half * 2.0f;
            scene_.set_transform(demo_.car_node, t);
        }
        scene_.update();

        rain_.update(camera_, controls_.rain, static_cast<float>(kSimDt) *
                                                  static_cast<float>(tick.steps));

        render();

        if (first_frame) {
            first_frame = false;
            clock_.reset();
            last = WallClock::now();
        }
    }

    AP_INFO("quit after %llu sim steps (%.2f s of sim time), %d frames",
            static_cast<unsigned long long>(step_index_),
            static_cast<double>(step_index_) * kSimDt, frames_rendered_);
    AP_INFO("last frame: %d visible nodes, %d batches (%d instanced), "
            "%d draw calls, %d instances, longest run %d, %u binds skipped",
            render_stats_.visible_nodes, render_stats_.batches,
            render_stats_.instanced_batches, render_stats_.draw_calls,
            render_stats_.instances, render_stats_.largest_run,
            render_stats_.skipped_binds);
    AP_INFO("last frame: hud %d quads in %d draw(s), rain %d drops / %d quads",
            hud_.last_quad_count(), hud_.last_draw_calls(), rain_.live_drops(),
            rain_.drawn_quads());

    gl_errors_ += drain_gl_errors("at shutdown");
    if (gl_errors_ > 0) {
        AP_ERROR("%d GL error(s) during the session — the frames you saw are "
                 "not the frames that were asked for",
                 gl_errors_);
        return 3;
    }
    AP_INFO("GL error queue clean for the whole session");
    return 0;
}

}  // namespace apricot

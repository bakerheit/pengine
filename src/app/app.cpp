#include "app/app.h"

#include <SDL.h>
#include <glad/gl.h>

#include <chrono>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "app/overlay.h"
#include "city/map.h"
#include "city/spines.h"
#include "core/log.h"
#include "gfx/gl_state.h"
#include "gfx/sky_env.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

using WallClock = std::chrono::steady_clock;

// How far the renderer draws. The fog band is set just inside it so the
// distance cull's cutoff hides behind atmosphere instead of popping.
//
// 2400 m, up from the 700 m the placeholder scene used, because the whole point
// of terrain LOD is that the far ring is affordable and there is no point
// paying for a 2304 m ring of chunks and then fog-culling it at 700. The island
// is 2.8 km across (terrain/heightmap.h, kIslandRadiusMetres), so this is
// "you can see the far coast", which is the legibility argument
// docs/design/pinatty.md makes for landmarks.
constexpr float kRenderDistance = 2400.0f;

// The near plane pays for that distance. Depth precision is distributed by the
// near/far RATIO, so pushing far from 900 to 2600 without touching near would
// cost precision up close where the car is. The chase camera sits 9 m back, so
// half a metre of near plane costs nothing anyone can see and buys the ratio
// back more than threefold.
constexpr float kNearPlane = 0.5f;

// Streaming work above this, in milliseconds, is a spike worth naming in the
// log. Set just over half a 120 Hz step so it catches anything that could cost
// a frame, and comfortably above the steady-state cost so it stays quiet.
constexpr double kStreamSpikeMs = 4.0;

// How many spikes get a line each before the log falls back to counting them.
constexpr int kMaxSpikeLogs = 3;

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

    // THE WORLD IS KEYED ON THE MAP SEED, NOT THE RUN SEED (PENG-41).
    //
    // docs/architecture.md's amendment made concrete: the world is a pure
    // function of (map, seed, coord). city::kMapSeed is pinned in the map
    // tables and selects the noise detail under the authored skeleton, so
    // Pinatty is the same place in every session. seed_ below is the SESSION
    // -- weather, ambient variation, the placeholder box field -- and it is
    // the one that will eventually come from a save file.
    collider_ = TerrainCollider(city::kMapSeed);

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

    // --- the car's box -------------------------------------------------------
    {
        Texture car_tex;
        if (!car_tex.make_checker(64, 2, glm::vec3{0.85f, 0.16f, 0.12f},
                                  glm::vec3{0.95f, 0.90f, 0.85f})) {
            AP_ERROR("car texture generation failed; cannot continue");
            return false;
        }
        car_material_ = renderer_.add_material(std::move(car_tex));
        car_mesh_ = renderer_.add_mesh(make_box(glm::vec3{0.5f}));
        if (car_mesh_ == kInvalidId) {
            AP_ERROR("car mesh upload failed; cannot continue");
            return false;
        }

        Renderable r;
        r.mesh = car_mesh_;
        r.material = car_material_;
        Transform t;
        t.scale = car_half_ * 2.0f;
        car_node_ = scene_.create(r, t, make_box(glm::vec3{0.5f}).bounds);
    }

    // --- the streamed world --------------------------------------------------
    //
    // city::kMapSeed, THE SAME ONE THE COLLIDER GOT, and it must stay that way.
    //
    // The streamer meshes chunks from this seed and physics reconstructs the
    // lattice from the collider's. Hand them different seeds and the engine's
    // oldest rule — the solid the car touches IS the surface the player sees —
    // is broken in the most confusing way available: everything renders, the
    // car drives, and it drives on a landscape that is not the one on screen.
    //
    // This very nearly shipped. The map ticket changed the collider to
    // city::kMapSeed while this ticket was writing the streamer against seed_,
    // and the two merged cleanly because neither line mentions the other.
    if (!world_.init(renderer_, city::kMapSeed, StreamerConfig{})) {
        AP_ERROR("world init failed; cannot continue");
        return false;
    }

    // So it is checked rather than commented. The drawn surface under the spawn
    // point, reconstructed from the streamer's seed, against the ground physics
    // will actually put the car on. These are the same function of the same
    // seed, so the only tolerance that means anything is zero.
    {
        const float drawn = mesh_height_at(world_.streamer().seed(), 0.0f, 0.0f);
        const float driven = collider_.height(0.0f, 0.0f);
        if (drawn != driven) {
            AP_ERROR("the world drawn is not the world driven: terrain seed "
                     "0x%016llX gives %.4f m at the origin, collider seed "
                     "0x%016llX gives %.4f m. Refusing to start.",
                     static_cast<unsigned long long>(world_.streamer().seed()),
                     static_cast<double>(drawn),
                     static_cast<unsigned long long>(collider_.seed()),
                     static_cast<double>(driven));
            return false;
        }
    }

    // --- roads ---------------------------------------------------------------
    // Pinatty's road network, from the authored tables in src/city/roads.h.
    // This used to be an empty list plus a --road-probe flag that baked two
    // crossing streets at the origin so the bake/upload/draw path was exercised
    // at all; the flag existed to be deleted the day map_spines() landed, and
    // this is that day.
    if (!world_.set_roads(renderer_, scene_, city::map_spines())) {
        AP_ERROR("road bake/upload failed; cannot continue");
        return false;
    }

    // COLD FILL, BEFORE THE CLOCK STARTS.
    //
    // Without this the first frame renders a car suspended over nothing and the
    // world arrives around it over the following second, with a hitch on the
    // frame that does the most work. Filling here costs the same milliseconds
    // and spends them during startup, where a hundred of them are invisible,
    // instead of during play, where they are the first thing anyone notices.
    // run() resets the frame clock after the first present, so this time is not
    // charged to the sim as dropped steps either.
    {
        const WallClock::time_point t0 = WallClock::now();
        last_fill_steps_ = world_.fill(scene_, renderer_, car_.position);
        last_fill_ms_ = std::chrono::duration<double>(WallClock::now() - t0)
                            .count() * 1000.0;
        AP_INFO("cold fill: %d steps, %.1f ms, %zu chunks, %.1f MB of terrain",
                last_fill_steps_, last_fill_ms_,
                world_.stats().resident_chunks,
                static_cast<double>(world_.stats().mesh_bytes) /
                    (1024.0 * 1024.0));
    }

    camera_.aspect = static_cast<float>(window_.width()) /
                     static_cast<float>(window_.height() > 0 ? window_.height() : 1);
    camera_.near_plane = kNearPlane;
    camera_.far_plane = kRenderDistance + 200.0f;
    update_camera();

    // A default that shows the whole feature set doing something on launch.
    controls_.rain = 0.35f;
    controls_.overcast = 0.30f;
    controls_.fog = 0.55f;

    AP_INFO("map 0x%016llX, run seed 0x%016llX, car spawned at %.2f m "
            "(ground %.2f m)",
            static_cast<unsigned long long>(city::kMapSeed),
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
    //
    // The world goes FIRST of all, because it is the only thing here that owns
    // resources jointly with something else: its chunk meshes live in the
    // renderer's table and its nodes live in the scene, and it is the only
    // object that knows which mesh belongs to which node.
    world_.shutdown(scene_, renderer_);
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

        // F8 warps across the island. Same reasoning as F7: a debug action
        // handled here rather than in InputMapper, because InputFrame is the
        // replay tape format and a teleport recorded into a tape would change
        // what a replay does. Latched rather than acted on here so the warp
        // happens at a step boundary with the rest of the world update.
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F8) {
            teleport_requested_ = true;
        }

        input_.handle_event(e);
    }

    input_.end_frame();
}

void App::teleport(glm::vec3 to) {
    // A mission warp, and the thing it is really testing is the fill path.
    //
    // Order matters and each line buys something specific:
    //
    //   1. spawn_vehicle rather than assigning a position, so the car arrives
    //      settled on its springs and aligned to the slope it lands on. Setting
    //      position by hand drops it in with its struts at free length and the
    //      first step launches it, which looks like a physics bug and is not.
    //   2. prev_car_ = car_, or the render interpolation spends one frame
    //      drawing the car smeared across the island between where it was and
    //      where it is.
    //   3. FILL BEFORE RESUMING. The destination has nothing resident. Without
    //      this the player is dropped into void and the ground arrives around
    //      them over the next second, with the meshing hitch landing on the
    //      frame they are most likely to be looking at something.
    //   4. clock_.reset(), because everything above took real milliseconds and
    //      FixedStep would otherwise owe the sim all of them at once and warn
    //      about dropped steps.
    // spawn_vehicle takes (x, z, yaw). Passing (x, yaw, z) compiles perfectly
    // and puts the car on the z = 0 line every time, facing a direction derived
    // from where it should have been standing.
    car_ = spawn_vehicle(tuning_, collider_, to.x, to.z, 0.0f);
    prev_car_ = car_;

    const WallClock::time_point t0 = WallClock::now();
    last_fill_steps_ = world_.fill(scene_, renderer_, car_.position);
    last_fill_ms_ =
        std::chrono::duration<double>(WallClock::now() - t0).count() * 1000.0;

    update_camera();
    clock_.reset();

    // The destination's level is logged alongside the cost, because a fill that
    // did NOTHING and a fill that was merely fast print the same number of
    // milliseconds otherwise. A zero-step fill is legitimate — the whole island
    // fits inside the evict radius, so a warp can land on ground that is
    // already resident at the level it wants — but "legitimate" and "the
    // readiness test is broken again" look identical without this.
    const ChunkCoord under = chunk_at(car_.position.x, car_.position.z);
    AP_INFO("teleport to (%.0f, %.0f): filled in %d steps / %.1f ms, "
            "destination lod %d, %zu chunks, %.1f MB",
            static_cast<double>(to.x), static_cast<double>(to.z),
            last_fill_steps_, last_fill_ms_,
            world_.streamer().resident_lod(under),
            world_.stats().resident_chunks,
            static_cast<double>(world_.stats().mesh_bytes) / (1024.0 * 1024.0));
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
    //
    // Timed because docs/design/pinatty.md's recommendation — cap resident
    // static instances near 60k and get there with draw-distance tiers rather
    // than building a BVH — is only sound while this number stays small. It was
    // measured at 0.278 ms for 60k synthetic nodes; this is the same scan over
    // the real thing, and it is the number that says when a broadphase has
    // stopped being premature. Display only: it never reaches the sim.
    const WallClock::time_point cull_t0 = WallClock::now();
    const Scene::CullResult& culled =
        scene_.cull(camera_.frustum(), camera_.position, kRenderDistance);
    cull_ms_ = std::chrono::duration<double>(WallClock::now() - cull_t0).count() *
               1000.0;

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

    const World::Stats& ws = world_.stats();
    stats.resident_chunks = ws.resident_chunks;
    for (int l = 0; l <= kMaxChunkLod; ++l) {
        stats.resident_by_lod[l] = ws.resident_by_lod[l];
    }
    stats.live_meshes = ws.live_meshes;
    stats.terrain_mb =
        static_cast<double>(ws.mesh_bytes) / (1024.0 * 1024.0);
    stats.chunks_built = ws.chunks_built;
    stats.chunks_refitted = ws.chunks_refitted;
    stats.chunks_evicted = ws.chunks_evicted;
    stats.meshes_freed = ws.meshes_freed;
    stats.stream_budget_hit = ws.budget_exhausted;
    stats.cull_ms = cull_ms_;
    stats.mesh_ms = mesh_ms_;
    stats.fill_ms = last_fill_ms_;
    stats.fill_steps = last_fill_steps_;

    // Peaks, not just the instantaneous value. The instantaneous one is what a
    // human watches; the peak is what says whether a spike happened while they
    // were looking at something else, which for streaming is most of the time.
    peak_cull_ms_ = std::max(peak_cull_ms_, cull_ms_);
    peak_mesh_ms_ = std::max(peak_mesh_ms_, mesh_ms_);

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
        // The SMOOTHED number is for the overlay, where a reader wants a value
        // that stops jittering. The summary at the end wants the honest mean
        // over the session, so accumulate the raw deltas too — and only after
        // the first present, for the same reason frame_ms_ starts there: one
        // 100 ms shader compile in a 600-frame mean is a 0.17 ms lie.
        if (!first_frame) {
            frame_ms_total_ += dt * 1000.0;
            ++frames_timed_;
            if (dt * 1000.0 > worst_frame_ms_) worst_frame_ms_ = dt * 1000.0;
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

        // The car node is the only transform that MOVES; the streamed world is
        // static, so Scene::update walks one dirty node per frame plus whatever
        // the streamer created this frame.
        if (car_node_ != kInvalidId) {
            const float a = static_cast<float>(clock_.alpha());
            Transform t;
            t.position = glm::mix(prev_car_.position, car_.position, a);
            t.rotation = glm::slerp(prev_car_.orientation, car_.orientation, a);
            t.scale = car_half_ * 2.0f;
            scene_.set_transform(car_node_, t);
        }

        // Residency, once per FRAME rather than once per sim step.
        //
        // The budgets are what a frame can afford, so a frame that owed three
        // sim steps would otherwise do three times the meshing on the frame
        // that was already late — which is the hitch amplifying itself.
        //
        // Keying it to frames is safe here in a way it would not be for AI LOD,
        // and the reason is worth stating: nothing about residency reaches the
        // sim. Physics queries the height field analytically and never asks
        // what is loaded, so two machines at different frame rates stream
        // differently and simulate identically.
        //
        // The FOCUS IS THE CAR, NOT THE CAMERA. The camera is a render-side
        // object updated at frame rate and free to look anywhere; streaming
        // keyed to where you are looking is streaming that depends on the
        // display. See docs/design/pinatty.md 7.2.
        if (warp_interval_ > 0 && frames_rendered_ > 0 &&
            frames_rendered_ % warp_interval_ == 0) {
            teleport_requested_ = true;
        }

        if (teleport_requested_) {
            teleport_requested_ = false;
            // Around the island rather than to one fixed spot, so successive
            // warps evict and refill genuinely different ground instead of
            // bouncing between two neighbourhoods that stay half-resident.
            const float angle = static_cast<float>(warps_done_) * 1.1f;
            const float radius = 900.0f;
            teleport(glm::vec3{std::cos(angle) * radius, 0.0f,
                               std::sin(angle) * radius});
            ++warps_done_;
            last = WallClock::now();  // do not charge the fill to the next frame
        } else {
            const WallClock::time_point mesh_t0 = WallClock::now();
            world_.update(scene_, renderer_, car_.position);
            mesh_ms_ =
                std::chrono::duration<double>(WallClock::now() - mesh_t0).count() *
                1000.0;

            // A streaming spike, named with the work that caused it. This fires
            // rarely by construction — the budgets exist to keep it that way —
            // so unlike a warning that fires every launch it still means
            // something when it appears. Without the breakdown a spike is just
            // a number, and "meshing was slow" is not a lead.
            if (mesh_ms_ > kStreamSpikeMs) {
                ++stream_spikes_;
                // The first few, then silence and a count at exit. A spike
                // during the opening fill is expected and logging two hundred
                // of them buries the one that happens an hour into a drive,
                // which is the only one anybody needed to see.
                if (stream_spikes_ <= kMaxSpikeLogs) {
                    const World::Stats& s = world_.stats();
                    AP_WARN("streaming spike: %.2f ms for %d chunks / %d quads, "
                            "%d instances, %d refits, %d evictions, %d frees",
                            mesh_ms_, s.chunks_built, s.quads_built,
                            s.instances_activated, s.chunks_refitted,
                            s.chunks_evicted, s.meshes_freed);
                }
            }
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
    {
        const World::Stats& ws = world_.stats();
        AP_INFO("terrain: %zu chunks resident (lod %zu / %zu / %zu / %zu), "
                "%zu meshes, %.1f MB of vertex data",
                ws.resident_chunks, ws.resident_by_lod[0], ws.resident_by_lod[1],
                ws.resident_by_lod[2], ws.resident_by_lod[3], ws.live_meshes,
                static_cast<double>(ws.mesh_bytes) / (1024.0 * 1024.0));
        AP_INFO("costs: cull %.3f ms (peak %.3f), meshing %.2f ms (peak %.2f), "
                "last fill %.1f ms in %d steps",
                cull_ms_, peak_cull_ms_, mesh_ms_, peak_mesh_ms_, last_fill_ms_,
                last_fill_steps_);
        AP_INFO("streaming spikes over %.1f ms: %d of %d frames",
                kStreamSpikeMs, stream_spikes_, frames_rendered_);
        if (frames_timed_ > 0) {
            const double mean = frame_ms_total_ / frames_timed_;
            AP_INFO("frame time: %.2f ms mean over %d frames (%.0f FPS), "
                    "%.2f ms worst; roads %zu triangles in %zu layers, %.2f MB",
                    mean, frames_timed_, 1000.0 / mean, worst_frame_ms_,
                    world_.roads().triangle_count(),
                    world_.roads().layer_count(),
                    static_cast<double>(world_.roads().gpu_bytes()) /
                        (1024.0 * 1024.0));
        }
    }

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

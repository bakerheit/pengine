#include "app/app.h"
#include "gfx/street_lamp_light.h"
#include "app/vehicle_model_tuning.h"
#include "app/weapon_wheel.h"
#include "app/weapon_audio.h"
#include "app/weapon_aim.h"
#include "game/intro_layout.h"
#include "game/delivery_mission.h"
#include "game/repair_shop.h"
#include "game/weather_hazards.h"
#include "city/neighborhood_bar.h"
#include "city/loom_cultural.h"
#include "city/pawn_shop.h"
#include "city/gun_store.h"
#include "city/hospital_exterior.h"
#include "city/miandi_night_lighting.h"
#include "city/tacomaco.h"
#include "core/asset_root.h"
#include "core/rng.h"

#include <SDL.h>
#include <glad/gl.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <chrono>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <glm/gtc/matrix_transform.hpp>

#include "app/overlay.h"
#include "app/emergency_lighting.h"
#include "audio/player_car_assets.h"
#include "city/map.h"
#include "city/marina.h"
#include "city/airport.h"
#include "city/airport_aircraft.h"
#include "city/halberd_helicopter.h"
#include "city/bank_vault_layout.h"
#include "city/spines.h"
#include "core/log.h"
#include "core/units.h"
#include "game/ui_canvas.h"
#include "game/vehicle_interaction.h"
#include "game/drunk.h"
#include "gfx/gl_state.h"
#include "gfx/sky_env.h"
#include "gfx/bellwether_sky.h"
#include "game/local_snow_conditions.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

using WallClock = std::chrono::steady_clock;
namespace fs = std::filesystem;

fs::path find_checkout_root() {
    std::error_code ec;
    std::array<fs::path, 2> starts{
        fs::absolute(fs::path(asset_root()), ec).parent_path(),
        fs::current_path(ec),
    };
    for (fs::path start : starts) {
        for (int i = 0; i < 8 && !start.empty(); ++i) {
            if (fs::exists(start / ".git", ec) &&
                fs::exists(start / "CMakeLists.txt", ec)) {
                return fs::weakly_canonical(start, ec);
            }
            if (start == start.parent_path()) break;
            start = start.parent_path();
        }
    }
    return {};
}

fs::path unique_bug_report_directory(const fs::path& checkout) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    const fs::path parent = checkout / "build" / "bug-reports";
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) return {};
    fs::path candidate = parent / stamp;
    for (int suffix = 2; fs::exists(candidate, ec); ++suffix) {
        candidate = parent / (std::string(stamp) + "-" +
                              std::to_string(suffix));
    }
    fs::create_directories(candidate, ec);
    return ec ? fs::path{} : candidate;
}

bool launch_codex_report(const fs::path& checkout, const fs::path& screenshot,
                         const fs::path& report_path, const fs::path& log_path,
                         const std::string& report_text) {
#if defined(__APPLE__) || defined(__linux__)
    fs::path codex = "/Applications/ChatGPT.app/Contents/Resources/codex";
    std::error_code ec;
    if (!fs::exists(codex, ec)) codex = "codex";

    std::string prompt =
        "This bug was submitted from the running Apricot game. Investigate "
        "and fix it in this checkout. The attached framebuffer screenshot and "
        "captured world coordinates are authoritative. Preserve all unrelated "
        "dirty work. Reproduce or diagnose first, then make a scoped fix and "
        "validate it with focused tests plus the relevant runtime evidence. "
        "The saved report is at " + report_path.string() + ".\n\n" + report_text;

    std::vector<std::string> args{
        codex.string(), "exec", "-C", checkout.string(), "-i",
        screenshot.string(), "--sandbox", "workspace-write",
        "--approve-for-me", "--color", "never", prompt,
    };
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return false;
    const int open_flags = O_WRONLY | O_CREAT | O_TRUNC;
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                     log_path.c_str(), open_flags, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                     O_RDONLY, 0);

    pid_t pid = 0;
    const int result = codex.is_absolute()
        ? posix_spawn(&pid, codex.c_str(), &actions, nullptr, argv.data(), environ)
        : posix_spawnp(&pid, codex.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) return false;
    std::thread([pid] {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }).detach();
    return true;
#else
    (void)checkout; (void)screenshot; (void)report_path; (void)log_path;
    (void)report_text;
    return false;
#endif
}

// Hard upper bound for rendering. Distance haze normally hides and culls the
// scene well inside this cap; the extra range still supports unusually clear
// debug weather and prevents the projection from clipping a fogged skyline.
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

// Where a session's frame CSV goes when nobody said.
//
// This used to resolve "build/perf" against the WORKING DIRECTORY, which is
// correct exactly once: when someone types ./build/bin/apricot from the repo
// root. Launch the app bundle instead — the normal way anyone starts a game on
// this platform — and the working directory is "/", so the recorder wrote
// nothing, said nothing a player could see, and the run was gone. A diagnostic
// that only survives one launch method is worse than none, because it is
// trusted right up until the session you needed it for.
//
// So it goes where the SAVE GAME already goes. That folder is per-user, always
// writable, and the same whether the app was launched from a shell, from
// Finder or from a bundle. src/app/app_save.cpp picked it first; this just
// follows it rather than inventing a second answer.
std::string default_perf_log_path() {
    namespace fs = std::filesystem;
    fs::path dir;
    if (char* folder = SDL_GetPrefPath("Bakerheit", "Probable Cause")) {
        dir = fs::path(folder) / "perf";
        SDL_free(folder);
    } else {
        dir = fs::path("build") / "perf";  // last resort, better than nothing
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return {};

    int highest = -1;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("session-", 0) != 0) continue;
        if (e.path().extension() != ".csv") continue;
        const int n = std::atoi(name.c_str() + 8);
        if (n > highest) highest = n;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "session-%03d.csv", highest + 1);
    return (dir / name).string();
}

// How many spikes get a line each before the log falls back to counting them.
constexpr int kMaxSpikeLogs = 3;

// Day length is NOT defined here. `game/conditions.h` owns it, because the same
// number drives weather, grip and the light the sky pass computes — and two
// copies of one constant are two numbers that eventually disagree. This file
// used to declare its own 240 s day; when the conditions system landed the two
// collided at the merge, which is the cheap way to find out.
//
// Debug time acceleration lives in `sky_speed`; normal play leaves it at 1 so
// the visible sky and HUD clock use the session day length exactly.

// `overcast` here is DECK OPACITY, and since PENG the axis split it no longer
// gets topped up by the rain figure — apply_weather() only holds it to a
// minimum (kPrecipCloudFloor). The old numbers were written expecting rain to
// contribute most of the deck, so a preset that wants a grey sky now has to
// say so outright. That is the point: it is what lets Sunshower ask for a
// downpour and keep its sun.
void apply_dev_weather(DevWeatherPreset preset, overlay::Controls& controls) {
    switch (preset) {
        case DevWeatherPreset::Clear:
            controls.rain = 0.0f;
            controls.overcast = 0.0f;
            controls.fog = 0.0f;
            break;
        case DevWeatherPreset::Sunshower:
            // Hard rain, barely any deck. The sun stays clear of the cloud
            // (cover lands under sky.frag's 0.55 swallow threshold) and the
            // rain still glosses the road through specular_strength.
            controls.rain = 0.62f;
            controls.overcast = 0.12f;
            controls.fog = 0.10f;
            break;
        case DevWeatherPreset::Overcast:
            controls.rain = 0.0f;
            controls.overcast = 0.72f;
            controls.fog = 0.25f;
            break;
        case DevWeatherPreset::Rain:
            // Was 0.30 deck, which under the summed model became 0.51. Stated
            // outright now, and a touch heavier: a rainy day is grey.
            controls.rain = 0.45f;
            controls.overcast = 0.80f;
            controls.fog = 0.55f;
            break;
        case DevWeatherPreset::Storm:
            controls.rain = 1.0f;
            controls.overcast = 1.0f;
            controls.fog = 0.85f;
            break;
        case DevWeatherPreset::Thunderstorm:
            controls.rain = 1.0f;
            controls.overcast = 1.0f;
            controls.fog = 0.72f;
            break;
        case DevWeatherPreset::Snow:
            controls.rain = 0.0f;
            controls.overcast = 0.86f;
            controls.fog = 0.38f;
            break;
        case DevWeatherPreset::Blizzard:
            controls.rain = 0.0f;
            controls.overcast = 1.0f;
            controls.fog = 1.0f;
            break;
        case DevWeatherPreset::Tornado:
            controls.rain = 0.62f;
            controls.overcast = 1.0f;
            controls.fog = 0.72f;
            break;
        case DevWeatherPreset::Flood:
            controls.rain = 0.90f;
            controls.overcast = 1.0f;
            controls.fog = 0.78f;
            break;
        case DevWeatherPreset::Hail:
            controls.rain = 0.48f;
            controls.overcast = 0.96f;
            controls.fog = 0.48f;
            break;
        case DevWeatherPreset::Heatwave:
            controls.rain = 0.0f;
            controls.overcast = 0.04f;
            controls.fog = 0.16f;
            break;
        case DevWeatherPreset::Dynamic:
        case DevWeatherPreset::kCount: break;
    }
}

void override_conditions_for_dev(DevWeatherPreset preset,
                                 overlay::Controls& controls,
                                 Conditions& conditions) {
    if (preset == DevWeatherPreset::Dynamic ||
        preset == DevWeatherPreset::kCount) return;

    conditions.rain = controls.rain;
    conditions.snow = 0.0f;
    conditions.snow_depth_m = 0.0f;
    conditions.snow_cover = 0.0f;
    conditions.flood = 0.0f;
    conditions.hail = 0.0f;
    conditions.heatwave = 0.0f;
    conditions.lightning = 0.0f;
    conditions.tornado_intensity = 0.0f;
    conditions.overcast = controls.overcast;
    conditions.fog = controls.fog;
    conditions.wind_mps = {1.0f, 0.3f};
    conditions.atmosphere = AtmosphericWeather::Clear;

    switch (preset) {
        case DevWeatherPreset::Clear: break;
        case DevWeatherPreset::Sunshower:
            // Rain for grip and particles; the thin deck lives in `controls`.
            // Atmosphere stays Clear so it never forces headlights on.
            conditions.atmosphere = AtmosphericWeather::Clear;
            conditions.wind_mps = {3.0f, 1.0f};
            break;
        case DevWeatherPreset::Overcast:
            conditions.atmosphere = AtmosphericWeather::Overcast;
            break;
        case DevWeatherPreset::Rain:
            conditions.atmosphere = AtmosphericWeather::Rain;
            conditions.wind_mps = {4.0f, 1.0f};
            break;
        case DevWeatherPreset::Storm:
            conditions.atmosphere = AtmosphericWeather::Storm;
            conditions.wind_mps = {10.0f, 3.0f};
            break;
        case DevWeatherPreset::Thunderstorm:
            conditions.atmosphere = AtmosphericWeather::Thunderstorm;
            conditions.lightning = 1.0f;
            conditions.wind_mps = {12.0f, 4.0f};
            break;
        case DevWeatherPreset::Snow:
            conditions.atmosphere = AtmosphericWeather::Snow;
            conditions.snow = 0.72f;
            conditions.wind_mps = {3.0f, 1.0f};
            break;
        case DevWeatherPreset::Blizzard:
            conditions.atmosphere = AtmosphericWeather::Blizzard;
            conditions.snow = 1.0f;
            conditions.wind_mps = {18.0f, 7.0f};
            break;
        case DevWeatherPreset::Tornado:
            conditions.atmosphere = AtmosphericWeather::Tornado;
            conditions.tornado_intensity = 1.0f;
            conditions.tornado_center_m = {0.0f, 0.0f};
            conditions.lightning = 0.55f;
            conditions.wind_mps = {18.0f, 8.0f};
            break;
        case DevWeatherPreset::Flood:
            conditions.atmosphere = AtmosphericWeather::Flood;
            conditions.flood = 1.0f;
            conditions.wind_mps = {7.0f, 2.0f};
            break;
        case DevWeatherPreset::Hail:
            conditions.atmosphere = AtmosphericWeather::Hail;
            conditions.hail = 1.0f;
            conditions.lightning = 0.35f;
            conditions.wind_mps = {10.0f, 5.0f};
            break;
        case DevWeatherPreset::Heatwave:
            conditions.atmosphere = AtmosphericWeather::Heatwave;
            conditions.heatwave = 1.0f;
            conditions.wind_mps = {0.8f, 0.2f};
            break;
        case DevWeatherPreset::Dynamic:
        case DevWeatherPreset::kCount: break;
    }

    conditions.wetness = std::clamp(
        std::max(conditions.rain, conditions.flood), 0.0f, 1.0f);
    conditions.weather = conditions.wetness >= 0.40f ? Weather::Wet :
        (conditions.wetness >= 0.05f ? Weather::Damp : Weather::Dry);
    conditions.headlight_level = automatic_headlight_level(
        conditions.sun_elevation, conditions.atmosphere);
    const float night =
        std::clamp(-conditions.sun_elevation * 2.0f, 0.0f, 1.0f);
    const float loss = kWetGripLoss * conditions.wetness +
        kRainGripLoss * conditions.rain +
        kSnowGripLoss * conditions.snow_cover +
        kFloodGripLoss * conditions.flood + kHailGripLoss * conditions.hail +
        kNightGripLoss * night;
    conditions.grip = std::clamp(1.0f - loss, kMinGrip, 1.0f);
}

TornadoParams tornado_params(const Conditions& conditions) {
    TornadoParams params;
    params.center_xz = conditions.tornado_center_m;
    params.core_radius_m = 14.0f;
    params.outer_radius_m = 185.0f;
    params.max_horizontal_force_n *= conditions.tornado_intensity;
    params.max_lift_force_n *= conditions.tornado_intensity;
    return params;
}

FloodParams flood_params() {
    FloodParams params;
    params.water_elevation_m = 11.5f;
    // The low city floor sits around 12 m. A full event should put water over
    // streets and wheels, not lift the ocean above the player's camera.
    params.max_event_rise_m = 0.90f;
    params.max_depth_m = 5.0f;
    params.full_severity_depth_m = 1.15f;
    return params;
}

HazardExposure hazard_exposure_at(const Conditions& conditions,
                                  glm::vec3 position,
                                  float terrain_elevation_m) {
    HazardExposure exposure;
    const TornadoForce tornado = apricot::tornado_force_at(
        {position.x, position.z}, tornado_params(conditions));
    exposure.tornado = tornado.exposure * conditions.tornado_intensity;
    exposure.snow_ice = conditions.snow_cover;
    exposure.flood =
        flood_at(terrain_elevation_m, conditions.flood, flood_params()).severity;
    exposure.hail = conditions.hail;
    exposure.heatwave = conditions.heatwave;
    return exposure;
}

float hazard_severity(const HazardExposure& exposure, GameplayHazard hazard) {
    switch (hazard) {
        case GameplayHazard::Tornado: return exposure.tornado;
        case GameplayHazard::SnowIce: return exposure.snow_ice;
        case GameplayHazard::Flood: return exposure.flood;
        case GameplayHazard::Hail: return exposure.hail;
        case GameplayHazard::Heatwave: return exposure.heatwave;
        case GameplayHazard::None: return 0.0f;
    }
    return 0.0f;
}

float flood_water_level(float intensity) {
    if (!(intensity > 0.0f)) return kSeaLevelMetres;
    const FloodParams params = flood_params();
    return params.water_elevation_m +
        std::clamp(intensity, 0.0f, 1.0f) * params.max_event_rise_m;
}

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

float wanted_heat_for_level(int level) {
    static constexpr float kHeat[] = {0.0f, 1.0f, 3.0f, 6.0f, 9.0f, 12.0f};
    return kHeat[static_cast<std::size_t>(std::clamp(level, 0, 5))];
}

}  // namespace

bool App::init() {
    if (clear_weather_) {
        dev_menu_.set_weather(DevWeatherPreset::Clear);
        apply_dev_weather(DevWeatherPreset::Clear,controls_);
    }
    AP_INFO("apricot %s starting", APRICOT_VERSION);

    WindowConfig cfg;
    cfg.title = std::string("Probable Cause ") + APRICOT_VERSION;
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
    // ONE definition of "nobody at the keyboard", used for all three things
    // that follow from it: no vsync above, no focus steal, no cursor capture.
    // A second rule here would eventually disagree with the first, and the way
    // that presents is a check that takes the pointer on some machines only.
    const bool unattended = frame_limit_ > 0 && !attended_;
    cfg.take_focus = !unattended;

    if (!window_.init(cfg)) {
        AP_ERROR("window init failed; cannot continue");
        return false;
    }

    // Before anything can ask for mouse look, so the cursor is never taken
    // even for the one frame between the window opening and the first check.
    input_.set_unattended(unattended);
    if (unattended)
        AP_INFO("unattended run: the window takes no focus and no cursor");

    // The overlay is optional. Losing it must not lose the app.
    if (!overlay::init(window_)) {
        AP_WARN("overlay unavailable; continuing without it");
    }
    // The UI backend bound its own objects while initialising, so nothing the
    // bind cache believes is true any more.
    gl_state::invalidate_all();

    // Present the layered intro before the cold world fill. Interactive
    // launches keep this frame on screen while the authored city, traffic and
    // terrain finish loading; bounded renderer smoke skips the human-facing
    // present so --frames remains a direct world/render test.
    const bool show_loading_screen = frame_limit_ <= 0;
    WallClock::time_point loading_screen_started;
    if (!loading_screen_.init(
            "textures/ui/probable-cause-intro-plate-v2.png")) {
        AP_ERROR("loading screen init failed; cannot continue");
        return false;
    }
    if (!hud_.init()) {
        AP_ERROR("hud init failed; cannot continue");
        return false;
    }
    const auto present_loading = [&](const char* stage) {
        if (!show_loading_screen) return;
        poll_events();
        if (window_.minimised()) return;
        loading_screen_.render(window_.width(), window_.height());
        const glm::vec2 vp = UiCanvas::from_drawable(
            {window_.width(), window_.height()}).size;
        hud_.begin(vp);
        const float left = vp.x * 0.06f;
        const float y = vp.y * 0.91f;
        const float alpha = std::clamp(loading_screen_.elapsed(), 0.0f, 1.0f);
        if (stage) {
            // Indeterminate activity, not a percentage or an invented work bar.
            const float phase = loading_screen_.elapsed() * 2.0f;
            for (int i = 0; i < 3; ++i) {
                const float strength = 0.35f + 0.65f *
                    (0.5f + 0.5f * std::sin(phase - static_cast<float>(i)));
                hud_.rect({left + static_cast<float>(i) * 12.0f, y + 11}, {left + static_cast<float>(i) * 12.0f + 5, y + 16},
                          {0.86f, 0.20f, 0.14f, alpha * strength});
            }
            hud_.text(stage, {left + 52, y}, 19, {0.75f, 0.74f, 0.69f, alpha});
        }
        hud_.end();
        window_.swap();
    };
    if (show_loading_screen) {
        loading_screen_started = WallClock::now();
        present_loading("INITIALIZING");
    }

    // THE WORLD IS KEYED ON THE MAP SEED, NOT THE RUN SEED (PENG-41).
    //
    // docs/architecture.md's amendment made concrete: the world is a pure
    // function of (map, seed, coord). city::kMapSeed is pinned in the map
    // tables and selects the noise detail under the authored skeleton, so
    // Pinatty is the same place in every session. seed_ below is the SESSION
    // -- weather, ambient variation, the placeholder box field -- and it is
    // the one that will eventually come from a save file.
    collider_ = TerrainCollider(city::kMapSeed);
    if (controls_.snow_depth_override_m >= 0.0f) {
        snowpack_.set_depth_m(controls_.snow_depth_override_m);
    } else if (dev_menu_.weather() == DevWeatherPreset::Dynamic) {
        snowpack_.set_depth_m(conditions_at(seed_, step_index_).snow_depth_m);
    } else {
        snowpack_ = {};
    }
    const SnowpackCollision opening_snow_collision =
        snowpack_collision_from_depth(snowpack_.depth_m());
    collider_.set_snow_collision_depth(opening_snow_collision.active
        ? static_cast<float>(opening_snow_collision.depth_m) : 0.0f);

    // spawn_vehicle settles the car on its springs and aligns it to the slope
    // it is standing on. Assigning a position by hand instead drops it in with
    // its struts at free length and the first step launches it, which looks
    // like a physics bug and is not one.
    // The player uses GTA-style two-pedal direction control: S brakes first,
    // then becomes reverse once the car is stopped. Traffic keeps the pure
    // brake/explicit-gear contract through VehicleTuning's opt-in flag.
    set_driving_mechanics(DrivingMechanicsStyle::ClassicGta);
    tuning_=player_model_tuning(driving_mechanics_style_,start_car_);
    car_ = spawn_vehicle(tuning_, collider_, start_position_.x,
                         start_position_.y, start_heading_radians_);
    car_.mechanical_key=splitmix64_mix(car_.mechanical_key ^ seed_);
    prev_car_ = car_;

    // Audio is non-fatal by contract. Normal vehicle playback uses only the
    // selected recordings; procedural placeholders stay out of the live mix.
    present_loading("LOADING AUDIO");
    audio_device_.start(player_car_audio_overrides());
    if (audio_device_.running()) {
        if (!intro_audio_.load(asset_path("audio/intro/probable-cause-title-score-v1.wav"),
                               audio_device_.bank().rain)) {
            AP_WARN("title audio unavailable; continuing with city ambience");
        }
        intro_audio_.update(audio_device_.mixer(), show_loading_screen, 1.0f);
    }
    city_audio_.start(audio_device_.mixer(), audio_device_.bank());
    vehicle_audio_.start(audio_device_.mixer(), audio_device_.bank());
    (void)vehicle_leak_ding();
    traffic_idle_audio_.start(audio_device_.mixer(), audio_device_.bank());
    traffic_horn_audio_.start(audio_device_.mixer(), audio_device_.bank());
    AP_INFO("traffic horns: %zu recorded variations loaded",traffic_horn_audio_.loaded_clip_count());
    police_siren_.start(audio_device_.mixer());
    apply_ui_settings();

    // --- renderer and its passes -------------------------------------------
    // Every one of these is fatal if it fails. A renderer that starts with a
    // broken shader draws black, and black is the hardest possible symptom to
    // work backwards from; better to refuse to start and say which stage died.
    present_loading("PREPARING RENDERER");
    if (!renderer_.init()) {
        AP_ERROR("renderer init failed; cannot continue");
        return false;
    }
    if (!sky_.init()) {
        AP_ERROR("sky init failed; cannot continue");
        return false;
    }
    if (!ocean_.init()) return false;
    if (!rain_.init(seed_)) {
        AP_ERROR("precipitation init failed; cannot continue");
        return false;
    }
    if (!tire_tracks_.init()) {
        AP_ERROR("tire-track renderer init failed; cannot continue");
        return false;
    }
    if (!tornado_.init()) {
        AP_ERROR("tornado renderer init failed; cannot continue");
        return false;
    }
    present_loading("BUILDING CITY MAP");
    game_ui_.build_map();
    const std::vector<RoadSpine> authored_roads = city::map_spines();
    AP_INFO("city map built from %zu vector land patches, %zu roads and %d districts",
            game_ui_.land_cell_count(), authored_roads.size(),
            city::kDistrictCount);

    // --- player car ----------------------------------------------------------
    // The alpha's body mesh is wheel-less on purpose. PlayerCarVisual attaches
    // four copies of the shared wheel model and drives them from the sim's live
    // suspension length, steering angle and accumulated spin.
    present_loading("LOADING VEHICLES");
    if (!car_visual_.init(renderer_, scene_, tuning_, car_)) {
        AP_ERROR("player car model failed; cannot continue");
        return false;
    }
    if (!car_visual_.select(scene_,tuning_,car_,start_car_)) return false;
    if (!paint_pool_.init(renderer_)) return false;
    respray_clips_ = build_respray_clips(respray_clip_paths());
    dev_menu_.set_player_car(car_visual_.active_car());
    vehicle_audio_.set_model(player_car_definition(car_visual_.active_car()).mesh_path);
    if (!vehicle_effects_.init(renderer_)) {
        AP_ERROR("vehicle fluid effects failed; cannot continue");
        return false;
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
    plan_lot_plows();

    // So it is checked rather than commented. The drawn surface under the spawn
    // point, reconstructed from the streamer's seed, against the collider's
    // underlying terrain with any temporary snow layer removed. These are the
    // same function of the same seed, so the only tolerance that means anything
    // is zero.
    {
        const float drawn = mesh_height_at(world_.streamer().seed(), 0.0f, 0.0f);
        const float driven = collider_.height(0.0f, 0.0f) -
                             collider_.snow_collision_depth();
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
    present_loading("BUILDING STREETS");
    if (!world_.set_roads(renderer_, scene_, collider_, authored_roads)) {
        AP_ERROR("road bake/upload failed; cannot continue");
        return false;
    }
    if (!traffic_visual_.init(renderer_, scene_, world_.lanes(),
                              world_.traffic_tuning(), collider_)) {
        AP_ERROR("traffic models or signals failed to initialize");
        return false;
    }
    world_.set_police_officer_vehicle_layout(traffic_visual_.police_officer_vehicle_layout());
    if (overhead_qa_) {
        // Diagnostic framing only; normal play keeps both shipped budgets. Each
        // actor class gets the same policy against its OWN retire radius, and
        // against the terrain floor rather than sea level, so the view holds
        // over deep coastal ground as well as over this inland junction.
        const CrowdTuning& crowd = world_.traffic_tuning();
        traffic_visual_.set_vehicle_draw_distance(
            overhead_qa_actor_draw_distance_m(crowd.vehicle_retire_m));
        character_visual_.set_npc_draw_distance(
            overhead_qa_actor_draw_distance_m(crowd.ped_retire_m));
    }

    // The first authored district: the player begins beside a working gas
    // station and motel, with apartments and a drive-through on nearby blocks.
    // Rendering and collision come from the same city table, so the buildings
    // cannot be visible boxes the car drives straight through.
    present_loading("LOADING O'HAVEN");
    if (!world_.set_starting_area(renderer_, scene_, collider_)) {
        AP_ERROR("starting area failed; cannot continue");
        return false;
    }
    snow_shelter_.build(world_.precipitation_cover());
    collider_.set_snow_shelter(&snow_shelter_);
    if (!renderer_.set_snow_shelter(snow_shelter_)) {
        AP_ERROR("snow shelter upload failed; cannot continue");
        return false;
    }
    AP_INFO("snow shelter: %zu authored roof and ceiling covers", snow_shelter_.boxes().size());
    if (road_start_qa_) {
        // Authored parking slabs and other firm ground overlays are attached
        // by set_starting_area(). Settle after that so --start-at can exercise
        // the actual top contact instead of the raw terrain hidden below it.
        car_ = spawn_vehicle(tuning_, collider_, start_position_.x,
                             start_position_.y, start_heading_radians_);
        const auto ground = collider_.probe_down(
            {start_position_.x, 120.0f, start_position_.y}, 160.0f);
        if (ground.hit)
            car_.position.y = ground.point.y + static_ride_height(tuning_);
        car_.mechanical_key = splitmix64_mix(car_.mechanical_key ^ seed_);
        prev_car_ = car_;
    }

    reset_aircraft();
    reset_helicopter();
    reset_boat();
    if (!trailer_visual_.init(renderer_,scene_)) return false;
    reset_freight_yard(true);
    // Start as a person beside the parked car. Player and crowd both use the
    // supplied PSX character pack through its proven Probable Cause rigs.
    character_spawned_ = true;
    place_character_next_to_car();
    if(start_player_position_set_ && !start_driving_) {
        auto placed=spawn_character(collider_,start_player_position_.x,start_player_position_.y,-start_heading_radians_);
        if(start_player_height_set_) {
            const auto floor=collider_.probe_down({placed.position.x,start_player_height_+.25f,placed.position.z},.5f);
            if(!floor.hit) { AP_ERROR("--start-player-height has no supporting floor");return false; }
            placed.position.y=floor.point.y;
        }
        if(!character_position_clear(collider_,placed.position,CharacterTuning{})) {
            AP_ERROR("--start-player-at is blocked by world geometry");return false;
        }
        player_character_=prev_player_character_=placed;
    }
    present_loading("LOADING CHARACTERS");
    if (!character_visual_.init(player_character_)) {
        AP_ERROR("character models failed; cannot continue");
        return false;
    }
    if (!weapon_visual_.init(renderer_,scene_)) return false;
    police_weapon_visual_.init(renderer_);
    weapon_shot_clip_=synth_pistol_shot();
    override_clip_from_wav(weapon_shot_clip_,asset_path("audio/weapons/Glock17_Shoot_004.wav"));
    weapon_reload_clip_=synth_pistol_reload();
    if (!fire_sprites_.init(renderer_)) {
        AP_ERROR("flame atlas failed to load; cannot continue");
        return false;
    }
    if (!molotov_visual_.init(renderer_,scene_,fire_sprites_)) return false;
    if (!fire_visual_.init(renderer_,scene_,fire_sprites_)) return false;
    if (!wreck_visual_.init(renderer_,scene_,fire_sprites_)) return false;
    // RECORDED ONLY, with no synthesised fallback, for the same reason
    // footsteps and the city bed have none: a generated pane of glass is a
    // burst of noise and a generated fire is a hiss, and the ear knows. A
    // missing file leaves the clip empty, and the mixer treats an empty clip
    // as silence rather than as an error. See assets/audio/weapons/SOURCES.md.
    override_clip_from_wav(molotov_glass_clip_,
        asset_path("audio/weapons/runtime/molotov_glass.wav"));
    override_clip_from_wav(molotov_whoosh_clip_,
        asset_path("audio/weapons/runtime/molotov_whoosh.wav"));
    override_clip_from_wav(fire_loop_clip_,
        asset_path("audio/weapons/runtime/fire_loop.wav"));
    // Optional: no blast recording ships yet, and the car bomb layers the
    // crash, the glass and the whoosh without it.
    override_clip_from_wav(car_bomb_blast_clip_,
        asset_path("audio/weapons/runtime/car_bomb_blast.wav"));
    if (start_driving_ &&
        (has_animated_driver(car_visual_.active_car()) || road_start_qa_)) {
        on_foot_=false;
        vehicle_audio_.set_model(player_car_definition(car_visual_.active_car()).mesh_path);
        vehicle_audio_.enter_vehicle(false,car_.position);
        collider_.set_kinematic_enabled(current_vehicle_collider_,false);
    } else if (start_driving_) toggle_player_mode();

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
        present_loading("PREPARING WORLD");
        const WallClock::time_point t0 = WallClock::now();
        last_fill_steps_ = world_.fill(scene_, renderer_, player_focus_position());
        last_fill_frame_ = frames_rendered_;
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
    chase_camera_.reset();
    if (camera_mode_ >= 0) chase_camera_.set_mode(camera_mode_);
    if (camera_orbit_set_) {
        chase_camera_.set_orbit(camera_orbit_yaw_, camera_orbit_pitch_);
        chase_camera_.set_auto_recenter(false);
    }
    camera_obstruction_distance_ = -1.0f;
    seen_impact_count_ = car_.impact_count;
    update_camera(0.0f);

    // A default that shows the whole feature set doing something on launch.
    update_weather();

    // Interactive launches begin at the title. The bounded renderer harness
    // has nobody to press Start, so let it drive immediately and keep its
    // physics/streaming coverage intact.
    init_save_game();
    if (start_wanted_level_ > 0)
        wanted_.add_heat(wanted_heat_for_level(start_wanted_level_));
    if (frame_limit_ > 0 || start_in_game_) ui_.enter_game();
    if (opening_preview_) { ui_.enter_game();begin_new_game(); }
    if (delivery_preview_ || delivery_check_) {
        ui_.enter_game();mission_stage_=MissionStage::DeliveryActive;on_foot_=true;
        const auto devon=city::devon_position();
        player_character_=spawn_character(collider_,devon.x,devon.z-1.05f,3.14159265f);
        prev_player_character_=player_character_;
        world_.fill(scene_,renderer_,player_character_.position);
        update_camera(0);
        if (delivery_preview_ && !begin_delivery_cutscene()) return false;
    }
    input_.set_ui_mode(opening_cutscene_.active() || ui_.modal());

    // Startup work above counts toward the seven seconds. If it finishes
    // early, keep presenting the plate and polling events for the remainder so
    // the window stays responsive and resize-safe instead of literally
    // blocking in a blind sleep.
    if (show_loading_screen) {
        while (!input_.quit_requested()) {
            const double shown_seconds = std::chrono::duration<double>(
                WallClock::now() - loading_screen_started).count();
            if (intro_ready(shown_seconds, true)) break;

            present_loading(nullptr);
            SDL_Delay(8u);
        }
        const double shown_seconds = std::chrono::duration<double>(
            WallClock::now() - loading_screen_started).count();
        AP_INFO("loading screen shown for %.2f s", shown_seconds);
    }
    loading_screen_.mark_ready();

    AP_INFO("map 0x%016llX, run seed 0x%016llX, player spawned on foot at "
            "(%.2f, %.2f), car at %.2f m (ground %.2f m)",
            static_cast<unsigned long long>(city::kMapSeed),
            static_cast<unsigned long long>(seed_),
            static_cast<double>(player_character_.position.x),
            static_cast<double>(player_character_.position.z),
            static_cast<double>(car_.position.y),
            static_cast<double>(collider_.height(start_position_.x,
                                                 start_position_.y)));

    gl_errors_ += drain_gl_errors("after init");

    running_ = true;
    return true;
}

void App::shutdown() {
    opening_cutscene_.destroy();
    intro_audio_.stop(audio_device_.mixer());
    rain_audio_.stop(audio_device_.mixer());
    // Order matters only in that the GL resources must die while the context
    // is still alive, so everything gfx goes before the window.
    //
    // The world goes FIRST of all, because it is the only thing here that owns
    // resources jointly with something else: its chunk meshes live in the
    // renderer's table and its nodes live in the scene, and it is the only
    // object that knows which mesh belongs to which node.
    vehicle_leak_warning_.stop(audio_device_.mixer());
    vehicle_audio_.stop();
    police_siren_.stop();
    traffic_idle_audio_.stop();
    traffic_horn_audio_.stop();
    city_audio_.stop();
    audio_device_.stop();
    traffic_visual_.destroy(scene_);
    police_weapon_visual_.destroy(scene_);
    weapon_visual_.destroy(scene_);
    molotov_visual_.destroy(scene_);
    fire_visual_.destroy(scene_);
    wreck_visual_.destroy(scene_);
    fire_sprites_.destroy();
    character_visual_.destroy();
    vehicle_effects_.destroy(scene_);
    world_.shutdown(scene_, renderer_);
    trailer_visual_.destroy(scene_);
    car_visual_.destroy(scene_);
    for (auto& parked:parked_vehicles_) parked.visual.destroy(scene_);
    parked_vehicles_.clear();
    hud_.destroy();
    tornado_.destroy();
    tire_tracks_.destroy();
    rain_.destroy();
    sky_.destroy();
    ocean_.destroy();
    loading_screen_.destroy();
    tiled_lighting_.destroy();
    // Its queries are GL objects like any other and must die while the context
    // is still alive.
    gpu_timer_.destroy();
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

bool App::save_screenshot(const std::string& path) {
    const int width = window_.width();
    const int height = window_.height();
    if (width <= 0 || height <= 0) return false;
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
        uint8_t* bottom = pixels.data() +
            static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::swap_ranges(top, top + row_bytes, bottom);
    }

    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".png") == 0) {
        const bool saved = stbi_write_png(path.c_str(), width, height, 4,
                                          pixels.data(), width * 4) != 0;
        if (!saved) AP_ERROR("screenshot PNG save failed: '%s'", path.c_str());
        else AP_INFO("screenshot saved to '%s'", path.c_str());
        return saved;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        AP_ERROR("screenshot surface failed: %s", SDL_GetError());
        return false;
    }
    const bool saved = SDL_SaveBMP(surface, path.c_str()) == 0;
    if (!saved) AP_ERROR("screenshot save failed: %s", SDL_GetError());
    else AP_INFO("screenshot saved to '%s'", path.c_str());
    SDL_FreeSurface(surface);
    return saved;
}

void App::begin_bug_report() {
    if (bug_report_.open) return;
    dev_menu_.close();
    bug_report_restore_mouse_ = input_.mouse_look();
    input_.set_mouse_look(false);
    bug_report_.begin(player_focus_position(), player_focus_forward());
    input_.set_ui_mode(true);
    input_.consume_edges();
    clock_.reset();
    AP_INFO("bug report opened at position %.3f, %.3f, %.3f",
            static_cast<double>(bug_report_.position.x),
            static_cast<double>(bug_report_.position.y),
            static_cast<double>(bug_report_.position.z));
}

void App::capture_and_submit_bug_report() {
    bug_report_capture_pending_ = false;
    const fs::path checkout = find_checkout_root();
    const fs::path directory = checkout.empty()
        ? fs::path{} : unique_bug_report_directory(checkout);
    if (directory.empty()) {
        vehicle_interaction_notice_ = "BUG REPORT SAVE FAILED";
        vehicle_notice_until_ = step_index_ + 300;
        AP_ERROR("bug report: could not find checkout or create report directory");
        return;
    }

    const fs::path report_path = directory / "report.md";
    const fs::path screenshot_path = directory / "screenshot.png";
    const fs::path log_path = directory / "codex.log";
    const char* mode = on_foot_ ? "on foot" :
        (in_aircraft_ ? "aircraft" :
         (in_helicopter_ ? "helicopter" : (in_boat_ ? "boat" : "vehicle")));
    const std::string report_text = bug_report_markdown(
        bug_report_, static_cast<unsigned long long>(frames_rendered_),
        static_cast<unsigned long long>(step_index_), mode, APRICOT_VERSION);

    std::ofstream report_file(report_path);
    report_file << report_text;
    report_file.close();
    if (!report_file || !save_screenshot(screenshot_path.string())) {
        vehicle_interaction_notice_ = "BUG REPORT CAPTURE FAILED";
        vehicle_notice_until_ = step_index_ + 300;
        AP_ERROR("bug report: capture failed; partial report kept at '%s'",
                 directory.c_str());
        return;
    }

    const bool launched = launch_codex_report(
        checkout, screenshot_path, report_path, log_path, report_text);
    vehicle_interaction_notice_ = launched
        ? "BUG REPORT SENT TO CODEX"
        : "BUG REPORT SAVED - CODEX UNAVAILABLE";
    vehicle_notice_until_ = step_index_ + 360;
    if (launched) {
        AP_INFO("bug report sent to Codex; files: '%s'", directory.c_str());
    } else {
        AP_WARN("bug report saved at '%s', but Codex could not be launched",
                directory.c_str());
    }
}

void App::step_weapon_use(bool available, float dt) {
    weapon_visual_.step_particles(dt);
    weapon_hit_feedback_=std::max(0.f,weapon_hit_feedback_-std::max(0.f,dt));
    WeaponUseInput controls;
    controls.available=available;
    controls.aim=weapon_aim_mouse_ || weapon_aim_pad_ || weapon_aim_toggle_;
    controls.fire_pressed=weapon_fire_pending_;
    controls.reload_pressed=weapon_reload_pending_;
    const bool was_reloading=weapon_use_.reloading;
    const bool fired=weapon_use_.step(weapon_wheel_.equipped,controls,dt);
    VoiceParams sound;
    sound.category=Category::Impacts;
    sound.gain=.65f;
    if (!was_reloading && weapon_use_.reloading)
        audio_device_.mixer().play_oneshot(&weapon_reload_clip_,sound);
    if (dt>0.f || !available) weapon_fire_pending_=weapon_reload_pending_=false;
    if (!fired) return;
    ++weapon_shots_;
    audio_device_.mixer().play_oneshot(&weapon_shot_clip_,sound);
    // Pick the crosshair target first, then trace from the actual held gun.
    // This second trace prevents the shoulder camera firing through nearby cover.
    const auto aim=weapon_aim_camera(player_character_.position,player_character_.view_yaw,
        player_character_.view_pitch,weapon_use_.aim_blend);
    ChaseCameraPose pose;
    pose.target=aim.target;pose.collision_pivot=aim.pivot;pose.desired_eye=aim.eye;
    pose.fov_y=aim.fov_y;
    apply_drunk_camera(pose,drunk_);
    if (transition_camera_release_>0.f) {
        const float blend=vehicle_transition_ease(1.f-transition_camera_release_);
        pose.target=glm::mix(transition_camera_.position+transition_camera_.forward()*4.8f,pose.target,blend);
        pose.desired_eye=glm::mix(transition_camera_.position,pose.desired_eye,blend);
    }
    glm::vec3 eye=pose.desired_eye;
    const glm::vec3 eye_ray=pose.desired_eye-pose.collision_pivot;
    const float eye_distance=glm::length(eye_ray);
    if (eye_distance>1e-4f) {
        const auto obstruction=collider_.raycast(pose.collision_pivot,eye_ray,eye_distance);
        float allowed=obstruction.hit ? std::max(.15f,obstruction.distance-.35f) : eye_distance;
        // Keep the current camera's eased return from cover while using this
        // tick's aim. New obstructions still pull the sight in immediately.
        if (camera_obstruction_distance_>=0.f) allowed=std::min(allowed,camera_obstruction_distance_);
        eye=pose.collision_pivot+eye_ray/eye_distance*std::min(allowed,eye_distance);
    }
    eye.y=std::max(eye.y,collider_.height(eye.x,eye.z)+1.2f);
    const glm::vec3 direction=glm::normalize(pose.target-eye);
    const auto sight=collider_.raycast(eye,direction,120.f);
    const auto sight_ped=world_.traffic().raycast_ped(eye,direction,
        sight.hit ? sight.distance : 120.f);
    const glm::vec3 target=sight_ped.hit ? sight_ped.point :
        eye+direction*(sight.hit ? sight.distance : 120.f);
    glm::vec3 muzzle,barrel;
    if (!weapon_visual_.muzzle_world(muzzle,barrel))
        muzzle=player_character_.position+glm::vec3{0,1.35f,0};
    else {
        const auto turn=character_root_rotation(player_character_.view_yaw)*
            glm::inverse(character_root_rotation(weapon_socket_player_yaw_));
        muzzle=player_character_.position+turn*(muzzle-weapon_socket_player_position_);
    }
    const glm::vec3 travel=target-muzzle;
    const float distance=glm::length(travel);
    weapon_visual_.clear_impact();
    // The held mesh may poke through thin cover. Don't let a muzzle already
    // beyond that wall turn a blocked shot into an unobstructed body hit.
    const glm::vec3 shoulder=player_character_.position+glm::vec3{0,1.35f,0};
    const glm::vec3 reach=muzzle-shoulder;
    const float reach_distance=glm::length(reach);
    if (reach_distance>.01f) {
        const auto cover=collider_.raycast(shoulder,reach,reach_distance);
        if (cover.hit) {
            weapon_visual_.show_impact(shoulder+reach/reach_distance*cover.distance);
            return;
        }
    }
    if (distance>.01f) {
        const auto hit=collider_.raycast(muzzle,travel/distance,std::min(120.f,distance+.1f));
        const auto body=world_.shoot_ped(muzzle,travel/distance,
            hit.hit ? hit.distance : std::min(120.f,distance+.1f),static_cast<int64_t>(step_index_));
        if (body.hit) {
            weapon_visual_.show_blood(body.point,travel/distance,weapon_shots_);
            ++weapon_body_hits_;
            weapon_hit_feedback_=.25f;
            if (body.officer) {
                // The heat lands whether or not anybody watched. Shooting a
                // uniformed officer is not a crime that needs a witness cone;
                // see game/police_combat.h.
                wanted_.add_heat(body.killed ? kOfficerKilledHeat
                                             : kOfficerWoundedHeat,
                                 WantedSystem::Crime::OfficerAssault);
                if (body.killed) ++weapon_kills_;
                AP_INFO("pistol %s police officer %llu/%u; wanted %d",
                    body.killed ? "killed" : "hit",
                    static_cast<unsigned long long>(body.lane_key),body.slot,
                    wanted_.level());
            } else {
                // Same rule, a tier down. A wound is a crime; the round that
                // kills is a bigger one, and `killed` is the one-shot edge so
                // the player is charged for the death exactly once.
                wanted_.add_heat(body.killed ? kCivilianKilledHeat
                                             : kCivilianWoundedHeat,
                                 body.killed ? WantedSystem::Crime::Violent
                                             : WantedSystem::Crime::Assault);
                if (body.killed) {
                    ++weapon_kills_;
                    police_escalation_.record_civilian_kill();
                }
                AP_INFO("pistol %s pedestrian %llu/%u; wanted %d",
                    body.killed ? "killed" : "hit",
                    static_cast<unsigned long long>(body.lane_key),body.slot,
                    wanted_.level());
            }
        } else if (hit.hit) weapon_visual_.show_impact(muzzle+travel/distance*hit.distance);
    }
}

void App::poll_events() {
    playtest_.pump();
    input_.begin_frame();
    player_horn_pending_=false;
    bank_input_consumed_ = false;
    weapon_input_consumed_=false;
    paint_input_consumed_=false;
    gun_store_.input_consumed=false;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        // A lighting comparison must keep exactly the same camera and scene
        // in both phases, even if keys arrive while this QA window has focus.
        if(lighting_benchmark_ && e.type!=SDL_QUIT && e.type!=SDL_WINDOWEVENT) continue;
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

        // Releases and focus changes must reach weapons even while a modal owns input.
        if (e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
            weapon_focus_=false;
            weapon_aim_mouse_=weapon_aim_pad_=weapon_aim_toggle_=false;
            weapon_fire_pad_=true; // require trigger release after focus returns
            weapon_fire_pending_=weapon_reload_pending_=false;
            molotov_throw_pending_=false;
        }
        if (e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)
            weapon_focus_=true;
        if (e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_RIGHT)
            weapon_aim_mouse_=false;
        if (e.type==SDL_CONTROLLERDEVICEREMOVED) {
            weapon_aim_pad_=false;
            weapon_fire_pad_=true;
        }
        const bool weapon_available=weapon_focus_ && on_foot_ &&
            ui_.screen()==UiScreen::Driving && !dev_menu_.open() && !bug_report_.open &&
            !bank_interaction_.modal() && !bank_input_consumed_ && !opening_cutscene_.active() &&
            !vehicle_transition_.active() && !boat_transition_.active() &&
            !weapon_wheel_.open && !weapon_input_consumed_ && !gun_store_holds_input() &&
            (weapon_wheel_.equipped==WeaponId::Pistol ||
             weapon_wheel_.equipped==WeaponId::Molotov);
        if (weapon_available && e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_q &&
            !(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
            if (!e.key.repeat) {
                weapon_aim_toggle_=!weapon_aim_toggle_;
                if (weapon_aim_toggle_) input_.set_mouse_look(true);
            }
            continue;
        }
        if (e.type==SDL_CONTROLLERAXISMOTION) {
            if (e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT)
                weapon_aim_pad_=weapon_available && e.caxis.value>16000;
            if (e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                const bool down=e.caxis.value>16000;
                if (weapon_available && down && !weapon_fire_pad_)
                    weapon_fire_pending_=molotov_throw_pending_=true;
                weapon_fire_pad_=down;
            }
        }
        if (weapon_available && e.type==SDL_MOUSEBUTTONDOWN) {
            if (e.button.button==SDL_BUTTON_RIGHT) {
                weapon_aim_mouse_=true; input_.set_mouse_look(true);
            }
            // The first left click captures the cursor; it must not also shoot.
            // Fire and throw are the same button. The pistol's state machine
            // is inactive while a molotov is equipped and vice versa, so both
            // flags are armed here and exactly one of them is ever spent.
            if (e.button.button==SDL_BUTTON_LEFT && input_.mouse_look())
                weapon_fire_pending_=molotov_throw_pending_=true;
        }
        const bool weapon_reload=(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_r) ||
            (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_X);
        if (weapon_available && weapon_reload) {
            if (e.type!=SDL_KEYDOWN || !e.key.repeat) weapon_reload_pending_=true;
            continue;
        }

        // The reporter owns all typing while open. ImGui already saw the
        // event above; keeping it out of the game mapper stops a bug sentence
        // from steering the car or opening another screen underneath it.
        if (bug_report_.open) {
            input_.handle_event(e);
            continue;
        }

        if (opening_cutscene_.active()) { input_.handle_event(e);continue; }

        if (bank_interaction_.modal() || bank_input_consumed_) {
            int w = 0, h = 0;
            SDL_GetWindowSize(window_.sdl(), &w, &h);
            bank_interaction_.event(e, {w, h}, bank_vault_);
            bank_input_consumed_ = true;
            input_.handle_event(e);
            continue;
        }
        if (route_paint_shop_event(e)) continue;
        if (route_gun_store_event(e)) continue;

        const bool wheel_down=(e.type==SDL_KEYDOWN && e.key.repeat==0 && e.key.keysym.sym==SDLK_TAB) ||
            (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        const bool wheel_up=(e.type==SDL_KEYUP && e.key.keysym.sym==SDLK_TAB) ||
            (e.type==SDL_CONTROLLERBUTTONUP && e.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        if (wheel_down && ui_.screen()==UiScreen::Driving && on_foot_ && !dev_menu_.open() &&
            !vehicle_transition_.active() && !boat_transition_.active() && !weapon_wheel_.open) {
            weapon_restore_mouse_=input_.mouse_look();weapon_wheel_.begin();
            input_.set_ui_mode(true);weapon_input_consumed_=true;
            int w=0,h=0;SDL_GetWindowSize(window_.sdl(),&w,&h);
            SDL_WarpMouseInWindow(window_.sdl(),w/2,h/2);
            continue;
        }
        if (weapon_wheel_.open) {
            weapon_input_consumed_=true;
            const bool focus_lost=e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST;
            const bool cancel=focus_lost ||
                (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) ||
                (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_B);
            if (wheel_up || cancel) {
                close_weapon_wheel(weapon_wheel_,economy_,!cancel);input_.set_ui_mode(ui_.modal());
                if (weapon_restore_mouse_ && !focus_lost) input_.set_mouse_look(true);
                input_.consume_edges();clock_.reset();
                continue;
            }
            if (e.type==SDL_MOUSEMOTION) {
                int w=0,h=0;SDL_GetWindowSize(window_.sdl(),&w,&h);
                const float scale=std::max(1.f,static_cast<float>(h)*.25f);
                weapon_wheel_.point((static_cast<float>(e.motion.x)-static_cast<float>(w)*.5f)/scale,
                    (static_cast<float>(e.motion.y)-static_cast<float>(h)*.5f)/scale);
            }
            if (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_1) weapon_wheel_.hovered=WeaponId::Unarmed;
            if (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_2) weapon_wheel_.hovered=WeaponId::Pistol;
            if (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_3) weapon_wheel_.hovered=WeaponId::Molotov;
            input_.handle_event(e);
            continue;
        }

        // Horn and siren are audio/presentation controls, outside the replay format.
        const bool road_vehicle_controls=weapon_focus_ && ui_.screen()==UiScreen::Driving &&
            !dev_menu_.open() && !bug_report_.open && !bank_interaction_.modal() &&
            !bank_input_consumed_ && !opening_cutscene_.active() && !weapon_wheel_.open &&
            !weapon_input_consumed_ && !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            !vehicle_transition_.active() && !boat_transition_.active();
        if (road_vehicle_controls && e.type==SDL_KEYDOWN && e.key.repeat==0 &&
            e.key.keysym.sym==SDLK_h) player_horn_pending_=true;
        // R / pad X in a Rook's bay the car has pulled into and stopped in.
        // Taken here, before the mapper, so it never reaches the trailer drop.
        if (try_open_paint_shop(e, road_vehicle_controls)) continue;
        // K: fit a car bomb in a Rook's bay, or set off the one you have.
        // game/car_bomb.h decides which.
        if (try_car_bomb_key(e)) continue;
        const bool emergency_toggle=(e.type==SDL_KEYDOWN && e.key.repeat==0 &&
            e.key.keysym.sym==SDLK_j) || (e.type==SDL_CONTROLLERBUTTONDOWN &&
            e.cbutton.button==SDL_CONTROLLER_BUTTON_LEFTSTICK);
        if (emergency_toggle && road_vehicle_controls &&
            has_police_lightbar(car_visual_.active_car())) {
            police_emergency_enabled_=!police_emergency_enabled_;
        }
        // Same key in a convertible. The canvas moves no dimension the physics
        // reads, so like the siren it is presentation and costs no replay
        // format change; it is still stepped on the sim cadence below so the
        // fold takes the same 2.4 s however fast the frames arrive.
        if (emergency_toggle && road_vehicle_controls &&
            is_convertible(car_visual_.active_car())) soft_top_toggle_pending_=true;
        if (emergency_toggle && road_vehicle_controls && has_passenger_door(car_visual_.active_car())) {
            if (passenger_door_target_) passenger_door_target_=false;
            else if (glm::length(car_.velocity)<.5f &&
                vehicle_transition_door_clear(car_visual_.active_car(),car_visual_.fitted_body_transform(car_),true)) {
                passenger_door_target_=true;
            } else {
                vehicle_interaction_notice_="Stop with room on the passenger side";
                vehicle_notice_until_=step_index_+240;
            }
        }
        // The bare-fist jab, on F or controller X while on foot and unarmed.
        // Handled here rather than through InputFrame for the same reason as
        // the siren above: it drives the character animator, which runs on the
        // render clock. The DAMAGE it does is applied where the animator's
        // one-shot contact latch is consumed, in the sync block below.
        // city/character_punch.h owns the timing; app/character_animation.h is
        // the caller. A RELEASE always clears the level, whatever the context,
        // so leaving the state that allows a punch cannot leave one latched on.
        const bool punch_down=(e.type==SDL_KEYDOWN && e.key.repeat==0 &&
            e.key.keysym.sym==SDLK_f) || (e.type==SDL_CONTROLLERBUTTONDOWN &&
            e.cbutton.button==SDL_CONTROLLER_BUTTON_X);
        const bool punch_up=(e.type==SDL_KEYUP && e.key.keysym.sym==SDLK_f) ||
            (e.type==SDL_CONTROLLERBUTTONUP &&
             e.cbutton.button==SDL_CONTROLLER_BUTTON_X);
        if (punch_up) character_visual_.set_player_punch_held(false);
        if (punch_down && ui_.screen()==UiScreen::Driving && !dev_menu_.open() &&
            on_foot_ && !vehicle_transition_.active() &&
            !boat_transition_.active() &&
            weapon_wheel_.equipped==WeaponId::Unarmed) {
            character_visual_.set_player_punch_held(true);
        }
        // The instancing A/B toggle is handled HERE rather than in InputMapper
        // on purpose. InputFrame is the replay tape format; a debug toggle
        // recorded into a tape would change what a replay does, and a replay
        // that disagrees with the run it recorded is worse than none.
        if (ui_.screen() == UiScreen::Driving &&
            e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F7) {
            controls_.instancing = !controls_.instancing;
            AP_INFO("instancing %s", controls_.instancing ? "ON" : "OFF (naive path)");
        }

        // F2 captures a bug without making the player leave the broken spot.
        if (ui_.screen() == UiScreen::Driving &&
            e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F2) {
            begin_bug_report();
            input_.handle_event(e);
            continue;
        }

        // Keep the large profiling panel out of normal play. Like the other
        // host-only debug keys, visibility is not part of a replay InputFrame.
        if (ui_.screen() == UiScreen::Driving &&
            e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F3) {
            controls_.toggle_stats();
            AP_INFO("debug stats %s",
                    controls_.stats_visible ? "VISIBLE" : "HIDDEN");
        }

        // F4 marks the moment. The single most valuable column in the
        // performance log is the one a human writes: a profiler can rank
        // frames by cost all day and still not know which of them the player
        // actually felt. Latched, then resolved in record_frame_sample()
        // against the frames that have already gone by — a stutter is noticed
        // after it happens, so the frame being reported is always behind the
        // key press.
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F4 && perf_log_.enabled()) {
            perf_mark_pending_ = true;
        }

        // F8 warps across the island. Same reasoning as F7: a debug action
        // handled here rather than in InputMapper, because InputFrame is the
        // replay tape format and a teleport recorded into a tape would change
        // what a replay does. Latched rather than acted on here so the warp
        // happens at a step boundary with the rest of the world update.
        if (ui_.screen() == UiScreen::Driving &&
            e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F8) {
            teleport_requested_ = true;
        }

        // GTA-style trainer menu. F1 itself stays host-side so opening a dev
        // tool can never leak into a recorded InputFrame. Navigation reuses
        // the normal menu edges only while the modal menu owns them.
        if (ui_.screen() == UiScreen::Driving &&
            e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.sym == SDLK_F1) {
            dev_menu_.toggle();
            if (dev_menu_.open()) input_.set_mouse_look(false);
            AP_INFO("developer menu %s", dev_menu_.open() ? "OPEN" : "CLOSED");
        }

        input_.handle_event(e);
    }

    // You hold only what you own, whatever wrote `equipped` (weapon_ownership.h).
    enforce_weapon_ownership(weapon_wheel_,economy_);
    input_.set_weapon_controls(on_foot_ && weapon_wheel_.equipped!=WeaponId::Unarmed);
    input_.end_frame();
}

void App::process_ui_input(float dt) {
    if (bug_report_.open) {
        input_.set_ui_mode(true);
        input_.consume_edges();
        clock_.reset();
        return;
    }
    if (opening_cutscene_.active()) {
        const auto pressed=input_.frame().pressed;
        if (pressed & kBtnBack) { opening_cutscene_.skip();finish_opening(); }
        else if (pressed & kBtnPause) opening_cutscene_.toggle_pause();
        input_.consume_edges();clock_.reset();return;
    }
    if (weapon_wheel_.open || weapon_input_consumed_) {
        weapon_wheel_.point(input_.ui_axis_x(),input_.ui_axis_y());
        input_.set_ui_mode(weapon_wheel_.open || ui_.modal());
        input_.consume_edges();clock_.reset();return;
    }
    if (bank_interaction_.modal() || bank_input_consumed_) {
        input_.set_ui_mode(bank_interaction_.modal() || ui_.modal());
        input_.consume_edges();
        clock_.reset();
        return;
    }
    if (process_paint_shop_input(dt)) return;
    if (process_gun_store_input()) return;
    if (ui_.screen() == UiScreen::Driving && !dev_menu_.open() &&
        !vehicle_transition_.active() &&
        was_pressed(input_.frame(), kBtnAccept) &&
        mission_stage_ == MissionStage::DeliveryActive &&
        delivery_contact(player_character_.position,on_foot_)) {
        begin_delivery_cutscene();
        input_.consume_edges();clock_.reset();return;
    }
    if (ui_.screen() == UiScreen::Driving && !dev_menu_.open() && on_foot_ && !vehicle_transition_.active() &&
        was_pressed(input_.frame(), kBtnAccept)) {
        const BankTarget target = bank_target(
            city::bank_local_position(player_character_.position), true);
        if (target != BankTarget::None) {
            bank_interaction_.interact(target, bank_vault_);
            input_.set_ui_mode(bank_interaction_.modal());
            input_.consume_edges();
            clock_.reset();
            return;
        }
    }
    const UiScreen before = ui_.screen();
    uint32_t pressed = input_.frame().pressed;
    const bool pointer_moved =
        input_.pointer_x() != last_ui_pointer_x_ ||
        input_.pointer_y() != last_ui_pointer_y_;
    last_ui_pointer_x_ = input_.pointer_x();
    last_ui_pointer_y_ = input_.pointer_y();

    int logical_w = window_.width();
    int logical_h = window_.height();
    SDL_GetWindowSize(window_.sdl(), &logical_w, &logical_h);
    const UiCanvas canvas = UiCanvas::from_drawable(
        {static_cast<float>(window_.width()),
         static_cast<float>(window_.height())});
    const glm::vec2 vp = canvas.size;
    const glm::vec2 logical_size{static_cast<float>(logical_w),
                                 static_cast<float>(logical_h)};

    // The dev menu is modal but is not a game screen. It pauses the fixed-step
    // clock and consumes its navigation edges before UiFlow or the vehicle can
    // see them. Teleport closes the menu so the destination is visible at once.
    if (dev_menu_.open()) {
        dev_menu_.set_waypoint_available(
            game_ui_.waypoint_position().has_value());
        dev_menu_.set_wanted_level(wanted_.level());
        const DevMenuAction dev_action = dev_menu_.update(pressed);
        if (dev_action.kind == DevMenuActionKind::ReportBug) {
            begin_bug_report();
        } else if (dev_action.kind == DevMenuActionKind::TeleportWaypoint) {
            const std::optional<glm::vec2> waypoint =
                game_ui_.waypoint_position();
            if (waypoint) {
                const glm::vec3 forward = player_focus_forward();
                const float heading = std::atan2(-forward.x, -forward.z);
                dev_menu_.close();
                teleport({waypoint->x, 0.0f, waypoint->y}, heading);
                AP_INFO("developer teleport: map waypoint (%.1f, %.1f)",
                        static_cast<double>(waypoint->x),
                        static_cast<double>(waypoint->y));
            }
        } else if (dev_action.kind == DevMenuActionKind::Teleport &&
            dev_action.location_index >= 0 &&
            dev_action.location_index <
                static_cast<int>(kDevTeleportLocations.size())) {
            const DevTeleportLocation& location =
                kDevTeleportLocations[static_cast<std::size_t>(
                    dev_action.location_index)];
            dev_menu_.close();
            teleport({location.world_xz.x, 0.0f, location.world_xz.y},
                     location.heading_radians);
            AP_INFO("developer teleport: %s", location.name);
        } else if (dev_action.kind ==
                   DevMenuActionKind::SetDrivingMechanics) {
            set_driving_mechanics(dev_action.driving_mechanics);
            dev_menu_.close();
        } else if (dev_action.kind == DevMenuActionKind::SetPlayerCar) {
            cancel_vehicle_transition();
            police_emergency_enabled_=false;
            drop_trailer();
            const auto old_tuning=tuning_;
            tuning_=player_model_tuning(driving_mechanics_style_,dev_action.player_car);
            const bool resized=tuning_.half_wheelbase!=old_tuning.half_wheelbase || tuning_.wheel_radius!=old_tuning.wheel_radius;
            if (resized) {
                collider_.set_kinematic_enabled(current_vehicle_collider_,false);
                const auto forward=car_.orientation*glm::vec3{0,0,-1};
                car_=spawn_vehicle(tuning_,collider_,car_.position.x,car_.position.z,std::atan2(-forward.x,-forward.z));
                prev_car_=car_;seen_impact_count_=car_.impact_count;
                camera_obstruction_distance_=-1.f;
            }
            if (car_visual_.select(
                    scene_, tuning_, car_, dev_action.player_car)) {
                reset_respray_state();
                dev_menu_.set_player_car(dev_action.player_car);
                if (!in_boat_) vehicle_audio_.set_model(player_car_definition(dev_action.player_car).mesh_path);
                dev_menu_.close();
                const PlayerCarDefinition& definition =
                    player_car_definition(dev_action.player_car);
                AP_INFO("developer vehicle selected: %s %s",
                        definition.brand, definition.model);
            } else {
                dev_menu_.set_player_car(car_visual_.active_car());
                AP_ERROR("developer vehicle selection failed");
            }
        } else if (dev_action.kind == DevMenuActionKind::RepairVehicle) {
            repair_vehicle(car_);
            prev_car_ = car_;
            seen_impact_count_ = car_.impact_count;
            dev_menu_.close();
            AP_INFO("developer vehicle repair: health %.0f, damage cleared",
                    static_cast<double>(car_.health));
        } else if (dev_action.kind ==
                   DevMenuActionKind::CopyPlayerPosition) {
            const glm::vec3 position = player_focus_position();
            const glm::vec3 forward = player_focus_forward();
            char text[192];
            std::snprintf(
                text, sizeof(text),
                "position={%.3ff, %.3ff, %.3ff} forward={%.6ff, %.6ff, %.6ff}",
                static_cast<double>(position.x),
                static_cast<double>(position.y),
                static_cast<double>(position.z),
                static_cast<double>(forward.x),
                static_cast<double>(forward.y),
                static_cast<double>(forward.z));
            const bool copied = SDL_SetClipboardText(text) == 0;
            vehicle_interaction_notice_ =
                copied ? "POSITION COPIED" : "POSITION COPY FAILED";
            vehicle_notice_until_ = step_index_ + 240;
            dev_menu_.close();
            if (copied) {
                AP_INFO("developer position copied: %s", text);
            } else {
                AP_WARN("developer position copy failed: %s; position: %s",
                        SDL_GetError(), text);
            }
        } else if (dev_action.kind == DevMenuActionKind::SetWantedLevel) {
            // Asking for stars re-arms the system: the menu clears NEVER
            // WANTED on the same press, so a level set here is a level you
            // actually keep rather than one add_heat() drops on the floor.
            wanted_.set_enabled(true);
            wanted_.set_level(dev_action.wanted_level);
            dev_menu_.set_wanted_level(wanted_.level());
            dev_menu_.close();
            AP_INFO("developer wanted level: %d", wanted_.level());
        } else if (dev_action.kind == DevMenuActionKind::SetFrameLogging) {
            set_frame_logging(dev_action.frame_logging);
        } else if (dev_action.kind == DevMenuActionKind::SetGodMode) {
            god_mode_ = dev_action.god_mode;
            AP_INFO("developer god mode: %s",
                    god_mode_ ? "ON (player takes no damage)" : "OFF");
        } else if (dev_action.kind == DevMenuActionKind::SetVehicleGodMode) {
            vehicle_god_mode_ = dev_action.vehicle_god_mode;
            AP_INFO("developer vehicle god mode: %s",
                    vehicle_god_mode_ ? "ON (no impact damage or dents)" : "OFF");
        } else if (dev_action.kind == DevMenuActionKind::SetNeverWanted) {
            // set_enabled(false) both refuses new heat and resets what is
            // already banked, so switching this on mid-pursuit ends it rather
            // than freezing it at the current level.
            wanted_.set_enabled(!dev_action.never_wanted);
            dev_menu_.set_wanted_level(wanted_.level());
            AP_INFO("developer never wanted: %s",
                    dev_action.never_wanted ? "ON (police will not engage)"
                                            : "OFF");
        } else if (dev_action.kind == DevMenuActionKind::SetWeather) {
            apply_dev_weather(dev_action.weather, controls_);
            update_weather();
            AP_INFO("developer weather: %s",
                    kDevWeatherLabels[static_cast<std::size_t>(
                        dev_action.weather)]);
        } else if (dev_action.kind == DevMenuActionKind::SetTime) {
            AP_INFO("developer time: %s",
                    kDevTimeLabels[static_cast<std::size_t>(dev_action.time)]);
        } else if (dev_action.kind == DevMenuActionKind::SetSnowDepth) {
            controls_.snow_depth_override_m = dev_action.snow_depth_m;
            update_weather();
            if (dev_action.snow_depth_m < 0.0f) {
                AP_INFO("developer snow accumulation: automatic (%.3f m now)",
                        static_cast<double>(conditions_.snow_depth_m));
            } else {
                AP_INFO("developer snow accumulation: %.3f m",
                        static_cast<double>(conditions_.snow_depth_m));
            }
        } else if (dev_action.kind == DevMenuActionKind::SetCameraMode) {
            chase_camera_.set_mode(dev_action.camera_mode);
            camera_obstruction_distance_ = -1.0f;
            AP_INFO("camera mode: %s", chase_camera_.mode_name());
        } else if (dev_action.kind ==
                   DevMenuActionKind::SetCameraAutoRecenter) {
            chase_camera_.set_auto_recenter(dev_action.camera_auto_recenter);
            AP_INFO("camera auto recenter: %s",
                    dev_action.camera_auto_recenter ? "ON" : "OFF");
        } else if (dev_action.kind ==
                   DevMenuActionKind::AuditionCarSound) {
            vehicle_audio_.audition(dev_action.car_sound_use,
                                    dev_action.sound_variant);
            AP_INFO("sound test: car group %d, option %d",
                    static_cast<int>(dev_action.car_sound_use) + 1,
                    dev_action.sound_variant + 1);
        }
        input_.set_ui_mode(dev_menu_.open() || bug_report_.open || ui_.modal());
        input_.consume_edges();
        clock_.reset();
        return;
    }

    if (ui_.item_count() > 0) {
        const glm::vec2 pointer = canvas.from_window(
            {static_cast<float>(input_.pointer_x()),
             static_cast<float>(input_.pointer_y())}, logical_size);
        const int hovered = game_ui_.hit_test(ui_, pointer, vp);
        if (ui_.screen() == UiScreen::Settings && hovered >= 100) {
            ui_.set_settings_hovered_category(hovered - 100);
            if (input_.pointer_pressed()) {
                ui_.open_settings_page(
                    static_cast<SettingsPage>(hovered - 99));
            }
        } else if (hovered >= 0 && (pointer_moved || input_.pointer_pressed())) {
            if (ui_.screen() == UiScreen::Settings && pointer_moved)
                ui_.clear_settings_hovered_category();
            ui_.set_selection(hovered);
            if (input_.pointer_pressed()) pressed |= kBtnAccept;
        }
    }

    const UiAction action = ui_.update(pressed);
    if (ui_.screen() == UiScreen::Map) {
        if (before != UiScreen::Map)
            game_ui_.open_map_view(player_focus_position(), vp);
        glm::vec2 drag{0.0f};
        if (input_.pointer_down()) {
            drag = canvas.from_window(
                {static_cast<float>(input_.pointer_dx()),
                 static_cast<float>(input_.pointer_dy())}, logical_size);
        }
        game_ui_.update_map(
            {input_.ui_axis_x(), input_.ui_axis_y()}, input_.ui_zoom_steps(),
            drag, (pressed & kBtnAccept) != 0u, player_focus_position(), dt, vp,
            canvas.from_window({static_cast<float>(input_.pointer_x()),
                                static_cast<float>(input_.pointer_y())}, logical_size),
            input_.ui_wheel_zoom_steps());
        if (input_.waypoint_pressed() || (pressed & kBtnRespawn) != 0u)
            game_ui_.toggle_waypoint(canvas.from_window(
                {static_cast<float>(input_.pointer_x()), static_cast<float>(input_.pointer_y())},
                logical_size), vp, !input_.waypoint_pressed());
    }
    if (action == UiAction::NewGame) {
        begin_new_game();
    } else if (action == UiAction::LoadGame) {
        load_game();
    } else if (action == UiAction::SaveGame) {
        save_game();
    } else if (action == UiAction::ResetVehicle) {
        teleport({0.0f, 0.0f, 0.0f});
    } else if (action == UiAction::QuitGame) {
        input_.request_quit();
    }
    apply_ui_settings();

    input_.set_ui_mode(opening_cutscene_.active() || ui_.modal());
    if (ui_.modal() || ui_.screen() != before) {
        // UI presses are host controls, not vehicle inputs. Clear them even on
        // a zero-step frame, and drop pause wall time before play resumes.
        input_.consume_edges();
        clock_.reset();
    }
}

void App::apply_ui_settings() {
    const UiSettings& settings = ui_.settings();
    if (!ui_settings_applied_ || settings.vsync != applied_ui_settings_.vsync)
        window_.set_vsync(settings.vsync);
    if (!ui_settings_applied_ || settings.master_volume != applied_ui_settings_.master_volume)
        audio_device_.mixer().set_master(settings.master_volume);
    if (!ui_settings_applied_ || settings.music_volume != applied_ui_settings_.music_volume)
        audio_device_.mixer().set_category(Category::Music, settings.music_volume);
    if (!ui_settings_applied_ || settings.sfx_volume != applied_ui_settings_.sfx_volume) {
        for (Category category : {Category::Engine, Category::Tyres,
                                  Category::Impacts, Category::Footsteps,
                                  Category::World, Category::Weather,
                                  Category::Ui})
            audio_device_.mixer().set_category(category, settings.sfx_volume);
    }
    if (!ui_settings_applied_ ||
        settings.camera_auto_recenter != applied_ui_settings_.camera_auto_recenter)
        chase_camera_.set_auto_recenter(settings.camera_auto_recenter);
    if (!ui_settings_applied_ || settings.camera_shake != applied_ui_settings_.camera_shake ||
        settings.reduced_motion != applied_ui_settings_.reduced_motion)
        chase_camera_.set_camera_shake(settings.camera_shake && !settings.reduced_motion);
    applied_ui_settings_ = settings;
    ui_settings_applied_ = true;
}

VehicleSnowWeather App::vehicle_snow_weather() const {
    return {conditions_.snow_cover, conditions_.snow, conditions_.heatwave};
}

void App::step_vehicle_snow() {
    const VehicleSnowWeather weather = vehicle_snow_weather();
    const float dt = static_cast<float>(kSimDt);
    const auto roof_exposure = [&](glm::vec3 roof) {
        return snow_shelter_.exposure(roof.x, roof.y, roof.z);
    };

    vehicle_snow_samples_.clear();
    for (const VehicleAgent& agent : world_.traffic().vehicles()) {
        VehicleSnowSample sample;
        sample.lane_key = agent.lane_key;
        sample.slot = agent.slot;
        sample.generation = agent.generation;
        sample.roof_exposure = roof_exposure(
            agent.pos + glm::vec3{0.0f, kVehicleSnowRoofAboveGroundM, 0.0f});
        sample.speed_mps = agent.speed_mps;
        vehicle_snow_samples_.push_back(sample);
    }
    traffic_snow_loads_.step(vehicle_snow_samples_, weather, dt);

    // car_.position is the body's centre of mass, about a metre under the roof.
    const glm::vec3 roof = car_.position + glm::vec3{0.0f, 1.0f, 0.0f};
    const float exposure = roof_exposure(roof);
    const PlayerCarId car = car_visual_.active_car();
    // A different car, or one teleported by a load or a reset: seed afresh.
    const bool moved_away = glm::distance(car_.position, player_snow_position_) > 15.0f;
    if (player_snow_load_ < 0.0f || car != player_snow_car_ || moved_away) {
        player_snow_load_ = start_vehicle_snow_load_ >= 0.0f
            ? std::min(start_vehicle_snow_load_, 1.0f)
            : seed_vehicle_snow_load(exposure, weather);
        start_vehicle_snow_load_ = -1.0f;
    } else {
        player_snow_load_ = step_vehicle_snow_load(
            player_snow_load_, exposure, glm::length(car_.velocity), !on_foot_,
            weather, dt);
    }
    player_snow_car_ = car;
    player_snow_position_ = car_.position;
}

void App::update_weather(bool step_snowpack) {
    dev_menu_.set_snow_depth_m(controls_.snow_depth_override_m);
    conditions_=conditions_at(seed_,step_index_);
    if (dev_menu_.weather()==DevWeatherPreset::Dynamic) {
        controls_.rain=conditions_.rain;
        controls_.overcast=conditions_.overcast;
        controls_.fog=conditions_.fog;
        snowpack_.set_depth_m(conditions_.snow_depth_m);
    } else {
        override_conditions_for_dev(dev_menu_.weather(), controls_,
                                    conditions_);
        if (step_snowpack) {
            snowpack_ = advance_snowpack(
                snowpack_, conditions_.snow, conditions_.heatwave, kSimDt);
        }
    }
    const double requested_depth = controls_.snow_depth_override_m >= 0.0f
        ? static_cast<double>(controls_.snow_depth_override_m)
        : snowpack_.depth_m();
    SnowpackState displayed_pack;
    displayed_pack.set_depth_m(requested_depth);
    conditions_.snow_depth_m = static_cast<float>(displayed_pack.depth_m());
    conditions_.snow_cover =
        visual_snow_cover_from_depth(displayed_pack.depth_m());
    const SnowpackCollision collision =
        snowpack_collision_from_depth(displayed_pack.depth_m());
    collider_.set_snow_collision_depth(
        collision.active ? static_cast<float>(collision.depth_m) : 0.0f);

    if (step_snowpack) {
        snow_clearance_.advance(conditions_.snow, conditions_.heatwave,
                                static_cast<float>(kSimDt), conditions_.snow_depth_m);
    }
    if (conditions_.snow_depth_m <= 0.0f) snow_clearance_.clear();
    collider_.set_snow_clearance(&snow_clearance_, conditions_.snow_depth_m);
    if (conditions_.snow_depth_m >= 0.02f) snowplow_service_active_ = true;
    else if (conditions_.snow_depth_m < 0.008f) snowplow_service_active_ = false;
    world_.set_snowplow_service(snowplow_service_active_);

    // A manual depth or a forced accumulating pack changes snow grip after the
    // atmospheric preset was built, so finish the derived physics terms here.
    const float night =
        std::clamp(-conditions_.sun_elevation * 2.0f, 0.0f, 1.0f);
    const float loss = kWetGripLoss * conditions_.wetness +
        kRainGripLoss * conditions_.rain +
        kSnowGripLoss * conditions_.snow_cover +
        kFloodGripLoss * conditions_.flood +
        kHailGripLoss * conditions_.hail + kNightGripLoss * night;
    conditions_.grip = std::clamp(1.0f - loss, kMinGrip, 1.0f);
    if (dev_menu_.weather() == DevWeatherPreset::Tornado) {
        if (!dev_tornado_center_set_) {
            const glm::vec3 forward = camera_.forward();
            dev_tornado_center_m_ = {
                camera_.position.x + forward.x * 140.0f,
                camera_.position.z + forward.z * 140.0f};
            dev_tornado_center_set_ = true;
        }
        conditions_.tornado_center_m = dev_tornado_center_m_;
    } else {
        dev_tornado_center_set_ = false;
    }
    auto& rain = rain_.tuning();
    rain.slant_x = conditions_.wind_mps.x / std::max(rain.fall_speed, 0.1f);
    rain.slant_z = conditions_.wind_mps.y / std::max(rain.fall_speed, 0.1f);
    rain_.snow_tuning().wind = conditions_.wind_mps;
    rain_.blizzard_tuning().wind = conditions_.wind_mps;
}

void App::set_weather_preset(DevWeatherPreset preset) {
    dev_menu_.set_weather(preset);
    apply_dev_weather(preset, controls_);
}

void App::set_snow_depth_override(float depth_m) {
    SnowpackState clamped;
    clamped.set_depth_m(depth_m);
    controls_.snow_depth_override_m = static_cast<float>(clamped.depth_m());
    dev_menu_.set_snow_depth_m(controls_.snow_depth_override_m);
}

void App::set_driving_mechanics(DrivingMechanicsStyle style) {
    driving_mechanics_style_ = style;
    tuning_ = player_model_tuning(style,car_visual_.active_car());
    dev_menu_.set_driving_mechanics(style);
    AP_INFO("driving mechanics: %s", driving_mechanics_name(style));
}

glm::vec3 App::player_focus_position() const {
    if (in_boat_) return boat_.position;
    if (vehicle_transition_.active()) return car_.position;
    if (in_aircraft_) return aircraft_.position;
    if (in_helicopter_) return helicopter_.position;
    return on_foot_ ? player_character_.position : car_.position;
}

glm::vec3 App::player_focus_forward() const {
    if (in_boat_) return boat_forward(boat_);
    if (in_aircraft_) return aircraft_forward(aircraft_);
    if (in_helicopter_) return helicopter_forward(helicopter_);
    return on_foot_
        ? character_forward(player_character_.facing_yaw)
        : car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
}

std::vector<VisiblePoliceIdentity> App::visible_police(
        glm::vec3 target, bool witness_only) const {
    const WallClock::time_point vis_t0 = WallClock::now();
    ++police_vis_calls_;
    std::vector<VisiblePoliceIdentity> visible;
    const PoliceTuning& police = world_.traffic().police_tuning();
    const glm::vec2 target_xz{target.x, target.z};
    std::vector<PoliceVisibilityBody> traffic_bodies;
    traffic_bodies.reserve(world_.traffic().vehicles().size());
    for (const auto& agent:world_.traffic().vehicles()) {
        traffic_bodies.push_back({{agent.lane_key,agent.slot},
            traffic_visual_.vehicle_source_bounds(agent),
            traffic_visual_.vehicle_body_transform(agent).matrix()});
    }
    for (const VehicleAgent& agent : world_.traffic().vehicles()) {
        if (!agent.police_unit) continue;
        // A downed officer sees nothing, witnesses nothing and arrests nobody.
        // This is the one list all three of those read, so gating it here is
        // what makes shooting a cop actually take him out of the fight.
        if (police_officer_downed(agent.officer)) continue;
        const glm::vec3 eye=police_officer_eye_position(agent);
        const glm::vec3 forward=police_officer_forward(agent);
        const glm::vec2 position{eye.x, eye.z};
        // A pursuer is in the gate within the level's DETECTION range, not
        // just the 70 m heat contact: the crowd's wanted centre needs to know
        // about a clear ray at a hundred metres on open road, or a chase that
        // opens a gap turns into a search for a suspect everyone can see.
        const float detect = police_level_profile(wanted_.level()).detect_range_m;
        const bool in_gate = agent.police_pursuit && !witness_only
            ? (police_maintains_contact(position, target_xz, true, police) ||
               glm::distance(position, target_xz) <= detect)
            : police_can_witness(position, {forward.x, forward.z},
                                 target_xz, true, true, police);
        if (!in_gate) continue;

        const glm::vec3 from = eye;
        const glm::vec3 to = target + glm::vec3{0.0f, 1.05f, 0.0f};
        const float distance = glm::length(to - from);
        if (distance <= 0.05f) {
            visible.push_back({agent.lane_key, agent.slot});
            continue;
        }
        // A VISIBILITY query, not a contact one. This used to call
        // collider_.raycast(), which marches the height field at a quarter of
        // the terrain's lattice spacing because a wheel depends on it —
        // measured at 0.208 ms a call here, five calls a sim step, and the
        // single largest cost in a bad frame by a wide margin.
        if (!collider_.line_of_sight_blocked(from, to) &&
            !police_traffic_blocks_view(from,to,traffic_bodies,
                {agent.lane_key,agent.slot}))
            visible.push_back({agent.lane_key, agent.slot});
    }
    police_vis_ms_ += std::chrono::duration<double>(
                          WallClock::now() - vis_t0).count() * 1000.0;
    return visible;
}

bool App::place_character_next_to_car(bool require_clear) {
    const glm::vec3 car_forward =
        car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 car_right =
        car_.orientation * glm::vec3{1.0f, 0.0f, 0.0f};
    const float yaw = std::atan2(car_forward.x, -car_forward.z);
    const glm::vec3 candidates[] = {
        car_.position - car_right * (tuning_.car_collision_half_width+.85f),
        car_.position + car_right * (tuning_.car_collision_half_width+.85f),
        car_.position - car_forward * (tuning_.car_collision_half_length+.9f),
    };
    PlayerCharacterState placed = spawn_character(
        collider_, candidates[0].x, candidates[0].z, yaw);
    bool clear=false;
    for (const glm::vec3 candidate : candidates) {
        PlayerCharacterState trial = spawn_character(
            collider_, candidate.x, candidate.z, yaw);
        bool traffic_clear=true;
        for (const auto& vehicle:world_.traffic().vehicles()) {
            const auto f=traffic_vehicle_footprint(traffic_vehicle_kind(vehicle));
            const auto delta=trial.position-vehicle.pos;
            const glm::vec3 right{-vehicle.fwd.z,0,vehicle.fwd.x};
            if (std::fabs(glm::dot(delta,vehicle.fwd))<f.half_length_m+.4f &&
                std::fabs(glm::dot(delta,right))<f.half_width_m+.4f) traffic_clear=false;
        }
        const float ground=car_.position.y-tuning_.wheel_radius-
            static_suspension_length(tuning_)-tuning_.com_height_above_mount;
        const glm::vec3 from=car_.position+glm::vec3{0,.55f,0};
        const glm::vec3 path=trial.position+glm::vec3{0,.85f,0}-from;
        const float path_length=glm::length(path);
        // While driving, this car's parked collider is disabled. World walls
        // still block the route out, even if the destination itself is clear.
        const auto obstruction=collider_.raycast(from,path/std::max(path_length,.001f),path_length);
        const bool clear_path=!require_clear || !obstruction.hit || obstruction.distance>=path_length-.08f;
        if (traffic_clear && clear_path && trial.position.y<=ground+.65f && trial.position.y>=ground-1.25f &&
            character_position_clear(collider_, trial.position,character_tuning_)) {
            placed = trial;
            clear=true;
            break;
        }
    }
    if (!clear && require_clear) return false;
    player_character_ = placed;
    prev_player_character_ = placed;
    return true;
}

void App::reset_aircraft() {
    aircraft_ = {};
    aircraft_.position = {city::kAirportSite.origin.x + city::kAirportAircraftStand.x,
        city::kAirportSite.ground_m + .13f,
        city::kAirportSite.origin.z + city::kAirportAircraftStand.z};
    aircraft_.yaw = city::kAirportAircraftYaw;
    prev_aircraft_ = aircraft_;
    world_.sync_aircraft(scene_, collider_, aircraft_);
    camera_obstruction_distance_ = -1;
}

bool App::nearby_aircraft() const {
    if (!on_foot_ || !aircraft_in_boarding_range(aircraft_,
        player_character_.position, aircraft_.position.y)) return false;
    const auto door = aircraft_point(aircraft_, {-2.7f,.85f,10.3f});
    const auto from = player_character_.position + glm::vec3{0,.85f,0};
    const auto delta = door-from;
    const float distance = glm::length(delta);
    if (distance < .05f) return true;
    const auto hit = collider_.raycast(from,delta/distance,distance);
    return !hit.hit || hit.distance >= distance-.08f;
}

void App::reset_helicopter() {
    helicopter_ = {};
    helicopter_.position = {city::kHalberdHelicopterWorldX,
                            city::kHalberdHelicopterWorldY,
                            city::kHalberdHelicopterWorldZ};
    helicopter_.yaw = city::kHalberdHelicopterYaw;
    prev_helicopter_ = helicopter_;
    world_.sync_helicopter(scene_, collider_, helicopter_);
    camera_obstruction_distance_ = -1;
}

bool App::nearby_helicopter() const {
    if (!on_foot_ || !helicopter_in_boarding_range(helicopter_,
        player_character_.position, helicopter_.position.y)) return false;
    const auto door = helicopter_point(helicopter_,
        {city::kHalberdHelicopterEntry.x, city::kHalberdHelicopterEntry.y,
         city::kHalberdHelicopterEntry.z});
    const auto from = player_character_.position + glm::vec3{0,.85f,0};
    const auto delta = door-from;
    const float distance = glm::length(delta);
    if (distance < .05f) return true;
    const auto hit = collider_.raycast(from,delta/distance,distance);
    return !hit.hit || hit.distance >= distance-.08f;
}

bool App::nearby_bent_elbow() const {
    if (!on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_) return false;
    const auto local = city::access_local(city::kNeighborhoodBarSite,
                                          {player_character_.position.x,
                                           player_character_.position.z});
    return std::fabs(local.x) < 9.0f && local.y > 7.0f && local.y < 16.0f;
}

void App::drink_at_bent_elbow() {
    if (!nearby_bent_elbow()) return;
    start_drunk(drunk_);
    vehicle_interaction_notice_ = "The Bent Elbow - drink " +
        std::to_string(drunk_.drinks) + "/" + std::to_string(kMaxDrinks) +
        ", drunk for 1.5 in-game hours";
    vehicle_notice_until_ = step_index_ + 300;
    AP_INFO("drinking at The Bent Elbow: drink %d/%d, intensity %.2f, %.0f sim seconds",
            drunk_.drinks, kMaxDrinks, drunk_.intensity(), kDrunkDurationSeconds);
}

void App::run_aircraft_check() {
    if (aircraft_check_ran_ || step_index_<20) return;
    aircraft_check_ran_=true;
    const auto fail=[&](const char* reason) { AP_ERROR("aircraft check: %s",reason); };
    reset_aircraft(); on_foot_=true; in_aircraft_=false; in_helicopter_=false;
    const auto door=aircraft_point(aircraft_,{-2.7f,0,10.3f});
    player_character_=spawn_character(collider_,door.x,door.z,0);
    prev_player_character_=player_character_;
    if (!nearby_aircraft()) { fail("ground-level door unreachable"); return; }
    toggle_player_mode();
    if (!in_aircraft_ || on_foot_) { fail("boarding failed"); return; }
    toggle_player_mode();
    if (in_aircraft_ || !on_foot_) { fail("ground exit failed"); return; }
    toggle_player_mode();
    if (!in_aircraft_) { fail("re-entry failed"); return; }
    // Exercise real runway support and the real world's obstacle queries.
    aircraft_={}; aircraft_.position={-210,city::kAirportSite.ground_m+.13f,2046};
    aircraft_.yaw=glm::half_pi<float>();
    world_.sync_aircraft(scene_,collider_,aircraft_);
    InputFrame lift; lift.throttle=1; lift.held=kBtnShiftUp;
    world_.enable_aircraft_collision(collider_,false);
    for (int i=0;i<1200;++i) aircraft_=step_aircraft(aircraft_,lift,collider_,1.f/120);
    world_.sync_aircraft(scene_,collider_,aircraft_);
    world_.enable_aircraft_collision(collider_,true);
    if (aircraft_.grounded || aircraft_.crashed || aircraft_.position.y<20) {
        AP_ERROR("aircraft check state: grounded=%d crashed=%d xyz=%.2f %.2f %.2f speed=%.2f",
            aircraft_.grounded,aircraft_.crashed,aircraft_.position.x,aircraft_.position.y,
            aircraft_.position.z,aircraft_.speed);
        fail("takeoff failed"); return;
    }
    toggle_player_mode();
    if (!in_aircraft_) { fail("unsafe airborne exit allowed"); return; }
    prev_aircraft_=aircraft_;
    world_.fill(scene_,renderer_,aircraft_.position);
    aircraft_check_passed_=true;
    AP_INFO("aircraft check passed: board, exit, re-enter, runway takeoff, airborne exit denied");
}

void App::run_helicopter_check() {
    if (helicopter_check_ran_ || step_index_<20) return;
    helicopter_check_ran_=true;
    const auto fail=[&](const char* reason) { AP_ERROR("helicopter check: %s",reason); };
    reset_helicopter(); on_foot_=true; in_aircraft_=false; in_helicopter_=false;
    // Walk up to the real door on the real apron. The point of doing this in
    // the app rather than in helicopter_tests is that here the stand, the
    // paving and the station's own collision are the ones the player gets.
    const auto door=helicopter_point(helicopter_,
        {city::kHalberdHelicopterEntry.x,0,city::kHalberdHelicopterEntry.z});
    player_character_=spawn_character(collider_,door.x,door.z,0);
    prev_player_character_=player_character_;
    if (!nearby_helicopter()) { fail("apron door unreachable"); return; }
    toggle_player_mode();
    if (!in_helicopter_ || on_foot_) { fail("boarding failed"); return; }
    toggle_player_mode();
    if (in_helicopter_ || !on_foot_) { fail("apron exit failed"); return; }
    toggle_player_mode();
    if (!in_helicopter_) { fail("re-entry failed"); return; }
    // Straight up off its own stand, against the station's real obstacles --
    // the blast walls, the light masts and the tower are all within a rotor
    // disc or two of here, so this is the query that matters.
    InputFrame lift; lift.held=kBtnShiftUp;
    world_.enable_helicopter_collision(collider_,false);
    for (int i=0;i<1800;++i) helicopter_=step_helicopter(helicopter_,lift,collider_,1.f/120);
    world_.sync_helicopter(scene_,collider_,helicopter_);
    world_.enable_helicopter_collision(collider_,true);
    const float climbed=helicopter_.position.y-city::kHalberdHelicopterWorldY;
    if (helicopter_.grounded || helicopter_.crashed || climbed<40.f) {
        AP_ERROR("helicopter check state: grounded=%d crashed=%d xyz=%.2f %.2f %.2f climbed=%.2f",
            helicopter_.grounded,helicopter_.crashed,helicopter_.position.x,
            helicopter_.position.y,helicopter_.position.z,
            static_cast<double>(climbed)>0?climbed:0.f);
        fail("vertical takeoff failed"); return;
    }
    toggle_player_mode();
    if (!in_helicopter_) { fail("unsafe airborne exit allowed"); return; }
    // ...and back down onto the apron it came off, under its own collective.
    InputFrame settle; settle.held=kBtnShiftDown;
    world_.enable_helicopter_collision(collider_,false);
    for (int i=0;i<4000 && !helicopter_.grounded;++i)
        helicopter_=step_helicopter(helicopter_,settle,collider_,1.f/120);
    world_.sync_helicopter(scene_,collider_,helicopter_);
    world_.enable_helicopter_collision(collider_,true);
    if (!helicopter_.grounded || helicopter_.crashed) {
        AP_ERROR("helicopter check state: grounded=%d crashed=%d y=%.2f",
            helicopter_.grounded,helicopter_.crashed,helicopter_.position.y);
        fail("landing failed"); return;
    }
    toggle_player_mode();
    if (in_helicopter_ || !on_foot_) { fail("exit after landing failed"); return; }
    prev_helicopter_=helicopter_;
    world_.fill(scene_,renderer_,helicopter_.position);
    helicopter_check_passed_=true;
    AP_INFO("helicopter check passed: board, exit, re-enter, vertical takeoff off "
            "the apron, airborne exit denied, landing, exit");
}

void App::toggle_player_mode() {
    if (!vehicle_transition_.active() && toggle_boat()) return;
    if (vehicle_transition_.active()) {
        if (transition_waiting_) {
            // The controller remains at its last exterior position during
            // entry; exit retains vehicle ownership until completion.
            cancel_vehicle_transition();
            vehicle_interaction_notice_="Cancelled";
            vehicle_notice_until_=step_index_+120;
        }
        return;
    }
    if (in_aircraft_) {
        if (!aircraft_can_exit(aircraft_)) {
            vehicle_interaction_notice_="Land and stop before getting out";
            vehicle_notice_until_=step_index_+240;
            return;
        }
        bool placed=false;
        const auto door=aircraft_point(aircraft_,{-2.7f,.85f,10.3f});
        for (float x : {-2.7f,-3.7f,-4.7f}) {
            const auto p=aircraft_point(aircraft_,{x,0,10.3f});
            const auto trial=spawn_character(collider_,p.x,p.z,-aircraft_.yaw);
            const auto path=trial.position+glm::vec3{0,.85f,0}-door;
            const float length=glm::length(path);
            const auto hit=length>.05f ? collider_.raycast(door,path/length,length)
                                      : TerrainCollider::GroundHit{};
            if (std::fabs(trial.position.y-aircraft_.position.y)>.65f ||
                (hit.hit && hit.distance<length-.08f) ||
                !character_position_clear(collider_,trial.position,character_tuning_)) continue;
            player_character_=trial; prev_player_character_=trial; placed=true; break;
        }
        if (!placed) {
            vehicle_interaction_notice_="No room at the aircraft door";
            vehicle_notice_until_=step_index_+240;
            return;
        }
        in_aircraft_=false; on_foot_=true;
        character_look_dx_pending_=character_look_dy_pending_=0;
        camera_obstruction_distance_=-1;
        AP_INFO("player exited aircraft");
        return;
    }
    if (in_helicopter_) {
        if (!helicopter_can_exit(helicopter_)) {
            vehicle_interaction_notice_="Land and stop before getting out";
            vehicle_notice_until_=step_index_+240;
            return;
        }
        bool placed=false;
        const auto door=helicopter_point(helicopter_,
            {city::kHalberdHelicopterEntry.x,city::kHalberdHelicopterEntry.y,
             city::kHalberdHelicopterEntry.z});
        for (float x : {-2.3f,-3.1f,-3.9f}) {
            const auto p=helicopter_point(helicopter_,
                {x,0,city::kHalberdHelicopterEntry.z});
            const auto trial=spawn_character(collider_,p.x,p.z,-helicopter_.yaw);
            const auto path=trial.position+glm::vec3{0,.85f,0}-door;
            const float length=glm::length(path);
            const auto hit=length>.05f ? collider_.raycast(door,path/length,length)
                                      : TerrainCollider::GroundHit{};
            if (std::fabs(trial.position.y-helicopter_.position.y)>.65f ||
                (hit.hit && hit.distance<length-.08f) ||
                !character_position_clear(collider_,trial.position,character_tuning_)) continue;
            player_character_=trial; prev_player_character_=trial; placed=true; break;
        }
        if (!placed) {
            vehicle_interaction_notice_="No room at the helicopter door";
            vehicle_notice_until_=step_index_+240;
            return;
        }
        in_helicopter_=false; on_foot_=true;
        character_look_dx_pending_=character_look_dy_pending_=0;
        camera_obstruction_distance_=-1;
        AP_INFO("player exited %s",city::kHalberdHelicopterName);
        return;
    }
    if (nearby_helicopter()) {
        in_helicopter_=true; on_foot_=false;
        vehicle_audio_.exit_vehicle();
        sync_current_vehicle_obstacle();
        character_look_dx_pending_=character_look_dy_pending_=0;
        camera_obstruction_distance_=-1;
        AP_INFO("player boarded %s",city::kHalberdHelicopterName);
        return;
    }
    if (nearby_aircraft()) {
        in_aircraft_=true; on_foot_=false;
        vehicle_audio_.exit_vehicle();
        sync_current_vehicle_obstacle();
        character_look_dx_pending_=character_look_dy_pending_=0;
        camera_obstruction_distance_=-1;
        AP_INFO("player boarded Aster A-80");
        return;
    }
    if (on_foot_) {
        const auto target = nearby_vehicle();
        if (target.locked) {
            vehicle_interaction_notice_="Patrol car locked";
            vehicle_notice_until_=step_index_+240;
            return;
        }
        if (target.kind!=VehicleEntryTarget::Kind::None && has_animated_driver(target.model)) {
            begin_vehicle_transition(target,true);
            return;
        }
        if (!take_nearby_vehicle(target)) return;
        on_foot_ = false;
        if (target.kind == VehicleEntryTarget::Kind::Current)
            enter_mission_car(mission_stage_);
        vehicle_audio_.set_model(player_car_definition(car_visual_.active_car()).mesh_path);
        vehicle_audio_.enter_vehicle(target.kind == VehicleEntryTarget::Kind::Traffic, car_.position);
        collider_.set_kinematic_enabled(current_vehicle_collider_,false);
        character_look_dx_pending_ = 0.0f;
        character_look_dy_pending_ = 0.0f;
        chase_camera_.reset();
        camera_obstruction_distance_ = -1.0f;
        AP_INFO("player entered vehicle");
        return;
    }

    const float speed = glm::length(glm::vec2{car_.velocity.x, car_.velocity.z});
    if (speed > 1.5f) {
        vehicle_interaction_notice_="Stop before getting out";
        vehicle_notice_until_=step_index_+240;
        return;
    }
    if (has_animated_driver(car_visual_.active_car())) {
        begin_vehicle_transition({},false);
        return;
    }
    if (!place_character_next_to_car(true)) {
        vehicle_interaction_notice_="No room to get out";
        vehicle_notice_until_=step_index_+240;
        return;
    }
    on_foot_ = true;
    police_emergency_enabled_=false;
    vehicle_audio_.exit_vehicle();
    sync_current_vehicle_obstacle();
    character_look_dx_pending_ = 0.0f;
    character_look_dy_pending_ = 0.0f;
    camera_obstruction_distance_ = -1.0f;
    AP_INFO("player exited vehicle");
}

void App::teleport(glm::vec3 to, float heading_radians) {
    traffic_horn_audio_.reset();
    vehicle_audio_.stop_horn(); player_horn_pending_=false;
    police_offenses_.reset();
    police_arrest_.reset();
    arrested_feedback_s_=0.0f;
    boat_transition_={};
    cancel_vehicle_transition();
    transition_camera_release_=0;
    police_emergency_enabled_=false;
    reset_respray_state();
    if (in_aircraft_) { in_aircraft_=false; on_foot_=true; }
    if (in_helicopter_) { in_helicopter_=false; on_foot_=true; }
    if (in_boat_) {
        in_boat_=false;on_foot_=true;boat_.speed=0;boat_.velocity={};boat_.throttle=0;
        vehicle_audio_.exit_vehicle();
    }
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
    drop_trailer();
    car_ = spawn_vehicle(tuning_, collider_, to.x, to.z, heading_radians);
    prev_car_ = car_;
    if (character_spawned_) place_character_next_to_car();

    const WallClock::time_point t0 = WallClock::now();
    last_fill_steps_ = world_.fill(scene_, renderer_, player_focus_position());
    last_fill_frame_ = frames_rendered_;
    last_fill_ms_ =
        std::chrono::duration<double>(WallClock::now() - t0).count() * 1000.0;

    chase_camera_.reset();
    camera_obstruction_distance_ = -1.0f;
    seen_impact_count_ = car_.impact_count;
    impact_feedback_seconds_ = 0.0f;
    update_camera(0.0f);
    clock_.reset();

    // The destination's level is logged alongside the cost, because a fill that
    // did NOTHING and a fill that was merely fast print the same number of
    // milliseconds otherwise. A zero-step fill is legitimate — the whole island
    // fits inside the evict radius, so a warp can land on ground that is
    // already resident at the level it wants — but "legitimate" and "the
    // readiness test is broken again" look identical without this.
    const glm::vec3 focus = player_focus_position();
    const ChunkCoord under = chunk_at(focus.x, focus.z);
    AP_INFO("teleport to (%.0f, %.0f): filled in %d steps / %.1f ms, "
            "destination lod %d, %zu chunks, %.1f MB",
            static_cast<double>(to.x), static_cast<double>(to.z),
            last_fill_steps_, last_fill_ms_,
            world_.streamer().resident_lod(under),
            world_.stats().resident_chunks,
            static_cast<double>(world_.stats().mesh_bytes) / (1024.0 * 1024.0));
}

void App::update_camera(float dt) {
    if (lot_plow_check_ && lot_plows_.dispatched() &&
        lot_plow_followed_ < lot_plows_.trucks().size()) {
        // Off the working truck's right shoulder and above, looking at the
        // blade, so the truck and the lanes it has cleared share the frame.
        const LotPlowTruck& truck = lot_plows_.trucks()[lot_plow_followed_];
        const glm::vec3 forward = truck.forward();
        const glm::vec3 right{-forward.z, 0.0f, forward.x};
        const glm::vec3 target = truck.position + forward * 1.5f;
        camera_.position = truck.position - forward * 9.0f + right * 8.0f + glm::vec3{0.0f, 6.5f, 0.0f};
        const glm::vec3 look = target - camera_.position;
        camera_.yaw = std::atan2(look.x, -look.z);
        camera_.pitch = std::atan2(look.y, glm::length(glm::vec2{look.x, look.z}));
        return;
    }
    if (snowplow_check_) {
        for (const auto& truck : world_.traffic().vehicles()) {
            if (!truck.snowplow_unit) continue;
            const glm::vec3 right{-truck.fwd.z, 0.0f, truck.fwd.x};
            const glm::vec3 target = truck.pos - truck.fwd * 5.0f;
            camera_.position = truck.pos - truck.fwd * 14.0f + right * 12.0f +
                glm::vec3{0.0f, 11.0f, 0.0f};
            const glm::vec3 look = target - camera_.position;
            camera_.yaw = std::atan2(look.x, -look.z);
            camera_.pitch = std::atan2(look.y, glm::length(glm::vec2{look.x, look.z}));
            return;
        }
    }
    if (overhead_qa_) {
        camera_.position = {start_position_.x, kOverheadQaCameraHeightM,
                            start_position_.y + 0.5f};
        camera_.yaw = 0.0f;
        camera_.pitch = -glm::half_pi<float>() + 0.001f;
        camera_.fov_y = glm::radians(64.0f);
        return;
    }
    const float a = static_cast<float>(clock_.alpha());
    // The respray bay shot hands over through the transition camera both
    // ways: it blends in from wherever the chase camera was, and releases back
    // to it the same way when the booth, the spray and its hold are done.
    const bool respray_camera=respray_camera_active();
    if (respray_camera!=respray_camera_was_active_) {
        transition_camera_=camera_;
        camera_obstruction_distance_=-1.0f;
        if (respray_camera) respray_camera_blend_=0.0f;
        else transition_camera_release_=1.0f;
        respray_camera_was_active_=respray_camera;
    }
    ChaseCameraPose pose;
    if (boat_transition_.active()) {
        const auto* body=world_.rendered_boat_transform(scene_);
        const float side=(glm::inverse(boat_rotation(boat_))*(boat_shore_pose_.position-boat_.position)).x<0?-1.f:1.f;
        const float progress=float(boat_transition_.tick)/float(kBoatTransitionTicks);
        const auto target=body?body->transform_point({side*.50f,1.f,-.85f}):boat_.position+glm::vec3{0,1,0};
        const float blend=vehicle_transition_ease(progress/.18f);
        pose.target=glm::mix(boat_transition_camera_.position+boat_transition_camera_.forward()*4.8f,target,blend);
        pose.collision_pivot=target;
        pose.desired_eye=glm::mix(boat_transition_camera_.position,
            boat_point(boat_,{side*5.5f,3.8f,-4.5f}),blend);
        pose.fov_y=glm::radians(60.f);
    } else if (vehicle_transition_.active()) {
        const auto body=car_visual_.fitted_body_transform(car_);
        const glm::vec3 side=body.rotation*glm::vec3{1,0,0};
        const auto sample=sample_vehicle_transition(vehicle_transition_,transition_waiting_ ? 1.f : a);
        const float blend=vehicle_transition_ease(sample.progress/.22f);
        const auto target=glm::mix(transition_door_.position+glm::vec3{0,1.1f,0},
            body.transform_point(vehicle_driver_layout(car_visual_.active_car()).hip)+glm::vec3{0,.25f,0},.5f);
        pose.target=glm::mix(transition_camera_.position+transition_camera_.forward()*4.8f,target,blend);
        pose.collision_pivot=target;
        pose.desired_eye=glm::mix(transition_camera_.position,
            target+side*4.5f+glm::vec3{0,2.2f,0},blend);
        pose.fov_y=glm::mix(transition_camera_.fov_y,glm::radians(60.f),blend);
    } else if (in_boat_) {
        const auto pos=glm::mix(prev_boat_.position,boat_.position,a);
        const auto forward=glm::slerp(boat_rotation(prev_boat_),boat_rotation(boat_),a)*glm::vec3{0,0,1};
        pose.target=pos+glm::vec3{0,.9f,0};
        pose.collision_pivot=pos+glm::vec3{0,1.8f,0};
        pose.desired_eye=pos-forward*10.f+glm::vec3{0,4.8f,0};
        pose.fov_y=glm::radians(65.f);
    } else if (in_aircraft_) {
        const auto pos=glm::mix(prev_aircraft_.position,aircraft_.position,a);
        const auto forward=glm::slerp(aircraft_rotation(prev_aircraft_),
            aircraft_rotation(aircraft_),a)*glm::vec3{0,0,1};
        pose.target=pos+glm::vec3{0,4,0};
        pose.collision_pivot=pos+glm::vec3{0,10,0};
        pose.desired_eye=pos-forward*42.0f+glm::vec3{0,16,0};
        pose.fov_y=glm::radians(65.0f);
    } else if (in_helicopter_) {
        // Closer and lower than the airliner's chase: this machine is 15 m
        // long against the Aster's 28 and it gets flown BETWEEN things, so the
        // shot has to show what the rotor is about to touch rather than a nice
        // wide view of the island. The eye rides the yaw only -- following the
        // full attitude puts the horizon on its ear every time the player
        // banks, which on a helicopter is constantly.
        const auto pos=glm::mix(prev_helicopter_.position,helicopter_.position,a);
        const float yaw=interpolate_camera_yaw(prev_helicopter_.yaw,helicopter_.yaw,a);
        const glm::vec3 forward=glm::angleAxis(yaw,glm::vec3{0,1,0})*glm::vec3{0,0,1};
        pose.target=pos+glm::vec3{0,3,0};
        pose.collision_pivot=pos+glm::vec3{0,5,0};
        pose.desired_eye=pos-forward*17.0f+glm::vec3{0,7.5f,0};
        pose.fov_y=glm::radians(65.0f);
    } else if (respray_camera) {
        respray_camera_pose(pose, dt);
    } else if (on_foot_) {
        const glm::vec3 pos = glm::mix(prev_player_character_.position,
                                       player_character_.position, a);
        const float yaw = interpolate_camera_yaw(prev_player_character_.view_yaw,
                                   player_character_.view_yaw, a);
        const float pitch = glm::mix(prev_player_character_.view_pitch,
                                     player_character_.view_pitch, a);
        const auto aim=weapon_aim_camera(pos,yaw,pitch,weapon_use_.aim_blend);
        pose.target=aim.target;
        pose.collision_pivot=aim.pivot;
        pose.desired_eye=aim.eye;
        pose.fov_y=aim.fov_y;
    } else {
        // Interpolate the car between its previous and current sim states.
        // Without this a 120 Hz sim visibly steps on a 144 Hz panel.
        const glm::vec3 pos = glm::mix(prev_car_.position, car_.position, a);
        const glm::quat rot =
            glm::slerp(prev_car_.orientation, car_.orientation, a);
        const glm::vec3 forward =
            rot * glm::vec3{0.0f, 0.0f, -1.0f};
        const glm::vec3 velocity =
            glm::mix(prev_car_.velocity, car_.velocity, a);
        const glm::vec3 angular_velocity =
            glm::mix(prev_car_.angular_velocity, car_.angular_velocity, a);
        const InputFrame& input = input_.frame();
        // Bounded QA: warm up for 180 frames, then exercise the normal orbit
        // path with identical input per frame for comparisons between models.
        const float sweep = frames_rendered_ >= 180 ? camera_sweep_ : 0.0f;
        pose = chase_camera_.update(
            pos, forward, velocity, angular_velocity.y,
            input.look_dx + sweep, input.look_dy, is_held(input, kBtnLookBack), dt);
        if (car_visual_.active_car()==PlayerCarId::HarrowCityliner || car_visual_.active_car()==PlayerCarId::HarrowHauler) {
            pose.target.y+=.9f;
            pose.collision_pivot=pose.target;
            pose.desired_eye=pose.target+(pose.desired_eye-pos)*(trailer_.attached?2.7f:1.65f);
            if(trailer_.attached) pose.desired_eye+=rot*glm::vec3{5.f,1.5f,0};
        }
    }

    apply_drunk_camera(pose, drunk_);

    if (!vehicle_transition_.active() && !boat_transition_.active() && transition_camera_release_>0.f) {
        const float blend=vehicle_transition_ease(1.f-transition_camera_release_);
        pose.target=glm::mix(transition_camera_.position+transition_camera_.forward()*4.8f,pose.target,blend);
        pose.desired_eye=glm::mix(transition_camera_.position,pose.desired_eye,blend);
        pose.fov_y=glm::mix(transition_camera_.fov_y,pose.fov_y,blend);
        transition_camera_release_=std::max(0.f,transition_camera_release_-std::max(dt,0.f)/.45f);
    }
    glm::vec3 eye = pose.desired_eye;
    const bool ignore_current_vehicle=vehicle_transition_.active() || (!on_foot_ && !in_boat_ && !in_helicopter_ && !in_aircraft_);
    if (ignore_current_vehicle) collider_.set_kinematic_enabled(current_vehicle_collider_,false);
    if (ignore_current_vehicle && trailer_.attached) enable_trailer_collision(false);
    if (in_aircraft_) world_.enable_aircraft_collision(collider_,false);
    if (in_helicopter_) world_.enable_helicopter_collision(collider_,false);
    if (in_boat_ || boat_transition_.active()) world_.enable_boat_collision(collider_,false);
    const glm::vec3 eye_ray = pose.desired_eye - pose.collision_pivot;
    const float desired_distance = glm::length(eye_ray);
    if (desired_distance > 1e-4f) {
        float allowed_distance = desired_distance;
        const TerrainCollider::GroundHit obstruction =
            collider_.raycast(pose.collision_pivot, eye_ray, desired_distance);
        if (obstruction.hit && obstruction.distance < desired_distance) {
            allowed_distance = std::max(obstruction.distance - 0.35f, 0.15f);
        }

        if (camera_obstruction_distance_ < 0.0f ||
            allowed_distance < camera_obstruction_distance_) {
            // Pull in immediately. One clipped frame is enough to fill the
            // screen with the inside of a wall.
            camera_obstruction_distance_ = allowed_distance;
        } else {
            // Ease back out after clearing the corner so the wall does not
            // catapult the view behind the car in one frame.
            const float release = 1.0f - std::exp(-4.0f * std::max(dt, 0.0f));
            camera_obstruction_distance_ =
                glm::mix(camera_obstruction_distance_, allowed_distance, release);
        }
        camera_obstruction_distance_ =
            std::min(camera_obstruction_distance_, desired_distance);
        eye = pose.collision_pivot +
              eye_ray / desired_distance * camera_obstruction_distance_;
    }

    if (in_aircraft_) world_.enable_aircraft_collision(collider_,true);
    if (in_helicopter_) world_.enable_helicopter_collision(collider_,true);
    if (in_boat_ || boat_transition_.active()) world_.enable_boat_collision(collider_,true);
    if (ignore_current_vehicle && trailer_.attached) enable_trailer_collision(true);
    if (ignore_current_vehicle) sync_current_vehicle_obstacle();
    // Never let the camera drop below the ground: a chase cam behind a car
    // climbing a hill ends up inside the hill, and the whole screen goes to
    // whatever the inside of the terrain looks like.
    const float ground = collider_.height(eye.x, eye.z) + 1.2f;
    if (eye.y < ground) eye.y = ground;

    const glm::vec3 dir = pose.target - eye;
    const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z);

    camera_.position = eye;
    camera_.yaw = std::atan2(dir.x, -dir.z);
    camera_.pitch = std::atan2(dir.y, flat > 1e-4f ? flat : 1e-4f);
    camera_.fov_y = pose.fov_y;
}

SkyEnv App::current_sky_env() const {
    // Sim time, not wall time. See the note in app.h.
    const float sim_seconds =
        static_cast<float>(static_cast<double>(step_index_) * kSimDt);
    const float live_time_of_day =
        0.28f + sim_seconds * controls_.sky_speed /
                    static_cast<float>(kSecondsPerDay);
    const float selected_time = kDevTimeValues[static_cast<std::size_t>(
        dev_menu_.time())];
    const float time_of_day = daylight_qa_ ? 0.5f :
        (lighting_night_ ? 0.0f : (selected_time >= 0.0f ? selected_time : live_time_of_day));

    WeatherParams weather;
    weather.rain = ui_.settings().weather_effects ? controls_.rain : 0.0f;
    weather.snow = ui_.settings().weather_effects ? conditions_.snow : 0.0f;
    weather.snow_cover =
        ui_.settings().weather_effects ? conditions_.snow_cover : 0.0f;
    weather.overcast = controls_.overcast;
    weather.fog = controls_.fog;
    weather.fog_start_m = kRenderDistance * 0.25f;
    weather.fog_end_m = kRenderDistance;
    SkyEnv env = compute_sky_env(time_of_day, weather);
    if(dusk_preview_ && !daylight_qa_ && !lighting_night_) env=bellwether_dusk_sky(weather);

    DistanceHazeParams haze;
    haze.weather_fog = controls_.fog;
    apply_distance_haze(env, haze);
    return env;
}

namespace {

// terrain's Surface and audio's AudioSurface deliberately mirror each other's
// order and both are append-only. The static_assert is what makes relying on
// that safe rather than lucky: append to one and this stops compiling instead
// of quietly renaming a material.
constexpr AudioSurface audio_surface_of(Surface s) {
    static_assert(kSurfaceCount == kAudioSurfaceCount);
    switch (s) {
        case Surface::Rock:   return AudioSurface::Rock;
        case Surface::Gravel: return AudioSurface::Gravel;
        case Surface::Grass:  return AudioSurface::Grass;
        case Surface::Sand:   return AudioSurface::Sand;
    }
    return AudioSurface::Rock;
}

}  // namespace

void App::update_footstep_audio() {
    const PlayerCharacterState& person = player_character_;

    // One probe per sim step while on foot. It is paid unconditionally rather
    // than only on the steps that owe a footfall, because the alternative is
    // caching the ground between steps and a stale surface is exactly the bug
    // that only shows on the frame you cross a kerb. The character step already
    // sweeps the static boxes several times over; this is a small addition to a
    // block the phase breakdown measures at 0.11 ms.
    const TerrainCollider::GroundHit ground = collider_.probe_down(
        {person.position.x, person.position.y + 0.25f, person.position.z}, 0.60f);
    // Off the end of the probe, classify the ground anyway rather than
    // defaulting, so a footfall on a frame where the feet sit a hair high is
    // still the right material.
    const Surface material = ground.hit
        ? ground.material
        : collider_.material(person.position.x, person.position.z);

    footstep_audio_.update(
        audio_device_.mixer(), audio_device_.bank(),
        footstep_walk(person.position, person.velocity, person.distance_walked_m,
                      person.grounded, person.sprinting,
                      audio_surface_of(material), ground.hit && ground.road,
                      ground.hit && ground.prop,
                      ground.hit ? ground.snow_depth_m : 0.0f),
        footstep_tuning_);
}

void App::render() {
    const WallClock::time_point render_t0 = WallClock::now();
    glViewport(0, 0, window_.width(), window_.height());
    if (audio_device_.running()) {
        const bool title = ui_.screen() == UiScreen::Title ||
            (ui_.screen() == UiScreen::Map && ui_.map_return_screen() == UiScreen::Title);
        intro_audio_.update(audio_device_.mixer(), title, camera_frame_dt_);
        rain_audio_.update(audio_device_.mixer(), audio_device_.bank(),
                           title || !ui_.settings().weather_effects
                               ? 0.0f : controls_.rain, camera_frame_dt_);
    }

    // The title shares the startup composition. Do not spend a world render
    // behind it or let a paused gameplay camera leak through the background.
    if (ui_.screen() == UiScreen::Title ||
        (ui_.screen() == UiScreen::Settings &&
         ui_.settings_return_screen() == UiScreen::Title)) {
        loading_screen_.render(window_.width(), window_.height());
        const glm::vec2 vp = UiCanvas::from_drawable(
            {window_.width(), window_.height()}).size;
        GameUiSnapshot snapshot;
        snapshot.save_notice=save_notice_.c_str();
        snapshot.title_opacity = loading_screen_.menu_opacity();
        hud_.begin(vp);
        game_ui_.draw(hud_, ui_, snapshot, vp);
        hud_.end();
        if (frames_rendered_ < 8) gl_errors_ += drain_gl_errors("title screen");
        publish_playtest_state();
        window_.swap();
        ++frames_rendered_;
        return;
    }

    if (opening_cutscene_.active()) {
        render_opening();
        // This path presents for itself and returns, so it never reaches the
        // swap timing below. Charge the whole thing to render and claim no
        // swap rather than leaving the previous frame's numbers standing —
        // a stale phase reading is worse than an absent one, because it looks
        // like a measurement.
        render_ms_ = std::chrono::duration<double>(WallClock::now() - render_t0)
                         .count() * 1000.0;
        swap_ms_ = 0.0;
        return;
    }

    // Started only AFTER every early return above it. A GL_TIME_ELAPSED query
    // begun and never ended stays open forever: the next begin() is refused,
    // no result ever lands, and the timer silently reads zero for the rest of
    // the session.
    // Spans everything this frame submits. It measures GPU EXECUTION, which
    // overlaps the CPU work above it — so it is never added into the frame's
    // phase budget, only read beside it.
    gpu_timer_.begin();

    const SkyEnv env = current_sky_env();
    // Bounded QA only: age the real driven paths just before the final image.
    // Normal play always advances this state exclusively at the fixed step.
    if (snowplow_refill_preview_seconds_ > 0.0f && !snowplow_refill_preview_applied_ &&
        frame_limit_ > 0 && frames_rendered_ + 1 >= frame_limit_) {
        snow_clearance_.advance(conditions_.snow, conditions_.heatwave,
            snowplow_refill_preview_seconds_, conditions_.snow_depth_m);
        snowplow_refill_preview_applied_ = true;
        AP_INFO("snow refill preview: aged actual paths %.1f seconds toward %.3f m main snow",
            static_cast<double>(snowplow_refill_preview_seconds_),
            static_cast<double>(conditions_.snow_depth_m));
    }
    renderer_.set_snow_clearance(snow_clearance_, conditions_.snow_depth_m);
    const float sim_seconds =
        static_cast<float>(static_cast<double>(step_index_) * kSimDt);
    // Window occupancy is replay-stable from the session seed and absolute
    // sim step, while this darkness gate follows the sky the player actually
    // sees (including --night and the dev time presets).
    const float visible_night_level = automatic_headlight_level(
        env.sun_dir.y, AtmosphericWeather::Clear);
    const float visible_headlight_level = automatic_headlight_level(
        env.sun_dir.y, conditions_.atmosphere);
    const float selected_time = kDevTimeValues[static_cast<std::size_t>(
        dev_menu_.time())];
    const float visible_time_of_day_per_step =
        !daylight_qa_ && !lighting_night_ && selected_time < 0.0f
            ? controls_.sky_speed * static_cast<float>(kSimDt /
                                                        kSecondsPerDay)
            : 0.0f;
    world_.sync_skyscraper_window_lights(
        scene_,seed_,step_index_,env.time_of_day,visible_night_level,
        visible_time_of_day_per_step,camera_.position);

    update_camera(camera_frame_dt_);
    if (tire_track_check_) tire_track_check_camera();
    if (plow_check_) plow_check_camera();
    camera_.fov_y = glm::radians(static_cast<float>(ui_.settings().camera_fov)) *
        (on_foot_ ? glm::mix(1.f,.8f,weapon_use_.aim_blend) : 1.f);
    if (weapon_check_ && (frames_rendered_==341 || frames_rendered_==351 || frames_rendered_==430)) {
        const glm::vec3 forward=character_forward(player_character_.facing_yaw);
        const glm::vec3 right{forward.z,0,-forward.x};
        const glm::vec3 target=player_character_.position+glm::vec3{0,1.25f,0};
        camera_.position=target+right*2.4f+forward*.75f+glm::vec3{0,.35f,0};
        const glm::vec3 look=target-camera_.position;
        camera_.yaw=std::atan2(look.x,-look.z);
        camera_.pitch=std::atan2(look.y,glm::length(glm::vec2{look.x,look.z}));
    }
    if(signal_check_)signal_check_camera();
    if(police_officer_check_)police_officer_check_camera();
    if(traffic_horn_check_)traffic_horn_check_camera();
    Listener listener;
    listener.position = camera_.position;
    listener.forward = camera_.forward();
    listener.right = camera_.right();
    vehicle_audio_.set_listener(listener);

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
    float scene_draw_distance = kRenderDistance;
    if (env.fog_density >= 0.999f && env.fog_end > env.fog_start) {
        // At fog_end the shader is 100% atmosphere. Keep a small guard band for
        // large bounds, then stop submitting invisible geometry behind it.
        scene_draw_distance =
            std::min(kRenderDistance, env.fog_end + 96.0f);
    }
    world_.sync_burgerpiz_parking_lamps(scene_,visible_night_level);
    world_.sync_ferrone_mast(scene_,camera_.position,sim_seconds,visible_night_level);
    traffic_visual_.sync_street_lights(scene_, camera_.position,
        visible_night_level);
    const Scene::CullResult& culled = scene_.cull(
        camera_.frustum(), camera_.position, scene_draw_distance);
    cull_ms_ = std::chrono::duration<double>(WallClock::now() - cull_t0).count() *
               1000.0;

    Renderer::Options opts;
    opts.instancing = controls_.instancing;
    // Follow the light the player can actually see. The demo sky can run
    // faster than session conditions through sky_speed, so using the sim's
    // headlight_level here would leave the lamps on under a visible noon sun.
    HeadlightRig headlights = car_visual_.headlights(
        prev_car_, car_, static_cast<float>(clock_.alpha()),
        visible_headlight_level);
    std::vector<TrafficSpotLight> stress_lights;
    if(lighting_stress_ && visible_headlight_level>0) {
        glm::vec3 forward=camera_.forward(); forward.y=0;
        forward=glm::normalize(forward);
        const glm::vec3 right=glm::normalize(glm::cross(forward,glm::vec3{0,1,0}));
        for(int row=0;row<10;++row) for(int col=0;col<10;++col) {
            const float distance=12.0f+static_cast<float>(row)*8.0f;
            glm::vec3 p=camera_.position+forward*distance+
                right*((static_cast<float>(col)-4.5f)/4.5f*distance*0.7f);
            p.y=collider_.height(p.x,p.z)+0.8f;
            const glm::vec3 direction=glm::normalize(-forward+glm::vec3{0,-0.075f,0});
            for(float side : {-0.7f,0.7f}) stress_lights.push_back({
                glm::vec4{p+right*side,32},glm::vec4{direction,4.5f}});
        }
    }
    emergency_light_sources_=lighting_stress_ ? stress_lights : traffic_visual_.headlights();
    // Indoor fixtures stay on during the day and share their visible positions.
    const auto add_interior_lights=[&](const city::StartSite& site,
        const auto& parts,std::string_view name,float range,glm::vec3 local_direction=glm::vec3{0,-1,0},
        float power=2.3f,float outer=.58f,glm::vec3 colour=glm::vec3{0.f}) {
        if (lighting_stress_) return;
        for (const auto& part:parts) {
            if (std::string_view(part.name)!=name) continue;
            const glm::vec3 p{site.origin.x+site.cos_yaw*part.centre.x+site.sin_yaw*part.centre.z,
                site.ground_m+part.bottom_m-.04f,
                site.origin.z-site.sin_yaw*part.centre.x+site.cos_yaw*part.centre.z};
            const auto direction=glm::normalize(glm::vec3{
                site.cos_yaw*local_direction.x+site.sin_yaw*local_direction.z,local_direction.y,
                -site.sin_yaw*local_direction.x+site.cos_yaw*local_direction.z});
            const glm::vec3 color = colour!=glm::vec3{0.f} ? colour
                : name == "quickbite interior ceiling light lens"
                ? glm::vec3{1.0f,.97f,.91f}
                : glm::vec3{1.0f,.88f,.70f};
            if (glm::distance(p,camera_.position)<65.f)
                emergency_light_sources_.push_back({glm::vec4{p,range},
                    glm::vec4{direction,power},{color,outer}});
        }
    };
    add_interior_lights(city::kFastFoodSite,city::kFastFoodParts,
        "quickbite interior ceiling light lens",10.5f,{0,-1,0},1.8f,.35f);
    add_interior_lights(city::kGasStationSite,city::kGasStationParts,
        "store interior ceiling light lens",6.6f);
    static const auto pawn_lights=city::bake_pawn_shop();
    add_interior_lights(city::kPawnShopSite,pawn_lights,"pawn ceiling light lens",6.4f);
    static const auto gun_store_lights=city::bake_gun_store();
    add_interior_lights(city::kGunStoreSite,gun_store_lights,"gun store ceiling light lens",7.2f,{0,-1,0},3.4f,.35f);
    add_interior_lights(city::kGunStoreSite,gun_store_lights,"gun store rack light lens",5.5f,{0,-.85f,-1});
    add_interior_lights(city::kGunStoreSite,gun_store_lights,"gun store counter light lens",7.f,{0,-.55f,-1},3.0f);
    add_interior_lights(city::kGunStoreSite,gun_store_lights,"gun store exterior light lens",5.5f);
    static const auto museum_lights=city::bake_loom_museum();
    // 9 m covers a gallery from the two pendant rows and stops well short of
    // the 12 m between an upper fitting and the ground floor. The grid has no
    // occlusion, so that gap is the only thing keeping the upstairs fittings
    // from lighting the room below straight through the slab. The .55 cone is
    // deliberately narrow: the grid's span radius is range/outer, so a wide
    // cone is expensive, and the shader's outer edge is hard rather than
    // feathered, so coverage has to come from overlap.
    add_interior_lights(city::kLoomMuseumSite,museum_lights,"museum gallery light lens",9.f,{0,-1,0},2.7f,.55f,{1.f,.97f,.93f});
    // Picture lights sit a metre off the wall and wash down it. One direction
    // per fixture name, so the heads are all downlights with a wide cone
    // rather than four names for four wall orientations.
    add_interior_lights(city::kLoomMuseumSite,museum_lights,"museum art light lens",6.5f,{0,-1,0},3.2f,.62f,{1.f,.98f,.95f});
    // Coves point at the ceiling. Without them the soffit takes no light at all
    // and a finished room still reads as a box with a black lid.
    add_interior_lights(city::kLoomMuseumSite,museum_lights,"museum cove light lens",7.5f,{0,1,0},2.2f,.55f,{1.f,.96f,.90f});
    static const auto park_lights=city::bake_loom_park();
    if (visible_night_level > .05f)
        add_interior_lights(city::kLoomParkSite,park_lights,"garden gazebo light lens",7.f);
    static const auto bar_lights=city::bake_neighborhood_bar();
    add_interior_lights(city::kNeighborhoodBarSite,bar_lights,"bar warm light lens",6.5f);
    static const auto hospital_lights = city::bake_polished_hospital_campus();
    add_interior_lights(city::kHospitalSite, hospital_lights,
        "hospital interior ceiling light lens", 8.0f,
        {0.0f, -1.0f, 0.0f}, 0.9f, 0.48f, {1.0f, 0.97f, 0.91f});
    // Indoor fixtures stay on; exterior campus lighting follows dusk.
    if (visible_night_level > 0.05f) {
        add_interior_lights(city::kHospitalSite, hospital_lights,
            "hospital facade wall light lens", 8.0f,
            {0.0f, -0.65f, -1.0f}, 2.4f, 0.42f);
        add_interior_lights(city::kHospitalSite, hospital_lights,
            "hospital arrival canopy light lens", 11.0f,
            {0.0f, -1.0f, 0.0f}, 3.4f, 0.48f);
        add_interior_lights(city::kHospitalSite, hospital_lights,
            "hospital grounds path light lens", 7.0f,
            {0.0f, -0.9f, 0.15f}, 2.2f, 0.55f);
        add_interior_lights(city::kHospitalSite, hospital_lights,
            "hospital garage ceiling light lens", 9.0f,
            {0.0f, -1.0f, 0.0f}, 2.8f, 0.50f);
        static const auto hospital_parking_lights =
            city::bake_hospital_north_parking();
        add_interior_lights(city::kHospitalNorthParkingSite,
            hospital_parking_lights, "hospital parking lot light lens", 23.0f,
            {0.0f, -1.0f, 0.0f}, 5.0f, 0.54f);
    }
    if(!lighting_stress_ && visible_night_level>0) for(const auto& p:world_.bellwether_lights()) {
        if(glm::distance(p.position,camera_.position)<110.f)
            emergency_light_sources_.push_back({glm::vec4{p.position,p.radius},
                {0,-1,0,p.strength*visible_night_level},{1.f,.72f,.38f,.50f}});
    }
    if(!lighting_stress_) for(const auto& p:world_.miandi_gas_station_lights()) {
        if(glm::distance(p,camera_.position)<75.f)
            emergency_light_sources_.push_back({glm::vec4{p,9.f},
                {0,-1,0,1.3f},{1,.94f,.84f,.35f}});
    }
    if(!lighting_stress_) for(const auto& p:world_.burgerpiz_parking_lights())
        if(const auto light=parking_lamp_light(p,camera_.position,visible_night_level))
            emergency_light_sources_.push_back(*light);
    if(!lighting_stress_) for(const auto& p:world_.burgerpiz_lights()) {
        if(glm::distance(p,camera_.position)<65.f)
            emergency_light_sources_.push_back({glm::vec4{p,7.f},
                {0,-1,0,2.6f},{1,.92f,.80f,.35f}});
    }
    // The transmitter shelter's door wall-pack, on at dusk like the lens.
    if(!lighting_stress_ && visible_night_level>0) for(const auto& p:world_.ferrone_mast_lamp_lights()) {
        if(glm::distance(p,camera_.position)<70.f)
            emergency_light_sources_.push_back({glm::vec4{p,7.f},
                {-.33f,-.94f,0,2.4f*visible_night_level},{1,.84f,.62f,.45f}});
    }
    if(!lighting_stress_) for(const auto& p:world_.residential_lights()) {
        if(glm::distance(p,camera_.position)<65.f)
            emergency_light_sources_.push_back({glm::vec4{p,5.8f},
                {0,-1,0,2.3f},{1,.88f,.70f,.58f}});
    }
    // Small authored marina downlights share the existing tiled light path.
    // Keep distant/off-day lamps out of the upload rather than global fill.
    if (!lighting_stress_ && visible_night_level>0) {
        for (const auto& at:city::kMarinaLampPositions) {
            const glm::vec3 p{city::kMarlinDockSite.origin.x+at.x,3.98f,
                              city::kMarlinDockSite.origin.z+at.z};
            if (glm::distance(p,camera_.position)<110.f)
                emergency_light_sources_.push_back({glm::vec4{p,13.f},
                    {0,-1,0,3.2f*visible_night_level},{1,.78f,.46f,.48f}});
        }
    }
    // Miandi neon tubes use emissive geometry for the visible source, while
    // these authored, bounded cones put the matching colour onto nearby walls
    // and paving. Keep them in the same tiled path as traffic and civic lights.
    if (!lighting_stress_ && visible_night_level > 0.0f) {
        for (const auto& light : city::kMiandiNightLights) {
            const glm::vec3 position{light.world_position.x,
                                     light.world_position.y,
                                     light.world_position.z};
            if (glm::distance(position, camera_.position) >= 220.0f) continue;
            const glm::vec3 direction{light.world_direction.x,
                                      light.world_direction.y,
                                      light.world_direction.z};
            const glm::vec3 color{light.linear_rgb.x, light.linear_rgb.y,
                                  light.linear_rgb.z};
            emergency_light_sources_.push_back({
                glm::vec4{position, light.range_m},
                glm::vec4{direction, city::miandi_night_light_power(
                                         light, visible_night_level)},
                glm::vec4{color, light.outer_cone_cos}});
        }
    }
    if (!lighting_stress_) append_fire_light(emergency_light_sources_);
    if (const auto* body=car_visual_.rendered_body_transform(scene_)) {
        append_police_lights(emergency_light_sources_,*body,step_index_,
            police_emergency_enabled_ && !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            has_police_lightbar(car_visual_.active_car()),car_visual_.active_car());
    }
    const auto& traffic_lights=emergency_light_sources_;
    lighting_source_count_=traffic_lights.size();
    const auto grid_start=WallClock::now();
    const std::vector<TrafficSpotLight> empty_lights;
    if(!tiled_lighting_.upload(traffic_lights_off_ ? empty_lights : traffic_lights,
        camera_.view_projection(),camera_.view(),window_.width(),window_.height())) {
        AP_ERROR("traffic lighting upload failed");
        ++gl_errors_;
    }
    headlights.traffic=tiled_lighting_.view();
    const int phase_frame=std::max(0,frames_rendered_-300);
    const bool lights_on=!traffic_lights_off_ && (!lighting_benchmark_ || (phase_frame/120)%2!=0);
    if(!lights_on) headlights.traffic.columns=0;
    const bool measure=lighting_benchmark_ && frames_rendered_>=300 && phase_frame%120>=16;
    if(measure) {
        light_grid_ms_.push_back(std::chrono::duration<double>(WallClock::now()-grid_start).count()*1000.0);
        light_build_ms_.push_back(tiled_lighting_.build_ms());
        light_upload_ms_.push_back(tiled_lighting_.upload_ms());
        tiled_lighting_.begin_timing(lights_on);
    }
    render_stats_ =
        renderer_.render(scene_, culled.visible, camera_, env, headlights,
                         world_.canopy_lights(), opts);
    tire_tracks_.render(camera_, env, headlights, world_.canopy_lights());

    // 3. Animated people. They use the same lighting as the opaque world, but
    // each draw carries its own continuously sampled bone palette.
    character_visual_.render(camera_, env, headlights,
                             world_.canopy_lights());
    if (ui_.settings().weather_effects &&
        conditions_.tornado_intensity > 0.05f) {
        const glm::vec2 centre = conditions_.tornado_center_m;
        tornado_.render(camera_, centre,
                        collider_.height(centre.x, centre.y),
                        conditions_.tornado_intensity, sim_seconds);
    }
    ocean_.render(camera_,env,headlights,world_.canopy_lights(),sim_seconds,
                  flood_water_level(conditions_.flood));
    renderer_.render_glass(scene_,culled.visible,camera_,env,headlights,world_.canopy_lights());
    render_stats_=renderer_.stats();
    if(measure) tiled_lighting_.end_timing();
    if(lighting_benchmark_ && (frames_rendered_==419 || frames_rendered_==539)) {
        AP_INFO("lighting phase %s: %d world draws, %d instanced batches, %d character draws",
            lights_on ? "ON" : "OFF",render_stats_.draw_calls,render_stats_.instanced_batches,
            character_visual_.last_draw_count());
    }

    // 4. Rain or snow over the world, blended.
    if (ui_.settings().weather_effects) {
        rain_.render(camera_, env, headlights, world_.canopy_lights(), collider_,
                     world_.precipitation_cover());
    }

    // 5. HUD last, one draw.
    {
        const glm::vec2 vp = UiCanvas::from_drawable(
            {static_cast<float>(window_.width()),
             static_cast<float>(window_.height())}).size;
        hud_.begin(vp);

        if (ui_.screen() == UiScreen::Driving) {
            GameUiSnapshot radar;
            radar.player_position = player_focus_position();
            radar.player_forward = player_focus_forward();
            radar.road_name = current_road_name_.update(
                {radar.player_position.x, radar.player_position.z});
            radar.speed_mph = metres_per_second_to_miles_per_hour(glm::length(
                on_foot_ ? player_character_.velocity : in_boat_ ? boat_.velocity :
                in_aircraft_ ? aircraft_.velocity :
                in_helicopter_ ? helicopter_.velocity : car_.velocity));
            radar.wanted_level = wanted_.level();
            radar.wanted_searching = world_.traffic().police_searching();
            radar.wanted_report_pending = wanted_report_blink_;
            radar.wanted_cooldown = wanted_.cooldown(world_.traffic().police_tuning());
            radar.police_stop_prompt = police_stop_prompt_;
            radar.step = static_cast<int64_t>(step_index_);
            radar.time_of_day = env.time_of_day;
            if (mission_stage_ == MissionStage::DeliveryNeedsCar)
                radar.mission_target = glm::vec2{car_.position.x, car_.position.z};
            else if (mission_stage_ == MissionStage::DeliveryActive)
                radar.mission_target = glm::vec2{city::devon_position().x,
                                                 city::devon_position().z};
            radar.perf_logging = perf_log_.enabled();
            radar.perf_log_label = perf_log_label_.c_str();
            radar.perf_marks = perf_marks_;
            radar.perf_mark_feedback_s = perf_mark_feedback_s_;
            game_ui_.draw_minimap(hud_, ui_, radar, vp);
            // The driving HUD reaches draw_minimap DIRECTLY rather than
            // through GameUi::draw, so the badge is drawn here beside it and
            // not in that switch.
            game_ui_.draw_perf_recorder(hud_, radar, vp);
            const glm::vec3 focus = player_focus_position();
            const auto snow_contact = collider_.probe_down(
                focus + glm::vec3{0.0f, 0.5f, 0.0f}, 4.0f,
                TerrainCollider::ProbeVehicles::Exclude);
            const Conditions local_weather = conditions_with_local_snow(conditions_,
                snow_contact.hit ? snow_contact.snow_depth_m : conditions_.snow_depth_m);
            const HazardExposure exposure = hazard_exposure_at(
                local_weather, focus, collider_.height(focus.x, focus.z));
            const GameplayHazard hazard = dominant_hazard(exposure);
            const float severity = hazard_severity(exposure, hazard);
            const char* warning = nullptr;
            if (hazard != GameplayHazard::None && severity >= 0.15f) {
                warning = hazard_warning(hazard, severity);
            } else if (conditions_.atmosphere == AtmosphericWeather::Tornado &&
                       conditions_.tornado_intensity >= 0.35f) {
                warning = "TORNADO WATCH";
            }
            if (warning) {
                const float half =
                    hud_.measure_text(warning, 21.0f) * 0.5f + 22.0f;
                const glm::vec2 lo{vp.x * 0.5f - half, 72.0f};
                const glm::vec2 hi{vp.x * 0.5f + half, 108.0f};
                hud_.rect(lo + glm::vec2{3.0f, 4.0f},
                          hi + glm::vec2{3.0f, 4.0f}, {0, 0, 0, 0.35f});
                hud_.rect(lo, hi, {0.20f, 0.025f, 0.02f, 0.90f});
                hud_.outline(lo, hi, 2.0f, {1.0f, 0.48f, 0.12f, 1.0f});
                hud_.text_centered(warning, vp.x * 0.5f, 78.0f, 21.0f,
                                   {1.0f, 0.92f, 0.72f, 1.0f});
            }
            if (mission_stage_ == MissionStage::DeliveryNeedsCar)
                hud_.text("GET IN YOUR CAR", {28,32},28,{1,.90f,.65f,1});
            else if (mission_stage_==MissionStage::DeliveryActive)
                hud_.text("DELIVERY: Take Lou's package to Devon at Ostend docks",{28,32},28,{1,.90f,.65f,1});

            if (conditions_.tornado_intensity > 0.05f) {
                const glm::vec2 centre = conditions_.tornado_center_m;
                const float ground = collider_.height(centre.x, centre.y);
                const auto cue = project_mission_cue(
                    {centre.x, ground + 28.0f, centre.y},
                    camera_.view_projection(), vp);
                const auto base_cue = project_mission_cue(
                    {centre.x, ground + 1.0f, centre.y},
                    camera_.view_projection(), vp);
                if (!cue.visible && !base_cue.visible) {
                    // Keep the event locatable when the funnel is behind the
                    // camera or hidden outside the view frustum.
                    const glm::vec3 camera_right = camera_.right();
                    const glm::vec2 to_tornado = centre - glm::vec2{
                        camera_.position.x, camera_.position.z};
                    const float side = glm::dot(
                        to_tornado, glm::vec2{camera_right.x, camera_right.z});
                    constexpr float kMarkerY = 178.0f;
                    if (side < 0.0f) {
                        hud_.text("< TORNADO", {24.0f, kMarkerY}, 20.0f,
                                  {1.0f, 0.58f, 0.20f, 1.0f});
                    } else {
                        constexpr const char* kLabel = "TORNADO >";
                        const float width = hud_.measure_text(kLabel, 20.0f);
                        hud_.text(kLabel, {vp.x - width - 24.0f, kMarkerY},
                                  20.0f, {1.0f, 0.58f, 0.20f, 1.0f});
                    }
                }
            }

            if (mission_car_cue_visible(mission_stage_, on_foot_)) {
                const float bob = std::sin(static_cast<float>(step_index_) *
                    static_cast<float>(kSimDt) * 3.2f) * .12f;
                const auto cue = project_mission_cue(
                    car_.position + glm::vec3{0, 2.55f + bob, 0},
                    camera_.view_projection(), vp);
                if (cue.visible) {
                    constexpr const char* label = "Enter your car";
                    const float half = hud_.measure_text(label, 24) * .5f + 17;
                    const glm::vec2 lo{cue.screen.x-half, cue.screen.y-53};
                    const glm::vec2 hi{cue.screen.x+half, cue.screen.y-17};
                    hud_.rect(lo+glm::vec2{3,4},hi+glm::vec2{3,4},{0,0,0,.42f});
                    hud_.rect(lo,hi,{.025f,.030f,.028f,.94f});
                    hud_.outline(lo,hi,2,{1,.76f,.18f,1});
                    hud_.text_centered(label,cue.screen.x,cue.screen.y-47,24,{1,.94f,.73f,1});
                    hud_.triangle({cue.screen.x-10,cue.screen.y-17},
                                  {cue.screen.x+10,cue.screen.y-17},
                                  {cue.screen.x,cue.screen.y-3},
                                  {1,.76f,.18f,1});
                }
            }
            if (in_boat_) {
                char speed[128];
                std::snprintf(speed,sizeof(speed),"MARLIN SPRINT 22   %.0f KNOTS",
                    static_cast<double>(glm::length(boat_.velocity)*1.943844f));
                hud_.text_centered(speed,vp.x*.5f,vp.y-160,22,{.8f,.95f,1,1});
                hud_.text_centered(boat_.blocked ? "SHALLOW WATER / OBSTACLE - REVERSE OR R TO RECOVER" :
                    "W / S  THROTTLE / REVERSE    A / D  STEER    SPACE  SLOW    R  RECOVER",
                    vp.x*.5f,vp.y-130,18,{1,1,1,1});
            } else if (in_aircraft_) {
                char flight[256];
                const auto support=collider_.probe_down(aircraft_.position+glm::vec3{0,.05f,0},10000);
                const float altitude=std::max(0.0f,aircraft_.position.y-
                    (support.hit?support.point.y:collider_.height(aircraft_.position.x,aircraft_.position.z)));
                std::snprintf(flight,sizeof(flight),"ASTER A-80   %.0f MPH   %.0f FT   THROTTLE %.0f%%",
                    static_cast<double>(aircraft_.speed*2.23694f),static_cast<double>(altitude*3.28084f),
                    static_cast<double>(aircraft_.throttle*100));
                hud_.text_centered(flight,vp.x*.5f,vp.y-160,22,{.8f,.95f,1,1});
                hud_.text_centered(aircraft_.crashed ? "AIRCRAFT DAMAGED - R TO RESET" :
                    "W/S THROTTLE   A/D BANK / TAXI   SHIFT/CTRL PITCH   SPACE BRAKE",
                    vp.x*.5f,vp.y-130,18,{1,1,1,1});
            } else if (in_helicopter_) {
                char flight[256];
                // Exclude the machine's own boxes before probing. The origin
                // is the bottom of its gear and the skid box starts there, so
                // a probe straight down from it hits ITSELF on the first
                // metre and every altitude in the air reads zero.
                world_.enable_helicopter_collision(collider_,false);
                const auto support=collider_.probe_down(
                    helicopter_.position+glm::vec3{0,.05f,0},10000);
                world_.enable_helicopter_collision(collider_,true);
                const float altitude=std::max(0.0f,helicopter_.position.y-
                    (support.hit?support.point.y:collider_.height(
                        helicopter_.position.x,helicopter_.position.z)));
                const float knots=glm::length(glm::vec2{helicopter_.velocity.x,
                    helicopter_.velocity.z})*1.943844f;
                std::snprintf(flight,sizeof(flight),
                    "HALBERD GUNSHIP   %.0f KTS   %.0f FT   ROTOR %.0f%%",
                    static_cast<double>(knots),static_cast<double>(altitude*3.28084f),
                    static_cast<double>(helicopter_.rotor*100));
                hud_.text_centered(flight,vp.x*.5f,vp.y-160,22,{.8f,.95f,1,1});
                // Named by what they DO, not by what they are. A player who
                // reads "cyclic" learns nothing; a player who reads CLIMB
                // first learns the one control that is not where a car keeps
                // it, which is the whole difficulty of the machine.
                hud_.text_centered(helicopter_.crashed ? "HELICOPTER WRECKED - R TO RESET" :
                    (helicopter_.rotor<kHeliLiftoffRotor ? "ROTOR SPOOLING UP..." :
                    "SHIFT/CTRL CLIMB / DESCEND   W/S FORWARD / BACK   A/D TURN   R RESET"),
                    vp.x*.5f,vp.y-130,18,{1,1,1,1});
            } else if (!on_foot_ && !paint_shop_.modal()) {
                // The respray booth's panel owns the right edge while it is
                // open; the car panel would show through under its hint line.
                const float speed_mph = metres_per_second_to_miles_per_hour(
                    glm::length(car_.velocity));
                const float speed_limit_mps = current_speed_limit_mps();
                const float speed_limit_mph = metres_per_second_to_miles_per_hour(
                    speed_limit_mps);
                const bool over_limit = speed_limit_mps > 0.0f &&
                    glm::length(car_.velocity) > speed_limit_mps +
                        PoliceOffenseTracker::speeding_tolerance_mps;
                const float health01 =
                    glm::clamp(car_.health / 100.0f, 0.0f, 1.0f);
                const float right = vp.x - 24.0f;
                const float bottom = vp.y - 24.0f;
                const float left = right - 260.0f;
                const float top = bottom - 154.0f;
                const glm::vec4 condition_color = glm::mix(
                    glm::vec4{0.92f, 0.12f, 0.08f, 1.0f},
                    glm::vec4{0.96f, 0.67f, 0.14f, 1.0f}, health01);

                // Sparse PS2-era GTA treatment: hard corners, deep smoked
                // plate, one strong accent and no telemetry dump.
                hud_.rect({left + 5.0f, top + 5.0f},
                          {right + 5.0f, bottom + 5.0f},
                          {0.0f, 0.0f, 0.0f, 0.28f});
                hud_.rect({left, top}, {right, bottom},
                          {0.01f, 0.015f, 0.02f, 0.72f});
                hud_.rect({right - 5.0f, top}, {right, bottom},
                          {0.84f, 0.10f, 0.06f, 0.95f});
                hud_.outline({left, top}, {right, bottom}, 1.0f,
                             {1.0f, 0.92f, 0.72f, 0.18f});

                char line[32];
                std::snprintf(line, sizeof(line), "%3.0f MPH",
                              static_cast<double>(speed_mph));
                hud_.text_centered(line, left + 127.0f, top + 10.0f, 42.0f,
                                   over_limit ? glm::vec4{1.0f, 0.18f, 0.10f, 1.0f}
                                              : glm::vec4{1.0f, 0.78f, 0.30f, 1.0f});

                char vehicle_status[64];
                if (speed_limit_mps > 0.0f)
                    std::snprintf(vehicle_status, sizeof(vehicle_status),
                        "%s   LIMIT %.0f",
                        vehicle_engine_failed(car_.mechanical) ? "ENGINE OUT" : "VEHICLE",
                        static_cast<double>(speed_limit_mph));
                else
                    std::snprintf(vehicle_status, sizeof(vehicle_status), "%s",
                        vehicle_engine_failed(car_.mechanical) ? "ENGINE OUT" : "VEHICLE");
                hud_.text(vehicle_status, {left + 18.0f, top + 68.0f}, 13.0f,
                          {0.68f, 0.70f, 0.68f, 1.0f});
                std::snprintf(line, sizeof(line), "%3.0f%%",
                              static_cast<double>(car_.health));
                hud_.text_centered(line, right - 32.0f, top + 68.0f, 13.0f,
                                   condition_color);

                const float bar_left = left + 18.0f;
                const float bar_right = right - 18.0f;
                const float bar_top = top + 93.0f;
                hud_.rect({bar_left, bar_top}, {bar_right, bar_top + 8.0f},
                          {0.08f, 0.09f, 0.09f, 0.95f});
                hud_.rect({bar_left, bar_top},
                          {bar_left + (bar_right - bar_left) * health01,
                           bar_top + 8.0f},
                          condition_color);

                const auto leaks = vehicle_fluid_leaks(car_.body_damage);
                const std::array<float, 3> fluid_levels{
                    car_.mechanical.fuel_remaining,
                    car_.mechanical.oil_remaining,
                    1.0f - leaks[static_cast<std::size_t>(
                                     VehicleFluidKind::Coolant)].severity,
                };
                constexpr std::array<UiSymbol, 3> fluid_symbols{
                    UiSymbol::LocalGasStation,
                    UiSymbol::OilBarrel,
                    UiSymbol::ModeDual,
                };
                const auto fluid_color = [](float level) {
                    if (level < 0.28f) {
                        return glm::vec4{0.96f, 0.12f, 0.08f, 1.0f};
                    }
                    if (level < 0.62f) {
                        return glm::vec4{1.0f, 0.62f, 0.12f, 1.0f};
                    }
                    return glm::vec4{0.72f, 0.82f, 0.66f, 1.0f};
                };
                for (std::size_t i = 0; i < fluid_levels.size(); ++i) {
                    const float slot_x = left + 18.0f +
                                         static_cast<float>(i) * 75.0f;
                    const float level = glm::clamp(fluid_levels[i], 0.0f, 1.0f);
                    const glm::vec4 color = fluid_color(level);
                    hud_.symbol(fluid_symbols[i], {slot_x, top + 112.0f},
                                21.0f, color);
                    hud_.rect({slot_x + 28.0f, top + 123.0f},
                              {slot_x + 62.0f, top + 128.0f},
                              {0.10f, 0.11f, 0.11f, 0.95f});
                    hud_.rect({slot_x + 28.0f, top + 123.0f},
                              {slot_x + 28.0f + 34.0f * level,
                               top + 128.0f},
                              color);
                }

                if (impact_feedback_seconds_ > 0.0f) {
                    const float flash_alpha = glm::clamp(
                        impact_feedback_seconds_ / 0.55f, 0.0f, 1.0f) * 0.65f;
                    hud_.outline({5.0f, 5.0f}, {vp.x - 5.0f, vp.y - 5.0f},
                                 8.0f, {1.0f, 0.16f, 0.08f, flash_alpha});
                }
            }

            if (!dev_menu_.open() && !bank_interaction_.modal() && !paint_shop_.modal() &&
                !weapon_wheel_.open && mission_success_feedback_s_ <= 0.0f) {
                std::string prompt;
                if (step_index_<vehicle_notice_until_) prompt=vehicle_interaction_notice_;
                else if (vehicle_transition_.active()) prompt=vehicle_transition_.direction==VehicleTransitionDirection::Enter
                    ? "Getting in" : "Getting out";
                else if (boat_transition_.active()) prompt=boat_transition_.direction==VehicleTransitionDirection::Enter
                    ? "Boarding Marlin Sprint 22" : "Climbing onto dock";
                else if (in_boat_) prompt="E / A  -  Exit beside dock or shore";
                else if (in_aircraft_) prompt=aircraft_can_exit(aircraft_) ?
                    "E / A  -  Exit aircraft" : "LAND AND STOP TO EXIT";
                else if (in_helicopter_) prompt=helicopter_can_exit(helicopter_) ?
                    "E / A  -  Exit helicopter" : "LAND AND STOP TO EXIT";
                else if (!on_foot_) prompt=is_convertible(car_visual_.active_car())
                    ? (soft_top_.target>.5f ? "E / A  -  Exit vehicle   H - HORN   J - TOP UP"
                                            : "E / A  -  Exit vehicle   H - HORN   J - TOP DOWN")
                    : has_passenger_door(car_visual_.active_car())
                        ? "E / A  -  Exit vehicle   H - HORN   J - PASSENGER DOOR"
                        : "E / A  -  Exit vehicle   H - HORN";
                else if (mission_stage_==MissionStage::DeliveryActive &&
                         delivery_contact(player_character_.position,on_foot_))
                    prompt="E / A  -  Give Lou's package to Devon";
                else if (nearby_bent_elbow())
                    prompt=drunk_.active()
                        ? "G  -  Have another drink at The Bent Elbow"
                        : "G  -  Have a drink at The Bent Elbow";
                else if (nearby_aircraft()) prompt="E / A  -  Board Aster A-80";
                else if (nearby_helicopter()) prompt="E / A  -  Board Halberd Gunship";
                else if (nearby_boat()) prompt="E / A  -  Board Marlin Sprint 22";
                else {
                    const auto target=nearby_vehicle();
                    if (target.kind!=VehicleEntryTarget::Kind::None) {
                        const auto& model=player_car_definition(target.model);
                        if (target.locked) prompt="Patrol car locked";
                        else {
                            prompt=target.kind==VehicleEntryTarget::Kind::Traffic?"E / A  -  Steal ":"E / A  -  Enter ";
                            prompt+=model.brand;prompt+=" ";prompt+=model.model;
                        }
                    }
                }
                if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && car_visual_.active_car()==PlayerCarId::HarrowHauler)
                    prompt += trailer_.attached ? "   T / DPAD RIGHT - DROP TRAILER" : "   T / DPAD RIGHT - COUPLE TRAILER";
                if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && has_police_lightbar(car_visual_.active_car()))
                    prompt += "   J / L3 - SIREN + LIGHTS";
                if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && has_plow_kit(car_visual_.active_car()))
                    prompt += plow_blade_.lowered ? "   V / DPAD DOWN - RAISE BLADE"
                                                  : "   V / DPAD DOWN - LOWER BLADE";
                if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && repair_shop_ready(car_,tuning_))
                    prompt += repair_shop_visit_.serviced ? "   SERVICE COMPLETE" : "   HOLD STILL - REPAIRING";
                if (!prompt.empty()) hud_.text_centered(prompt.c_str(),vp.x*.5f,vp.y-90,20,{1,.95f,.75f,1});
                draw_respray_prompt(vp);
                draw_car_bomb_prompt(vp);
            }
            game_ui_.draw_dev_menu(hud_, dev_menu_, vp);
            if (on_foot_ && !dev_menu_.open() && !bank_interaction_.modal() &&
                !weapon_wheel_.open && mission_success_feedback_s_ <= 0.0f)
                hud_.text_centered(weapon_wheel_.equipped==WeaponId::Pistol
                    ? "RMB / LT - HOLD AIM    Q - TOGGLE AIM    LMB / RT - FIRE    R / X - RELOAD    TAB / LB - WEAPONS"
                    : (weapon_wheel_.equipped==WeaponId::Molotov
                        ? "RMB / LT - HOLD AIM    Q - TOGGLE AIM    LMB / RT - THROW    TAB / LB - WEAPONS"
                        : "TAB / LB - WEAPONS"),vp.x*.5f,vp.y-56,16,{.8f,.82f,.83f,1});
            if (on_foot_ && weapon_wheel_.equipped==WeaponId::Pistol &&
                !dev_menu_.open() && !bank_interaction_.modal() && !weapon_wheel_.open) {
                char ammo[80];
                std::snprintf(ammo,sizeof(ammo),"PISTOL   %02d / %02d%s",weapon_use_.magazine,
                    weapon_use_.reserve,weapon_use_.reloading ? "   RELOADING" :
                    (weapon_use_.magazine==0 ? "   EMPTY - RELOAD" : ""));
                hud_.text(ammo,{vp.x-320.f,92.f},24,{1,.95f,.8f,1});
                if (weapon_use_.reloading)
                    hud_.rect({vp.x-320.f,124.f},{vp.x-320.f+220.f*weapon_use_.reload_progress(),128.f},{1,.8f,.35f,1});
                if (!weapon_use_.reloading) {
                    const glm::vec2 c=vp*.5f;
                    const bool aiming=weapon_use_.aim_blend>.5f;
                    const float gap=(aiming ? 7.f : 13.f)+weapon_use_.recoil*12.f;
                    const glm::vec4 ink=aiming ? glm::vec4{1,1,.92f,1} : glm::vec4{1,1,1,.55f};
                    const auto stroke=[&](glm::vec2 a,glm::vec2 b) {
                        hud_.line(a,b,5,{0,0,0,.75f});
                        hud_.line(a,b,2.5f,ink);
                    };
                    stroke(c+glm::vec2{-gap-10,0},c+glm::vec2{-gap,0});
                    stroke(c+glm::vec2{gap,0},c+glm::vec2{gap+10,0});
                    stroke(c+glm::vec2{0,-gap-10},c+glm::vec2{0,-gap});
                    stroke(c+glm::vec2{0,gap},c+glm::vec2{0,gap+10});
                    hud_.circle(c,3.5f,{0,0,0,.8f});
                    hud_.circle(c,1.5f,ink);
                    if (aiming) hud_.text_centered(weapon_aim_toggle_ ? "AIM - Q TO RELEASE" : "AIM",
                        c.x,c.y+38,17,{1,.9f,.6f,.95f});
                    if (weapon_hit_feedback_>0.f) {
                        hud_.line(c+glm::vec2{-9,-9},c+glm::vec2{9,9},3,{1,.15f,.08f,1});
                        hud_.line(c+glm::vec2{-9,9},c+glm::vec2{9,-9},3,{1,.15f,.08f,1});
                    }
                }
            }
            // The molotov's readout. No magazine and no reload bar: what the
            // player needs to know is how many bottles are left and whether
            // the next one is back in the hand yet, and the second of those is
            // a state the pistol does not have.
            if (on_foot_ && weapon_wheel_.equipped==WeaponId::Molotov &&
                !dev_menu_.open() && !bank_interaction_.modal() && !weapon_wheel_.open) {
                char bottles[80];
                std::snprintf(bottles,sizeof(bottles),"MOLOTOV   x%d%s",
                    molotov_use_.stock,
                    molotov_use_.stock==0 ? "   OUT" :
                    (molotov_use_.armed() ? "" : "   READYING"));
                hud_.text(bottles,{vp.x-320.f,92.f},24,
                    molotov_use_.stock==0 ? glm::vec4{1,.55f,.45f,1} : glm::vec4{1,.88f,.62f,1});
                if (!molotov_use_.armed() && molotov_use_.stock>0)
                    hud_.rect({vp.x-320.f,124.f},
                        {vp.x-320.f+220.f*molotov_use_.rearm_progress(),128.f},{1,.55f,.18f,1});
                // The throw reticle is a RING, not the pistol's cross: a
                // molotov travels on an arc and lands in an area, and lending
                // it the gun's precise crosshair promises an accuracy the
                // bottle does not have.
                const glm::vec2 c=vp*.5f;
                const bool aiming=molotov_use_.aim_blend>.5f;
                const glm::vec4 ink=aiming ? glm::vec4{1,.82f,.45f,1} : glm::vec4{1,.85f,.6f,.55f};
                hud_.circle(c,aiming ? 9.f : 13.f,{0,0,0,.55f});
                hud_.circle(c,aiming ? 6.f : 9.5f,ink);
                hud_.circle(c,aiming ? 3.f : 5.f,{0,0,0,.75f});
                if (aiming) hud_.text_centered(weapon_aim_toggle_ ? "AIM - Q TO RELEASE" : "AIM",
                    c.x,c.y+38,17,{1,.9f,.6f,.95f});
            }
            if (on_foot_ && !dev_menu_.open() && !bank_interaction_.modal()) {
                char health[32];
                std::snprintf(health, sizeof(health), "HEALTH %3.0f",
                    static_cast<double>(player_vitals_.health));
                const glm::vec4 health_color = player_vitals_.health <= 32.0f
                    ? glm::vec4{1.0f, 0.18f, 0.10f, 1.0f}
                    : glm::vec4{0.82f, 0.93f, 0.76f, 1.0f};
                const float health_top = 142.0f +
                    (wanted_.level() > 0 ? GameUi::kWantedMeterDrop : 0.0f);
                hud_.text(health, {vp.x - 216.0f, health_top}, 21.0f, health_color);
                hud_.rect({vp.x - 216.0f, health_top + 29.0f},
                          {vp.x - 56.0f, health_top + 35.0f},
                          {0.04f, 0.05f, 0.05f, 0.9f});
                hud_.rect({vp.x - 216.0f, health_top + 29.0f},
                          {vp.x - 216.0f + 160.0f * player_vitals_.fraction(),
                           health_top + 35.0f}, health_color);
            }
            if (player_hit_feedback_s_ > 0.0f)
                hud_.outline({5.0f, 5.0f}, {vp.x - 5.0f, vp.y - 5.0f}, 9.0f,
                    {1.0f, 0.05f, 0.02f,
                     0.65f * glm::clamp(player_hit_feedback_s_ / 0.45f, 0.0f, 1.0f)});
            // WASTED is drawn off the DEATH ITSELF rather than off a feedback
            // timer that happened to be set beside a respawn, so the banner is
            // up for exactly as long as the player is dead and not a moment
            // during which they are already driving again.
            if (!player_vitals_.alive()) {
                hud_.rect({0.0f, 0.0f}, vp,
                    {0.28f, 0.0f, 0.0f,
                     0.55f * glm::clamp(player_vitals_.dead_seconds / 0.6f,
                                        0.0f, 1.0f)});
                hud_.title_text_centered("WASTED", vp.x * 0.5f, 116.0f, 58.0f,
                                         {0.92f, 0.08f, 0.05f, 1.0f});
            }
            draw_weapon_wheel(hud_,weapon_wheel_,vp,economy_.owned_weapons);
            if(repair_shop_feedback_s_>0 && respray_feedback_s_<=0)
                hud_.text_centered("CAR REPAIRED",vp.x*.5f,vp.y*.25f,30,{.65f,1,.65f,1});
            draw_respray_card(vp);
            if (respray_feedback_s_ <= 0.0f) draw_car_bomb_card(vp);
            if (mission_success_feedback_s_ > 0.0f) {
                constexpr float kDisplaySeconds = 6.25f;
                const float age = kDisplaySeconds - mission_success_feedback_s_;
                const float enter = glm::smoothstep(0.0f, 0.42f, age);
                const float leave = glm::smoothstep(0.0f, 1.25f,
                    mission_success_feedback_s_);
                const float alpha = enter * leave;
                const float centre_x = vp.x * 0.5f + (1.0f - enter) * 92.0f;
                const float top = vp.y * 0.32f;
                const float title_h = 94.0f;
                const float title_w = hud_.measure_title_text(
                    "Mission Success", title_h);
                const float half = std::max(340.0f, title_w * 0.5f + 58.0f);

                // An original late-90s crime-game card: hard slanted plate,
                // warm title ink, thick offset shadow and a single red slash.
                hud_.quad({centre_x - half - 22.0f, top + 8.0f},
                          {centre_x + half, top - 4.0f},
                          {centre_x + half + 18.0f, top + 141.0f},
                          {centre_x - half, top + 154.0f},
                          {0.015f, 0.012f, 0.012f, 0.62f * alpha});
                hud_.rect({centre_x - half + 72.0f, top + 124.0f},
                          {centre_x + half - 48.0f, top + 131.0f},
                          {0.82f, 0.10f, 0.07f, 0.95f * alpha});
                hud_.rect({centre_x + half - 124.0f, top + 124.0f},
                          {centre_x + half - 48.0f, top + 131.0f},
                          {1.0f, 0.57f, 0.12f, alpha});
                hud_.title_text_centered("Mission Success", centre_x + 7.0f,
                                         top + 19.0f, title_h,
                                         {0.02f, 0.015f, 0.012f,
                                          0.92f * alpha});
                hud_.title_text_centered("Mission Success", centre_x,
                                         top + 10.0f, title_h,
                                         {0.94f, 0.91f, 0.82f, alpha});
                hud_.text_centered("PACKAGE DELIVERED", centre_x,
                                   top + 137.0f, 17.0f,
                                   {1.0f, 0.72f, 0.30f, alpha});
            }
            if (!dev_menu_.open()) draw_respray_booth(vp);
            if (!dev_menu_.open()) draw_gun_store(vp);
            if (!dev_menu_.open()) bank_interaction_.draw(hud_, vp,
                bank_target(city::bank_local_position(player_character_.position), on_foot_),
                bank_vault_);
            game_ui_.draw_arrested(hud_, arrested_feedback_s_, vp);
        } else {
            GameUiSnapshot snapshot;
        snapshot.save_notice=save_notice_.c_str();
            snapshot.player_position = player_focus_position();
            snapshot.player_forward = player_focus_forward();
            if (mission_stage_ == MissionStage::DeliveryNeedsCar)
                snapshot.mission_target = glm::vec2{car_.position.x, car_.position.z};
            else if (mission_stage_ == MissionStage::DeliveryActive)
                snapshot.mission_target = glm::vec2{city::devon_position().x,
                                                    city::devon_position().z};
            snapshot.speed_mph = metres_per_second_to_miles_per_hour(
                glm::length(on_foot_ ? player_character_.velocity
                                     : (in_boat_ ? boat_.velocity
                                        : (in_aircraft_ ? aircraft_.velocity
                                           : (in_helicopter_ ? helicopter_.velocity
                                              : car_.velocity)))));
            snapshot.vehicle_health = car_.health;
            snapshot.perf_logging = perf_log_.enabled();
            snapshot.perf_log_label = perf_log_label_.c_str();
            snapshot.perf_marks = perf_marks_;
            snapshot.perf_mark_feedback_s = perf_mark_feedback_s_;
            snapshot.weather = weather_name(conditions_.weather);
            snapshot.daylight = daylight_name(conditions_.daylight);
            snapshot.time_of_day = env.time_of_day;
            game_ui_.draw(hud_, ui_, snapshot, vp);
        }

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
    stats.snow_depth_m = conditions_.snow_depth_m;
    stats.fog_start_m = env.fog_start;
    stats.fog_end_m = env.fog_end;
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

    if (bug_report_.open) {
        overlay::draw_bug_report(window_, bug_report_);
    } else if (ui_.screen() == UiScreen::Driving && controls_.stats_visible &&
               !bug_report_capture_pending_) {
        overlay::draw(window_, stats, controls_);
    }

    if (bug_report_.cancel_requested) {
        bug_report_.cancel_requested = false;
        bug_report_.close();
        input_.set_ui_mode(ui_.modal());
        if (bug_report_restore_mouse_) input_.set_mouse_look(true);
        AP_INFO("bug report cancelled");
    } else if (bug_report_.submit_requested) {
        bug_report_.submit_requested = false;
        bug_report_.close();
        bug_report_capture_pending_ = true;
        bug_report_submit_frame_ = frames_rendered_;
        input_.set_ui_mode(ui_.modal());
        if (bug_report_restore_mouse_) input_.set_mouse_look(true);
        AP_INFO("bug report queued for clean framebuffer capture");
    }

    // Wait one rendered frame after the form closes. The attachment then
    // contains the game view the player meant to report, not the report form.
    if (bug_report_capture_pending_ &&
        frames_rendered_ > bug_report_submit_frame_) {
        capture_and_submit_bug_report();
    }

    if (character_identity_check_) character_identity_check();

    if (weapon_check_) {
        const int capture_frame=frames_rendered_+1;
        if (capture_frame==60 && weapon_wheel_.open && weapon_wheel_.hovered==WeaponId::Pistol &&
            save_screenshot(screenshot_path_+".wheel.bmp")) weapon_check_captures_|=1u;
        glm::mat4 hand{1};
        if (capture_frame==100 && !weapon_wheel_.open && weapon_wheel_.equipped==WeaponId::Pistol &&
            character_visual_.player_right_hand_transform(hand) &&
            save_screenshot(screenshot_path_+".held.bmp")) weapon_check_captures_|=2u;
        if (capture_frame==240 && !weapon_wheel_.open && weapon_wheel_.equipped==WeaponId::Unarmed &&
            save_screenshot(screenshot_path_+".unarmed.bmp")) weapon_check_captures_|=4u;
        if (capture_frame==340 && weapon_use_.aim_blend>.95f &&
            save_screenshot(screenshot_path_+".aim.bmp")) weapon_check_captures_|=8u;
        if (capture_frame==351 && weapon_shots_==1 && weapon_use_.magazine==11 &&
            save_screenshot(screenshot_path_+".fire.bmp")) weapon_check_captures_|=16u;
        if (capture_frame==430 && weapon_use_.reloading && weapon_shots_==2 &&
            save_screenshot(screenshot_path_+".reload.bmp")) weapon_check_captures_|=32u;
        if (capture_frame==500 && !weapon_use_.reloading && weapon_use_.magazine==12 &&
            weapon_use_.reserve==46) weapon_check_captures_|=64u;
        if (capture_frame==580 && weapon_shots_==2 && !weapon_wheel_.open &&
            weapon_use_.magazine==12) weapon_check_captures_|=128u;
        if (capture_frame==342) save_screenshot(screenshot_path_+".aim-side.png");
        if (capture_frame==352) save_screenshot(screenshot_path_+".fire-side.png");
        if (capture_frame==431) save_screenshot(screenshot_path_+".reload-side.png");
        capture_weapon_hit_check();

    }
    if (molotov_check_) capture_molotov_check();
    if (gun_store_.check) capture_gun_store_check();

    if(lighting_benchmark_ && !screenshot_path_.empty() &&
        (frames_rendered_==419 || frames_rendered_==539)) {
        save_screenshot(screenshot_path_+(frames_rendered_==419 ? ".off.bmp" : ".on.bmp"));
    }
    if (police_check_ && frames_rendered_>=180 && !screenshot_path_.empty()) {
        const auto power=police_flash_power(step_index_,police_emergency_enabled_);
        const unsigned bit=power[0]>0 ? 1u : power[1]>0 ? 2u :
            (!police_emergency_enabled_ && frames_rendered_>450 ? 4u : 0u);
        if (bit && !(police_check_captures_&bit) && police_siren_.started()) {
            if (save_screenshot(screenshot_path_+(bit==1 ? ".red.bmp" : bit==2 ? ".blue.bmp" : ".off.bmp"))) {
                police_check_captures_|=bit;
                AP_INFO("police check captured phase %u with siren controller ready",bit);
            }
        }
    }
    if (driver_transition_check_ && is_motorbike(car_visual_.active_car()) &&
        vehicle_transition_.active() && vehicle_transition_.tick>=150 &&
        !screenshot_path_.empty()) {
        const unsigned bit=driver_check_stage_==1 ? 8u : driver_check_stage_==3 ? 16u : 0u;
        if (bit && !(driver_check_camera_captures_&bit) &&
            save_screenshot(screenshot_path_+(bit==8 ? ".saddle-enter-mid.bmp" :
                ".saddle-exit-mid.bmp"))) driver_check_camera_captures_|=bit;
    }
    if (driver_transition_check_ && driver_check_stage_==4 && !screenshot_path_.empty()) {
        const unsigned bit=transition_camera_release_>.9f ? 1u :
            (transition_camera_release_>.4f && transition_camera_release_<.6f ? 2u :
             (transition_camera_release_<=0.f ? 4u : 0u));
        if (bit && !(driver_check_camera_captures_&bit) &&
            save_screenshot(screenshot_path_+(bit==1 ? ".exit-start.bmp" :
                bit==2 ? ".exit-mid.bmp" : ".exit-end.bmp"))) driver_check_camera_captures_|=bit;
    }
    if(house_check_)capture_house_check();
    if(signal_check_)capture_signal_check();
    if(police_officer_check_)capture_police_officer_check();
    if(traffic_horn_check_)capture_traffic_horn_check();
    if (convertible_check_) capture_convertible_check();
    if (paint_check_) capture_paint_check();
    if (car_bomb_check_) capture_car_bomb_check();
    if (!screenshot_path_.empty() && frame_limit_ > 0 &&
        frames_rendered_ + 1 >= frame_limit_) {
        save_screenshot(screenshot_path_);
        screenshot_path_.clear();
    }

    // Check the error queue for the first stretch of frames. Every frame
    // forever would be a needless driver round trip; never checking at all is
    // how a broken call ships.
    if (frames_rendered_ < 8) {
        gl_errors_ += drain_gl_errors("during the first frames");
    }

    publish_playtest_state();
    gpu_timer_.end();

    // The swap is measured SEPARATELY from the rest of render() because the
    // two mean opposite things. Time above this line is work; time inside
    // swap is the CPU blocked waiting for the display. A dip made of swap has
    // already told you the CPU was fast enough, and that every phase above it
    // is the wrong place to look.
    const WallClock::time_point swap_t0 = WallClock::now();
    render_ms_ =
        std::chrono::duration<double>(swap_t0 - render_t0).count() * 1000.0;
    window_.swap();
    swap_ms_ =
        std::chrono::duration<double>(WallClock::now() - swap_t0).count() *
        1000.0;
    ++frames_rendered_;
}

void App::step_player_plow(const InputFrame& input, bool first_step_of_frame) {
    const PlayerCarId car = car_visual_.active_car();
    if (car != plow_car_) {
        // A different truck starts with its blade down and no strip open.
        plow_car_ = car;
        plow_blade_ = {};
        plow_sweep_.reset();
    }
    if (!has_plow_kit(car)) return;
    const bool driving = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
                         !vehicle_transition_.active();
    // Latched edges repeat on every step of a frame, so a live press is read
    // once per frame; the scripted check sets its press on exactly one step.
    if (driving && (first_step_of_frame || plow_check_) && was_pressed(input, kBtnPlowBlade))
        plow_blade_.lowered = !plow_blade_.lowered;
    plow_blade_.step(static_cast<float>(kSimDt));
    const PlowBladeMount mount = plow_blade_mount_for(car, tuning_);
    const glm::vec3 edge = car_.position + car_.orientation * mount.edge_local(plow_blade_.raised);
    plow_sweep_.step(edge, vehicle_speed(car_), plow_blade_.scraping(),
                     mount.half_width_m, snow_clearance_, conditions_.snow_depth_m);
}

namespace {
constexpr PlayerCarId kLotPlowModels[] = {PlayerCarId::RodeoGrazerPlow,
                                          PlayerCarId::HarrowWorkmanPlow};

VehicleState lot_plow_state(const LotPlowTruck& truck, const VehicleTuning& tuning) {
    VehicleState state;
    state.position = truck.position;
    state.orientation = truck.orientation();
    state.steer_angle = truck.steer_rad;
    state.velocity = truck.forward() * truck.speed_mps;
    state.mechanical_key = splitmix64_mix(0x4C4F54504C4F57ull ^ truck.plan.lot_index);
    for (auto& wheel : state.wheels) {
        wheel.suspension_length = static_suspension_length(tuning);
        wheel.spin = truck.wheel_spin;
    }
    return state;
}
}  // namespace

bool App::lot_plow_check_passed() const {
    float swept = 0.0f;
    for (const auto& truck : lot_plows_.trucks()) swept += truck.sweep.cleared_distance_m();
    return lot_plows_.dispatched() && swept >= 20.0f;
}

void App::plan_lot_plows() {
    const auto lots = city::authored_building_access_lots();
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < lots.size(); ++i)
        if (is_lot_plow_candidate(lots[i])) candidates.push_back(i);
    std::vector<LotPlowTruckSpec> specs;
    for (const PlayerCarId model : kLotPlowModels)
        specs.push_back(lot_plow_truck_spec(model, player_model_tuning(driving_mechanics_style_, model)));
    lot_plows_.plan(lots, candidates, seed_, specs);
    // The QA camera follows the crew nearest the start.
    float nearest = 1e30f;
    for (std::size_t i = 0; i < lot_plows_.trucks().size(); ++i) {
        const glm::vec2 at = lot_plows_.trucks()[i].plan.passes.front().start;
        const float d = glm::distance(at, start_position_);
        if (d < nearest) { nearest = d; lot_plow_followed_ = i; }
    }
    for (const auto& truck : lot_plows_.trucks())
        AP_INFO("lot plows: %s %s works '%s', %zu passes",
                player_car_definition(kLotPlowModels[truck.model]).brand,
                player_car_definition(kLotPlowModels[truck.model]).model,
                truck.plan.lot, truck.plan.passes.size());
}

void App::clear_lot_plows() {
    for (auto& truck : lot_plows_.trucks()) {
        if (truck.collider != static_cast<std::size_t>(-1))
            collider_.set_kinematic_enabled(truck.collider, false);
    }
    for (auto& rig : lot_plow_rigs_) rig.visual.destroy(scene_);
    lot_plow_rigs_.clear();
    // Replan so the next storm sends fresh crews to fresh passes; kinematic
    // slots are stable and reused.
    std::vector<std::size_t> slots;
    for (const auto& truck : lot_plows_.trucks()) slots.push_back(truck.collider);
    plan_lot_plows();
    for (std::size_t i = 0; i < lot_plows_.trucks().size() && i < slots.size(); ++i)
        lot_plows_.trucks()[i].collider = slots[i];
}

void App::step_lot_plows() {
    if (conditions_.snow_depth_m <= 0.0f) {
        if (lot_plows_.dispatched()) clear_lot_plows();
        return;
    }
    // Everything a truck must stop for, near enough to matter this step.
    std::vector<LotPlowObstacle> obstacles;
    const auto near_a_truck = [&](glm::vec3 p) {
        for (const auto& truck : lot_plows_.trucks())
            if (glm::distance(glm::vec2{p.x, p.z}, glm::vec2{truck.position.x, truck.position.z}) < 30.0f)
                return true;
        return false;
    };
    const auto add_body = [&](glm::vec3 centre, glm::vec3 forward, float half_width, float half_length) {
        if (!near_a_truck(centre)) return;
        glm::vec2 f{forward.x, forward.z};
        const float length = glm::length(f);
        f = length > 1e-4f ? f / length : glm::vec2{0.0f, -1.0f};
        const float reach = std::max(0.0f, half_length - half_width);
        for (const float t : {-1.0f, 0.0f, 1.0f})
            obstacles.push_back({glm::vec2{centre.x, centre.z} + f * (t * reach), half_width + 0.1f});
    };
    if (lot_plows_.dispatched()) {
        for (const auto& ped : world_.traffic().peds())
            if (near_a_truck(ped.pos)) obstacles.push_back({{ped.pos.x, ped.pos.z}, 0.45f});
        for (const auto& v : world_.traffic().vehicles()) {
            const auto footprint = traffic_vehicle_footprint(traffic_vehicle_kind(v));
            add_body(v.pos, v.fwd, footprint.half_width_m, footprint.half_length_m);
        }
        for (const auto& parked : parked_vehicles_)
            add_body(parked.state.position, vehicle_forward(parked.state),
                     parked.tuning.car_collision_half_width, parked.tuning.car_collision_half_length);
        if (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_) {
            if (near_a_truck(player_character_.position))
                obstacles.push_back({{player_character_.position.x, player_character_.position.z}, 0.5f});
        }
        add_body(car_.position, vehicle_forward(car_), tuning_.car_collision_half_width,
                 tuning_.car_collision_half_length + tuning_.car_collision_front_extension);
    }
    std::vector<std::pair<std::size_t, LotPlowTruck::Phase>> before;
    for (const auto& truck : lot_plows_.trucks()) before.push_back({truck.pass, truck.phase});
    lot_plows_.step(static_cast<float>(kSimDt), conditions_.snow_depth_m, collider_, obstacles,
                    snow_clearance_);
    if (!lot_plows_.dispatched()) return;
    for (std::size_t i = 0; i < before.size() && i < lot_plows_.trucks().size(); ++i) {
        const auto& truck = lot_plows_.trucks()[i];
        if (truck.phase == LotPlowTruck::Phase::Done && before[i].second != LotPlowTruck::Phase::Done)
            AP_INFO("lot plows: '%s' done after pass %zu of %zu, %.1f m of blade run",
                    truck.plan.lot, truck.pass + 1, truck.plan.passes.size(),
                    static_cast<double>(truck.sweep.cleared_distance_m()));
        else if (truck.pass != before[i].first)
            AP_DEBUG("lot plows: '%s' pass %zu of %zu", truck.plan.lot, truck.pass + 1,
                     truck.plan.passes.size());
    }
    // Solid to the player: one box over the body and the blade, flagged as a
    // vehicle so contact sounds and reads like hitting a truck.
    for (auto& truck : lot_plows_.trucks()) {
        const float front = truck.spec.blade.edge_forward_m + 0.3f;
        const float rear = truck.spec.rear_m;
        const glm::vec3 centre = truck.position + truck.forward() * (0.5f * (front - rear)) +
                                 glm::vec3{0.0f, 0.1f, 0.0f};
        const glm::vec3 half{std::max(truck.spec.half_width_m, truck.spec.blade.half_width_m), 0.95f,
                             0.5f * (front + rear)};
        if (truck.collider == static_cast<std::size_t>(-1)) {
            truck.collider = collider_.add_kinematic_oriented_box(centre, half, truck.heading);
            collider_.set_kinematic_vehicle(truck.collider, true);
        } else {
            collider_.set_kinematic_oriented_box(truck.collider, centre, half, truck.heading);
        }
        collider_.set_kinematic_enabled(truck.collider, true);
    }
}

void App::sync_lot_plows(float alpha, float headlight_level) {
    if (!lot_plows_.dispatched()) return;
    if (lot_plow_rigs_.empty()) {
        for (const auto& truck : lot_plows_.trucks()) {
            LotPlowRig rig;
            const PlayerCarId model = kLotPlowModels[truck.model];
            rig.tuning = player_model_tuning(driving_mechanics_style_, model);
            rig.current = rig.previous = lot_plow_state(truck, rig.tuning);
            car_visual_.clone_parked(scene_, rig.visual);
            if (!rig.visual.select(scene_, rig.tuning, rig.current, model)) {
                rig.visual.destroy(scene_);
                continue;
            }
            lot_plow_rigs_.push_back(std::move(rig));
        }
    }
    const std::size_t count = std::min(lot_plow_rigs_.size(), lot_plows_.trucks().size());
    for (std::size_t i = 0; i < count; ++i) {
        LotPlowRig& rig = lot_plow_rigs_[i];
        const LotPlowTruck& truck = lot_plows_.trucks()[i];
        const VehicleState now = lot_plow_state(truck, rig.tuning);
        if (glm::distance(now.position, rig.current.position) > 1e-5f ||
            now.orientation != rig.current.orientation) {
            rig.previous = rig.current;
            rig.current = now;
        }
        rig.visual.set_plow_raised(truck.blade.raised);
        // Slow lot work never sheds snow, so the settled load for where the
        // truck stands is its whole state, as for a parked car.
        const glm::vec3 roof = now.position + glm::vec3{0.0f, 1.0f, 0.0f};
        const float snow_load = ui_.settings().weather_effects
            ? seed_vehicle_snow_load(snow_shelter_.exposure(roof.x, roof.y, roof.z),
                                     vehicle_snow_weather())
            : -1.0f;
        rig.visual.sync(scene_, rig.tuning, rig.previous, rig.current, alpha, headlight_level,
                        std::fabs(truck.speed_mps) < 0.05f && truck.working() ? 1.0f : 0.0f,
                        snow_load);
        rig.visual.sync_plow_lights(scene_, step_index_ + i * 37u, truck.working(), headlight_level);
    }
}

// --plow-check: push a pass along Cloggers' frontage with the blade down,
// stop, lift the blade and back up the next lane over, drop it and push a
// second pass beside the first. Closed loop on the truck's real pose (a
// time script drove into the street the first time it was tried), and
// deterministic: it reads nothing but sim state.
InputFrame App::plow_check_input() const {
    InputFrame input;
    const auto& site = city::kFastFoodSite;
    const glm::vec2 axis{site.cos_yaw, -site.sin_yaw};   // site +x in world
    const glm::vec2 across{site.sin_yaw, site.cos_yaw};  // site +z in world
    const glm::vec2 origin{site.origin.x, site.origin.z};
    const glm::vec2 at{car_.position.x, car_.position.z};
    const float x = glm::dot(at - origin, axis);
    const glm::vec3 f3 = vehicle_forward(car_);
    const glm::vec2 forward = glm::normalize(glm::vec2{f3.x, f3.z});
    const float speed = vehicle_speed(car_);
    // Pure pursuit on a lane at site z = lane, looking 6 m along the travel.
    const auto steer_to = [&](float lane, bool backwards) {
        const glm::vec2 travel = backwards ? axis : -axis;
        const glm::vec2 target = origin + axis * (x + glm::dot(travel, axis) * 6.0f) + across * lane;
        const glm::vec2 to = glm::normalize(target - at);
        const glm::vec2 facing = backwards ? -forward : forward;
        const float cross = facing.x * to.y - facing.y * to.x;
        const float angle = std::atan2(cross, glm::dot(facing, to));
        // Positive steer turns right; reversing, it swings the tail right.
        return std::clamp((backwards ? -2.5f : 2.5f) * angle, -1.0f, 1.0f);
    };
    const auto hold_speed = [&](float want) {
        if (std::fabs(speed) < want) input.throttle = 0.45f;
    };
    constexpr float kFirstLane = -12.0f, kSecondLane = -14.0f;
    constexpr float kPushTo = -10.0f, kBackTo = 14.0f;
    if (step_index_ < 240u) return input;  // settle on the springs
    switch (plow_check_phase_) {
        case 0:  // first push, west along the frontage
            if (x <= kPushTo) { plow_check_phase_ = 1; plow_check_mark_ = step_index_; break; }
            hold_speed(2.8f);
            input.steer = steer_to(kFirstLane, false);
            break;
        case 1:  // stop on the handbrake, then lift the blade
            input.handbrake = 1.0f;
            if (std::fabs(speed) < 0.05f && step_index_ > plow_check_mark_ + 60u) {
                input.pressed = kBtnPlowBlade;
                plow_check_phase_ = 2;
                plow_check_mark_ = step_index_;
            }
            break;
        case 2:  // back up the next lane over
            if (step_index_ < plow_check_mark_ + 120u) { input.handbrake = 1.0f; break; }
            if (x >= kBackTo) { plow_check_phase_ = 3; plow_check_mark_ = step_index_; break; }
            if (std::fabs(speed) < 2.2f) input.brake = 0.5f;  // the brake at a standstill is reverse
            input.steer = steer_to(kSecondLane, true);
            break;
        case 3:  // stop, drop the blade
            input.handbrake = 1.0f;
            if (std::fabs(speed) < 0.05f && step_index_ > plow_check_mark_ + 60u) {
                input.pressed = kBtnPlowBlade;
                plow_check_phase_ = 4;
                plow_check_mark_ = step_index_;
            }
            break;
        case 4:  // second push beside the first
            if (step_index_ < plow_check_mark_ + 120u) { input.handbrake = 1.0f; break; }
            if (x <= kPushTo) { plow_check_phase_ = 5; break; }
            hold_speed(2.8f);
            input.steer = steer_to(kSecondLane, false);
            break;
        default:
            input.handbrake = 1.0f;
            break;
    }
    return input;
}

void App::plow_check_camera() {
    glm::vec3 forward = car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    forward.y = 0.0f;
    const float length = glm::length(forward);
    forward = length > 1e-5f ? forward / length : glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 right{-forward.z, 0.0f, forward.x};
    // High and off the truck's right shoulder, looking back down its path so
    // the cleared strips and the truck that cut them share the frame.
    const glm::vec3 target = car_.position - forward * 6.0f;
    camera_.position = car_.position + forward * 7.0f + right * 11.0f + glm::vec3{0.0f, 10.0f, 0.0f};
    const glm::vec3 look = target - camera_.position;
    camera_.yaw = std::atan2(look.x, -look.z);
    camera_.pitch = std::atan2(look.y, glm::length(glm::vec2{look.x, look.z}));
}

InputFrame App::tire_track_check_input() const {
    InputFrame input;
    // Four seconds to build speed, then a long handbrake arc. The last phase
    // releases the rear tyres and leaves the camera looking across the trail.
    if (step_index_ < 480u) {
        input.throttle = 1.0f;
    } else if (step_index_ < 820u) {
        input.throttle = 0.38f;
        input.steer = 0.88f;
        input.handbrake = 1.0f;
    } else if (step_index_ < 1080u) {
        input.throttle = 0.28f;
        input.steer = -0.35f;
    }
    return input;
}

void App::tire_track_check_camera() {
    glm::vec3 track_min{0.0f};
    glm::vec3 track_max{0.0f};
    if (tire_tracks_.recent_bounds(track_min, track_max, 48u)) {
        const glm::vec3 target = (track_min + track_max) * 0.5f;
        const float span = std::max(
            std::max(track_max.x - track_min.x, track_max.z - track_min.z),
            12.0f);
        camera_.position = target +
            glm::vec3{span * 0.42f, std::max(15.0f, span * 0.86f),
                      span * 0.42f};
        const glm::vec3 look = target - camera_.position;
        camera_.yaw = std::atan2(look.x, -look.z);
        camera_.pitch = std::atan2(
            look.y, glm::length(glm::vec2{look.x, look.z}));
        return;
    }

    glm::vec3 forward = car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    forward.y = 0.0f;
    const float forward_length = glm::length(forward);
    forward = forward_length > 1e-5f
        ? forward / forward_length
        : glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 right{-forward.z, 0.0f, forward.x};
    const glm::vec3 target = car_.position - forward * 7.0f;
    camera_.position = car_.position - forward * 2.0f + right * 15.0f +
                       glm::vec3{0.0f, 16.0f, 0.0f};
    const glm::vec3 look = target - camera_.position;
    camera_.yaw = std::atan2(look.x, -look.z);
    camera_.pitch = std::atan2(
        look.y, glm::length(glm::vec2{look.x, look.z}));
}

void App::set_frame_logging(bool on) {
    if (on == perf_log_.enabled()) return;

    if (on) {
        // A fresh file per switch-on. Appending to the previous one would put
        // two unrelated stretches of play under a single summary, and the
        // percentiles of a blend are nobody's percentiles.
        std::string path = perf_log_path_;
        if (path.empty()) path = default_perf_log_path();
        if (path.empty()) {
            AP_WARN("could not create a folder for the frame log; not recording");
            return;
        }
        FrameLog::Config cfg;
        cfg.spike_ms = perf_spike_ms_;
        perf_log_.configure(cfg);
        if (!perf_log_.open(path.c_str())) {
            AP_WARN("could not open performance log '%s'; not recording",
                    path.c_str());
            return;
        }
        // Only the CHOSEN path is remembered when it came from --perf-log; a
        // default path is re-derived each time so the session number advances.
        perf_spikes_logged_ = 0;
        perf_marks_ = 0;
        perf_mark_feedback_s_ = 0.0f;
        perf_clock_reset_ = true;  // the frame that spans the switch is not a frame

        const std::size_t slash = path.find_last_of("/\\");
        std::string name = (slash == std::string::npos) ? path
                                                        : path.substr(slash + 1);
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name.resize(dot);
        const std::size_t dash = name.find_last_of('-');
        perf_log_label_ =
            (name.compare(0, 8, "session-") == 0 && dash != std::string::npos)
                ? name.substr(dash + 1)
                : name.substr(0, 12);

        std::error_code abs_ec;
        const std::string shown = std::filesystem::absolute(path, abs_ec).string();
        AP_INFO("frame logging ON: %s (dips over %.1f ms; F4 marks a moment)",
                abs_ec ? path.c_str() : shown.c_str(), perf_spike_ms_);
        return;
    }

    const FrameLog::Summary perf = perf_log_.summary();
    AP_INFO("performance: %d frames, p50 %.2f ms (%.0f fps), p99 %.2f ms, "
            "worst %.2f ms; %d dips (%.2f%%), %.0f ms of stutter",
            perf.frames, perf.p50_ms,
            perf.p50_ms > 0.0 ? 1000.0 / perf.p50_ms : 0.0, perf.p99_ms,
            perf.worst_ms, perf.spikes,
            perf.frames > 0 ? 100.0 * perf.spikes / perf.frames : 0.0,
            perf.stutter_ms);
    for (const FrameLog::Hotspot& h : perf_log_.hotspots()) {
        AP_INFO("  dips at (%.0f, %.0f) %s: %u of %u frames slow, worst %.1f ms",
                static_cast<double>(h.x), static_cast<double>(h.z), h.place,
                h.slow, h.frames, h.worst_ms);
    }
    std::error_code abs_ec;
    const std::string written =
        std::filesystem::absolute(perf_log_.path(), abs_ec).string();
    const std::string fallback = perf_log_.path();
    perf_log_.close();
    AP_INFO("frame logging OFF; written: %s",
            abs_ec ? fallback.c_str() : written.c_str());
}

void App::record_frame_sample(double ms) {
    if (!perf_log_.enabled()) return;

    const glm::vec3 focus = player_focus_position();
    const World::Stats& ws = world_.stats();

    FrameSample s;
    s.frame = frames_rendered_;
    // The SAME clock the text log stamps its lines with, on purpose: a WARN
    // about a dip and the CSV rows around it have to line up by eye, and two
    // timelines that nearly agree are worse than one.
    s.t_s = log::uptime_seconds();
    s.ms = ms;

    s.place = city::district_name(city::district_at(focus.x, focus.z));
    s.mode = interior_presentation_lod_ ? "indoor"
             : in_aircraft_             ? "air"
             : in_helicopter_           ? "heli"
             : in_boat_                 ? "boat"
             : on_foot_                 ? "foot"
                                        : "drive";
    // The ATMOSPHERIC weather, not the road surface: rain, snow and storms are
    // what cost frames, and "dry" on every row of a blizzard helps nobody.
    s.weather = atmospheric_weather_name(conditions_.atmosphere);
    s.x = focus.x;
    s.y = focus.y;
    s.z = focus.z;
    s.speed_mph = metres_per_second_to_miles_per_hour(glm::length(
        on_foot_ ? player_character_.velocity
                 : (in_boat_ ? boat_.velocity
                             : (in_aircraft_ ? aircraft_.velocity
                                : (in_helicopter_ ? helicopter_.velocity
                                                  : car_.velocity)))));
    s.time_of_day = conditions_.time_of_day;

    s.sim_steps = last_steps_;
    s.step_clamped = last_clamped_;
    s.sim_ms = sim_ms_;
    s.sim_traffic_ms = sim_traffic_ms_;
    s.sim_police_ms = sim_police_ms_;
    s.sim_character_ms = sim_character_ms_;
    s.police_vis_ms = police_vis_ms_;
    s.police_ctx_ms = police_ctx_ms_;
    s.police_calls = police_vis_calls_;
    s.police_units = static_cast<int>(world_.traffic().police_unit_count());
    // world_.update() is what mesh_ms_ has always measured; the phase column
    // is the same number under the name that says what it actually covers.
    s.world_ms = mesh_ms_;
    s.visual_ms = visual_ms_;
    s.scene_ms = scene_ms_;
    s.render_ms = render_ms_;
    s.swap_ms = swap_ms_;
    s.gpu_ms = gpu_timer_.last_ms();
    s.cull_ms = cull_ms_;
    s.mesh_ms = mesh_ms_;
    s.light_ms = tiled_lighting_.build_ms() + tiled_lighting_.upload_ms();
    // Only on the frame that actually paid for it. last_fill_ms_ is sticky by
    // design — the overlay wants "what the last fill cost" — but a per-frame
    // column repeating 22.0 for a whole session reads as a fill every frame,
    // which is the exact wrong conclusion to hand someone hunting a dip.
    s.fill_ms = (last_fill_frame_ >= frames_rendered_ - 1) ? last_fill_ms_ : 0.0;

    s.draw_calls = render_stats_.draw_calls;
    s.instances = render_stats_.instances;
    s.visible_nodes = render_stats_.visible_nodes;
    s.scene_nodes = static_cast<int>(scene_.size());
    s.batches = render_stats_.batches;
    s.skipped_binds = render_stats_.skipped_binds;

    s.chunks_built = ws.chunks_built;
    s.chunks_evicted = ws.chunks_evicted;
    s.resident_chunks = ws.resident_chunks;
    s.terrain_mb = static_cast<double>(ws.mesh_bytes) / (1024.0 * 1024.0);
    s.budget_hit = ws.budget_exhausted;

    s.cars = static_cast<int>(traffic_visual_.car_count());
    s.parked = static_cast<int>(traffic_visual_.parked_car_count());
    s.npcs = static_cast<int>(character_visual_.ambient_npc_count() +
                              character_visual_.staff_count());
    s.character_draws = character_visual_.last_draw_count();
    s.lights = tiled_lighting_.grid().visible_lights;
    s.rain_quads = rain_.drawn_quads();
    s.hud_quads = hud_.last_quad_count();
    s.gl_errors = gl_errors_;

    if (perf_mark_pending_) {
        perf_mark_pending_ = false;
        ++perf_marks_;
        perf_log_.mark("player-marked", s);
        perf_mark_feedback_s_ = 1.6f;
        AP_INFO("perf mark %d at t=%.1f s, (%.0f, %.0f) in %s", perf_marks_,
                s.t_s, static_cast<double>(s.x), static_cast<double>(s.z),
                s.place);
    }

    // A frame whose delta was measured across a clock reset — a resume from
    // the pause screen, the end of a cutscene, a teleport — is not a frame
    // anybody rendered. Recording it puts a fake sub-millisecond sample in the
    // histogram and drags the median under the truth.
    if (perf_clock_reset_) {
        perf_clock_reset_ = false;
        return;
    }

    if (!perf_log_.record(s)) return;

    // Put the first stretch of dips in the text log too, so a session run with
    // --log leaves a human-readable trail without anyone opening the CSV.
    // Capped, because a log line per dip in a genuinely bad session is both
    // useless and — this logger flushes every line — a cause of more dips.
    constexpr int kMaxSpikeLines = 40;
    if (++perf_spikes_logged_ > kMaxSpikeLines) return;
    AP_WARN("frame dip: %.1f ms at t=%.1f s, (%.0f, %.0f) in %s [%s] — "
            "cull %.2f mesh %.2f light %.2f fill %.1f, %d draws, %d chunks "
            "built, %d cars, %d npcs%s",
            s.ms, s.t_s, static_cast<double>(s.x), static_cast<double>(s.z),
            s.place, s.mode, s.cull_ms, s.mesh_ms, s.light_ms, s.fill_ms,
            s.draw_calls, s.chunks_built, s.cars, s.npcs,
            perf_spikes_logged_ == kMaxSpikeLines ? " (last one logged)" : "");
}

int App::run() {
    if (!running_) return 1;

    // Off unless the command line asked for it. The F1 menu is the normal way
    // in now, so a plain launch records nothing until someone says so.
    if (perf_logging_) set_frame_logging(true);

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
        if(house_check_ && (house_check_failed_ || (house_check_complete_ && house_check_capture_.empty())))break;
        if(signal_check_ && (signal_check_failed_ || (signal_check_done_ && signal_check_capture_.empty())))break;
        if(police_officer_check_ && (police_officer_check_failed_ ||
            (police_officer_check_done_ && police_officer_check_capture_.empty())))break;
        if(traffic_horn_check_ && (traffic_horn_check_failed_ ||
            (traffic_horn_check_done_ && traffic_horn_check_capture_.empty())))break;
        if(gun_store_.check && (gun_store_.check_failed ||
            (gun_store_.check_done && gun_store_.check_capture.empty())))break;
        if (delivery_check_) tick_delivery_check();
        if (molotov_check_) tick_molotov_check();
        if (gun_store_.check) tick_gun_store_check();
        if (damage_check_) { tick_damage_check(); capture_damage_check(); }
        if (paint_check_ && (paint_check_failed_ || (paint_check_done_ && paint_check_capture_.empty()))) break;
        if (paint_check_) tick_paint_check();
        if (car_bomb_check_ && (car_bomb_check_failed_ ||
            (car_bomb_check_done_ && car_bomb_check_capture_.empty()))) break;
        if (car_bomb_check_) tick_car_bomb_check();
        if (weapon_check_) {
            // The check is about aim, fire and reload, not the shop: it owns
            // the pistol (the gun store's own check proves buying one).
            economy_.owned_weapons=kAllWeaponBits;
            tick_weapon_hit_check();
            const auto key=[&](SDL_Keycode code,bool down) {
                SDL_Event event{};event.type=down ? SDL_KEYDOWN:SDL_KEYUP;
                event.key.keysym.sym=code;event.key.keysym.scancode=SDL_GetScancodeFromKey(code);
                SDL_PushEvent(&event);
            };
            if (frames_rendered_==30 || frames_rendered_==120 || frames_rendered_==170) key(SDLK_TAB,true);
            if (frames_rendered_==35) key(SDLK_2,true);
            if (frames_rendered_==36) key(SDLK_2,false);
            if (frames_rendered_==60 || frames_rendered_==200) key(SDLK_TAB,false);
            if (frames_rendered_==125 || frames_rendered_==175) key(SDLK_1,true);
            if (frames_rendered_==126 || frames_rendered_==176) key(SDLK_1,false);
            if (frames_rendered_==150) key(SDLK_ESCAPE,true);
            if (frames_rendered_==151) { key(SDLK_ESCAPE,false);key(SDLK_TAB,false); }
            if (frames_rendered_==160 && weapon_wheel_.equipped!=WeaponId::Pistol)
                weapon_check_captures_|=512u;
            const auto mouse=[&](uint8_t button,bool down) {
                SDL_Event event{}; event.type=down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.button=button; SDL_PushEvent(&event);
            };
            if (frames_rendered_==260 || frames_rendered_==520) key(SDLK_TAB,true);
            if (frames_rendered_==265) key(SDLK_2,true);
            if (frames_rendered_==266) key(SDLK_2,false);
            if (frames_rendered_==280) key(SDLK_TAB,false);
            if (frames_rendered_==310) mouse(SDL_BUTTON_RIGHT,true);
            if (frames_rendered_==350 || frames_rendered_==375 || frames_rendered_==525) mouse(SDL_BUTTON_LEFT,true);
            if (frames_rendered_==372 || frames_rendered_==376 || frames_rendered_==526) mouse(SDL_BUTTON_LEFT,false);
            if (frames_rendered_==400) key(SDLK_r,true);
            if (frames_rendered_==410) {
                SDL_Event repeat{}; repeat.type=SDL_KEYDOWN;
                repeat.key.keysym.sym=SDLK_r; repeat.key.keysym.scancode=SDL_SCANCODE_R;
                repeat.key.repeat=1; SDL_PushEvent(&repeat);
            }
            if (frames_rendered_==411) key(SDLK_r,false);
            if (frames_rendered_==510) mouse(SDL_BUTTON_RIGHT,false);
            if (frames_rendered_==550) key(SDLK_ESCAPE,true);
            if (frames_rendered_==551) { key(SDLK_ESCAPE,false);key(SDLK_TAB,false); }

        }
        if (traffic_horn_check_) {
            const auto key=[&](SDL_Keycode code,bool down,bool repeat=false) {
                SDL_Event event{};event.type=down ? SDL_KEYDOWN : SDL_KEYUP;
                event.key.keysym.sym=code;event.key.keysym.scancode=SDL_GetScancodeFromKey(code);
                event.key.repeat=repeat ? 1 : 0;SDL_PushEvent(&event);
            };
            if (frames_rendered_==10) key(SDLK_h,true);
            if (frames_rendered_==11 || frames_rendered_==12) key(SDLK_h,true,true);
            if (frames_rendered_==13) key(SDLK_h,false);
            if (frames_rendered_==40 && (vehicle_audio_.horn_count()!=1 || police_emergency_enabled_)) {
                AP_ERROR("player H horn check: expected one horn and no siren toggle");
                traffic_horn_check_failed_=true;
            }
            if (frames_rendered_==45 || frames_rendered_==55) key(SDLK_j,true);
            if (frames_rendered_==46 || frames_rendered_==56) key(SDLK_j,false);
            if (frames_rendered_==50 && has_police_lightbar(car_visual_.active_car()) && !police_emergency_enabled_)
                traffic_horn_check_failed_=true;
            if (frames_rendered_==60) {
                if (police_emergency_enabled_) traffic_horn_check_failed_=true;
                if (!traffic_horn_check_failed_)
                    AP_INFO("player H horn check: PASS; one recorded horn, key repeat ignored, J siren separate");
            }
        }
        // Drive the real J path rather than the state directly: the point of
        // this check is that the key reaches the canvas in a Mistral.
        if (convertible_check_ && (frames_rendered_==60 || frames_rendered_==420)) {
            SDL_Event event{}; event.type=SDL_KEYDOWN;
            event.key.keysym.sym=SDLK_j; event.key.keysym.scancode=SDL_SCANCODE_J;
            SDL_PushEvent(&event);
            event.type=SDL_KEYUP; SDL_PushEvent(&event);
        }
        if (police_check_ && (frames_rendered_==60 || frames_rendered_==450)) {
            SDL_Event event{}; event.type=SDL_KEYDOWN;
            event.key.keysym.sym=SDLK_j; event.key.keysym.scancode=SDL_SCANCODE_J;
            SDL_PushEvent(&event);
            event.type=SDL_KEYUP; SDL_PushEvent(&event);
        }

        const WallClock::time_point now = WallClock::now();
        double dt = std::chrono::duration<double>(now - last).count();
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
            // Recorded HERE, at the top of the loop, and not after render().
            // `dt` is the span between this loop top and the last one, so it
            // is the cost of the PREVIOUS frame — and every counter below
            // still holds that frame's values, because this iteration has not
            // overwritten them yet. Sampling after render() instead would pair
            // a frame's duration with the next frame's counters and quietly
            // blame the wrong one.
            record_frame_sample(dt * 1000.0);
        }

        poll_events();
        const bool cinematic_before_ui=opening_cutscene_.active();
        const UiScreen screen_before_ui = ui_.screen();
        process_ui_input(static_cast<float>(std::clamp(dt, 0.0, 0.1)));
        if (opening_cutscene_.active()) {
            weapon_aim_mouse_=weapon_aim_pad_=weapon_aim_toggle_=false;
            step_weapon_use(false,0.f);
            step_molotov(false,0.f);
            camera_frame_dt_=static_cast<float>(std::clamp(dt,0.0,0.1));
            const float elapsed=frame_limit_>0 ? 1.f/30.f : camera_frame_dt_;
            if (opening_cutscene_.advance(elapsed)) { finish_opening();dt=0;last=WallClock::now();perf_clock_reset_=true; }
            else {
                opening_cutscene_.sync();
                camera_=opening_cutscene_.camera(window_.aspect());
                world_.update(scene_,renderer_,camera_.position);scene_.update();
                render();first_frame=false;clock_.reset();continue;
            }
        }
        if (cinematic_before_ui && !opening_cutscene_.active()) { dt=0;last=WallClock::now();perf_clock_reset_=true; }
        const bool began_or_resumed =
            screen_before_ui != UiScreen::Driving &&
            ui_.screen() == UiScreen::Driving;
        if (began_or_resumed) {
            last = WallClock::now();
            perf_clock_reset_ = true;
        }

        camera_frame_dt_ = static_cast<float>(std::clamp(dt, 0.0, 0.1));
        perf_mark_feedback_s_=std::max(0.f,perf_mark_feedback_s_-camera_frame_dt_);
        repair_shop_feedback_s_=std::max(0.f,repair_shop_feedback_s_-camera_frame_dt_);
        respray_feedback_s_=std::max(0.f,respray_feedback_s_-camera_frame_dt_);
        car_bomb_feedback_s_=std::max(0.f,car_bomb_feedback_s_-camera_frame_dt_);
        respray_camera_hold_s_=std::max(0.f,respray_camera_hold_s_-camera_frame_dt_);
        mission_success_feedback_s_=std::max(
            0.f,mission_success_feedback_s_-camera_frame_dt_);
        if (ui_.screen()==UiScreen::Driving)
            arrested_feedback_s_=std::max(0.f,arrested_feedback_s_-camera_frame_dt_);

        impact_feedback_seconds_ =
            std::max(0.0f, impact_feedback_seconds_ - camera_frame_dt_);
        if (ui_.screen() == UiScreen::Driving && on_foot_ && !vehicle_transition_.active() &&
            !bank_interaction_.modal() && !bank_input_consumed_ && !weapon_wheel_.open && !weapon_input_consumed_ &&
            !gun_store_holds_input()) {
            // A 144 Hz render frame can owe zero 120 Hz sim steps. Keep look
            // deltas until one real character step consumes them.
            character_look_dx_pending_ += input_.frame().look_dx;
            character_look_dy_pending_ += input_.frame().look_dy;
        }

        const bool weapon_available=weapon_focus_ && ui_.screen()==UiScreen::Driving &&
            !dev_menu_.open() && !bug_report_.open && !bank_interaction_.modal() &&
            !bank_input_consumed_ && !weapon_wheel_.open && !weapon_input_consumed_ &&
            !gun_store_holds_input() &&
            on_foot_ && !vehicle_transition_.active() && !boat_transition_.active();
        if (!weapon_available) {
            weapon_fire_pending_=weapon_reload_pending_=false;
            molotov_throw_pending_=false;
            weapon_aim_mouse_=weapon_aim_pad_=weapon_aim_toggle_=false;
            step_weapon_use(false,0.f);
            step_molotov(false,0.f);
        }
        FixedStep::Tick tick;
        if (ui_.screen() == UiScreen::Driving && !began_or_resumed &&
            !dev_menu_.open() && !bank_interaction_.modal() && !bank_input_consumed_ &&
            !weapon_wheel_.open && !weapon_input_consumed_ &&
            !paint_shop_.modal() && !paint_input_consumed_ &&
            !gun_store_holds_input() &&
            !(lighting_benchmark_ && frames_rendered_>=300)) {
            tick = clock_.advance((weapon_check_ || molotov_check_ || lighting_benchmark_ || driver_transition_check_ || house_check_ || signal_check_ || trailer_check_ || tire_track_check_ || plow_check_ || paint_check_ || car_bomb_check_) ? 1.0/60.0 : dt);
        } else {
            // Title, pause and map are real pauses. Never let wall time from a
            // modal screen turn into a burst of vehicle steps on return.
            clock_.reset();
        }
        if (weapon_check_ && was_pressed(input_.frame(),kBtnRespawn)) weapon_check_captures_|=512u;
        last_steps_ = tick.steps;
        last_clamped_ = tick.clamped;
        if (tick.clamped) {
            AP_WARN("frame owed more than %d sim steps; dropped the surplus",
                    kMaxStepsPerFrame);
        }

        const WallClock::time_point sim_t0 = WallClock::now();
        sim_traffic_ms_ = sim_police_ms_ = sim_character_ms_ = 0.0;
        police_vis_ms_ = police_ctx_ms_ = 0.0;
        police_vis_calls_ = 0;
        // Accumulate rather than assign: a frame can owe a dozen steps, and
        // the question is what the FRAME spent, not what its last step did.
        const auto add_ms = [](double& into, WallClock::time_point from) {
            into += std::chrono::duration<double>(WallClock::now() - from)
                        .count() * 1000.0;
        };
        for (int i = 0; i < tick.steps; ++i) {
            const InputFrame live_input = plow_check_
                ? plow_check_input()
                : tire_track_check_
                ? tire_track_check_input()
                : (signal_check_ ? signal_check_input()
                    : (traffic_horn_check_ ? traffic_horn_check_input()
                        : (police_officer_check_ ? police_officer_check_input()
                            : (paint_check_ || car_bomb_check_ ? paint_check_input() : input_.frame()))));
            // A DEAD PLAYER DRIVES NOTHING. Gating here rather than at each
            // consumer is deliberate: the car, the character, the weapon and
            // the door interactions all read from this one frame, and a gate
            // added to three of the four is the version where the corpse can
            // still shoot. The world keeps running underneath — traffic,
            // police and the crowd are not paused by the player dying.
            const InputFrame raw_input =
                player_vitals_.alive() ? live_input : InputFrame{};
            // Snapshot before EACH step, not before the batch: prev_car_ has to
            // be exactly one step behind or the render interpolation covers the
            // wrong span on a multi-step frame.
            prev_car_ = car_;
            prev_trailer_=trailer_;
            prev_aircraft_ = aircraft_;
            prev_helicopter_ = helicopter_;
            prev_boat_ = boat_;
            prev_player_character_ = player_character_;

            const bool swing_occupied =
                (on_foot_ && bank_vault_swing_occupied(
                    city::bank_local_position(player_character_.position), 0.45f)) ||
                bank_vault_swing_occupied(city::bank_local_position(car_.position), 3.0f);
            step_bank_vault(bank_vault_, static_cast<float>(kSimDt), swing_occupied);
            world_.sync_bank_vault(scene_, collider_, bank_vault_.openness);

            if (i == 0 && was_pressed(input_.frame(), kBtnTrailer)) toggle_trailer();
            if (i == 0 && was_pressed(input_.frame(), kBtnRespawn) && !on_foot_) drop_trailer();
            // Not on the step a respray order enters, nor in the first moment
            // of the spray: a second tap of the button that confirmed it must
            // not throw the player out of the car. Past that, getting out
            // cancels the spray.
            if (i == 0 && was_pressed(input_.frame(), kBtnAccept) &&
                !respray_order_pending_ && !respray_blocks_exit(respray_visit_)) {
                toggle_player_mode();
            }
            if (i == 0 && was_pressed(input_.frame(), kBtnDrink))
                drink_at_bent_elbow();
            soft_top_=step_mistral_soft_top(soft_top_,
                i == 0 && soft_top_toggle_pending_, static_cast<float>(kSimDt));
            if (i == 0) soft_top_toggle_pending_=false;
            if (!has_passenger_door(car_visual_.active_car()) || glm::length(car_.velocity)>.5f)
                passenger_door_target_=false;
            const float passenger_step=static_cast<float>(kSimDt)/.85f;
            passenger_door_open_=std::clamp(passenger_door_open_+
                (passenger_door_target_?passenger_step:-passenger_step),0.f,1.f);
            if (i == 0 && on_foot_ && !vehicle_transition_.active() && !boat_transition_.active() &&
                was_pressed(input_.frame(), kBtnRespawn)) {
                place_character_next_to_car();
            }
            if (i == 0 && in_aircraft_ && was_pressed(input_.frame(), kBtnRespawn)) reset_aircraft();
            if (i == 0 && in_helicopter_ && was_pressed(input_.frame(), kBtnRespawn)) reset_helicopter();
            if (i == 0 && in_boat_ && !boat_transition_.active() && was_pressed(input_.frame(), kBtnRespawn)) reset_boat();
            const bool boat_was_transitioning=boat_transition_.active();
            if (boat_was_transitioning) step_boat_transition();
            if (in_boat_ && !boat_was_transitioning) {
                world_.enable_boat_collision(collider_,false);
                boat_=step_boat(boat_,input_.frame(),collider_,static_cast<float>(kSimDt));
                world_.sync_boat(scene_,collider_,boat_);
                world_.enable_boat_collision(collider_,true);
            }
            if (in_aircraft_) {
                world_.enable_aircraft_collision(collider_,false);
                aircraft_=step_aircraft(aircraft_,input_.frame(),collider_,static_cast<float>(kSimDt));
                world_.sync_aircraft(scene_,collider_,aircraft_);
                world_.enable_aircraft_collision(collider_,true);
            }
            // The rotor keeps turning whether or not anyone is aboard, so this
            // steps while parked too -- but only the flown one excludes its own
            // collision, because a parked machine has to stay solid to the
            // world it is parked in.
            if (in_helicopter_) {
                world_.enable_helicopter_collision(collider_,false);
                helicopter_=step_helicopter(helicopter_,input_.frame(),collider_,
                                            static_cast<float>(kSimDt));
                world_.sync_helicopter(scene_,collider_,helicopter_);
                world_.enable_helicopter_collision(collider_,true);
            } else if (!helicopter_.grounded && helicopter_.crashed) {
                // Wrecked but still in the air with nobody aboard -- it has to
                // keep falling, or stepping it only while occupied would leave
                // one hanging over the city the moment the player bails.
                world_.enable_helicopter_collision(collider_,false);
                helicopter_=step_helicopter(helicopter_,{},collider_,
                                            static_cast<float>(kSimDt));
                world_.sync_helicopter(scene_,collider_,helicopter_);
                world_.enable_helicopter_collision(collider_,true);
            } else if (helicopter_.rotor > 0) {
                helicopter_.rotor=std::max(0.0f,
                    helicopter_.rotor-kHeliSpoolRate*static_cast<float>(kSimDt));
                helicopter_.rotor_angle=std::remainder(helicopter_.rotor_angle+
                    helicopter_.rotor*kHeliRotorSpeed*static_cast<float>(kSimDt),
                    glm::two_pi<float>());
                world_.sync_helicopter(scene_,collider_,helicopter_);
            }
            // One bang per bump of the counter, wherever the machine was when
            // it happened. Reading the edge rather than the flag is what gets
            // the second blast when the falling wreck finally arrives.
            if (helicopter_.impacts != prev_helicopter_.impacts) {
                wreck_blast_.emit(
                    helicopter_point(helicopter_,{0,2.f,0}), helicopter_.impacts);
            }
            // The smoke outlives the crash, and it outlives R as well, so this
            // runs every step rather than only while something is burning.
            wreck_blast_.step(static_cast<float>(kSimDt));

            // Conditions are a pure function of (seed, ABSOLUTE step), never an
            // accumulator, so a tape replayed from any point in the session
            // gets its own weather back. See game/conditions.h.
            update_weather(true);
            collider_.set_kinematic_enabled(current_vehicle_collider_,false);
            if (trailer_.attached) enable_trailer_collision(false);
            const auto snow_contact = collider_.probe_down(
                car_.position + glm::vec3{0.0f, 0.5f, 0.0f}, 4.0f,
                TerrainCollider::ProbeVehicles::Exclude);
            const Conditions road_conditions = conditions_with_local_snow(conditions_,
                snow_contact.hit ? snow_contact.snow_depth_m : conditions_.snow_depth_m);
            auto step_tuning = conditioned_tuning(tuning_, road_conditions);
            if (vehicle_god_mode_) {
                // Both knobs, not one. impact_damage_per_mps is what removes
                // health; body_damage_gain is what deforms the shell and what
                // the mechanical systems read to decide a leak has started.
                // Zeroing only the first leaves an undentable car that still
                // bleeds oil, which reads as the toggle being broken.
                step_tuning.impact_damage_per_mps = 0.0f;
                step_tuning.body_damage_gain = 0.0f;
            }
            // Loaded rig accelerates/brakes more slowly without changing the
            // tractor suspension mass or letting it sag through its wheels.
            if (trailer_.attached) {
                step_tuning.engine_peak_torque*=.62f;
                step_tuning.brake_torque*=.80f;
            }
            const bool transitioning=vehicle_transition_.active();
            InputFrame vehicle_input = apply_drunk_input(raw_input, drunk_);
            // A spray holds the car still, from the step its order enters, so
            // a key still held from the picker cannot roll it out of the bay.
            if (respray_visit_.spraying() || (i == 0 && respray_order_pending_))
                vehicle_input = hold_for_respray(vehicle_input);
            car_ = (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_ || transitioning)
                ? step_unoccupied_vehicle(car_, step_tuning, collider_,
                                          static_cast<float>(kSimDt))
                : step_vehicle(car_, step_tuning, vehicle_input, collider_,
                               static_cast<float>(kSimDt));
            if (conditions_.tornado_intensity > 0.0f) {
                const TornadoForce tornado = apricot::tornado_force_at(
                    {car_.position.x, car_.position.z},
                    tornado_params(conditions_));
                car_.velocity += tornado.force_n /
                    std::max(tuning_.mass_kg, 1.0f) *
                    static_cast<float>(kSimDt);
            }
            traffic_visual_.step_signals(scene_, collider_, car_, static_cast<float>(kSimDt));
            if (car_.breakaway_id != UINT32_MAX)
                vehicle_audio_.play_car_collision(glm::length(car_.breakaway_velocity), car_.position);
            if(step_repair_shop(repair_shop_visit_,car_,tuning_,static_cast<float>(kSimDt),
                !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && !transitioning)) {
                prev_car_=car_;repair_shop_feedback_s_=3.f;
                AP_INFO("Rook's Auto Repair: body and mechanical condition restored");
            }
            step_trailer();
            sync_current_vehicle_obstacle();
            for (auto& parked:parked_vehicles_)
                step_vehicle_mechanical(parked.state.mechanical,parked.state.body_damage,
                    static_cast<float>(kSimDt),parked.state.mechanical_key);
            if (transitioning) step_vehicle_transition();
            const WallClock::time_point character_t0 = WallClock::now();
            if (on_foot_ && !transitioning && !boat_was_transitioning) {
                InputFrame character_input = apply_drunk_input(raw_input, drunk_);
                character_input.look_dx = i == 0
                    ? character_look_dx_pending_ : 0.0f;
                character_input.look_dy = i == 0
                    ? character_look_dy_pending_ : 0.0f;
                if (i != 0) character_input.pressed = 0u;
                if (house_check_) character_input = house_check_input();
                if (weapon_use_.aim_blend>.01f) {
                    character_input.held &= ~kBtnShiftUp;
                    character_input.steer*=.55f;
                    character_input.throttle*=.55f;
                    character_input.brake*=.55f;
                }
                world_.step_house_doors(scene_,collider_,&player_character_,character_tuning_,
                    character_input,static_cast<float>(kSimDt));
                const bool was_grounded = player_character_.grounded;
                player_character_ = step_character(
                    player_character_, character_tuning_, character_input,
                    collider_, static_cast<float>(kSimDt));
                check_player_fall_damage(was_grounded);
                if (conditions_.tornado_intensity > 0.0f) {
                    const TornadoForce tornado = apricot::tornado_force_at(
                        {player_character_.position.x,
                         player_character_.position.z},
                        tornado_params(conditions_));
                    player_character_.velocity += tornado.force_n /
                        std::max(tuning_.mass_kg, 1.0f) *
                        static_cast<float>(kSimDt);
                }
                if (house_check_) update_house_check();
                // After the tornado push, so the speed the footfall gain is
                // taken from is the speed the character actually moved at.
                // Nothing resets this when the player gets into a car: the
                // character that comes back out is spawned with a fresh
                // distance counter, which FootstepAudio reads as a re-arm.
                update_footstep_audio();
                if (i == 0) {
                    character_look_dx_pending_ = 0.0f;
                    character_look_dy_pending_ = 0.0f;
                }
            }
            if (!on_foot_ || transitioning || boat_was_transitioning)
                world_.step_house_doors(scene_,collider_,nullptr,character_tuning_,
                    InputFrame{},static_cast<float>(kSimDt));
            step_drunk(drunk_, static_cast<float>(kSimDt));
            OnFootTrafficHazard foot_hazard;
            const OnFootTrafficHazard* foot_hazard_ptr = nullptr;
            if (on_foot_) {
                foot_hazard.height_m = player_character_.position.y;
                foot_hazard.position = {
                    player_character_.position.x, player_character_.position.z};
                foot_hazard.velocity = {
                    player_character_.velocity.x, player_character_.velocity.z};
                foot_hazard_ptr = &foot_hazard;
            }
            const bool armed_available=weapon_available && on_foot_ &&
                !vehicle_transition_.active() && !boat_transition_.active();
            step_weapon_use(armed_available,static_cast<float>(kSimDt));
            step_molotov(armed_available,static_cast<float>(kSimDt));
            // The fire is world state, not weapon state: it goes on burning
            // while the player drives away, sits in a menu-free cutscene or
            // dies, so it is stepped unconditionally beside the rest of the
            // simulation rather than gated on holding a bottle.
            step_fire(static_cast<float>(kSimDt));
            add_ms(sim_character_ms_, character_t0);
            if (on_foot_ && (weapon_use_.aim_blend>.01f || weapon_use_.recoil>0.f)) {
                player_character_.facing_yaw=player_character_.view_yaw;
            }
            const WallClock::time_point police_t0 = WallClock::now();
            const bool player_armed = player_has_drawn_weapon();
            const glm::vec3 police_target = player_focus_position();
            const int wanted_level_before_offenses = wanted_.level();
            police_stop_feedback_s_ = std::max(
                0.0f, police_stop_feedback_s_ - static_cast<float>(kSimDt));
            check_police_driving_offenses();
            check_police_armed_offense(player_armed);
            const auto police_visible = visible_police(police_target);
            const bool driving_suspect = !on_foot_ && !in_aircraft_ &&
                !in_helicopter_ && !in_boat_ && !vehicle_transition_.active() &&
                !boat_transition_.active();
            bool officer_on_foot_near = false;
            for (const VehicleAgent& unit : world_.traffic().vehicles()) {
                if (!unit.police_pursuit ||
                    unit.officer.phase != PoliceOfficerPhase::Pursuing) continue;
                if (glm::distance(unit.officer.pos, police_target) <= 7.0f) {
                    officer_on_foot_near = true;
                    break;
                }
            }
            const PoliceEscalationOutput escalation = police_escalation_.step({
                wanted_.level(), driving_suspect, player_armed,
                world_.traffic().police_pursuit_count() > 0,
                officer_on_foot_near, glm::length(car_.velocity),
                static_cast<float>(kSimDt)});
            if (escalation.minimum_wanted_level > wanted_.level()) {
                wanted_.set_level(escalation.minimum_wanted_level);
                AP_INFO("police escalation: %s; wanted %d",
                    escalation.fled_stop ? "failed to stop" : "armed or multiple-kill threat",
                    wanted_.level());
            }
            if (escalation.stop_resolved) {
                wanted_.reset();
                police_escalation_.reset();
                police_stop_prompt_ = "TRAFFIC STOP COMPLETE";
                police_stop_feedback_s_ = 3.0f;
                AP_INFO("police traffic stop resolved without a chase");
            } else if (escalation.pull_over_prompt) {
                police_stop_prompt_ = "PULL OVER - STOP FOR OFFICER";
            } else if (police_stop_feedback_s_ > 0.0f) {
                police_stop_prompt_ = "TRAFFIC STOP COMPLETE";
            } else {
                police_stop_prompt_ = "";
            }
            const WallClock::time_point ctx_t0 = WallClock::now();
            world_.set_police_context(escalation.stop_resolved ? 0 : wanted_.level(), police_target,
                                      police_visible);
            const glm::vec3 target_velocity = on_foot_
                ? player_character_.velocity : car_.velocity;
            world_.set_police_officer_context(on_foot_, player_armed,
                {target_velocity.x, target_velocity.z}, &collider_);
            // A free-driving pursuit is stepped by the same physics as the
            // player's car, so it needs the CRUISER's handling and the same
            // weather grip — a cop on ice must be on the ice everyone else is.
            world_.set_police_vehicle_tuning(conditioned_tuning(
                player_model_tuning(driving_mechanics_style_,
                                    PlayerCarId::MunicipalCruiser91C),
                road_conditions));
            add_ms(police_ctx_ms_, ctx_t0);
            add_ms(sim_police_ms_, police_t0);

            const WallClock::time_point traffic_t0 = WallClock::now();
            world_.step_traffic(static_cast<int64_t>(step_index_), car_,
                                foot_hazard_ptr);
            add_ms(sim_traffic_ms_, traffic_t0);

            const WallClock::time_point police_t1 = WallClock::now();
            check_police_shots();
            check_pedestrian_casualties();
            check_on_foot_traffic_hits();
            add_ms(sim_police_ms_, police_t1);
            snowplow_service_.step(world_.traffic().vehicles(), snow_clearance_,
                                    conditions_.snow_depth_m);
            step_lot_plows();
            step_vehicle_snow();
            traffic_horn_audio_.update(step_index_,world_.traffic().vehicles(),
                world_.lanes(),world_.traffic_tuning(),camera_.position);
            const WallClock::time_point traffic_t1 = WallClock::now();
            world_.resolve_traffic_collision(
                car_, tuning_.car_collision_half_width,
                tuning_.car_collision_half_length, tuning_.mass_kg,
                vehicle_god_mode_ ? 0.0f : tuning_.body_damage_gain,
                tuning_.car_collision_front_extension);
            add_ms(sim_traffic_ms_, traffic_t1);
            const WallClock::time_point police_t2 = WallClock::now();
            check_police_collision_offenses();
            add_ms(sim_police_ms_, police_t2);
            // Traffic may displace the tractor after its vehicle step. Keep
            // the final published hitch pose exact, including while on foot.
            if (trailer_.attached && glm::distance(tractor_hitch(car_,tuning_),
                    trailer_point(trailer_,kTrailerKingpin))>.001f) {
                enable_trailer_collision(false);
                collider_.set_kinematic_enabled(current_vehicle_collider_,false);
                trailer_=prev_trailer_;
                step_tractor_trailer(trailer_,car_,prev_car_,tuning_,collider_);
                sync_trailer_collision();sync_current_vehicle_obstacle();
            }
            if (car_.car_contact_speed > 0.0f) {
                vehicle_audio_.play_car_collision(car_.car_contact_speed,
                                                  car_.position);
            }
            vehicle_effects_.step(scene_, collider_, step_index_, car_, tuning_,
                                  world_.traffic());
            tire_tracks_.step(car_, vehicle_input.handbrake,
                              road_conditions.snow_cover, step_index_);
            step_player_plow(raw_input, i == 0);
            const WallClock::time_point police_t3 = WallClock::now();
            const auto current_police_visible=visible_police(player_focus_position());
            check_police_arrest(current_police_visible);
            wanted_.update(
                static_cast<float>(kSimDt),
                !current_police_visible.empty(),
                world_.traffic().police_tuning());
            add_ms(sim_police_ms_, police_t3);
            // After the wanted update, from the same visible list, so the
            // respray's pull-in latch needs no second sight query.
            police_eyes_on_=!current_police_visible.empty();
            step_respray_visit(i);
            step_car_bomb_rules(i);
            // The stars flash from the crime until the dispatch radio goes
            // out (PENG-46); the crowd owns that step, so the latch clears
            // on exactly the frame the callout would play.
            wanted_report_blink_ = wanted_report_blink_step(
                wanted_report_blink_,
                wanted_level_before_offenses == 0 && wanted_.level() > 0,
                /*dispatch_armed=*/true,
                world_.traffic().police_dispatch_radio_fires(
                    static_cast<int64_t>(step_index_)),
                wanted_.level());
            if (car_.impact_count != seen_impact_count_) {
                seen_impact_count_ = car_.impact_count;
                impact_feedback_seconds_ = 0.55f;
                chase_camera_.add_impact(car_.last_impact_speed);
                check_player_crash_damage(car_.last_impact_speed);
                AP_INFO("vehicle impact: %.1f m/s, %.1f damage, %.0f health",
                        static_cast<double>(car_.last_impact_speed),
                        static_cast<double>(car_.last_impact_damage),
                        static_cast<double>(car_.health));
            }
            step_player_vitals(static_cast<float>(kSimDt));
            ++step_index_;
            if (vehicle_entry_check_ && !vehicle_entry_check_passed_) run_vehicle_entry_check();
            if (driver_transition_check_ && driver_check_stage_<6) run_driver_transition_check();
            if (aircraft_check_ && !aircraft_check_ran_) run_aircraft_check();
            if (helicopter_check_ && !helicopter_check_ran_) run_helicopter_check();
            if (boat_check_ && !boat_check_ran_) run_boat_check();
            if (trailer_check_ && !trailer_check_ran_) run_trailer_check();
        }
        step_respray_reveal();
        sim_ms_ = std::chrono::duration<double>(WallClock::now() - sim_t0)
                      .count() * 1000.0;

        // Consume latched edges ONLY when a step actually ran. On a zero-step
        // frame the edges stay latched for the next one. Moving this out of
        // the guard silently drops presses at high frame rates.
        if (tick.steps > 0) {
            if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && !vehicle_transition_.active() && was_pressed(input_.frame(), kBtnCamCycle)) {
                chase_camera_.cycle();
                dev_menu_.set_camera_mode(chase_camera_.mode());
                camera_obstruction_distance_ = -1.0f;
                AP_INFO("camera mode: %s", chase_camera_.mode_name());
            }
            input_.consume_edges();
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
        if (ui_.screen() == UiScreen::Driving && warp_interval_ > 0 &&
            frames_rendered_ > 0 &&
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
            perf_clock_reset_ = true;
        } else {
            const WallClock::time_point mesh_t0 = WallClock::now();
            world_.update(scene_, renderer_, player_focus_position());
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

        VehicleAudioFrame audio_frame;
        audio_frame.active =
            ui_.screen() == UiScreen::Driving && !dev_menu_.open() &&
            !bank_interaction_.modal() && !weapon_wheel_.open;
        audio_frame.dt_seconds = camera_frame_dt_;
        audio_frame.horn_available=weapon_focus_ && !bug_report_.open &&
            !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            !vehicle_transition_.active() && !boat_transition_.active();
        audio_frame.horn_pressed=player_horn_pending_;
        traffic_horn_audio_.set_active(audio_frame.active);
        audio_frame.engine_running = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            !vehicle_engine_failed(car_.mechanical);
        audio_frame.engine_rpm = car_.engine_rpm;
        const float drive_pedal = car_.gear == kGearReverse
                                      ? input_.frame().brake
                                      : input_.frame().throttle;
        audio_frame.accelerating = !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            !vehicle_transition_.active() && !vehicle_engine_failed(car_.mechanical) && drive_pedal > 0.20f;
        audio_frame.handbrake = on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_
                                    ? 0.0f
                                    : input_.frame().handbrake;
        audio_frame.drift_slip = std::max(
            car_.wheels[kWheelRearLeft].slip,
            car_.wheels[kWheelRearRight].slip);
        audio_frame.speed_mps = glm::length(
            glm::vec3{car_.velocity.x, 0.0f, car_.velocity.z});
        if (audio_frame.accelerating && audio_frame.speed_mps < 7.0f) {
            for (const auto& wheel : car_.wheels) {
                if (!wheel.grounded) continue;
                const float excess_speed = std::fabs(wheel.angular_velocity) *
                    tuning_.wheel_radius - audio_frame.speed_mps;
                audio_frame.burnout_amount = std::max(audio_frame.burnout_amount,
                    std::clamp((excess_speed - 2.0f) / 6.0f, 0.0f, 1.0f));
            }
        }
        audio_frame.gear = car_.gear;
        audio_frame.position = car_.position;
        if (in_boat_) {
            audio_frame.engine_running=true;
            audio_frame.engine_rpm=850.f+std::fabs(boat_.throttle)*1800.f+std::fabs(boat_.speed)*100.f;
            audio_frame.accelerating=std::fabs(boat_.throttle)>.2f;
            audio_frame.handbrake=0;audio_frame.drift_slip=0;audio_frame.gear=1;
            audio_frame.speed_mps=glm::length(boat_.velocity);audio_frame.position=boat_.position;
        }
        vehicle_audio_.update(audio_frame);
        if(vehicle_leak_warning_.update(audio_device_.mixer(),audio_frame.active,
            !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ && !vehicle_transition_.active(),
            car_.mechanical_key,vehicle_leak_mask(car_.body_damage),
            static_cast<double>(tick.steps)*kSimDt))
            AP_INFO("vehicle fluid leak warning chime");
        if (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_ || !has_police_lightbar(car_visual_.active_car()))
            police_emergency_enabled_=false;
        const bool player_police_siren = police_emergency_enabled_ && !on_foot_ &&
            !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
            has_police_lightbar(car_visual_.active_car());
        const VehicleAgent* ai_pursuer =
            world_.traffic().nearest_police_pursuer();
        const glm::vec3 siren_position = player_police_siren || !ai_pursuer
            ? car_.position : ai_pursuer->pos;
        police_siren_.update(audio_frame.active,
                             player_police_siren || ai_pursuer != nullptr,
                             siren_position);
        traffic_idle_audio_.begin_frame(camera_.position);
        for (const auto& vehicle : world_.traffic().vehicles()) {
            if (vehicle_engine_failed(vehicle.mechanical)) continue;
            const char* model = "car5";
            switch (traffic_vehicle_kind(vehicle)) {
                case TrafficVehicleKind::Bwc360: model = "bwc_360"; break;
                case TrafficVehicleKind::Sedan: break;
                case TrafficVehicleKind::BoxTruck: model = "car8"; break;
                case TrafficVehicleKind::Ambulance: model = "ambulance"; break;
                case TrafficVehicleKind::Firetruck: model = "firetruck"; break;
                case TrafficVehicleKind::HalcyonSix: model = "halcyon_six"; break;
                case TrafficVehicleKind::MontroseRegentEight: model = "montrose_regent_eight"; break;
                case TrafficVehicleKind::VesperVx91: model = "vesper_vx91"; break;
                // RECONSTRUCTED, not authored: this case was lost to an
                // overwrite and "car8" is a stand-in, picked because a plow is
                // a truck and BoxTruck already uses it. Whoever owns the
                // snowplow should set the profile they actually want.
                case TrafficVehicleKind::Snowplow: model = "car8"; break;
                case TrafficVehicleKind::Police: model = "municipal_cruiser_91c"; break;
            }
            traffic_idle_audio_.submit(vehicle.lane_key, vehicle.slot, vehicle.pos,
                                       vehicle.speed_mps, model);
        }
        traffic_idle_audio_.end_frame(audio_frame.active);

        // Five transforms move: the wheel-less body and four separately driven
        // wheels. Do this after a possible teleport so the model cannot spend a
        // frame at the old end of the island while the camera is at the new one.
        const SkyEnv visual_env = current_sky_env();
        const float visible_headlight_level = automatic_headlight_level(
            visual_env.sun_dir.y, conditions_.atmosphere);
        const float brake_level = (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_ || vehicle_transition_.active())
            ? 0.0f
            : (car_.gear == kGearReverse
                   ? input_.frame().throttle
                   : input_.frame().brake);
        const glm::vec3 presentation_focus = player_focus_position();
        const bool inside_interior = world_.inside_authored_interior(
            presentation_focus,
            interior_presentation_lod_ ? city::kInteriorExitMarginM : 0.0f);
        if (inside_interior != interior_presentation_lod_) {
            interior_presentation_lod_ = inside_interior;
            AP_INFO("interior presentation: %s (traffic %.0f m, NPC %.0f m)",
                    interior_presentation_lod_ ? "nearby-only" : "full outdoor",
                    static_cast<double>(interior_presentation_lod_
                        ? city::kInteriorTrafficPresentationRadiusM : 0.0f),
                    static_cast<double>(interior_presentation_lod_
                        ? city::kInteriorNpcPresentationRadiusM : 0.0f));
        }
        const float traffic_presentation_radius = interior_presentation_lod_
            ? city::kInteriorTrafficPresentationRadiusM : 0.0f;
        const float npc_presentation_radius = interior_presentation_lod_
            ? city::kInteriorNpcPresentationRadiusM : 0.0f;
        trailer_visual_.sync(scene_,prev_trailer_,trailer_,static_cast<float>(clock_.alpha()),visible_headlight_level,brake_level);
        // With weather effects off the world draws no snow; vehicles follow.
        const bool vehicle_snow = ui_.settings().weather_effects;
        car_visual_.set_plow_raised(plow_blade_.raised);
        car_visual_.sync(scene_, tuning_, prev_car_, car_,
                         static_cast<float>(clock_.alpha()),
                         visible_headlight_level, brake_level,
                         vehicle_snow ? player_snow_load_ : -1.0f);
        traffic_visual_.set_snow(vehicle_snow ? &traffic_snow_loads_ : nullptr,
                                 &snow_shelter_, vehicle_snow_weather());
        car_visual_.sync_emergency(scene_,step_index_,police_emergency_enabled_ &&
            !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_);
        // A working plow truck runs its service bar whenever someone is in
        // the cab; it is off once the driver walks away from it.
        car_visual_.sync_plow_lights(scene_,step_index_,
            !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_,
            visible_headlight_level);
        sync_lot_plows(static_cast<float>(clock_.alpha()), visible_headlight_level);
        car_visual_.sync_soft_top(scene_,soft_top_.stowed);
        car_visual_.sync_driver_door(scene_,vehicle_transition_.active()
            ? vehicle_transition_door_open(sample_vehicle_transition(vehicle_transition_,
                transition_waiting_ ? 1.f : static_cast<float>(clock_.alpha()))) : 0.f);
        car_visual_.sync_passenger_door(scene_,vehicle_transition_ease(passenger_door_open_));
        const WallClock::time_point visual_t0 = WallClock::now();
        // Each weapon reports its OWN draw and aim clocks. Feeding the
        // pistol's numbers while a molotov is equipped leaves the arm down,
        // and the bottle is parented to the hand that never came up.
        const bool holding_molotov=weapon_wheel_.equipped==WeaponId::Molotov;
        character_visual_.set_player_weapon_pose(weapon_wheel_.equipped,
            holding_molotov ? molotov_use_.equip_blend : weapon_use_.equip_blend,
            holding_molotov ? molotov_use_.aim_blend : weapon_use_.aim_blend,
            holding_molotov ? 0.f : weapon_use_.recoil,
            holding_molotov ? 0.f : weapon_use_.reload_progress(),
            holding_molotov ? false : weapon_use_.reloading,
            player_character_.view_pitch);
        character_visual_.sync(
            world_.traffic(), prev_player_character_,
            player_character_, static_cast<float>(clock_.alpha()),
            static_cast<int64_t>(step_index_), on_foot_, presentation_focus,
            npc_presentation_radius, &collider_, !player_vitals_.alive());
        if (boat_transition_.active())
            character_visual_.sync_boat_transition(world_.rendered_boat_transform(scene_),
                boat_transition_,static_cast<float>(clock_.alpha()));
        else if (in_boat_)
            character_visual_.sync_boat_driver(world_.rendered_boat_transform(scene_),true,
                input_.frame().steer,static_cast<float>(step_index_)*static_cast<float>(kSimDt));
        else if (vehicle_transition_.active())
            character_visual_.sync_transition(car_visual_.active_car(), car_visual_.rendered_body_transform(scene_),
                vehicle_transition_,transition_waiting_ ? 1.f : static_cast<float>(clock_.alpha()));
        else character_visual_.sync_driver(car_visual_.active_car(),
            !on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_,car_visual_.rendered_body_transform(scene_));
        // The fist LANDS. The animator opens a one-shot contact window in the
        // middle of the jab (city/character_punch.h owns that clock), and this
        // is the one place it is consumed.
        if (character_visual_.consume_player_punch_contact()) throw_player_punch();
        glm::mat4 weapon_hand{1};
        const bool has_weapon_hand=on_foot_ && !vehicle_transition_.active() && !boat_transition_.active() &&
            character_visual_.player_right_hand_transform(weapon_hand);
        weapon_visual_.sync(scene_,weapon_wheel_.equipped,has_weapon_hand ? &weapon_hand:nullptr,weapon_use_);
        molotov_visual_.sync_held(scene_,weapon_wheel_.equipped,
            has_weapon_hand ? &weapon_hand:nullptr,molotov_use_,camera_.position);
        molotov_visual_.sync_projectiles(scene_,molotov_shots_,camera_.position);
        fire_visual_.sync(scene_,fire_,camera_.position);
        wreck_visual_.sync(scene_,wreck_blast_,camera_.position);
        weapon_socket_player_position_=glm::mix(prev_player_character_.position,
            player_character_.position,static_cast<float>(clock_.alpha()));
        weapon_socket_player_yaw_=interpolate_camera_yaw(prev_player_character_.facing_yaw,
            player_character_.facing_yaw,static_cast<float>(clock_.alpha()));
        traffic_visual_.sync(scene_, world_.traffic(), world_.lanes(),
                             static_cast<int64_t>(step_index_),
                             visible_headlight_level, presentation_focus,
                             traffic_presentation_radius,static_cast<float>(clock_.alpha()));
        character_visual_.sync_police(world_.traffic(),traffic_visual_,
            static_cast<float>(clock_.alpha()),static_cast<int64_t>(step_index_),
            presentation_focus,traffic_presentation_radius);
        police_weapon_visual_.sync(
            scene_, character_visual_.police_weapon_sockets());
        const WallClock::time_point scene_t0 = WallClock::now();
        visual_ms_ = std::chrono::duration<double>(scene_t0 - visual_t0)
                         .count() * 1000.0;
        scene_.update();
        scene_ms_ = std::chrono::duration<double>(WallClock::now() - scene_t0)
                        .count() * 1000.0;

        PrecipitationType precipitation_type = PrecipitationType::Rain;
        float precipitation_intensity = controls_.rain;
        if (conditions_.snow > 0.0f) {
            precipitation_type =
                conditions_.atmosphere == AtmosphericWeather::Blizzard
                    ? PrecipitationType::Blizzard
                    : PrecipitationType::Snow;
            precipitation_intensity = conditions_.snow;
        }
        rain_.update(
            camera_, precipitation_type,
            ui_.settings().weather_effects ? precipitation_intensity : 0.0f,
            static_cast<float>(kSimDt) * static_cast<float>(tick.steps));

        render();

        if (first_frame) {
            first_frame = false;
            clock_.reset();
            last = WallClock::now();
            perf_clock_reset_ = true;
        }
    }

    if (weapon_check_) AP_INFO("weapon check: captures=%u expected=255 shots=%u ammo=%d/%d",
        weapon_check_captures_,weapon_shots_,weapon_use_.magazine,weapon_use_.reserve);
    AP_INFO("quit after %llu sim steps (%.2f s of sim time), %d frames",
            static_cast<unsigned long long>(step_index_),
            static_cast<double>(step_index_) * kSimDt, frames_rendered_);
    AP_INFO("last frame: %d visible nodes, %d batches (%d instanced), "
            "%d draw calls, %d instances, longest run %d, %u binds skipped",
            render_stats_.visible_nodes, render_stats_.batches,
            render_stats_.instanced_batches, render_stats_.draw_calls,
            render_stats_.instances, render_stats_.largest_run,
            render_stats_.skipped_binds);
    AP_INFO("last frame: hud %d quads in %d draw(s), precip %d particles / %d quads",
            hud_.last_quad_count(), hud_.last_draw_calls(), rain_.live_drops(),
            rain_.drawn_quads());
    AP_INFO("tire tracks: %zu live bounded decals (%zu rubber, %zu dirt, %zu "
            "snow), %d drawn last frame",
            tire_tracks_.live_count(),
            tire_tracks_.live_count(TireTrackSurface::Rubber),
            tire_tracks_.live_count(TireTrackSurface::Dirt),
            tire_tracks_.live_count(TireTrackSurface::Snow),
            tire_tracks_.drawn_quads());
    AP_INFO("vehicle effects: %zu live fluid marks",
            vehicle_effects_.mark_count());
    AP_INFO("weather: %s, snowfall %.2f, snowpack %.3f m (collision %.3f m)",
            atmospheric_weather_name(conditions_.atmosphere),
            static_cast<double>(conditions_.snow),
            static_cast<double>(conditions_.snow_depth_m),
            static_cast<double>(collider_.snow_collision_depth()));
    AP_INFO("traffic: %zu simulated cars, %zu simulated pedestrians, "
            "%zu signal heads; %zu police (%zu pursuing)",
            world_.traffic().vehicles().size(), world_.traffic().peds().size(),
            traffic_visual_.signal_head_count(),
            world_.traffic().police_unit_count(),
            world_.traffic().police_pursuit_count());
    AP_INFO("BWC 360 traffic: %zu moving rigs, %zu parked rigs",
            traffic_visual_.model_count(TrafficVehicleKind::Bwc360),
            traffic_visual_.model_count(TrafficVehicleKind::Bwc360,true));
    AP_INFO("presentation: %zu traffic rigs, %zu parked rigs, %zu ambient NPC rigs, %zu staff "
            "rigs; %d character draws; interior %s",
            traffic_visual_.car_count(), traffic_visual_.parked_car_count(),
            character_visual_.ambient_npc_count(),
            character_visual_.staff_count(), character_visual_.last_draw_count(),
            interior_presentation_lod_ ? "nearby-only" : "full outdoor");
    {
        const auto& windows=world_.skyscraper_window_stats();
        AP_INFO("city windows: %zu/%zu lit (office %zu, residential %zu, "
                "mixed %zu; LOD %zu dynamic / %zu static)",windows.lit,
                windows.panes,windows.office,windows.residential,windows.mixed,
                windows.dynamic_lod,windows.static_lod);
    }
    AP_INFO("audio: %zu live loops, %u dropped mixer commands",
            vehicle_audio_.loop_count() + city_audio_.loop_count() + traffic_idle_audio_.loop_count(),
            audio_device_.mixer().dropped_commands());
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

    // Closed HERE rather than at the end of run(), because the paths below
    // return early on a failed check and a performance log without its trailer
    // is the half of the file nobody can read.
    set_frame_logging(false);

    gl_errors_ += drain_gl_errors("at shutdown");
    const auto& light_grid=tiled_lighting_.grid();
    AP_INFO("traffic lighting: %zu source beams, %zu view-relevant beams, max %u per cell, %zu light references%s",
        lighting_source_count_,light_grid.visible_lights,light_grid.max_cell_lights,light_grid.indices.size(),
        lighting_stress_ ? " (synthetic 100-car fixture)" : " (actual traffic)");
    if(lighting_benchmark_) {
        const auto median=[](std::vector<double> values) {
            if(values.empty()) return -1.0;
            std::sort(values.begin(),values.end());
            return values[values.size()/2];
        };
        const auto& timings=tiled_lighting_.timings();
        AP_INFO("lighting benchmark: GPU world+people OFF %.3f ms (%zu samples), ON %.3f ms (%zu samples); CPU grid+upload %.3f ms median",
            median(timings[0]),timings[0].size(),median(timings[1]),timings[1].size(),median(light_grid_ms_));
        AP_INFO("lighting CPU breakdown: binning %.3f ms, upload %.3f ms median",median(light_build_ms_),median(light_upload_ms_));
    }
    if (gl_errors_ > 0) {
        AP_ERROR("%d GL error(s) during the session — the frames you saw are "
                 "not the frames that were asked for",
                 gl_errors_);
        return 3;
    }
    AP_INFO("snowplows: %zu active units, %.1f m swept, %zu clearance strips",
        world_.traffic().snowplow_unit_count(),
        static_cast<double>(snowplow_service_.cleared_distance_m()),
        snow_clearance_.strips().size());
    if (snowplow_check_ && (snowplow_service_.cleared_distance_m() < 10.0f ||
                            (snow_clearance_.strips().empty() &&
                             !snowplow_refill_preview_applied_))) {
        AP_ERROR("snowplow check: no meaningful cleared route");
        return 4;
    }
    AP_INFO("GL error queue clean for the whole session");
    return 0;
}

}  // namespace apricot

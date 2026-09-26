// apricot: entry point.
//
// Deliberately thin. Everything is in App (src/app/app.h); this file exists to
// parse the handful of process-level arguments and to make sure the version is
// the first thing in every log, so a bug report's first line identifies the
// build it came from.

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <cstring>
#include <cstdlib>
#include <random>
#include <cerrno>

#include "app/app.h"
#include "app/emergency_lighting.h"
#include "city/bellwether_layout.h"
#include "core/log.h"

namespace {

void print_usage() {
    std::printf(
        "apricot %s\n"
        "\n"
        "  --delivery-check verify interaction, pause, skip, movement and natural completion (--frames 900)\n"
        "  --delivery-preview start Mission 1 package handoff (bounded runs do not autosave)\n"
        "  --opening-preview start the in-game opening (bounded runs do not autosave)\n"
        "  --save-file FILE use an isolated checkpoint slot\n"
        "  --verbose       log at debug level\n"
        "  --log FILE      also append the log to FILE\n"
        "  --perf-log FILE record frame timings to FILE from launch\n"
        "  --perf-record   record frame timings from launch (F1 toggles otherwise)\n"
        "  --perf-spike-ms N call a frame a dip above N ms (default 20)\n"
        "  --frames N      render N frames, print a summary, then exit\n"
        "  --vehicle-entry-check run bounded entry/theft/exit regression\n"
        "  --mistral-entry-check run staged Mistral entry/blocked exit/exit/re-entry\n"
        "  --workman-entry-check run staged Workman entry/blocked exit/exit/re-entry\n"
        "  --aircraft-check run bounded boarding/flight regression\n"
        "  --helicopter-check run bounded Halberd gunship boarding/flight regression\n"
        "  --boat-check     run bounded boarding/boat regression\n"
        "  --police-check capture real siren/light toggle, red/blue/off phases\n"
        "  --police-officer-check test witnessed red, impact, officer exit/fire/arrest/return (6000+ frames)\n"
        "  --police-pursuit-check test an away-facing cruiser turning and pulling over (6000+ frames)\n"
        "  --traffic-horn-check test real traffic impatience, horn playback and lane release (6000+ frames)\n"
        "  --convertible-check capture the Mistral canvas up, folding and stowed (1000+ frames)\n"
        "  --paint-check   drive cars into Rook's and respray them: booth, wanted rule, theft,\n"
        "                  saves, police liveries, firetruck; nine captures (18000+ frames)\n"
        "  --wanted N      start with wanted level 1-5 for pursuit QA\n"
        "  --no-instancing start on the naive per-node draw path\n"
        "  --warp-every N  teleport across the island every N frames\n"
        "  --start-at X Z   start at a world position for visual QA\n"
        "  --start-player-at X Z place the on-foot player separately for interior QA\n"
        "  --start-player-height Y select a supported floor with --start-player-at\n"
        "  --start-heading DEG face a direction at the visual-QA start\n"
        "  --camera-orbit YAW PITCH pose the chase camera in degrees for visual QA\n"
        "  --camera-mode near|chase|far set the capture distance for visual QA\n"
        "  --player-car KEY select a model folder key for visual QA\n"
        "  --driver-transition-check test the selected car entry/exit\n"
        "  --start-driving start seated in the selected car\n"
        "  --trailer-check exercise semi coupling, driving and dropping (bounded)\n"
        "  --tire-track-check script a drift at --start-at for tire-track QA\n"
        "  --screenshot FILE save the final --frames image as a BMP\n"
        "  --overhead      use a fixed daylight QA camera above --start-at\n"
        "  --daylight      hold the sky at noon for visual QA\n"
        "  --road-start    settle --start-at after authored road collision loads\n"
        "  --weapon-check  check aim, fire, reload, NPC blood hits and input guards (900+ frames)\n"
        "  --molotov-check throw one molotov and watch the fire spread and die back (1300+ frames)\n"
        "  --car-bomb-check fit a bomb at Rook's, set it off from the street, then from the\n"
        "                  driver's seat, respawn clear of the fire, and set a pedestrian alight (3600+ frames)\n"
        "  --attended      drive a --frames run by hand: let the window take focus and the cursor\n"
        "  --damage-check  check that three rounds kill a civilian, the body stays down,\n"
        "                  and the player dies, freezes and respawns (2400+ frames)\n"
        "  --house-check   walk through 102 Sycamore's push doors and both exits\n"
        "  --signal-check  crash-test signals, street lamps and stop signs (1700+ frames)\n"
        "  --character-identity-check prove a new departure at one (lane,slot) gets a fresh rig\n"
        "  --night          hold the sky at midnight for lighting QA\n"
        "  --dusk           hold the blue-hour concept-art sky\n"
        "  --bellwether     start in the new town at dusk (explicit start flags override)\n"
        "  --clear          clear weather for visual QA\n"
        "  --weather NAME   force clear, sunshower, overcast, rain, storm, thunderstorm, snow, blizzard, tornado, flood, hail, or heatwave\n"
        "  --snow-depth M   pin accumulated snow depth from 0.0 to 1.5 metres\n"
        "  --vehicle-snow-load L  start the player car carrying snow L (0..1), as if\n"
        "                   it had just driven in from open weather\n"
        "  --snowplow-check follow a working traffic plow; use --weather snow --snow-depth 0.18\n"
        "  --plow-check     push two passes across a lot in a plow truck (default rodeo_grazer_plow,\n"
        "                  snow 0.15 m); fails if the blade cleared under 12 m (2200+ frames)\n"
        "  --lot-plow-check follow the lot plow crew nearest the start (snow 0.12 m unless set);\n"
        "                  fails if the crews cleared under 20 m\n"
        "  --snowplow-refill-seconds N age actual cleared paths before final --frames image (QA)\n"
        "  --seed N         reproduce a session weather sequence\n"
        "  --no-traffic-headlights disable traffic beams (keep lamp glow)\n"
        "  --lighting-benchmark freeze after warmup, alternate beams off/on, measure GPU\n"
        "  --traffic-light-stress use a synthetic 100-car / 200-beam fixture\n"
        "  --version       print the version and exit\n"
        "  --help          this text\n"
        "\n"
        "Controls: WASD move/drive, Shift sprint, Space/left-stick click jump, E enter/exit, P pause, M map.\n"
        "F4 marks the moment in the performance log when a dip is felt.\n"
        "Driving: H honks; J/controller L3 toggles police siren and lights.\n"
        "Weapons: Tab/LB opens wheel; RMB/LT holds aim; Q toggles aim; LMB/RT fires; R/X reloads.\n"
        "On the map: WASD/stick or drag pans; wheel or +/- zooms.\n"
        "Right click marks a waypoint; R/controller X marks the crosshair. Repeat clears.\n"
        "Enter/A selects, Esc/B goes back. Ctrl/Cmd+Q quits.\n"
        "F1 opens the dev menu; F2 reports a bug; F3 toggles debug stats.\n"
        "F7 toggles instancing; F8 warps across the island.\n"
        "\n"
        "--frames exists so the renderer can be exercised without a human at\n"
        "the keyboard: it runs a fixed number of frames, reports the draw and\n"
        "instance counts and the GL error state, and returns non-zero if any\n"
        "GL call failed.\n"
        "\n"
        "--warp-every is the same idea applied to STREAMING. A teleport evicts\n"
        "the entire resident world and refills it somewhere else, which is the\n"
        "hardest thing the streamer does and the one path a human would have to\n"
        "remember to test. Repeating it on a timer means a run either survives\n"
        "a dozen of them with a clean GL queue and a flat mesh count, or it\n"
        "does not.\n",
        APRICOT_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    const char* log_path = nullptr;
    const char* perf_log_path = nullptr;
    // Off by default: the F1 menu turns recording on when it is wanted.
    bool perf_logging = false;
    double perf_spike_ms = 20.0;
    int frame_limit = 0;
    bool attended = false;
    uint64_t session_seed=0;
    bool explicit_seed=false;
    int warp_every = 0;
    glm::vec2 start_position{apricot::city::kOpeningMissionCarPosition.x,
                             apricot::city::kOpeningMissionCarPosition.z};
    bool start_position_set = false;
    float start_heading_radians = apricot::city::kOpeningMissionCarHeading;
    bool start_heading_set = false;
    float camera_orbit_yaw_radians = 0.0f;
    float camera_orbit_pitch_radians = 0.0f;
    bool camera_orbit_set = false;
    int camera_mode = -1;
    glm::vec2 start_player_position{0};
    bool start_player_position_set=false;
    float start_player_height=0;
    bool start_player_height_set=false;
    const char* screenshot_path = nullptr;
    bool dusk_preview=false,bellwether_start=false;
    bool instancing = true;
    bool vehicle_entry_check=false;
    bool driver_transition_check=false;
    apricot::PlayerCarId transition_check_car=apricot::PlayerCarId::VesperMistral;
    bool aircraft_check=false;
    bool helicopter_check=false;
    bool boat_check=false;
    bool trailer_check=false;
    bool tire_track_check=false;
    bool police_check=false;
    bool police_officer_check=false;
    bool police_pursuit_check=false;
    bool traffic_horn_check=false;
    bool convertible_check=false;
    bool paint_check=false;
    bool car_bomb_check=false;
    int start_wanted=0;
    bool weapon_check=false;
    bool molotov_check=false;
    bool damage_check=false;
    bool house_check=false;
    bool signal_check=false;
    bool character_identity_check=false;
    bool overhead_qa=false;
    bool daylight_qa=false;
    bool road_start_qa=false;
    bool opening_preview=false,delivery_preview=false,delivery_check=false;
    std::string save_file;
    bool clear_weather=false;
    apricot::DevWeatherPreset weather_preset =
        apricot::DevWeatherPreset::Dynamic;
    bool weather_preset_set = false;
    float snow_depth_m = -1.0f;
    bool snow_depth_set = false;
    float start_vehicle_snow_load = -1.0f;
    bool snowplow_check = false;
    bool plow_check = false;
    bool lot_plow_check = false;
    float snowplow_refill_preview_seconds = 0.0f;
    apricot::PlayerCarId start_car=apricot::PlayerCarId::LegacyCar5;
    bool player_car_explicit=false;
    bool start_driving=false;
    bool lighting_night=false, traffic_lights_off=false, lighting_benchmark=false, lighting_stress=false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a,"--delivery-check")==0) { delivery_check=true;continue; }
        if (std::strcmp(a,"--delivery-preview")==0) { delivery_preview=true;continue; }
        if (std::strcmp(a,"--opening-preview")==0) { opening_preview=true;continue; }
        if (std::strcmp(a,"--save-file")==0) {
            if (++i>=argc) { std::fprintf(stderr,"--save-file needs a path\n");return 2; }
            save_file=argv[i];continue;
        }
        if (std::strcmp(a,"--weapon-check")==0) { weapon_check=true;continue; }
        if (std::strcmp(a,"--molotov-check")==0) { molotov_check=true;continue; }
        if (std::strcmp(a,"--damage-check")==0) { damage_check=true;continue; }
        if (std::strcmp(a,"--house-check")==0) { house_check=true;continue; }
        if (std::strcmp(a,"--signal-check")==0) { signal_check=true;continue; }
        if (std::strcmp(a,"--character-identity-check")==0) {
            character_identity_check=true;continue; }
        if (std::strcmp(a,"--tire-track-check")==0) { tire_track_check=true;continue; }
        if (std::strcmp(a,"--snowplow-check")==0) { snowplow_check=true;continue; }
        if (std::strcmp(a,"--plow-check")==0) { plow_check=true;continue; }
        if (std::strcmp(a,"--lot-plow-check")==0) { lot_plow_check=true;continue; }
        if (std::strcmp(a,"--overhead")==0) { overhead_qa=true;continue; }
        if (std::strcmp(a,"--daylight")==0) { daylight_qa=true;continue; }
        if (std::strcmp(a,"--road-start")==0) { road_start_qa=true;continue; }
        if (std::strcmp(a,"--seed")==0) {
            if (++i>=argc || argv[i][0]=='-') { std::fprintf(stderr,"--seed needs an unsigned integer\n"); return 2; }
            char* end=nullptr;errno=0;
            session_seed=std::strtoull(argv[i],&end,0);
            if (errno || end==argv[i] || *end) { std::fprintf(stderr,"invalid --seed\n"); return 2; }
            explicit_seed=true;continue;
        }
        if (std::strcmp(a,"--driver-transition-check")==0) {
            driver_transition_check=true;transition_check_car=apricot::PlayerCarId::kCount;continue;
        }
        if (std::strcmp(a,"--vehicle-entry-check")==0) { vehicle_entry_check=true; continue; }
        if (std::strcmp(a,"--mistral-entry-check")==0) {
            driver_transition_check=true; transition_check_car=apricot::PlayerCarId::VesperMistral; continue;
        }
        if (std::strcmp(a,"--workman-entry-check")==0) {
            driver_transition_check=true; transition_check_car=apricot::PlayerCarId::HarrowWorkman; continue;
        }
        if (std::strcmp(a,"--aircraft-check")==0) { aircraft_check=true; continue; }
        if (std::strcmp(a,"--helicopter-check")==0) { helicopter_check=true; continue; }
        if (std::strcmp(a,"--trailer-check")==0) { trailer_check=true; continue; }
        if (std::strcmp(a,"--boat-check")==0) { boat_check=true; continue; }
        if (std::strcmp(a,"--police-check")==0) { police_check=true; continue; }
        if (std::strcmp(a,"--police-officer-check")==0) { police_officer_check=true; continue; }
        if (std::strcmp(a,"--police-pursuit-check")==0) {
            police_pursuit_check=true; police_officer_check=true; continue;
        }
        if (std::strcmp(a,"--traffic-horn-check")==0) { traffic_horn_check=true; continue; }
        if (std::strcmp(a,"--convertible-check")==0) { convertible_check=true; continue; }
        if (std::strcmp(a,"--paint-check")==0) { paint_check=true; continue; }
        if (std::strcmp(a,"--car-bomb-check")==0) { car_bomb_check=true; continue; }
        if (std::strcmp(a,"--wanted")==0) {
            if (++i>=argc) { std::fprintf(stderr,"--wanted needs a level from 1 to 5\n"); return 2; }
            start_wanted=std::atoi(argv[i]);
            if (start_wanted<1 || start_wanted>5) {
                std::fprintf(stderr,"--wanted needs a level from 1 to 5\n");return 2;
            }
            continue;
        }
        if (std::strcmp(a,"--start-driving")==0) { start_driving=true; continue; }
        if (std::strcmp(a,"--player-car")==0 && i+1<argc) {
            const std::string key=std::string("/")+argv[++i]+"/";
            bool found=false;
            for (const auto& model:apricot::kPlayerCars) {
                // A variant sharing its base's folder answers to its own key.
                if (const char* variant=apricot::player_car_variant_key(model.id)) {
                    if (key==std::string("/")+variant+"/") {
                        start_car=model.id; found=true; player_car_explicit=true; break;
                    }
                    continue;
                }
                if (std::string(model.mesh_path).find(key)!=std::string::npos) {
                    start_car=model.id; found=true; player_car_explicit=true; break;
                }
            }
            if (!found) { std::fprintf(stderr,"unknown player car\n"); return 2; }
            continue;
        }
        if (std::strcmp(a,"--night")==0) { lighting_night=true; continue; }
        if (std::strcmp(a,"--dusk")==0) { dusk_preview=true;continue; }
        if (std::strcmp(a,"--bellwether")==0) { bellwether_start=true;dusk_preview=true;clear_weather=true;continue; }
        if (std::strcmp(a,"--clear")==0) { clear_weather=true; continue; }
        if (std::strcmp(a,"--weather")==0) {
            if (++i>=argc) { std::fprintf(stderr,"--weather needs a name\n"); return 2; }
            const char* name=argv[i];
            if (std::strcmp(name,"clear")==0) weather_preset=apricot::DevWeatherPreset::Clear;
            else if (std::strcmp(name,"overcast")==0) weather_preset=apricot::DevWeatherPreset::Overcast;
            else if (std::strcmp(name,"rain")==0) weather_preset=apricot::DevWeatherPreset::Rain;
            else if (std::strcmp(name,"sunshower")==0) weather_preset=apricot::DevWeatherPreset::Sunshower;
            else if (std::strcmp(name,"storm")==0) weather_preset=apricot::DevWeatherPreset::Storm;
            else if (std::strcmp(name,"thunderstorm")==0) weather_preset=apricot::DevWeatherPreset::Thunderstorm;
            else if (std::strcmp(name,"snow")==0) weather_preset=apricot::DevWeatherPreset::Snow;
            else if (std::strcmp(name,"blizzard")==0) weather_preset=apricot::DevWeatherPreset::Blizzard;
            else if (std::strcmp(name,"tornado")==0) weather_preset=apricot::DevWeatherPreset::Tornado;
            else if (std::strcmp(name,"flood")==0) weather_preset=apricot::DevWeatherPreset::Flood;
            else if (std::strcmp(name,"hail")==0) weather_preset=apricot::DevWeatherPreset::Hail;
            else if (std::strcmp(name,"heatwave")==0) weather_preset=apricot::DevWeatherPreset::Heatwave;
            else if (std::strcmp(name,"dynamic")==0) weather_preset=apricot::DevWeatherPreset::Dynamic;
            else { std::fprintf(stderr,"unknown --weather name: %s\n",name); return 2; }
            weather_preset_set=true;continue;
        }
        if (std::strcmp(a,"--snowplow-refill-seconds")==0) {
            if (++i>=argc) {
                std::fprintf(stderr,"--snowplow-refill-seconds needs 0..86400 seconds\n");
                return 2;
            }
            char* end=nullptr;
            errno=0;
            snowplow_refill_preview_seconds=std::strtof(argv[i],&end);
            if (errno || end==argv[i] || *end || !std::isfinite(snowplow_refill_preview_seconds) ||
                snowplow_refill_preview_seconds<0.0f || snowplow_refill_preview_seconds>86400.0f) {
                std::fprintf(stderr,"--snowplow-refill-seconds needs 0..86400 seconds\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a,"--vehicle-snow-load")==0) {
            char* end=nullptr;
            if (++i<argc) start_vehicle_snow_load=std::strtof(argv[i],&end);
            if (i>=argc || end==argv[i] || *end || !std::isfinite(start_vehicle_snow_load) ||
                start_vehicle_snow_load<0.0f || start_vehicle_snow_load>1.0f) {
                std::fprintf(stderr,"--vehicle-snow-load needs a load from 0.0 to 1.0\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a,"--snow-depth")==0) {
            if (++i>=argc) {
                std::fprintf(stderr,"--snow-depth needs metres from 0.0 to 1.5\n");
                return 2;
            }
            char* end=nullptr;
            errno=0;
            snow_depth_m=std::strtof(argv[i],&end);
            if (errno || end==argv[i] || *end || !std::isfinite(snow_depth_m) ||
                snow_depth_m<0.0f || snow_depth_m>1.5f) {
                std::fprintf(stderr,"--snow-depth needs metres from 0.0 to 1.5\n");
                return 2;
            }
            snow_depth_set=true;
            continue;
        }
        if (std::strcmp(a,"--no-traffic-headlights")==0) { traffic_lights_off=true; continue; }
        if (std::strcmp(a,"--lighting-benchmark")==0) { lighting_benchmark=true; continue; }
        if (std::strcmp(a,"--traffic-light-stress")==0) { lighting_stress=true; continue; }
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(a, "--version") == 0) {
            std::printf("%s\n", APRICOT_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--attended") == 0) { attended = true; continue; }
        if (std::strcmp(a, "--verbose") == 0) {
            apricot::log::min_level() = apricot::log::Level::Debug;
            continue;
        }
        if (std::strcmp(a, "--perf-log") == 0 && i + 1 < argc) {
            perf_log_path = argv[++i];
            perf_logging = true;  // naming a file is asking for a recording
            continue;
        }
        if (std::strcmp(a, "--perf-record") == 0) { perf_logging = true; continue; }
        if (std::strcmp(a, "--perf-spike-ms") == 0 && i + 1 < argc) {
            char* end = nullptr;
            perf_spike_ms = std::strtod(argv[++i], &end);
            if (end == argv[i] || !(perf_spike_ms > 0.0) ||
                !std::isfinite(perf_spike_ms)) {
                std::fprintf(stderr, "--perf-spike-ms needs a positive number of milliseconds\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a, "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
            continue;
        }
        if (std::strcmp(a, "--frames") == 0 && i + 1 < argc) {
            frame_limit = std::atoi(argv[++i]);
            if (frame_limit <= 0) {
                std::fprintf(stderr, "--frames needs a positive count\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a, "--no-instancing") == 0) {
            instancing = false;
            continue;
        }
        if (std::strcmp(a, "--warp-every") == 0 && i + 1 < argc) {
            warp_every = std::atoi(argv[++i]);
            if (warp_every <= 0) {
                std::fprintf(stderr, "--warp-every needs a positive count\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a,"--start-player-height")==0) {
            if(i+1>=argc) {std::fprintf(stderr,"--start-player-height needs a finite number\n");return 2;}
            char* end=nullptr;
            const char* value=argv[++i];
            start_player_height=std::strtof(value,&end);
            if(end==value || *end!='\0' || !std::isfinite(start_player_height)) {
                std::fprintf(stderr,"--start-player-height needs a finite number\n");return 2;
            }
            start_player_height_set=true;continue;
        }
        if ((std::strcmp(a, "--start-at") == 0 || std::strcmp(a,"--start-player-at")==0) && i + 2 < argc) {
            const bool player=std::strcmp(a,"--start-player-at")==0;
            auto& position=player?start_player_position:start_position;
            char* x_end = nullptr;
            char* z_end = nullptr;
            position.x = std::strtof(argv[++i], &x_end);
            position.y = std::strtof(argv[++i], &z_end);
            if (x_end == nullptr || *x_end != '\0' || z_end == nullptr ||
                *z_end != '\0' || !std::isfinite(position.x) ||
                !std::isfinite(position.y)) {
                std::fprintf(stderr, "%s needs two finite numbers\n",a);
                return 2;
            }
            if(player) start_player_position_set=true;
            else start_position_set=true;
            continue;
        }
        if (std::strcmp(a, "--start-heading") == 0 && i + 1 < argc) {
            char* heading_end = nullptr;
            const float heading_degrees = std::strtof(argv[++i], &heading_end);
            if (heading_end == nullptr || *heading_end != '\0' ||
                !std::isfinite(heading_degrees)) {
                std::fprintf(stderr,
                             "--start-heading needs one finite degree value\n");
                return 2;
            }
            start_heading_radians =
                heading_degrees * 3.14159265358979323846f / 180.0f;
            start_heading_set = true;
            continue;
        }
        if (std::strcmp(a, "--camera-orbit") == 0 && i + 2 < argc) {
            char* yaw_end = nullptr;
            char* pitch_end = nullptr;
            const float yaw_degrees = std::strtof(argv[++i], &yaw_end);
            const float pitch_degrees = std::strtof(argv[++i], &pitch_end);
            if (yaw_end == nullptr || *yaw_end != '\0' || pitch_end == nullptr ||
                *pitch_end != '\0' || !std::isfinite(yaw_degrees) ||
                !std::isfinite(pitch_degrees)) {
                std::fprintf(stderr, "--camera-orbit needs two finite degree values\n");
                return 2;
            }
            constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
            camera_orbit_yaw_radians = yaw_degrees * kDegreesToRadians;
            camera_orbit_pitch_radians = pitch_degrees * kDegreesToRadians;
            camera_orbit_set = true;
            continue;
        }
        if (std::strcmp(a, "--camera-mode") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (std::strcmp(mode, "near") == 0) camera_mode = 0;
            else if (std::strcmp(mode, "chase") == 0) camera_mode = 1;
            else if (std::strcmp(mode, "far") == 0) camera_mode = 2;
            else {
                std::fprintf(stderr, "--camera-mode needs near, chase, or far\n");
                return 2;
            }
            continue;
        }
        if (std::strcmp(a, "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
            continue;
        }
        std::fprintf(stderr, "unknown argument: %s\n", a);
        print_usage();
        return 2;
    }


    if (log_path && !apricot::log::open_log_file(log_path)) {
        // Not fatal: console logging still works, and refusing to start over a
        // log file would be a poor trade.
        AP_WARN("could not open log file '%s'; console only", log_path);
    }

    apricot::App app;
    if (paint_check) {
        // Every stage drives a car in from the forecourt and some wait for a
        // real police unit to see it, so it needs room; it also owns the car,
        // the wanted level and the camera, so it runs alone.
        if (frame_limit<18000 || vehicle_entry_check || driver_transition_check || aircraft_check ||
            helicopter_check || boat_check || trailer_check || tire_track_check || police_check ||
            police_officer_check || traffic_horn_check || convertible_check || weapon_check ||
            molotov_check || damage_check || house_check || signal_check || character_identity_check ||
            lighting_benchmark || warp_every) {
            std::fprintf(stderr,"--paint-check needs --frames 18000 or more and no other checks or warps\n");
            return 2;
        }
        if (!player_car_explicit) start_car=apricot::PlayerCarId::VesperMistral;
        // Clear daylight, so the captures are about the paint. `--night` is
        // honoured: it is how the lamp and lightbar glow over a respray is seen.
        clear_weather=true;
        if (!lighting_night) daylight_qa=true;
        start_driving=true;
        if (!start_position_set) {
            // Rook's forecourt, in front of bay one, facing the garage.
            const auto& site=apricot::city::kAutoRepairSite;
            start_position={site.origin.x+site.cos_yaw*-9.0f+site.sin_yaw*12.0f,
                            site.origin.z-site.sin_yaw*-9.0f+site.cos_yaw*12.0f};
            if (!start_heading_set) start_heading_radians=std::atan2(site.sin_yaw,site.cos_yaw);
        }
        if (!screenshot_path) screenshot_path="build/paint-check";
        app.set_paint_check(true);
    }
    if (car_bomb_check) {
        // Drives the bay with the paint check's autopilot, so it runs alone
        // for the same reasons that one does.
        if (frame_limit<3600 || paint_check || vehicle_entry_check || driver_transition_check ||
            aircraft_check || helicopter_check || boat_check || trailer_check || tire_track_check ||
            police_check || police_officer_check || traffic_horn_check || convertible_check ||
            weapon_check || molotov_check || damage_check || house_check || signal_check ||
            character_identity_check || lighting_benchmark || warp_every) {
            std::fprintf(stderr,"--car-bomb-check needs --frames 3600 or more and no other checks or warps\n");
            return 2;
        }
        if (!player_car_explicit) start_car=apricot::PlayerCarId::VesperMistral;
        clear_weather=true;
        if (!lighting_night) daylight_qa=true;
        start_driving=true;
        if (!start_position_set) {
            const auto& site=apricot::city::kAutoRepairSite;
            start_position={site.origin.x+site.cos_yaw*-9.0f+site.sin_yaw*12.0f,
                            site.origin.z-site.sin_yaw*-9.0f+site.cos_yaw*12.0f};
            if (!start_heading_set) start_heading_radians=std::atan2(site.sin_yaw,site.cos_yaw);
        }
        if (!screenshot_path) screenshot_path="build/car-bomb-check";
        app.set_car_bomb_check(true);
    }
    if (convertible_check) {
        // The canvas takes 2.4 s each way and the second press lands at frame
        // 420; below this there is no room left to capture the return.
        if (frame_limit<1000) {
            std::fprintf(stderr,"--convertible-check needs --frames 1000 or more\n");return 2;
        }
        if (!player_car_explicit) start_car=apricot::PlayerCarId::VesperMistral;
        if (!apricot::is_convertible(start_car)) {
            std::fprintf(stderr,"--convertible-check needs a convertible --player-car\n");return 2;
        }
        clear_weather=true; daylight_qa=true; start_driving=true; road_start_qa=true;
        if (!screenshot_path) screenshot_path="build/convertible-check";
        app.set_convertible_check(true);
    }
    if (traffic_horn_check) {
        if (frame_limit<6000 || house_check || signal_check || tire_track_check ||
            vehicle_entry_check || driver_transition_check || aircraft_check || helicopter_check || boat_check ||
            trailer_check || police_check || police_officer_check || weapon_check ||
            lighting_benchmark || warp_every || opening_preview || delivery_preview || delivery_check) {
            std::fprintf(stderr,"--traffic-horn-check needs --frames 6000 or more and no other checks/previews/warps\n");
            return 2;
        }
        clear_weather=true;daylight_qa=true;start_driving=true;road_start_qa=true;
        if (!start_position_set) start_position={950,40};
        if (!screenshot_path) screenshot_path="build/traffic-horn-check";
        app.set_traffic_horn_check(true);
    }
    if (police_officer_check) {
        if (frame_limit<6000 || house_check || signal_check || tire_track_check ||
            vehicle_entry_check || driver_transition_check || aircraft_check || helicopter_check || boat_check ||
            trailer_check || police_check || weapon_check || lighting_benchmark || warp_every ||
            opening_preview || delivery_preview || delivery_check) {
            std::fprintf(stderr,"--police-officer-check needs --frames 6000 or more and no other checks/previews/warps\n");
            return 2;
        }
        clear_weather=true;daylight_qa=true;start_driving=true;road_start_qa=true;
        if (!start_position_set) start_position={950,40};
        if (!screenshot_path) screenshot_path="build/police-officer-check";
        app.set_police_officer_check(true);
        if (police_pursuit_check) app.set_police_pursuit_check(true);
    }
    if(signal_check) {
        if(frame_limit<1700 || house_check || tire_track_check || vehicle_entry_check || driver_transition_check ||
            aircraft_check || helicopter_check || boat_check || police_check || weapon_check || lighting_benchmark || warp_every || opening_preview) {
            std::fprintf(stderr,"--signal-check needs --frames 1700 or more and no other checks/warps\n");
            return 2;
        }
        clear_weather=true;start_driving=true;start_position={1070,180};
        if(!screenshot_path)screenshot_path="build/signal-check";
        app.set_signal_check(true);
    }
    if(tire_track_check) {
        if(frame_limit<650 || house_check || signal_check || vehicle_entry_check ||
            driver_transition_check || aircraft_check || helicopter_check || boat_check || trailer_check ||
            police_check || weapon_check || lighting_benchmark || warp_every ||
            opening_preview || delivery_preview || delivery_check) {
            std::fprintf(stderr,"--tire-track-check needs --frames 650 or more and no other checks/previews\n");
            return 2;
        }
        start_driving=true;
        road_start_qa=true;
        if (!start_position_set) start_position={-260.0f,2046.0f};
        if (!start_heading_set) start_heading_radians=-1.57079632679f;
        if(!weather_preset_set) clear_weather=true;
        if(!screenshot_path)screenshot_path="build/tire-track-check.bmp";
        app.set_tire_track_check(true);
    }
    if(plow_check) {
        if(frame_limit<2200 || house_check || signal_check || tire_track_check || vehicle_entry_check ||
            driver_transition_check || aircraft_check || helicopter_check || boat_check || trailer_check ||
            police_check || weapon_check || lighting_benchmark || warp_every || paint_check || car_bomb_check ||
            opening_preview || delivery_preview || delivery_check) {
            std::fprintf(stderr,"--plow-check needs --frames 2200 or more and no other checks/previews\n");
            return 2;
        }
        if (!player_car_explicit) start_car=apricot::PlayerCarId::RodeoGrazerPlow;
        if (!apricot::has_plow_kit(start_car)) {
            std::fprintf(stderr,"--plow-check needs a plow --player-car\n");return 2;
        }
        start_driving=true;
        if (!start_position_set) {
            // Cloggers' lot, across the street north of the pumps, pushing
            // west along the frontage bays.
            const auto& site=apricot::city::kFastFoodSite;
            start_position={site.origin.x+site.cos_yaw*14.0f+site.sin_yaw*-12.0f,
                            site.origin.z-site.sin_yaw*14.0f+site.cos_yaw*-12.0f};
            // Facing site -x (west), down the frontage.
            if (!start_heading_set) start_heading_radians=std::atan2(site.cos_yaw,-site.sin_yaw);
        }
        if (!weather_preset_set) { weather_preset=apricot::DevWeatherPreset::Snow; weather_preset_set=true; }
        if (!snow_depth_set) { snow_depth_m=0.15f; snow_depth_set=true; }
        if (!lighting_night) daylight_qa=true;
        if(!screenshot_path)screenshot_path="build/plow-check.bmp";
        app.set_plow_check(true);
    }
    if(lot_plow_check) {
        if (!weather_preset_set) { weather_preset=apricot::DevWeatherPreset::Snow; weather_preset_set=true; }
        if (!snow_depth_set) { snow_depth_m=0.12f; snow_depth_set=true; }
        if (!lighting_night) daylight_qa=true;
        if(!screenshot_path)screenshot_path="build/lot-plow-check.bmp";
        app.set_lot_plow_check(true);
    }
    if(house_check) {
        if(frame_limit<6000 || tire_track_check || vehicle_entry_check || driver_transition_check || aircraft_check ||
            boat_check || police_check || weapon_check || lighting_benchmark || lighting_stress || warp_every || start_driving) {
            std::fprintf(stderr,"--house-check needs --frames 6000 or more and no other check/warp/driving modes\n");
            return 2;
        }
        start_position={1023.919f,223.786f}; // Park in 102's drive, clear of the street.
        clear_weather=true;
        app.set_house_check(true);
    }
    if ((vehicle_entry_check || aircraft_check || helicopter_check || boat_check || driver_transition_check) && frame_limit<=0) {
        std::fprintf(stderr,"entry/aircraft checks require --frames\n");
        return 2;
    }
    app.set_vehicle_entry_check(vehicle_entry_check);
    app.set_driver_transition_check(driver_transition_check);
    if (driver_transition_check) {
        if (vehicle_entry_check || aircraft_check || helicopter_check || boat_check || police_check || lighting_benchmark || warp_every) {
            std::fprintf(stderr,"driver transition checks must run without other check/warp modes\n");
            return 2;
        }
        if(transition_check_car==apricot::PlayerCarId::kCount) transition_check_car=start_car;
        if(!apricot::has_animated_driver(transition_check_car)) {
            std::fprintf(stderr,"selected car has no articulated driver setup\n");return 2;
        }
        start_car=transition_check_car;
        start_driving=false;
    }
    app.set_aircraft_check(aircraft_check);
    if (helicopter_check && aircraft_check) {
        std::fprintf(stderr,"--helicopter-check and --aircraft-check must run separately\n");
        return 2;
    }
    app.set_helicopter_check(helicopter_check);
    if (boat_check && (aircraft_check || helicopter_check || vehicle_entry_check || police_check || lighting_benchmark || warp_every)) {
        std::fprintf(stderr,"--boat-check must run without other check/warp modes\n");return 2;
    }
    app.set_boat_check(boat_check);
    if(trailer_check) {
        if(frame_limit<300 || boat_check || aircraft_check || helicopter_check || vehicle_entry_check || driver_transition_check ||
           opening_preview || delivery_preview || delivery_check || house_check || signal_check || police_check ||
           weapon_check || lighting_benchmark || warp_every) {
            std::fprintf(stderr,"--trailer-check needs --frames 300 or more and no other checks/previews\n");return 2;
        }
        start_car=apricot::PlayerCarId::HarrowHauler;start_driving=true;
    }
    app.set_trailer_check(trailer_check);
    app.set_police_check(police_check);
    app.set_start_wanted(start_wanted);
    if (police_check) {
        if (frame_limit<600) frame_limit=600;
        if (!player_car_explicit) start_car=apricot::PlayerCarId::MunicipalCruiser91C;
        if (!apricot::has_police_lightbar(start_car)) {
            std::fprintf(stderr,"--police-check needs a police --player-car\n");return 2;
        }
        start_driving=true; lighting_night=true;
        if (!screenshot_path) screenshot_path="build/police-check.bmp";
    }
    if(lighting_benchmark && frame_limit<1200) frame_limit=1200;
    app.set_lighting_diagnostics(lighting_night,traffic_lights_off,lighting_benchmark,lighting_stress);
    // Set before init(): both affect what the first frame does.
    app.set_frame_limit(frame_limit);
    app.set_overhead_qa(overhead_qa);
    app.set_character_identity_check(character_identity_check);
    app.set_daylight_qa(daylight_qa || overhead_qa);
    app.set_road_start_qa(road_start_qa);
    app.set_opening_preview(opening_preview);
    app.set_delivery_preview(delivery_preview);
    if(delivery_check && (frame_limit<900 || opening_preview || delivery_preview ||
        house_check || signal_check || weapon_check || boat_check || driver_transition_check ||
        vehicle_entry_check || aircraft_check || helicopter_check || police_check || lighting_benchmark || warp_every)) {
        std::fprintf(stderr,"--delivery-check needs --frames 900 or more and no other checks/previews\n");return 2;
    }
    app.set_delivery_check(delivery_check);
    if (!save_file.empty()) app.set_save_path(save_file);
    if (weapon_check) {
        if (frame_limit<900) { std::fprintf(stderr,"--weapon-check needs --frames 900 or more\n");return 2; }
        if (!screenshot_path) screenshot_path="build/weapon-check";
        app.set_weapon_check(true);
    }
    if (molotov_check) {
        if (frame_limit<1300 || weapon_check || damage_check || house_check ||
            signal_check || police_check || police_officer_check ||
            traffic_horn_check || lighting_benchmark || warp_every ||
            opening_preview || delivery_preview || delivery_check) {
            std::fprintf(stderr,"--molotov-check needs --frames 1300 or more and no other checks/previews/warps\n");
            return 2;
        }
        // Clear daylight by default, because the point of the screenshots is
        // the FLAME and rain over it is a way to disagree about what you are
        // looking at. `--night` is honoured rather than overridden: the fire's
        // own tiled spot light is invisible at noon and obvious after dark, so
        // running the same check at night is how that half gets looked at.
        clear_weather=true;
        if (!lighting_night) daylight_qa=true;
        if (!screenshot_path) screenshot_path="build/molotov-check";
        app.set_molotov_check(true);
    }
    if (damage_check) {
        if (frame_limit<2400 || weapon_check || house_check || signal_check ||
            police_check || police_officer_check || traffic_horn_check ||
            lighting_benchmark || warp_every) {
            std::fprintf(stderr,"--damage-check needs --frames 2400 or more and no other checks/warps\n");
            return 2;
        }
        clear_weather=true;daylight_qa=true;
        if (!screenshot_path) screenshot_path="build/damage-check";
        app.set_damage_check(true);
    }
    app.set_attended(attended);
    app.set_dusk_preview(dusk_preview);
    app.set_start_in_game(bellwether_start);
    if(bellwether_start) {
        if(!start_position_set)start_position={apricot::city::kBellwetherCarStart.x,
                                              apricot::city::kBellwetherCarStart.z};
        if(!start_player_position_set && !start_driving) {
            start_player_position={apricot::city::kBellwetherPlayerStart.x,
                                   apricot::city::kBellwetherPlayerStart.z};
            start_player_position_set=true;
        }
        if(!start_heading_set)start_heading_radians=1.57079632679f;
    }
    app.set_instancing(instancing);
    app.set_warp_interval(warp_every);
    app.set_start_position(start_position);
    if(start_player_height_set && !start_player_position_set) {
        std::fprintf(stderr,"--start-player-height requires --start-player-at\n");return 2;
    }
    if(start_player_height_set)app.set_start_player_height(start_player_height);
    if(start_player_position_set) {
        if(start_driving) {std::fprintf(stderr,"--start-player-at requires on-foot mode\n");return 2;}
        app.set_start_player_position(start_player_position);
    }
    app.set_start_heading(start_heading_radians);
    if (camera_orbit_set)
        app.set_camera_orbit(camera_orbit_yaw_radians, camera_orbit_pitch_radians);
    if (camera_mode >= 0) app.set_camera_mode(camera_mode);
    if (!explicit_seed) {
        // Host-only entropy chooses a run; simulation consumes the saved seed.
        std::random_device entropy;
        session_seed=(static_cast<uint64_t>(entropy())<<32)^static_cast<uint64_t>(entropy());
    }
    app.set_session_seed(session_seed);
    if (weather_preset_set) clear_weather=false;
    app.set_clear_weather(clear_weather);
    if (weather_preset_set) app.set_weather_preset(weather_preset);
    if (snow_depth_set) app.set_snow_depth_override(snow_depth_m);
    app.set_start_vehicle_snow_load(start_vehicle_snow_load);
    app.set_snowplow_check(snowplow_check);
    app.set_snowplow_refill_preview(snowplow_refill_preview_seconds);
    app.set_vehicle_preview(start_car,start_driving);
    if (perf_log_path) app.set_perf_log_path(perf_log_path);
    app.set_perf_logging(perf_logging);
    app.set_perf_spike_ms(perf_spike_ms);
    if (screenshot_path) app.set_screenshot_path(screenshot_path);
    if (!app.init()) {
        AP_ERROR("startup failed");
        app.shutdown();
        apricot::log::close_log_file();
        return 1;
    }

    int rc = app.run();
    if (traffic_horn_check && !app.traffic_horn_check_passed()) {
        AP_ERROR("traffic horn gameplay regression did not complete");rc=1;
    }
    if (convertible_check && !app.convertible_check_passed()) {
        AP_ERROR("convertible top regression did not capture all three poses");rc=1;
    }
    if (car_bomb_check && !app.car_bomb_check_passed()) {
        AP_ERROR("car bomb check did not pass: captures 0x%x", app.car_bomb_check_captures());rc=1;
    }
    if (paint_check && !app.paint_check_passed()) {
        AP_ERROR("paint check did not pass: stages 0x%x, captures 0x%x",
                 app.paint_check_bits(), app.paint_check_captures());rc=1;
    }
    if (police_officer_check && !app.police_officer_check_passed()) {
        AP_ERROR("police officer gameplay regression did not complete");rc=1;
    }
    if(delivery_check && !app.delivery_check_passed()) {
        AP_ERROR("delivery cutscene regression did not complete");rc=1;
    }
    if (signal_check && !app.signal_check_passed()) {
        AP_ERROR("roadside fixture destruction regression did not complete");rc=1;
    }
    if (character_identity_check && !app.character_identity_check_passed()) {
        AP_ERROR("ambient rig identity regression did not complete");rc=1;
    }
    if (lot_plow_check && !app.lot_plow_check_passed()) {
        AP_ERROR("lot plow check: the crews cleared too little");rc=1;
    }
    if (plow_check && !app.plow_check_passed()) {
        AP_ERROR("plow check: the blade cleared too little of the lot");rc=1;
    }
    if (tire_track_check && !app.tire_track_check_passed()) {
        AP_ERROR("tire-track regression did not produce enough decals");rc=1;
    }
    if (house_check && !app.house_check_passed()) {
        AP_ERROR("house push-door regression did not complete");rc=1;
    }
    if (weapon_check && !app.weapon_check_passed()) {
        AP_ERROR("weapon selection regression did not complete");rc=1;
    }
    if (molotov_check && !app.molotov_check_passed()) {
        AP_ERROR("molotov and fire regression did not complete");rc=1;
    }
    if (damage_check && !app.damage_check_passed()) {
        AP_ERROR("damage and death check did not complete");rc=1;
    }
    if (trailer_check && !app.trailer_check_passed()) return 1;
    if (boat_check && !app.boat_check_passed()) {
        AP_ERROR("boat regression did not complete");rc=1;
    }
    if (driver_transition_check && !app.driver_transition_check_passed()) {
        AP_ERROR("Driver transition regression did not complete"); rc=1;
    }
    if (police_check && !app.police_check_passed()) {
        AP_ERROR("police light toggle regression did not complete"); rc=1;
    }
    if (helicopter_check && !app.helicopter_check_passed()) {
        AP_ERROR("helicopter regression did not complete"); rc=1;
    }
    if (aircraft_check && !app.aircraft_check_passed()) {
        AP_ERROR("aircraft regression did not complete"); rc=1;
    }
    if (vehicle_entry_check && !app.vehicle_entry_check_passed()) {
        AP_ERROR("vehicle entry regression did not complete");
        rc=1;
    }
    app.shutdown();
    apricot::log::close_log_file();
    return rc;
}

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "app/driving_mechanics.h"
#include "app/cutscene_player.h"
#include "game/save_game.h"
#include "app/character_visual.h"
#include "app/bank_interaction.h"
#include "app/bug_report.h"
#include "app/overlay.h"
#include "app/game_ui.h"
#include "app/mistral_soft_top.h"
#include "app/player_car_visual.h"
#include "app/trailer_visual.h"
#include "app/traffic_visual.h"
#include "app/vehicle_effects.h"
#include "app/weapon_visual.h"
#include "game/weapon.h"
#include "game/wanted_system.h"
#include "game/police_offenses.h"
#include "game/player_vitals.h"
#include "game/police_arrest.h"
#include "game/police_visibility.h"
#include "game/repair_shop.h"
#include "game/road_name.h"
#include "app/world.h"
#include "city/start_area.h"
#include "audio/city_audio.h"
#include "audio/intro_audio.h"
#include "audio/footstep_audio.h"
#include "audio/rain_audio.h"
#include "audio/device.h"
#include "audio/vehicle_audio.h"
#include "audio/vehicle_leak_warning.h"
#include "audio/traffic_idle_audio.h"
#include "audio/traffic_horn_audio.h"
#include "audio/police_siren.h"
#include "core/fixed_step.h"
#include "core/frame_log.h"
#include "gfx/gpu_timer.h"
#include "game/conditions.h"
#include "game/snowpack.h"
#include "app/snowplow_service.h"
#include "physics/snow_shelter.h"
#include "game/character.h"
#include "game/drunk.h"
#include "game/aircraft.h"
#include "game/helicopter.h"
#include "game/boat.h"
#include "gfx/camera.h"
#include "gfx/chase_camera.h"
#include "gfx/hud.h"
#include "gfx/loading_screen.h"
#include "gfx/precip.h"
#include "gfx/renderer.h"
#include "gfx/sky.h"
#include "gfx/tire_tracks.h"
#include "gfx/ocean.h"
#include "gfx/tiled_lighting.h"
#include "gfx/tornado.h"
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
    void set_delivery_check(bool enabled) { delivery_check_=enabled; }
    bool delivery_check_passed() const { return delivery_check_passed_; }
    void set_delivery_preview(bool enabled) { delivery_preview_=enabled; }
    void set_opening_preview(bool enabled) { opening_preview_=enabled; }
    void set_save_path(std::string path) { save_path_=std::move(path); }
    void set_frame_limit(int frames) { frame_limit_ = frames; }
    void set_overhead_qa(bool enabled) { overhead_qa_ = enabled; }
    void set_daylight_qa(bool enabled) { daylight_qa_ = enabled; }
    void set_road_start_qa(bool enabled) { road_start_qa_ = enabled; }
    void set_vehicle_entry_check(bool enabled) { vehicle_entry_check_=enabled; }
    bool vehicle_entry_check_passed() const { return vehicle_entry_check_passed_; }
    void set_driver_transition_check(bool enabled) { driver_transition_check_=enabled; }
    bool driver_transition_check_passed() const { return driver_check_stage_==6; }
    void set_aircraft_check(bool enabled) { aircraft_check_=enabled; }
    bool aircraft_check_passed() const { return aircraft_check_passed_; }
    void set_helicopter_check(bool enabled) { helicopter_check_=enabled; }
    bool helicopter_check_passed() const { return helicopter_check_passed_; }
    void set_trailer_check(bool enabled) { trailer_check_=enabled; }
    bool trailer_check_passed() const { return trailer_check_passed_; }
    void set_boat_check(bool enabled) { boat_check_=enabled; }
    bool boat_check_passed() const { return boat_check_passed_; }
    void set_police_check(bool enabled) { police_check_=enabled; }
    bool police_check_passed() const { return police_check_captures_==7; }
    void set_police_officer_check(bool enabled) { police_officer_check_=enabled; }
    void set_police_pursuit_check(bool enabled) {
        police_pursuit_check_=enabled; police_officer_check_=enabled;
    }
    void set_traffic_horn_check(bool enabled) { traffic_horn_check_=enabled; }
    void set_convertible_check(bool enabled) { convertible_check_=enabled; }
    bool convertible_check_passed() const {
        return convertible_check_captures_==7u && !convertible_check_failed_;
    }
    bool traffic_horn_check_passed() const {
        return traffic_horn_check_done_ && !traffic_horn_check_failed_;
    }
    bool police_officer_check_passed() const {
        return police_officer_check_done_ && !police_officer_check_failed_;
    }
    void set_start_wanted(int level) { start_wanted_level_=level; }
    void set_weapon_check(bool enabled) { weapon_check_=enabled; }
    bool weapon_check_passed() const {
        return weapon_check_captures_==255 && weapon_hit_check_done_ && !weapon_hit_check_failed_;
    }
    void set_damage_check(bool on) { damage_check_=on; }
    bool damage_check_passed() const {
        return damage_check_done_ && !damage_check_failed_;
    }
    void set_house_check(bool enabled) { house_check_=enabled; }
    bool house_check_passed() const { return house_check_complete_ && !house_check_failed_; }
    void set_signal_check(bool enabled) { signal_check_=enabled; }
    bool signal_check_passed() const { return signal_check_done_ && !signal_check_failed_; }
    void set_character_identity_check(bool enabled) {
        character_identity_check_ = enabled;
    }
    bool character_identity_check_passed() const {
        return character_identity_check_done_ && !character_identity_check_failed_;
    }
    void set_tire_track_check(bool enabled) { tire_track_check_ = enabled; }
    bool tire_track_check_passed() const {
        return tire_tracks_.live_count() >= 20u;
    }

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

    // Start at an authored remote site without driving there first. This is a
    // visual-QA option only; the default remains the Pinatty Row spawn.
    void set_start_position(glm::vec2 xz) { start_position_ = xz; }
    // Interior QA can place the person independently of the parked car.
    void set_start_player_height(float y) { start_player_height_=y;start_player_height_set_=true; }
    void set_start_player_position(glm::vec2 xz) { start_player_position_=xz;start_player_position_set_=true; }
    void set_start_heading(float radians) { start_heading_radians_ = radians; }
    void set_session_seed(uint64_t seed) { seed_=new_game_seed_=seed; }
    void set_clear_weather(bool clear) { clear_weather_=clear; }
    void set_weather_preset(DevWeatherPreset preset);
    void set_snow_depth_override(float depth_m);
    void set_snowplow_check(bool enabled) { snowplow_check_ = enabled; }
    void set_snowplow_refill_preview(float seconds) { snowplow_refill_preview_seconds_ = seconds; }
    void set_vehicle_preview(PlayerCarId car, bool driving) {
        start_car_=car; start_driving_=driving;
    }
    void set_lighting_diagnostics(bool night, bool off, bool benchmark, bool stress) {
        lighting_night_=night || benchmark; traffic_lights_off_=off;
        lighting_benchmark_=benchmark; lighting_stress_=stress;
    }

    // Capture the final bounded-smoke frame as a BMP for visual regression
    // checks. Ignored for an unbounded interactive run.
    // Where this session's per-frame performance CSV goes. Empty disables the
    // recorder entirely, and a disabled recorder does no per-frame work.
    void set_perf_log_path(std::string path) { perf_log_path_ = std::move(path); }
    // False disables recording outright. When true and no path was given, the
    // app picks one next to the save game — see default_perf_log_path().
    void set_perf_logging(bool on) { perf_logging_ = on; }
    void set_perf_spike_ms(double ms) { perf_spike_ms_ = ms; }

    void set_screenshot_path(std::string path) {
        screenshot_path_ = std::move(path);
    }

private:
    void init_save_game();
    bool save_game();
    bool load_game();
    void begin_new_game();
    void finish_opening();
    bool begin_delivery_cutscene();
    void tick_delivery_check();
    void render_opening();
    std::string save_path_,save_notice_;
    MissionStage mission_stage_=MissionStage::Opening;
    CurrentRoadName current_road_name_;
    CutscenePlayer opening_cutscene_;
    bool opening_preview_=false;
    bool delivery_preview_=false,delivery_cutscene_=false;
    bool delivery_check_=false,delivery_check_failed_=false,delivery_check_passed_=false;
    float delivery_check_pause_time_=0;
    glm::vec3 delivery_check_position_{};
    InputFrame signal_check_input();
    // Stages a departure change at one (lane_key, slot) against the real
    // ambient reconciliation. See character_identity_check.cpp.
    void character_identity_check();
    void sync_ambient_for_check(const std::vector<PedAgent>& agents,
                                double seconds);
    InputFrame tire_track_check_input() const;
    void tire_track_check_camera();
    void signal_check_camera();
    void capture_signal_check();
    bool signal_check_=false, signal_check_failed_=false, signal_check_done_=false;
    bool character_identity_check_=false, character_identity_check_failed_=false,
         character_identity_check_done_=false;
    bool tire_track_check_ = false;
    bool overhead_qa_=false;
    bool daylight_qa_=false;
    bool road_start_qa_=false;
    int signal_check_ticks_=0;
    int signal_check_fixture_kind_=0;
    std::size_t signal_check_index_=0;
    glm::vec3 signal_check_base_{0.0f}, signal_check_direction_{0,0,-1};
    std::string signal_check_capture_;
    TiledLighting tiled_lighting_;
    bool lighting_night_=false, traffic_lights_off_=false;
    bool lighting_benchmark_=false, lighting_stress_=false;
    std::vector<double> light_grid_ms_;
    std::vector<double> light_build_ms_, light_upload_ms_;
    std::size_t lighting_source_count_=0;
    void poll_events();
    void process_ui_input(float dt);
    void render();
    bool save_screenshot(const std::string& path);
    void begin_bug_report();
    void capture_and_submit_bug_report();
    void update_camera(float dt);
    void set_driving_mechanics(DrivingMechanicsStyle style);
    void update_weather(bool step_snowpack = false);
    void apply_ui_settings();
    bool place_character_next_to_car(bool require_clear = false);
    void toggle_player_mode();
    bool nearby_aircraft() const;
    bool nearby_helicopter() const;
    bool nearby_boat() const;
    bool nearby_bent_elbow() const;
    void drink_at_bent_elbow();
    bool toggle_boat();
    void step_boat_transition();
    void reset_boat();
    void run_boat_check();
    void reset_aircraft();
    void run_aircraft_check();
    void reset_helicopter();
    void run_helicopter_check();
    void run_vehicle_entry_check();
    void run_driver_transition_check();
    InputFrame house_check_input();
    void update_house_check();
    void capture_house_check();
    bool house_check_=false;
    bool house_check_started_=false;
    bool house_check_complete_=false;
    bool house_check_failed_=false;
    std::size_t house_check_stage_=0;
    int house_check_ticks_=0;
    unsigned house_check_opened_=0;
    std::string house_check_capture_;
    struct VehicleEntryTarget {
        enum class Kind { None, Current, Parked, Traffic } kind = Kind::None;
        std::size_t parked_index = 0;
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        PlayerCarId model = PlayerCarId::LegacyCar5;
        bool locked = false;
    };
    VehicleEntryTarget nearby_vehicle() const;
    bool take_nearby_vehicle(const VehicleEntryTarget& target);
    bool begin_vehicle_transition(const VehicleEntryTarget& target, bool entering);
    bool vehicle_transition_door_clear(PlayerCarId car, const Transform& body) const;
    void step_vehicle_transition();
    bool vehicle_transition_route_clear(glm::vec3 from, glm::vec3 to, float ground) const;
    void cancel_vehicle_transition();
    void park_current_vehicle();
    void sync_current_vehicle_obstacle();
    glm::vec3 player_focus_position() const;
    glm::vec3 player_focus_forward() const;
    std::vector<VisiblePoliceIdentity> visible_police(
        glm::vec3 target, bool witness_only = false) const;
    void check_police_driving_offenses();
    void check_police_armed_offense(bool player_armed);
    void check_police_collision_offenses();
    void check_police_shots();
    void check_police_arrest(const std::vector<VisiblePoliceIdentity>& visible);
    bool player_has_drawn_weapon() const;
    float current_speed_limit_mps() const;
    InputFrame police_officer_check_input();
    InputFrame police_pursuit_check_input();
    InputFrame traffic_horn_check_input();
    void traffic_horn_check_camera();
    void capture_traffic_horn_check();
    void capture_convertible_check();
    void police_officer_check_camera();
    void capture_police_officer_check();
    SkyEnv current_sky_env() const;

    // Drain and log the GL error queue. Returns how many were found.
    int drain_gl_errors(const char* where);

    Window window_;
    InputMapper input_;
    FixedStep clock_;
    Camera camera_;
    ChaseCameraRig chase_camera_;
    float camera_obstruction_distance_ = -1.0f;
    float camera_frame_dt_ = 0.0f;
    uint32_t seen_impact_count_ = 0;
    float impact_feedback_seconds_ = 0.0f;

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
    uint64_t new_game_seed_ = 0xA5EED0FFC0FFEE11ull;

    TerrainCollider collider_{0};

    // The drive. There is no game layer between this and the physics yet — the
    // rally that used to sit here is gone (PENG-23) and Pinatty
    // (docs/design/pinatty.md) has no code. So App steps the vehicle directly,
    // which is exactly as much game as this build has.
    DrivingMechanicsStyle driving_mechanics_style_ =
        DrivingMechanicsStyle::ClassicGta;
    VehicleTuning tuning_;
    VehicleState car_;

    // The car one sim step ago. Render interpolates between this and the
    // current state by clock_.alpha(), which is what stops a 120 Hz sim from
    // visibly juddering on a 144 Hz panel.
    VehicleState prev_car_;

    // The game starts on foot beside the parked car. E / controller A swaps
    // between this deterministic controller and the existing vehicle sim.
    CharacterTuning character_tuning_;
    PlayerCharacterState player_character_;
    PlayerCharacterState prev_player_character_;
    DrunkState drunk_;
    BankVaultState bank_vault_;
    BankInteraction bank_interaction_;
    bool bank_input_consumed_ = false;
    WeaponWheel weapon_wheel_;
    WeaponUseState weapon_use_;
    PcmClip weapon_shot_clip_,weapon_reload_clip_;
    bool weapon_aim_mouse_=false, weapon_aim_pad_=false, weapon_aim_toggle_=false;
    bool weapon_fire_pad_=false, weapon_fire_pending_=false, weapon_reload_pending_=false;
    bool weapon_focus_=true;
    unsigned weapon_shots_=0;
    unsigned weapon_body_hits_=0;
    // Bodies, not hits. Counted apart because three rounds into one person and
    // one round into each of three people are the same number of body hits and
    // very different things to have done.
    unsigned weapon_kills_=0;
    float weapon_hit_feedback_=0.f;
    glm::vec3 weapon_socket_player_position_{0.f};
    float weapon_socket_player_yaw_=0.f;
    void step_weapon_use(bool available, float dt);
    void tick_weapon_hit_check();
    void capture_weapon_hit_check();
    // --damage-check. See src/app/damage_check.cpp for what it drives and why
    // a headless suite cannot replace it.
    bool damage_check_=false;
    bool damage_check_done_=false, damage_check_failed_=false;
    int damage_check_stage_=0;
    int damage_check_rounds_=0;
    int damage_check_last_shot_frame_=-100;
    int damage_check_last_restage_frame_=-100;
    float damage_check_last_health_=kBodyHealth;
    int damage_check_stage_frame_=0;
    uint64_t damage_check_lane_=0;
    uint32_t damage_check_slot_=0;
    glm::vec3 damage_check_body_{0.f};
    glm::vec3 damage_check_death_position_{0.f};
    void tick_damage_check();
    void capture_damage_check();

    bool weapon_hit_check_done_=false, weapon_hit_check_failed_=false;
    uint64_t weapon_hit_check_lane_=0;
    uint32_t weapon_hit_check_slot_=0;
    unsigned weapon_hit_check_start_hits_=0;

    WeaponVisual weapon_visual_;
    PoliceWeaponVisual police_weapon_visual_;
    bool weapon_restore_mouse_=false,weapon_input_consumed_=false;
    bool weapon_check_=false;
    unsigned weapon_check_captures_=0;
    VehicleLeakWarning vehicle_leak_warning_;
    float repair_shop_feedback_s_=0;
    float mission_success_feedback_s_=0;
    RepairShopVisit repair_shop_visit_;
    bool on_foot_ = true;
    VehicleTransitionState vehicle_transition_;
    bool transition_waiting_=false;
    bool transition_enters_mission_car_=false;
    PlayerCharacterState transition_start_, transition_door_;
    glm::vec3 transition_car_position_{0};
    glm::quat transition_car_rotation_{1,0,0,0};
    Camera transition_camera_;
    float transition_camera_release_ = 0;
    bool in_aircraft_ = false;
    bool in_helicopter_ = false;
    bool in_boat_ = false;
    TrailerState trailer_,prev_trailer_;
    TrailerVisual trailer_visual_;
    std::array<std::size_t,3> trailer_colliders_{static_cast<std::size_t>(-1),static_cast<std::size_t>(-1),static_cast<std::size_t>(-1)};
    bool trailer_check_=false,trailer_check_ran_=false,trailer_check_passed_=false;
    void reset_freight_yard(bool add_tractor);
    void spawn_freight_tractor();
    void enable_trailer_collision(bool enabled);
    void sync_trailer_collision();
    void toggle_trailer();
    void drop_trailer();
    void step_trailer();
    void run_trailer_check();
    BoatState boat_,prev_boat_;
    BoatTransitionState boat_transition_;
    PlayerCharacterState boat_shore_pose_;
    Camera boat_transition_camera_;
    bool boat_check_=false,boat_check_ran_=false,boat_check_passed_=false;
    AircraftState aircraft_, prev_aircraft_;
    bool aircraft_check_=false, aircraft_check_ran_=false, aircraft_check_passed_=false;
    HelicopterState helicopter_, prev_helicopter_;
    bool helicopter_check_=false, helicopter_check_ran_=false,
         helicopter_check_passed_=false;
    bool character_spawned_ = false;
    struct ParkedVehicle {
        PlayerCarVisual visual;
        VehicleState state;
        VehicleTuning tuning;
        std::size_t collider = static_cast<std::size_t>(-1);
    };
    std::vector<ParkedVehicle> parked_vehicles_;
    std::size_t current_vehicle_collider_ = static_cast<std::size_t>(-1);
    std::string vehicle_interaction_notice_;
    bool vehicle_entry_check_=false, vehicle_entry_check_passed_=false;
    bool driver_transition_check_=false;
    unsigned driver_check_stage_=0, driver_check_starts_=0;
    uint64_t driver_check_started_=0;
    glm::vec3 driver_check_previous_eye_{0};
    float driver_check_min_camera_distance_=100.f, driver_check_max_camera_step_=0;
    unsigned driver_check_camera_captures_=0;
    uint64_t vehicle_notice_until_ = 0;
    float character_look_dx_pending_ = 0.0f;
    float character_look_dy_pending_ = 0.0f;

    // Sim steps elapsed. The authoritative sim clock, and the key the weather
    // is drawn from — conditions_at() is a pure function of (seed, step), so
    // the sky and the grip agree with each other on every machine.
    uint64_t step_index_ = 0;
    Conditions conditions_;
    SnowpackState snowpack_;
    SnowClearanceField snow_clearance_;
    SnowShelterField snow_shelter_;
    SnowplowService snowplow_service_;
    bool snowplow_service_active_ = false;
    bool snowplow_check_ = false;
    float snowplow_refill_preview_seconds_ = 0.0f;
    bool snowplow_refill_preview_applied_ = false;
    glm::vec2 dev_tornado_center_m_{0.0f};
    bool dev_tornado_center_set_ = false;

    Scene scene_;
    Renderer renderer_;
    Sky sky_;
    Ocean ocean_;
    Precipitation rain_;
    TireTracks tire_tracks_;
    TornadoRenderer tornado_;
    Hud hud_;
    LoadingScreen loading_screen_;
    UiFlow ui_;
    UiSettings applied_ui_settings_;
    bool ui_settings_applied_ = false;
    GameUi game_ui_;
    WantedSystem wanted_;
    // The report-pending blink latch (PENG-46): armed on a fresh crime,
    // cleared the step the dispatch radio fires.
    bool wanted_report_blink_ = false;
    PoliceOffenseTracker police_offenses_;
    PoliceArrestTracker police_arrest_;
    float arrested_feedback_s_=0.0f;
    unsigned police_arrest_reports_=0;
    unsigned police_red_light_reports_=0;
    unsigned police_stop_sign_reports_=0;
    unsigned police_speeding_reports_=0;
    unsigned police_armed_reports_=0;
    unsigned police_collision_reports_=0;
    unsigned police_shot_reports_=0;
    unsigned pedestrian_kill_reports_=0;
    unsigned player_death_reports_=0;
    unsigned player_run_down_reports_=0;

    // The player's hundred points and the span of being dead. See
    // game/player_vitals.h for why the span exists; the short version is that
    // health used to hit zero and respawn in the same statement, so there was
    // no state in which the player was dead and nothing else could hook into
    // it. src/app/player_damage.cpp is the only file that writes this.
    PlayerVitals player_vitals_;
    // Downward speed on the last airborne step, kept because the character
    // solver has already zeroed it by the time `grounded` goes true.
    float player_landing_speed_mps_=0.0f;
    // Red edge flash, from ANY damage source rather than only a police round.
    float player_hit_feedback_s_=0.0f;
    bool damage_player(float amount, const char* cause);
    void begin_player_death(const char* cause);
    void step_player_vitals(float dt);
    void check_player_fall_damage(bool was_grounded);
    void check_player_crash_damage(float impact_speed_mps);
    void check_pedestrian_casualties();
    void check_on_foot_traffic_hits();
    // One footfall's worth of ground, probed after the character has been
    // stepped. Its own function because it owes one collider probe and only
    // wants to spend it when the character is actually walking.
    void update_footstep_audio();
    void throw_player_punch();
    bool police_officer_check_=false;
    bool police_pursuit_check_=false;
    bool police_pursuit_check_saw_yield_=false;
    bool police_pursuit_check_saw_merge_=false;
    bool police_pursuit_check_saw_bypass_=false;
    VisiblePoliceIdentity police_pursuit_check_yield_unit_{};
    glm::vec3 police_pursuit_check_yield_position_{0};
    LaneRef police_pursuit_check_start_lane_=kInvalidLane;
    bool police_officer_check_done_=false;
    bool police_officer_check_failed_=false;
    int police_officer_check_stage_=0;
    uint64_t police_officer_check_tick_=0;
    uint64_t police_officer_check_stage_tick_=0;
    VisiblePoliceIdentity police_officer_check_unit_{};
    unsigned police_officer_check_phases_=0;
    bool police_officer_check_foot_target_set_=false;
    std::string police_officer_check_capture_;
    bool convertible_check_=false, convertible_check_failed_=false;
    unsigned convertible_check_captures_=0u;
    bool traffic_horn_check_=false, traffic_horn_check_done_=false, traffic_horn_check_failed_=false;
    int traffic_horn_check_stage_=0;
    uint64_t traffic_horn_check_tick_=0, traffic_horn_check_stage_tick_=0, traffic_horn_check_seen_=0;
    VisiblePoliceIdentity traffic_horn_check_driver_{};
    std::string traffic_horn_check_capture_;
    DevMenu dev_menu_;
    BugReportUi bug_report_;
    bool bug_report_restore_mouse_ = false;
    bool bug_report_capture_pending_ = false;
    int bug_report_submit_frame_ = -1;
    World world_;
    bool interior_presentation_lod_ = false;
    int last_ui_pointer_x_ = -1;
    int last_ui_pointer_y_ = -1;

    // Wheel-less alpha body plus four independent wheels. The host visual reads
    // the real suspension, steer and spin fields without feeding anything back
    // into the deterministic vehicle state.
    PlayerCarVisual car_visual_;
    CharacterVisual character_visual_;
    TrafficVisual traffic_visual_;
    VehicleEffects vehicle_effects_;

    // Hardware ownership stays in App; the parameter mapping and voice
    // lifetime stay in the headless-testable audio module.
    AudioDevice audio_device_;
    CityAudio city_audio_;
    IntroAudio intro_audio_;
    RainAudio rain_audio_;
    FootstepAudio footstep_audio_;
    FootstepTuning footstep_tuning_;
    VehicleAudio vehicle_audio_;
    TrafficIdleAudio traffic_idle_audio_;
    TrafficHornAudio traffic_horn_audio_;
    bool player_horn_pending_=false;
    PoliceSiren police_siren_;
    bool police_emergency_enabled_=false;
    // J does double duty: a siren in a cruiser, the folding top in the Mistral.
    // No car is both, so the two never contend for the key.
    MistralSoftTop soft_top_;
    bool soft_top_toggle_pending_=false;
    bool police_check_=false;
    int start_wanted_level_=0;
    unsigned police_check_captures_=0;
    std::vector<TrafficSpotLight> emergency_light_sources_;

    // Teleport the car across the island and refill before resuming. Bound to a
    // key so the cold-fill path is exercisable by hand, because it is the path
    // a mission warp will take and it must not be first run in anger.
    void teleport(glm::vec3 to, float heading_radians = 0.0f);
    bool teleport_requested_ = false;
    int warp_interval_ = 0;
    glm::vec2 start_position_{city::kOpeningMissionCarPosition.x,
                             city::kOpeningMissionCarPosition.z};
    float start_heading_radians_ = city::kOpeningMissionCarHeading;
    glm::vec2 start_player_position_{0};
    bool start_player_position_set_=false;
    float start_player_height_=0;
    bool start_player_height_set_=false;
    PlayerCarId start_car_=PlayerCarId::LegacyCar5;
    bool start_driving_=false;
    bool clear_weather_=false;
    int warps_done_ = 0;

    // What the last fill cost, in wall milliseconds. App owns the only clock in
    // the program, so this is measured here and not inside World.
    double last_fill_ms_ = 0.0;
    int last_fill_steps_ = 0;
    // Which frame the fill above was charged to, so the per-frame performance
    // log can report it once instead of forever.
    int last_fill_frame_ = -2;

    // Frame costs, measured here for the same reason. Display only; neither
    // ever reaches the sim, which sees a constant dt and nothing else.
    double cull_ms_ = 0.0;
    double mesh_ms_ = 0.0;

    // The phase breakdown, measured in App because App owns the program's only
    // clock. Display and log only; none of it ever reaches the sim.
    double sim_ms_ = 0.0;
    // Inside the sim step. Accumulated ACROSS the steps a frame owes, so they
    // are comparable with sim_ms_ directly rather than per-step.
    double sim_traffic_ms_ = 0.0;
    double sim_police_ms_ = 0.0;
    double sim_character_ms_ = 0.0;

    // Inside sim_police_ms_. visible_police() is const and is reached from
    // five different callers per step, so it accounts for ITSELF rather than
    // being bracketed at each site — a bracket per call site is a bracket
    // somebody forgets to add to the sixth caller.
    mutable double police_vis_ms_ = 0.0;
    mutable int police_vis_calls_ = 0;
    double police_ctx_ms_ = 0.0;
    double visual_ms_ = 0.0;
    double scene_ms_ = 0.0;
    double render_ms_ = 0.0;
    double swap_ms_ = 0.0;
    GpuTimer gpu_timer_;
    double peak_cull_ms_ = 0.0;
    double peak_mesh_ms_ = 0.0;
    int stream_spikes_ = 0;

    overlay::Controls controls_;

    bool running_ = false;
    int frame_limit_ = 0;
    std::string screenshot_path_;
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

    // The per-frame performance recorder. Fed from the TOP of the loop, where
    // the members above still hold the frame that the measured delta paid for
    // — see record_frame_sample().
    FrameLog perf_log_;
    std::string perf_log_path_;
    double perf_spike_ms_ = 20.0;
    // Recording is opt-in now: the F1 menu opens the log mid-session rather
    // than every launch paying for one. --perf-log still forces it on.
    bool perf_logging_ = false;
    bool god_mode_ = false;
    bool vehicle_god_mode_ = false;
    void set_frame_logging(bool on);
    bool perf_mark_pending_ = false;
    bool perf_clock_reset_ = false;
    // Seconds left on the on-screen confirmation after an F4 press.
    float perf_mark_feedback_s_ = 0.0f;
    // Just the session number out of the path, for the HUD badge.
    std::string perf_log_label_;
    int perf_marks_ = 0;
    int perf_spikes_logged_ = 0;

    // Assemble one FrameSample from everything the app knows and hand it to
    // perf_log_. `ms` is what the PREVIOUS frame cost.
    void record_frame_sample(double ms);
};

}  // namespace apricot

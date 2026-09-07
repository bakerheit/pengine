#pragma once

#include <cstddef>

#include <glm/glm.hpp>

#include "audio/mixer.h"
#include "audio/synth.h"
#include "audio/vehicle_sound_profile.h"

namespace apricot {

// Presentation state for the player's car. It contains no input-device or
// playback-device types, so the whole mapping remains headless-testable.
struct VehicleAudioFrame {
    bool active = true;
    bool engine_running = true;
    float dt_seconds = 0.0f;
    float engine_rpm = 900.0f;
    bool accelerating = false;
    float handbrake = 0.0f;
    float burnout_amount = 0.0f;
    float drift_slip = 0.0f;
    float speed_mps = 0.0f;
    int gear = 1;
    glm::vec3 position{0.0f};
};

// One menu audition. Kept as a pure lookup so tests can prove every visible
// menu choice points at real PCM without needing an audio device.
VoiceMix car_sound_audition_mix(const SfxBank& bank, CarSoundUse use,
                                int variant);
int acceleration_sound_for_gear(int gear);
float recorded_throttle_pitch(float engine_rpm);
float recorded_shift_gain(float seconds_since_shift);
float recorded_collision_gain(float impact_mps);

// Owns recorded ignition, idle and acceleration for the current player car. Procedural engine,
// tyre and surface sounds are deliberately absent from normal driving:
// if a real recording has not been selected yet, silence is better than a fake
// placeholder. Car-to-car impacts use one dedicated real recording, and the
// remaining recorded candidates stay available in the F1 sound lab.
class VehicleAudio {
public:
    bool start(VoiceMixer& mixer, const SfxBank& bank);
    void stop();

    void set_listener(const Listener& listener);
    // Call on model selection and before enter_vehicle on entry/theft. Accepts
    // the catalog mesh_path or canonical model key; safe before start(). Live
    // loops retain phase and use the mixer's per-sample pitch smoothing.
    // In-flight one-shots finish at their latched pitch to preserve timing.
    void set_model(std::string_view mesh_path) {
        profile_ = vehicle_sound_profile(mesh_path);
    }
    void update(const VehicleAudioFrame& frame);
    // Called only after successful entry/exit. Traffic already has a driver
    // and a running engine; an unattended parked car needs the ignition clip.
    void enter_vehicle(bool already_running, glm::vec3 position);
    void exit_vehicle();
    bool starting() const { return startup_remaining_ > 0.0f; }
    unsigned startup_count() const { return startup_count_; }
    void play_car_collision(float impact_mps, glm::vec3 position);
    void audition(CarSoundUse use, int variant);

    bool started() const { return mixer_ != nullptr && bank_ != nullptr; }
    std::size_t loop_count() const;

private:
    void open_throttle_loop(glm::vec3 position, float pitch);

    VoiceMixer* mixer_ = nullptr;
    VehicleSoundProfile profile_{"default", 1.0f};
    const SfxBank* bank_ = nullptr;
    VoiceHandle throttle_hold_{};
    VoiceHandle idle_{};
    VoiceHandle drift_tyres_{};
    VoiceHandle burnout_{};
    OneShotHandle startup_voice_{};
    float startup_remaining_ = 0.0f;
    float startup_pitch_ = 1.0f;
    unsigned startup_count_ = 0;
    OneShotHandle acceleration_voice_{};
    OneShotHandle collision_voice_{};
    bool was_accelerating_ = false;
    float throttle_seconds_ = 0.0f;
    float attack_pitch_ = 1.0f;
    float shift_elapsed_seconds_ = 1.0f;
    int previous_gear_ = 1;
};

}  // namespace apricot

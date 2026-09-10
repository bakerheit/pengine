#include "audio/vehicle_audio.h"

#include <algorithm>

namespace apricot {
namespace {

constexpr float kCrashGainBoost = 1.25f;

bool has_recorded_car_audio(const SfxBank& bank) {
    if (!bank.engine_start.empty() || !bank.engine_idle.empty()) return true;
    for (const auto& horn : bank.traffic_horns) if (!horn.empty()) return true;
    if (!bank.player_throttle_attack.empty() ||
        !bank.player_throttle_hold.empty() ||
        !bank.player_throttle_release.empty() ||
        !bank.player_car_collision.empty() ||
        !bank.player_drift_tyres.empty() || !bank.player_burnout.empty()) {
        return true;
    }
    for (const auto& group : bank.car_sound_audition) {
        for (const PcmClip& clip : group) {
            if (!clip.empty()) return true;
        }
    }
    return false;
}

std::size_t clamped_variant(int variant) {
    return static_cast<std::size_t>(
        std::clamp(variant, 0, kCarSoundVariantCount - 1));
}

}  // namespace

VoiceMix car_sound_audition_mix(const SfxBank& bank, CarSoundUse use,
                                int variant) {
    const std::size_t group = std::min(
        static_cast<std::size_t>(use), kCarSoundUseCount - 1u);
    const std::size_t v = clamped_variant(variant);
    VoiceMix mix;
    mix.clip = &bank.car_sound_audition[group][v];
    mix.gain = mix.clip->empty() ? 0.0f : (use == CarSoundUse::Tyres ? 1.0f : kVehicleSoundGain);
    if (use == CarSoundUse::Crash) mix.gain *= kCrashGainBoost;
    mix.pitch = 1.0f;
    return mix;
}

int acceleration_sound_for_gear(int gear) {
    if (gear < 1) return -1;
    return std::clamp(gear - 1, 0, kCarSoundVariantCount - 1);
}

float recorded_throttle_pitch(float engine_rpm) {
    constexpr float kIdleRpm = 900.0f;
    constexpr float kRedlineRpm = 6800.0f;
    const float rpm01 = std::clamp(
        (engine_rpm - kIdleRpm) / (kRedlineRpm - kIdleRpm), 0.0f, 1.0f);
    return 0.78f + 0.56f * rpm01;
}

float recorded_shift_gain(float seconds_since_shift) {
    constexpr float kShiftRecoverySeconds = 0.18f;
    const float recovery = std::clamp(
        seconds_since_shift / kShiftRecoverySeconds, 0.0f, 1.0f);
    return 0.18f + 0.82f * recovery;
}

float recorded_collision_gain(float impact_mps) {
    constexpr float kQuietImpactMps = 2.5f;
    constexpr float kFullImpactMps = 15.0f;
    if (!(impact_mps > kQuietImpactMps)) return 0.0f;
    const float severity = std::clamp(
        (impact_mps - kQuietImpactMps) /
            (kFullImpactMps - kQuietImpactMps),
        0.0f, 1.0f);
    return 0.22f + 0.73f * severity;
}

bool VehicleAudio::start(VoiceMixer& mixer, const SfxBank& bank) {
    stop();
    mixer_ = &mixer;
    bank_ = &bank;

    // A silent or failed AudioDevice owns an empty bank. That is a supported
    // outcome, not an error; leave this controller inert and cheap.
    if (!has_recorded_car_audio(bank)) {
        mixer_ = nullptr;
        bank_ = nullptr;
        return false;
    }
    open_throttle_loop(glm::vec3{0.0f}, recorded_throttle_pitch(900.0f) * profile_.pitch);
    VoiceParams idle;
    idle.category = Category::Engine;
    idle.gain = 0.0f;
    idle.pitch = profile_.pitch;
    idle.spatial = true;
    idle.attenuation = {5.0f, 65.0f, 1.0f};
    idle_ = mixer_->open_loop(&bank.engine_idle, idle);
    VoiceParams tyres;
    tyres.category = Category::Tyres;
    tyres.gain = 0.0f;
    tyres.pitch = 1.0f;
    tyres.spatial = true;
    tyres.attenuation = {5.0f, 80.0f, 0.85f};
    drift_tyres_ = mixer_->open_loop(&bank.player_drift_tyres, tyres);
    burnout_ = mixer_->open_loop(&bank.player_burnout, tyres);
    return true;
}

void VehicleAudio::exit_vehicle() {
    stop_horn();
    if (!mixer_) return;
    mixer_->stop_oneshot(startup_voice_);
    mixer_->stop_oneshot(acceleration_voice_);
    startup_voice_ = {};
    acceleration_voice_ = {};
    startup_remaining_ = 0.0f;
    was_accelerating_ = false;
    throttle_seconds_ = 0.0f;
    VoiceParams quiet;
    quiet.category = Category::Engine;
    quiet.gain = 0.0f;
    mixer_->set_loop(idle_, quiet);
    mixer_->set_loop(throttle_hold_, quiet);
    quiet.category = Category::Tyres;
    mixer_->set_loop(drift_tyres_, quiet);
    mixer_->set_loop(burnout_, quiet);
}

void VehicleAudio::enter_vehicle(bool already_running, glm::vec3 position) {
    exit_vehicle();
    if (!mixer_ || !bank_ || already_running || bank_->engine_start.empty()) return;
    VoiceParams p;
    p.category = Category::Engine;
    p.gain = .65f * kVehicleSoundGain;
    startup_pitch_ = profile_.pitch;
    p.pitch = startup_pitch_;
    p.spatial = true;
    p.position = position;
    p.attenuation = {5.0f, 65.0f, 1.0f};
    startup_voice_ = mixer_->play_oneshot(&bank_->engine_start, p);
    startup_remaining_ = bank_->engine_start.duration_seconds() / startup_pitch_;
    ++startup_count_;
}

void VehicleAudio::open_throttle_loop(glm::vec3 position, float pitch) {
    if (!mixer_ || !bank_ || bank_->player_throttle_hold.empty()) return;
    VoiceParams p;
    p.category = Category::Engine;
    p.gain = 0.0f;
    p.pitch = pitch;
    p.looping = true;
    p.spatial = true;
    p.position = position;
    p.attenuation.ref_distance = 5.0f;
    p.attenuation.max_distance = 90.0f;
    p.attenuation.rolloff = 0.75f;
    throttle_hold_ = mixer_->open_loop(&bank_->player_throttle_hold, p);
}

void VehicleAudio::stop() {
    stop_horn();
    horn_count_ = 0;
    if (mixer_) {
        mixer_->stop_oneshot(acceleration_voice_);
        mixer_->stop_oneshot(collision_voice_);
        mixer_->close_loop(throttle_hold_);
        mixer_->close_loop(idle_);
        mixer_->close_loop(drift_tyres_);
        mixer_->close_loop(burnout_);
        mixer_->stop_oneshot(startup_voice_);
    }
    throttle_hold_ = VoiceHandle{};
    idle_ = {};
    drift_tyres_ = {};
    burnout_ = {};
    startup_voice_ = {};
    startup_remaining_ = 0.0f;
    startup_count_ = 0;
    acceleration_voice_ = OneShotHandle{};
    collision_voice_ = OneShotHandle{};
    was_accelerating_ = false;
    throttle_seconds_ = 0.0f;
    shift_elapsed_seconds_ = 1.0f;
    previous_gear_ = 1;
    mixer_ = nullptr;
    bank_ = nullptr;
}

void VehicleAudio::set_listener(const Listener& listener) {
    if (mixer_) mixer_->set_listener(listener);
}

void VehicleAudio::update(const VehicleAudioFrame& frame) {
    if (!mixer_ || !bank_) return;

    if (!frame.active || !frame.horn_available) {
        stop_horn();
    } else {
        horn_remaining_=std::max(0.f,horn_remaining_-std::clamp(frame.dt_seconds,0.f,.1f));
        if (horn_remaining_<=0.f) stop_horn();
        if (frame.horn_pressed && horn_remaining_<=0.f) {
            const auto model=profile_.model_key;
            const bool truck=model=="car8" || model=="firetruck" || model=="harrow_workman" ||
                model=="harrow_cityliner" || model=="harrow_hauler" || model=="harrow_parcel";
            const std::size_t clip=truck ? 2u : (profile_.pitch>1.05f ? 1u : 0u);
            if (!bank_->traffic_horns[clip].empty()) {
                horn_params_={};
                horn_params_.category=Category::World;
                horn_params_.spatial=true;
                horn_params_.position=frame.position+glm::vec3{0,.8f,0};
                horn_params_.attenuation={4.f,70.f,1.1f};
                horn_params_.gain=.8f;
                horn_params_.pitch=std::clamp(profile_.pitch,.94f,1.06f);
                horn_voice_=mixer_->play_oneshot(&bank_->traffic_horns[clip],horn_params_);
                if (horn_voice_.valid()) {
                    horn_remaining_=bank_->traffic_horns[clip].duration_seconds()/horn_params_.pitch+.12f;
                    ++horn_count_;
                }
            }
        }
        if (horn_voice_.valid()) {
            horn_params_.position=frame.position+glm::vec3{0,.8f,0};
            mixer_->set_oneshot(horn_voice_,horn_params_);
        }
    }

    if (!frame.active || !frame.engine_running) {
        mixer_->stop_oneshot(startup_voice_);
        startup_voice_ = {};
        startup_remaining_ = 0.0f;
    }
    const bool cranking = starting();
    const bool audible = frame.active && frame.engine_running;
    const bool driving = audible && !cranking;
    if (cranking) {
        VoiceParams p;
        p.category = Category::Engine;
        p.gain = .65f * kVehicleSoundGain;
        p.pitch = startup_pitch_;
        p.spatial = true;
        p.position = frame.position;
        p.attenuation = {5.0f, 65.0f, 1.0f};
        mixer_->set_oneshot(startup_voice_, p);
    }
    VoiceParams idle;
    idle.category = Category::Engine;
    // The same gentle RPM response around each model's idle note.
    idle.pitch = profile_.pitch * std::clamp(frame.engine_rpm / 900.0f, .92f, 1.12f);
    const float rpm_idle_weight = std::clamp((2200.0f - frame.engine_rpm) / 1300.0f, 0.0f, 1.0f);
    const float idle_weight = frame.accelerating ? rpm_idle_weight : 1.0f;
    const float start_fade = std::clamp(1.0f - startup_remaining_ / .15f, 0.0f, 1.0f);
    idle.gain = audible ? kVehicleSoundGain * .39f * idle_weight * start_fade * (frame.accelerating ? .25f : 1.0f) : 0.0f;
    idle.spatial = true;
    idle.position = frame.position;
    idle.attenuation = {5.0f, 65.0f, 1.0f};
    mixer_->set_loop(idle_, idle);

    const float speed_gate =
        std::clamp((frame.speed_mps - 2.0f) / 5.0f, 0.0f, 1.0f);
    const float slip_amount =
        std::clamp((frame.drift_slip - 0.75f) / 1.25f, 0.0f, 1.0f);
    const float handbrake_amount =
        std::clamp(frame.handbrake, 0.0f, 1.0f) * speed_gate;
    const float drift_amount = std::max(slip_amount, handbrake_amount);
    VoiceParams tyres;
    tyres.category = Category::Tyres;
    tyres.gain = driving ? 0.58f * drift_amount * speed_gate : 0.0f;
    tyres.pitch = 0.92f + 0.22f * drift_amount +
                  0.06f * std::clamp(frame.speed_mps / 35.0f, 0.0f, 1.0f);
    tyres.lp_cutoff_hz = 1800.0f + 6200.0f * drift_amount;
    tyres.spatial = true;
    tyres.position = frame.position;
    tyres.attenuation = {5.0f, 80.0f, 0.85f};
    mixer_->set_loop(drift_tyres_, tyres);
    // Grounded driven-wheel spin stays audible even with no road speed.
    tyres.gain = driving ? 0.29f * std::clamp(frame.burnout_amount, 0.0f, 1.0f) : 0.0f;
    tyres.pitch = 1.0f;
    tyres.lp_cutoff_hz = 0.0f;
    mixer_->set_loop(burnout_, tyres);

    const bool should_play_acceleration =
        driving && frame.accelerating && frame.gear != 0;
    const bool acceleration_edge =
        should_play_acceleration && !was_accelerating_;
    const bool release_edge = driving && frame.gear != 0 &&
                              !frame.accelerating && was_accelerating_;
    const bool gear_changed = should_play_acceleration && was_accelerating_ &&
                              previous_gear_ >= 1 &&
                              frame.gear != previous_gear_;

    if (!audible || cranking || frame.gear == 0) {
        mixer_->stop_oneshot(acceleration_voice_);
        acceleration_voice_ = OneShotHandle{};
    }
    if (!frame.active) {
        mixer_->stop_oneshot(collision_voice_);
        collision_voice_ = OneShotHandle{};
    }
    if (acceleration_edge) {
        // A muted loop still advances in the mixer. Restart this dynamic rev
        // pass at the pedal edge so it never fades in at an arbitrary point.
        mixer_->close_loop(throttle_hold_);
        throttle_hold_ = VoiceHandle{};
        open_throttle_loop(frame.position,
                           recorded_throttle_pitch(frame.engine_rpm) * profile_.pitch);
        mixer_->stop_oneshot(acceleration_voice_);
        VoiceParams p;
        p.category = Category::Engine;
        p.gain = kVehicleSoundGain;
        attack_pitch_ = profile_.pitch;
        p.pitch = attack_pitch_;
        p.spatial = true;
        p.position = frame.position;
        p.attenuation.ref_distance = 5.0f;
        p.attenuation.max_distance = 90.0f;
        p.attenuation.rolloff = 0.75f;
        acceleration_voice_ = mixer_->play_oneshot(
            &bank_->player_throttle_attack, p);
        throttle_seconds_ = 0.0f;
    } else if (release_edge) {
        mixer_->stop_oneshot(acceleration_voice_);
        VoiceParams p;
        p.category = Category::Engine;
        p.gain = kVehicleSoundGain;
        // Let the real falling-rev recording breathe. At its native speed the
        // whole overrun passes in barely two seconds and sounds like a switch
        // being flipped; 0.72x stretches it to about 2.85 seconds while also
        // giving the lift-off the lower note a coasting engine should have.
        p.pitch = 0.72f * profile_.pitch;
        p.spatial = true;
        p.position = frame.position;
        p.attenuation.ref_distance = 5.0f;
        p.attenuation.max_distance = 90.0f;
        p.attenuation.rolloff = 0.75f;
        acceleration_voice_ = mixer_->play_oneshot(
            &bank_->player_throttle_release, p);
    }

    if (gear_changed) {
        // The source ramp cannot represent a falling upshift. Hand over to the
        // steady real recording at the shift, cut it like a lifted throttle,
        // then let the new gear's actual RPM establish the lower note.
        mixer_->stop_oneshot(acceleration_voice_);
        acceleration_voice_ = OneShotHandle{};
        throttle_seconds_ = std::max(throttle_seconds_, 2.70f);
        shift_elapsed_seconds_ = 0.0f;
    }

    if (should_play_acceleration) {
        // Source-time keeps the hold fade aligned with a slower/faster attack.
        throttle_seconds_ += std::clamp(frame.dt_seconds, 0.0f, 0.1f) * attack_pitch_;
    } else {
        throttle_seconds_ = 0.0f;
    }

    // The source reaches its stable held rev around 2.7 seconds. Fade its
    // static middle underneath the tail of the attack, then lightly follow the
    // simulated RPM. The tight pitch clamp preserves the real recording's
    // timbre instead of stretching it back into a synthetic whine.
    const float hold_fade = std::clamp(
        (throttle_seconds_ - 2.05f) / (2.70f - 2.05f), 0.0f, 1.0f);
    VoiceParams hold;
    hold.category = Category::Engine;
    const float shift_gain = recorded_shift_gain(shift_elapsed_seconds_);
    hold.gain = should_play_acceleration
                    ? kVehicleSoundGain * 0.72f * hold_fade * shift_gain
                    : 0.0f;
    hold.pitch = recorded_throttle_pitch(frame.engine_rpm) * profile_.pitch;
    hold.looping = true;
    hold.spatial = true;
    hold.position = frame.position;
    hold.attenuation.ref_distance = 5.0f;
    hold.attenuation.max_distance = 90.0f;
    hold.attenuation.rolloff = 0.75f;
    mixer_->set_loop(throttle_hold_, hold);

    shift_elapsed_seconds_ += std::clamp(frame.dt_seconds, 0.0f, 0.1f);

    // Preserve the held state through modal screens so closing F1 or pause
    // cannot manufacture a fresh throttle edge.
    was_accelerating_ = cranking ? false : frame.accelerating;
    startup_remaining_ = std::max(0.0f, startup_remaining_ - std::clamp(frame.dt_seconds, 0.0f, .1f));
    previous_gear_ = frame.gear;
}

void VehicleAudio::stop_horn() {
    if (mixer_ && horn_voice_.valid()) mixer_->stop_oneshot(horn_voice_);
    horn_voice_={}; horn_remaining_=0.f;
}

void VehicleAudio::play_car_collision(float impact_mps, glm::vec3 position) {
    if (!mixer_ || !bank_ || bank_->player_car_collision.empty()) return;
    const float gain = recorded_collision_gain(impact_mps);
    if (!(gain > 0.0f)) return;

    // One collision owns one voice. A second hit replaces the first instead of
    // stacking identical full-band crashes into clipping noise during a pileup.
    mixer_->stop_oneshot(collision_voice_);
    VoiceParams p;
    p.category = Category::Impacts;
    p.gain = gain * kVehicleSoundGain * kCrashGainBoost;
    p.pitch = 1.0f;
    p.spatial = true;
    p.position = position;
    p.attenuation.ref_distance = 7.0f;
    p.attenuation.max_distance = 100.0f;
    p.attenuation.rolloff = 0.65f;
    collision_voice_ = mixer_->play_oneshot(&bank_->player_car_collision, p);
}

void VehicleAudio::audition(CarSoundUse use, int variant) {
    if (!mixer_ || !bank_) return;
    const VoiceMix mix = car_sound_audition_mix(*bank_, use, variant);
    VoiceParams p;
    p.category = use == CarSoundUse::Crash
                     ? Category::Impacts
                     : (use == CarSoundUse::Tyres ? Category::Tyres
                                                  : Category::Engine);
    if (use == CarSoundUse::Surface) p.category = Category::World;
    p.gain = mix.gain;
    p.pitch = mix.pitch;
    p.lp_cutoff_hz = mix.lp_cutoff_hz;
    p.spatial = false;
    mixer_->play_oneshot(mix.clip, p);
}

std::size_t VehicleAudio::loop_count() const {
    return (throttle_hold_.valid() ? 1u : 0u) + (idle_.valid() ? 1u : 0u) +
           (drift_tyres_.valid() ? 1u : 0u) + (burnout_.valid() ? 1u : 0u);
}

}  // namespace apricot

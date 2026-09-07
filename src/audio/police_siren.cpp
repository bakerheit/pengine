#include "audio/police_siren.h"
#include "audio/vehicle_sound_profile.h"

#include <cmath>

namespace apricot {
namespace {

VoiceParams siren_params(bool audible, glm::vec3 position) {
    VoiceParams params;
    // This is a vehicle emitter, so the existing engine-volume control owns it.
    params.category = Category::Engine;
    params.gain = audible ? 0.32f * kVehicleSoundGain : 0.0f;
    params.looping = true;
    params.spatial = true;
    params.position = position;
    params.attenuation = {8.0f, 180.0f, 1.0f};
    return params;
}

}  // namespace

const PcmClip& police_siren_wail() {
    static const PcmClip clip = [] {
        PcmClip result;
        result.sample_rate = kDefaultSampleRate;
        result.channels = 1;
        constexpr double kTau = 6.2831853071795864769;
        constexpr double kPeriod = 4.0;
        result.samples.resize(4u * kDefaultSampleRate);
        for (std::size_t i = 0; i < result.samples.size(); ++i) {
            const double time = static_cast<double>(i) / kDefaultSampleRate;
            // Analytically integrated frequency: 1100 - 400*cos(tau*t/4).
            // Starts at 700 Hz, rises smoothly to 1500 Hz at 2 s, falls to
            // 700 Hz at 4 s. Exactly 4400 carrier cycles per loop makes both
            // phase and slope continuous at the seam, without a fade/gap.
            const double phase = kTau * 1100.0 * time -
                                 400.0 * kPeriod * std::sin(kTau * time / kPeriod);
            // Restrained harmonics give the wail a buzzy speaker character;
            // peak <= .94, highest partial <= 4500 Hz (well below Nyquist).
            result.samples[i] = static_cast<float>(
                .72 * std::sin(phase) + .16 * std::sin(2.0 * phase) +
                .06 * std::sin(3.0 * phase));
        }
        return result;
    }();
    return clip;
}

bool PoliceSiren::start(VoiceMixer& mixer) {
    if (mixer_ == &mixer && started()) return true;
    stop();
    if (mixer.silent()) return false;
    voice_ = mixer.open_loop(&police_siren_wail(), siren_params(false, {}));
    if (!voice_.valid()) return false;
    mixer_ = &mixer;
    return true;
}

void PoliceSiren::update(bool active, bool enabled, glm::vec3 position) {
    if (!started()) return;
    // Bad presentation coordinates must not poison the mixer with NaNs.
    const bool finite = std::isfinite(position.x) && std::isfinite(position.y) &&
                        std::isfinite(position.z);
    const bool audible = active && enabled && finite;
    if (!finite) position = position_;
    if (audible == audible_ && (!audible || position == position_)) return;
    mixer_->set_loop(voice_, siren_params(audible, position));
    audible_ = audible;
    position_ = position;
}

void PoliceSiren::stop() {
    if (mixer_) mixer_->close_loop(voice_);
    mixer_ = nullptr;
    voice_ = {};
    audible_ = false;
    position_ = {};
}

}  // namespace apricot

#pragma once

#include "audio/mixer.h"

namespace apricot {

// Non-spatial weather bed. The bank outlives the voice; clear weather and the
// title fade it out (IntroAudio owns the title's rain voice).
class RainAudio {
public:
    void update(VoiceMixer& mixer, const SfxBank& bank, float intensity, float dt) {
        const auto rain = weather_mix(bank, intensity, 0.0f).rain;
        const float step = std::max(dt, 0.0f) * 0.75f;
        gain_ += std::clamp(rain.gain - gain_, -step, step);
        if (gain_ > 0.0f && !bank.rain.empty()) {
            VoiceParams p;
            p.category = Category::Weather;
            p.gain = gain_;
            p.spatial = false;
            p.looping = true;
            // Keep the last cutoff during fade-out instead of closing the
            // filter abruptly when weather becomes clear.
            if (rain.clip) cutoff_ = rain.lp_cutoff_hz;
            p.lp_cutoff_hz = cutoff_;
            if (!voice_.valid()) voice_ = mixer.open_loop(&bank.rain, p);
            else mixer.set_loop(voice_, p);
        } else {
            stop(mixer);
        }
    }

    void stop(VoiceMixer& mixer) {
        mixer.close_loop(voice_);
        voice_ = {};
        gain_ = 0.0f;
    }

private:
    VoiceHandle voice_{};
    float gain_ = 0.0f;
    float cutoff_ = 7700.0f;
};

}  // namespace apricot

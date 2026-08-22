#include "audio/synth.h"

#include <algorithm>
#include <cmath>

namespace apricot {

namespace {
constexpr float kTwoPi = 6.2831853071795864769f;
}

float PcmClip::duration_seconds() const {
    if (sample_rate == 0) return 0.0f;
    return static_cast<float>(frame_count()) / static_cast<float>(sample_rate);
}

PcmClip synth_tone(float hz, float seconds, float fade_seconds,
                   uint32_t sample_rate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    clip.channels = 1;

    if (sample_rate == 0 || !(seconds > 0.0f)) return clip;

    const double rate = static_cast<double>(sample_rate);
    const std::size_t frames =
        static_cast<std::size_t>(static_cast<double>(seconds) * rate);
    if (frames == 0) return clip;

    // Phase advance per sample, in double. Accumulating a float phase over a
    // few seconds at 48 kHz drifts audibly flat by the end of the clip.
    const double phase_step = static_cast<double>(hz) * kTwoPi / rate;

    const std::size_t fade =
        std::min(frames / 2,
                 static_cast<std::size_t>(
                     std::max(0.0, static_cast<double>(fade_seconds) * rate)));

    clip.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        float s = static_cast<float>(std::sin(phase_step * static_cast<double>(i)));

        // Linear head/tail ramp so the clip starts and ends at zero.
        if (fade > 0) {
            if (i < fade) {
                s *= static_cast<float>(i) / static_cast<float>(fade);
            } else if (i >= frames - fade) {
                const std::size_t tail = frames - 1 - i;
                s *= static_cast<float>(tail) / static_cast<float>(fade);
            }
        }
        clip.samples[i] = s;
    }
    return clip;
}

PcmClip synth_engine_loop(float hz, float seconds, uint32_t sample_rate) {
    // PLACEHOLDER — see the header. No harmonics, no jitter, no load timbre.
    // A loop needs no fade (it would pump on every cycle), so fades are off.
    // TODO(engine audio ticket): replace with the real harmonic stack.
    return synth_tone(hz, seconds, 0.0f, sample_rate);
}

}  // namespace apricot

#pragma once

#include <algorithm>
#include <cmath>
#include "audio/synth.h"
#include "core/rng.h"

namespace apricot {

// Build once before audio playback and retain the clips for the mixer lifetime.
// The reload clip is only the initial magazine release, so canceling a reload
// cannot leave a delayed insertion sound playing after the gun is holstered.
inline PcmClip synth_weapon_feedback(bool shot, uint32_t sample_rate = kDefaultSampleRate) {
    PcmClip clip;
    clip.sample_rate = sample_rate;
    if (sample_rate == 0) return clip;
    const float duration = shot ? .24f : .10f;
    const auto count = static_cast<std::size_t>(static_cast<float>(sample_rate) * duration);
    clip.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        const auto bits = splitmix64_mix(0xA91C0715ull + static_cast<uint64_t>(i));
        const float noise = static_cast<float>(bits >> 40) / 8388607.5f - 1.f;
        const float head = std::clamp(t / .0004f, 0.f, 1.f);
        const float tail = std::clamp((duration - t) / .02f, 0.f, 1.f);
        const float body = shot
            ? .72f * noise * std::exp(-t * 48.f) + .25f * std::sin(kTwoPi * 115.f * t) * std::exp(-t * 27.f)
            : .38f * noise * std::exp(-t * 90.f) + .26f * std::sin(kTwoPi * 1450.f * t) * std::exp(-t * 65.f);
        clip.samples[i] = head * tail * body;
    }
    if (!clip.samples.empty()) clip.samples.back() = 0.f;
    return clip;
}

inline PcmClip synth_pistol_shot(uint32_t sample_rate = kDefaultSampleRate) {
    return synth_weapon_feedback(true, sample_rate);
}
inline PcmClip synth_pistol_reload(uint32_t sample_rate = kDefaultSampleRate) {
    return synth_weapon_feedback(false, sample_rate);
}

}  // namespace apricot

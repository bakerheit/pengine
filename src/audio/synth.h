#pragma once

#include <cstdint>
#include <vector>

namespace apricot {

// PCM generation. No device, no file loading, no streaming — this half of the
// audio module is pure maths that turns parameters into samples, which is why
// it lives in the sim library and is testable with no sound hardware.
//
// apricot has no audio assets. Every sound is synthesised at load, so there is
// nothing to ship, nothing to license and nothing to go missing on a fresh
// clone.

inline constexpr uint32_t kDefaultSampleRate = 48000;

struct PcmClip {
    uint32_t sample_rate = kDefaultSampleRate;
    uint16_t channels = 1;

    // Interleaved when channels > 1. Normalised to roughly [-1, 1]; the mixer
    // applies gain, so generators should not pre-attenuate.
    std::vector<float> samples;

    std::size_t frame_count() const {
        return channels ? samples.size() / channels : 0;
    }
    float duration_seconds() const;
};

// A windowed sine. Real and complete — it is the reference the other
// generators are checked against, and it is what the tests can assert exact
// sample values from.
//
// `fade_seconds` ramps the head and tail. Without it a clip starts and ends on
// a non-zero sample and every playback begins with an audible click, which is
// the single most common synthesised-audio bug.
PcmClip synth_tone(float hz, float seconds, float fade_seconds = 0.005f,
                   uint32_t sample_rate = kDefaultSampleRate);

// A seamlessly loopable engine note at a given crank frequency.
//
// PLACEHOLDER: currently returns a bare synth_tone() at `hz`, which sounds
// like a test signal and nothing like an engine.
// TODO(engine audio ticket): stack the firing-order harmonics, add per-
// cylinder jitter and load-dependent timbre, and make the loop point
// phase-continuous so pitch can be modulated without a seam.
PcmClip synth_engine_loop(float hz, float seconds,
                          uint32_t sample_rate = kDefaultSampleRate);

}  // namespace apricot

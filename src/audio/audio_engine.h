#pragma once

namespace pengine {

// Manages vehicle audio: engine loop (pitch-mapped to speed), horn, and
// handbrake squeal. Uses miniaudio internally; miniaudio.h is NOT included
// here to keep compile times fast — only audio_engine.cpp sees it.
class AudioEngine {
public:
    bool init();
    void shutdown();

    // Call once per render frame. speed_kmh / max_speed_kmh drive engine pitch.
    // horn_pressed and handbrake_pressed should be edge-triggered (just-pressed).
    void update(float speed_kmh, float max_speed_kmh,
                bool in_vehicle, bool horn_pressed, bool handbrake_pressed);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace pengine

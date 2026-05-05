#pragma once

#include <vector>

#include <glm/glm.hpp>

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

    // Trigger a one-shot concrete footstep sample. Voices are rotated so
    // close-together steps (sprint, fast turns) overlap cleanly instead
    // of cutting each other off.
    void play_footstep_concrete();

    // Drive the looping metal-scrape sound (chassis dragging on the road).
    // Call once per frame: `intensity` 0..1 sets the steady-state volume,
    // `pitch` is the playback pitch factor (1.0 = original). The loop
    // starts on first non-zero intensity, fades smoothly on `dt`, and
    // stops once it has fully faded out — caller just sets the target.
    void update_scrape(float dt, float intensity, float pitch);

    // One spatialized engine voice in the AI traffic pool. `id` is any
    // stable per-car identifier (we use the Car* pointer); voices keep
    // their assignment across frames as long as their car is still in
    // the nearest set, so a moving car doesn't get its engine sound
    // re-cycled mid-pass.
    struct TrafficSource {
        const void* id            = nullptr;
        glm::vec3   position      {0.f};
        float       speed_kmh     = 0.f;
        float       max_speed_kmh = 1.f;
    };

    // Position the listener and update the spatialized engine pool. Call
    // once per frame from the place that already builds a per-frame view
    // of the world (Application::update). Source list can have any size;
    // we'll pick the nearest TRAFFIC_VOICES within audible range.
    void update_traffic(const glm::vec3& listener_pos,
                        const std::vector<TrafficSource>& sources);

    // Trigger a spatialised pedestrian footstep at `world_pos`. Voices are
    // rotated round-robin and pitched ±5% per call, so a crowd doesn't
    // sound like one synchronised marching column. Caller is responsible
    // for ensuring the listener has been positioned this frame (we rely
    // on the same listener that update_traffic just set).
    void play_ped_footstep(const glm::vec3& world_pos);

    // Trigger a one-shot pistol gunshot. 2D unspatialised — the player's
    // own gun is always at the listener.
    void play_gunshot();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace pengine

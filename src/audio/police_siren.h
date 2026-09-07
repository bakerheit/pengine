#pragma once

#include "audio/mixer.h"

namespace apricot {

// Immutable 48 kHz mono PCM, generated once on first access (during start).
// Shared process lifetime keeps audio-thread/queued clip pointers valid after
// a controller stops or is destroyed. No recordings or audio device required.
const PcmClip& police_siren_wail();

// Host-owned presentation controller; the mixer must outlive this object.
// A single reserved voice remains phase-continuous through toggle/pause fades.
// Call stop before destroying/re-preparing the mixer, then start after reopen.
class PoliceSiren {
public:
    PoliceSiren() = default;
    ~PoliceSiren() { stop(); }
    PoliceSiren(const PoliceSiren&) = delete;
    PoliceSiren& operator=(const PoliceSiren&) = delete;
    PoliceSiren(PoliceSiren&&) = delete;
    PoliceSiren& operator=(PoliceSiren&&) = delete;

    // Idempotent for the same mixer. Returns false for a silent/full mixer.
    bool start(VoiceMixer& mixer);
    // enabled is a latched host toggle, not a key-held state or toggle event.
    // active must include unpaused gameplay AND occupancy of the police car.
    // Clear enabled on leaving/changing vehicles; false active always mutes.
    // No allocation, decoding or voice recreation during an update.
    void update(bool active, bool enabled, glm::vec3 position);
    // Immediate teardown; use update(false, false, position) for a smooth
    // in-game mute. Safe repeatedly and before the first start.
    void stop();

    bool started() const { return mixer_ != nullptr && voice_.valid(); }
    std::size_t loop_count() const { return started() ? 1u : 0u; }

private:
    VoiceMixer* mixer_ = nullptr;
    VoiceHandle voice_{};
    bool audible_ = false;
    glm::vec3 position_{0.0f};
};

}  // namespace apricot

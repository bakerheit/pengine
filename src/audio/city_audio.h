#pragma once

#include <cstddef>

#include "audio/mixer.h"
#include "audio/synth.h"

namespace apricot {

// Owns the non-spatial city bed. It intentionally keeps playing behind menus:
// the title and pause overlays sit over the same living city, while engine and
// collision voices remain responsible for muting themselves when play pauses.
class CityAudio {
public:
    bool start(VoiceMixer& mixer, const SfxBank& bank);
    void stop();

    bool started() const { return mixer_ != nullptr; }
    std::size_t loop_count() const { return ambience_.valid() ? 1u : 0u; }

private:
    VoiceMixer* mixer_ = nullptr;
    VoiceHandle ambience_{};
};

}  // namespace apricot

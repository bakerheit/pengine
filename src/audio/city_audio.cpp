#include "audio/city_audio.h"

namespace apricot {

bool CityAudio::start(VoiceMixer& mixer, const SfxBank& bank) {
    stop();
    if (bank.city_ambience.empty()) return false;

    VoiceParams p;
    p.category = Category::World;
    // The recording already contains nearby traffic and voices. Keep it well
    // under the player's engine so it reads as place, not foreground action.
    p.gain = 0.22f;
    p.pitch = 1.0f;
    p.looping = true;
    p.spatial = false;
    ambience_ = mixer.open_loop(&bank.city_ambience, p);
    if (!ambience_.valid()) return false;
    mixer_ = &mixer;
    return true;
}

void CityAudio::stop() {
    if (mixer_) mixer_->close_loop(ambience_);
    ambience_ = VoiceHandle{};
    mixer_ = nullptr;
}

}  // namespace apricot

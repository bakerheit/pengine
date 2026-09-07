#pragma once

#include "audio/mixer.h"

namespace apricot {

// Fade the two title stems out on Start and back in on return. The bank owns
// the shared rain PCM and must outlive these voices, just like the score here.
class IntroAudio {
public:
    bool load(const std::string& score, const PcmClip& rain) {
        rain_ = &rain;
        return load_wav_clip(score, music_) && !rain.empty();
    }
    void update(VoiceMixer& mixer, bool visible, float dt) {
        const float target = visible ? 1.0f : 0.0f;
        gain_ += std::clamp(target - gain_, -std::max(dt, 0.0f) * 1.5f,
                            std::max(dt, 0.0f) * 0.7f);
        if (visible && !music_voice_.valid()) {
            music_voice_ = mixer.open_loop(&music_, params(Category::Music, 0));
            rain_voice_ = mixer.open_loop(rain_, params(Category::Weather, 0));
            if (!music_voice_.valid() || !rain_voice_.valid()) {
                stop(mixer);
                return;
            }
        }
        if (music_voice_.valid()) {
            mixer.set_loop(music_voice_, params(Category::Music, gain_ * 0.42f));
            mixer.set_loop(rain_voice_, params(Category::Weather, gain_ * 0.35f));
        }
        if (!visible && gain_ == 0.0f) stop(mixer);
    }
    void stop(VoiceMixer& mixer) {
        mixer.close_loop(music_voice_);
        mixer.close_loop(rain_voice_);
        music_voice_ = {};
        rain_voice_ = {};
        gain_ = 0;
    }
private:
    static VoiceParams params(Category category, float gain) {
        VoiceParams p;
        p.category = category;
        p.gain = gain;
        p.spatial = false;
        p.looping = true;
        return p;
    }
    PcmClip music_;
    const PcmClip* rain_ = nullptr;
    VoiceHandle music_voice_, rain_voice_;
    float gain_ = 0;
};

}  // namespace apricot

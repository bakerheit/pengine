#include "audio/audio_engine.h"

#include "miniaudio.h"
#include "core/log.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

struct AudioEngine::Impl {
    ma_engine engine{};

    ma_sound engine_loop{};
    ma_sound horn{};
    ma_sound brake{};

    bool engine_loop_loaded = false;
    bool horn_loaded        = false;
    bool brake_loaded       = false;

    bool engine_playing = false;
};

bool AudioEngine::init() {
    impl_ = new Impl{};

    ma_result r = ma_engine_init(nullptr, &impl_->engine);
    if (r != MA_SUCCESS) {
        PE_WARN("AudioEngine: ma_engine_init failed (ma_result=%d)", (int)r);
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    PE_INFO("AudioEngine: device open, sample-rate=%u",
            ma_engine_get_sample_rate(&impl_->engine));

    auto load = [&](ma_sound& snd, bool& loaded, const char* path, bool looping) {
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        ma_result lr = ma_sound_init_from_file(&impl_->engine, path, flags,
                                               nullptr, nullptr, &snd);
        if (lr == MA_SUCCESS) {
            ma_sound_set_looping(&snd, looping ? MA_TRUE : MA_FALSE);
            loaded = true;
            PE_INFO("AudioEngine: loaded %s", path);
        } else {
            PE_WARN("AudioEngine: could not load %s (ma_result=%d)", path, (int)lr);
        }
    };

    load(impl_->engine_loop, impl_->engine_loop_loaded,
         ASSETS_DIR "/Vehicles_psx/Sound effects/Car_Engine_Loop.ogg", true);
    load(impl_->horn, impl_->horn_loaded,
         ASSETS_DIR "/Vehicles_psx/Sound effects/Car_Horn.ogg", false);
    load(impl_->brake, impl_->brake_loaded,
         ASSETS_DIR "/Vehicles_psx/Sound effects/Car_Parking_Brake.ogg", false);

    return true;
}

void AudioEngine::shutdown() {
    if (!impl_) return;
    if (impl_->engine_loop_loaded) ma_sound_uninit(&impl_->engine_loop);
    if (impl_->horn_loaded)        ma_sound_uninit(&impl_->horn);
    if (impl_->brake_loaded)       ma_sound_uninit(&impl_->brake);
    ma_engine_uninit(&impl_->engine);
    delete impl_;
    impl_ = nullptr;
}

void AudioEngine::update(float speed_kmh, float max_speed_kmh,
                          bool in_vehicle, bool horn_pressed, bool handbrake_pressed) {
    if (!impl_) return;

    // Engine loop: start/stop based on vehicle mode
    if (impl_->engine_loop_loaded) {
        if (in_vehicle && !impl_->engine_playing) {
            ma_sound_seek_to_pcm_frame(&impl_->engine_loop, 0);
            ma_result sr = ma_sound_start(&impl_->engine_loop);
            PE_INFO("AudioEngine: engine loop start (ma_result=%d)", (int)sr);
            impl_->engine_playing = true;
        } else if (!in_vehicle && impl_->engine_playing) {
            ma_sound_stop(&impl_->engine_loop);
            impl_->engine_playing = false;
        }

        if (impl_->engine_playing) {
            // Map speed to pitch: 0.55 at idle, 1.9 at max speed
            float t = (max_speed_kmh > 0.f) ? (speed_kmh / max_speed_kmh) : 0.f;
            if (t < 0.f) t = -t;
            if (t > 1.f) t = 1.f;
            ma_sound_set_pitch(&impl_->engine_loop, 0.55f + t * 1.35f);
        }
    }

    // Horn: one-shot, retrigger on each press
    if (horn_pressed && impl_->horn_loaded) {
        ma_sound_seek_to_pcm_frame(&impl_->horn, 0);
        ma_sound_start(&impl_->horn);
    }

    // Parking brake squeal: one-shot on first press
    if (handbrake_pressed && impl_->brake_loaded) {
        ma_sound_seek_to_pcm_frame(&impl_->brake, 0);
        ma_sound_start(&impl_->brake);
    }
}

} // namespace pengine

#include "audio/audio_engine.h"

#include <cmath>

#include "miniaudio.h"
#include "core/log.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

namespace {
// Concrete footstep samples — alternated strictly per step so the cadence
// reads as left-right-left-right rather than the same clip repeating.
constexpr const char* FOOTSTEP_CONCRETE_PATHS[] = {
    ASSETS_DIR "/audio/foley_footstep_concrete_2.wav",
    ASSETS_DIR "/audio/foley_footstep_concrete_4.wav",
};
constexpr int FOOTSTEP_SAMPLE_COUNT =
    sizeof(FOOTSTEP_CONCRETE_PATHS) / sizeof(FOOTSTEP_CONCRETE_PATHS[0]);
// Per-sample voice fan-out so sprinting steps overlap cleanly within
// the same clip too.
constexpr int FOOTSTEP_VOICES_PER_SAMPLE = 2;

// Spatialized AI traffic engine pool. Each voice is a separate copy of
// the engine loop sample, positioned at the assigned car. We pick the
// nearest N within audible range each frame.
constexpr int   TRAFFIC_VOICES        = 8;
constexpr float TRAFFIC_MIN_DISTANCE  = 4.f;   // m — full volume within
constexpr float TRAFFIC_MAX_DISTANCE  = 120.f; // m — silent past
constexpr float TRAFFIC_ROLLOFF       = 1.0f;  // miniaudio inverse rolloff
constexpr float TRAFFIC_BASE_VOLUME   = 0.6f;  // per-voice gain at full proximity

// Spatialised pedestrian footstep pool. Steps are short (~0.3 s) but a
// crowd of 30 peds can fire many per second; oversize the pool so a
// dense block doesn't cut earlier steps in half. Two source samples
// alternate per call (left/right foot), so the pool is laid out as
// [sample][voice] like the player footsteps.
constexpr int   PED_STEP_VOICES_PER_SAMPLE = 6;
constexpr float PED_STEP_MIN_DISTANCE      = 1.5f;  // m
constexpr float PED_STEP_MAX_DISTANCE      = 35.f;  // m — beyond this, silent
constexpr float PED_STEP_ROLLOFF           = 1.5f;  // tighter falloff than cars
constexpr float PED_STEP_BASE_VOLUME       = 0.55f; // raw sample is loud
} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};

    ma_sound engine_loop{};
    ma_sound horn{};
    ma_sound brake{};
    ma_sound scrape{};            // looping white-noise -> metal-scrape
    ma_sound gunshot{};           // one-shot pistol shot

    bool  scrape_loaded   = false;
    bool  scrape_playing  = false;
    float scrape_volume   = 0.f;  // smoothed current volume for fade in/out

    // [sample][voice]. Sample cursor strictly alternates per call;
    // each sample owns its own voice round-robin so overlapping plays
    // of the same clip don't cut each other off.
    ma_sound footstep_concrete[FOOTSTEP_SAMPLE_COUNT]
                              [FOOTSTEP_VOICES_PER_SAMPLE]{};
    bool     footstep_concrete_loaded[FOOTSTEP_SAMPLE_COUNT]
                                     [FOOTSTEP_VOICES_PER_SAMPLE]{};
    int      next_footstep_sample = 0;
    int      next_footstep_voice[FOOTSTEP_SAMPLE_COUNT]{};

    // Spatialized AI engine voices. traffic_voice_id[i] is the Car* this
    // slot is currently tracking (nullptr = idle).
    ma_sound    traffic_engine[TRAFFIC_VOICES]{};
    bool        traffic_engine_loaded[TRAFFIC_VOICES]{};
    const void* traffic_voice_id[TRAFFIC_VOICES]{};

    // Spatialised ped footstep voices. Same [sample][voice] layout as
    // the player footsteps but each voice is a 3D-positioned ma_sound,
    // round-robin'd by play_ped_footstep().
    ma_sound ped_step[FOOTSTEP_SAMPLE_COUNT]
                     [PED_STEP_VOICES_PER_SAMPLE]{};
    bool     ped_step_loaded[FOOTSTEP_SAMPLE_COUNT]
                            [PED_STEP_VOICES_PER_SAMPLE]{};
    int      next_ped_step_sample = 0;
    int      next_ped_step_voice[FOOTSTEP_SAMPLE_COUNT]{};
    // Cheap LCG-style step counter for pitch jitter (don't drag in
    // <random> just for two random nibbles).
    unsigned ped_step_jitter = 0;

    bool engine_loop_loaded = false;
    bool horn_loaded        = false;
    bool brake_loaded       = false;
    bool gunshot_loaded     = false;

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

    // Helper to disable 3D spatialization on a sound — used for first-person
    // and UI-style audio (player engine, horn, brake, scrape, footsteps)
    // that should always play at full volume regardless of where in the
    // world the listener is. miniaudio's default attenuation would otherwise
    // silence them once we move the listener away from the world origin
    // for the spatialized AI traffic voices.
    auto disable_spatialization = [&](ma_sound& snd, bool loaded) {
        if (loaded) ma_sound_set_spatialization_enabled(&snd, MA_FALSE);
    };

    load(impl_->engine_loop, impl_->engine_loop_loaded,
         ASSETS_DIR "/vehicles/Vehicles_psx/Sound effects/Car_Engine_Loop.ogg", true);
    disable_spatialization(impl_->engine_loop, impl_->engine_loop_loaded);
    load(impl_->horn, impl_->horn_loaded,
         ASSETS_DIR "/vehicles/Vehicles_psx/Sound effects/Car_Horn.ogg", false);
    disable_spatialization(impl_->horn, impl_->horn_loaded);
    load(impl_->brake, impl_->brake_loaded,
         ASSETS_DIR "/vehicles/Vehicles_psx/Sound effects/Car_Parking_Brake.ogg", false);
    disable_spatialization(impl_->brake, impl_->brake_loaded);
    load(impl_->scrape, impl_->scrape_loaded,
         ASSETS_DIR "/audio/metal-scrape.mp3", true);
    if (impl_->scrape_loaded) {
        // Start silent — caller drives volume via update_scrape().
        ma_sound_set_volume(&impl_->scrape, 0.f);
    }
    disable_spatialization(impl_->scrape, impl_->scrape_loaded);
    load(impl_->gunshot, impl_->gunshot_loaded,
         ASSETS_DIR "/audio/Glock17_Shoot_004.wav", false);
    disable_spatialization(impl_->gunshot, impl_->gunshot_loaded);
    for (int s = 0; s < FOOTSTEP_SAMPLE_COUNT; ++s) {
        for (int v = 0; v < FOOTSTEP_VOICES_PER_SAMPLE; ++v) {
            load(impl_->footstep_concrete[s][v],
                 impl_->footstep_concrete_loaded[s][v],
                 FOOTSTEP_CONCRETE_PATHS[s], false);
            disable_spatialization(impl_->footstep_concrete[s][v],
                                    impl_->footstep_concrete_loaded[s][v]);
        }
    }

    // Spatialized AI engine pool: each voice is its own copy of the engine
    // loop so they can have independent positions / pitches / playhead.
    for (int i = 0; i < TRAFFIC_VOICES; ++i) {
        load(impl_->traffic_engine[i], impl_->traffic_engine_loaded[i],
             ASSETS_DIR "/vehicles/Vehicles_psx/Sound effects/Car_Engine_Loop.ogg",
             /*looping=*/true);
        if (impl_->traffic_engine_loaded[i]) {
            ma_sound_set_volume(&impl_->traffic_engine[i],
                                TRAFFIC_BASE_VOLUME);
            ma_sound_set_attenuation_model(&impl_->traffic_engine[i],
                                            ma_attenuation_model_inverse);
            ma_sound_set_min_distance(&impl_->traffic_engine[i],
                                        TRAFFIC_MIN_DISTANCE);
            ma_sound_set_max_distance(&impl_->traffic_engine[i],
                                        TRAFFIC_MAX_DISTANCE);
            ma_sound_set_rolloff(&impl_->traffic_engine[i],
                                  TRAFFIC_ROLLOFF);
        }
        impl_->traffic_voice_id[i] = nullptr;
    }

    // Spatialised ped footstep pool: same two source samples as the player
    // (foley_footstep_concrete_2/4.wav), 6 voices per sample for overlap.
    for (int s = 0; s < FOOTSTEP_SAMPLE_COUNT; ++s) {
        for (int v = 0; v < PED_STEP_VOICES_PER_SAMPLE; ++v) {
            load(impl_->ped_step[s][v],
                 impl_->ped_step_loaded[s][v],
                 FOOTSTEP_CONCRETE_PATHS[s], /*looping=*/false);
            if (impl_->ped_step_loaded[s][v]) {
                ma_sound_set_volume(&impl_->ped_step[s][v],
                                     PED_STEP_BASE_VOLUME);
                ma_sound_set_attenuation_model(&impl_->ped_step[s][v],
                                                ma_attenuation_model_inverse);
                ma_sound_set_min_distance(&impl_->ped_step[s][v],
                                            PED_STEP_MIN_DISTANCE);
                ma_sound_set_max_distance(&impl_->ped_step[s][v],
                                            PED_STEP_MAX_DISTANCE);
                ma_sound_set_rolloff(&impl_->ped_step[s][v],
                                      PED_STEP_ROLLOFF);
            }
        }
    }

    return true;
}

void AudioEngine::shutdown() {
    if (!impl_) return;
    if (impl_->engine_loop_loaded) ma_sound_uninit(&impl_->engine_loop);
    if (impl_->horn_loaded)        ma_sound_uninit(&impl_->horn);
    if (impl_->brake_loaded)       ma_sound_uninit(&impl_->brake);
    if (impl_->scrape_loaded)      ma_sound_uninit(&impl_->scrape);
    if (impl_->gunshot_loaded)     ma_sound_uninit(&impl_->gunshot);
    for (int s = 0; s < FOOTSTEP_SAMPLE_COUNT; ++s) {
        for (int v = 0; v < FOOTSTEP_VOICES_PER_SAMPLE; ++v) {
            if (impl_->footstep_concrete_loaded[s][v])
                ma_sound_uninit(&impl_->footstep_concrete[s][v]);
        }
    }
    for (int i = 0; i < TRAFFIC_VOICES; ++i) {
        if (impl_->traffic_engine_loaded[i])
            ma_sound_uninit(&impl_->traffic_engine[i]);
    }
    for (int s = 0; s < FOOTSTEP_SAMPLE_COUNT; ++s) {
        for (int v = 0; v < PED_STEP_VOICES_PER_SAMPLE; ++v) {
            if (impl_->ped_step_loaded[s][v])
                ma_sound_uninit(&impl_->ped_step[s][v]);
        }
    }
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

void AudioEngine::update_scrape(float dt, float intensity, float pitch) {
    if (!impl_ || !impl_->scrape_loaded) return;

    if (intensity < 0.f) intensity = 0.f;
    // Cap at 8× — miniaudio amplifies past 1.0, but we keep a sanity ceiling
    // so a runaway intensity can't blow the mix or the user's ears.
    if (intensity > 8.f) intensity = 8.f;

    // Fade smoothing: separate up/down rates so a scrape pops in fast
    // but lingers a moment as it dies, hiding substep flicker when the
    // chassis briefly lifts off the road.
    constexpr float ATTACK_TAU = 1.f / 25.f; // s — almost instant
    constexpr float RELEASE_TAU = 1.f / 6.f; // s — gentler tail
    float tau = (intensity > impl_->scrape_volume) ? ATTACK_TAU : RELEASE_TAU;
    float k = 1.f - std::exp(-dt / tau);
    impl_->scrape_volume += (intensity - impl_->scrape_volume) * k;

    if (impl_->scrape_volume > 0.001f) {
        if (!impl_->scrape_playing) {
            ma_sound_seek_to_pcm_frame(&impl_->scrape, 0);
            ma_sound_start(&impl_->scrape);
            impl_->scrape_playing = true;
        }
        ma_sound_set_volume(&impl_->scrape, impl_->scrape_volume);
        ma_sound_set_pitch (&impl_->scrape, pitch);
    } else if (impl_->scrape_playing) {
        ma_sound_stop(&impl_->scrape);
        impl_->scrape_playing = false;
        impl_->scrape_volume  = 0.f;
        ma_sound_set_volume(&impl_->scrape, 0.f);
    }
}

void AudioEngine::play_footstep_concrete() {
    if (!impl_) return;
    int s = impl_->next_footstep_sample;
    impl_->next_footstep_sample = (s + 1) % FOOTSTEP_SAMPLE_COUNT;
    int v = impl_->next_footstep_voice[s];
    impl_->next_footstep_voice[s] = (v + 1) % FOOTSTEP_VOICES_PER_SAMPLE;
    if (!impl_->footstep_concrete_loaded[s][v]) return;
    ma_sound_seek_to_pcm_frame(&impl_->footstep_concrete[s][v], 0);
    ma_sound_start(&impl_->footstep_concrete[s][v]);
}

void AudioEngine::play_gunshot() {
    if (!impl_ || !impl_->gunshot_loaded) return;
    ma_sound_seek_to_pcm_frame(&impl_->gunshot, 0);
    ma_sound_start(&impl_->gunshot);
}

void AudioEngine::play_ped_footstep(const glm::vec3& world_pos) {
    if (!impl_) return;
    int s = impl_->next_ped_step_sample;
    impl_->next_ped_step_sample = (s + 1) % FOOTSTEP_SAMPLE_COUNT;
    int v = impl_->next_ped_step_voice[s];
    impl_->next_ped_step_voice[s] = (v + 1) % PED_STEP_VOICES_PER_SAMPLE;
    if (!impl_->ped_step_loaded[s][v]) return;

    // Cheap pitch jitter: ±5% so a crowd doesn't sound like one footprint
    // looping. LCG-style counter avoids dragging in <random>.
    impl_->ped_step_jitter = impl_->ped_step_jitter * 1664525u + 1013904223u;
    float jitter = static_cast<float>((impl_->ped_step_jitter >> 16) & 0xFFFu)
                    / static_cast<float>(0xFFFu);
    float pitch  = 0.95f + jitter * 0.10f;

    ma_sound_set_position(&impl_->ped_step[s][v],
                          world_pos.x, world_pos.y, world_pos.z);
    ma_sound_set_pitch(&impl_->ped_step[s][v], pitch);
    ma_sound_seek_to_pcm_frame(&impl_->ped_step[s][v], 0);
    ma_sound_start(&impl_->ped_step[s][v]);
}

void AudioEngine::update_traffic(const glm::vec3& listener_pos,
                                  const std::vector<TrafficSource>& sources) {
    if (!impl_) return;

    // Move the listener to wherever the camera is. miniaudio's distance
    // attenuation is computed relative to this point per spatialized sound.
    ma_engine_listener_set_position(&impl_->engine, 0,
                                    listener_pos.x, listener_pos.y, listener_pos.z);

    // Rank candidate sources by squared distance and keep the closest
    // TRAFFIC_VOICES that are inside the audible radius.
    struct Ranked { const TrafficSource* src; float dist2; };
    std::vector<Ranked> ranked;
    ranked.reserve(sources.size());
    constexpr float MAX_D2 = TRAFFIC_MAX_DISTANCE * TRAFFIC_MAX_DISTANCE;
    for (const auto& s : sources) {
        glm::vec3 d = s.position - listener_pos;
        float d2 = glm::dot(d, d);
        if (d2 > MAX_D2) continue;
        ranked.push_back({&s, d2});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const Ranked& a, const Ranked& b) { return a.dist2 < b.dist2; });
    if (static_cast<int>(ranked.size()) > TRAFFIC_VOICES)
        ranked.resize(TRAFFIC_VOICES);

    // Stable assignment: for each existing voice, keep its slot if the
    // car it was tracking is still in the new top-N. This prevents the
    // engine sound from "teleporting" between cars when the rank shuffles.
    bool voice_kept[TRAFFIC_VOICES]   = {};
    bool source_taken[TRAFFIC_VOICES] = {};
    for (int i = 0; i < TRAFFIC_VOICES; ++i) {
        if (!impl_->traffic_engine_loaded[i]) continue;
        const void* id = impl_->traffic_voice_id[i];
        if (!id) continue;
        for (std::size_t j = 0; j < ranked.size(); ++j) {
            if (ranked[j].src->id == id) {
                voice_kept[i]   = true;
                source_taken[j] = true;
                break;
            }
        }
    }

    // Stop voices whose tracked car fell out of range / disappeared.
    for (int i = 0; i < TRAFFIC_VOICES; ++i) {
        if (!impl_->traffic_engine_loaded[i]) continue;
        if (impl_->traffic_voice_id[i] && !voice_kept[i]) {
            ma_sound_stop(&impl_->traffic_engine[i]);
            impl_->traffic_voice_id[i] = nullptr;
        }
    }

    // Allocate idle voices to ranked sources that don't yet have one.
    for (std::size_t j = 0; j < ranked.size(); ++j) {
        if (source_taken[j]) continue;
        for (int i = 0; i < TRAFFIC_VOICES; ++i) {
            if (!impl_->traffic_engine_loaded[i]) continue;
            if (impl_->traffic_voice_id[i] != nullptr) continue;
            impl_->traffic_voice_id[i] = ranked[j].src->id;
            ma_sound_seek_to_pcm_frame(&impl_->traffic_engine[i], 0);
            ma_sound_start(&impl_->traffic_engine[i]);
            voice_kept[i]   = true;
            source_taken[j] = true;
            break;
        }
    }

    // Update position + pitch for every active voice this frame.
    for (int i = 0; i < TRAFFIC_VOICES; ++i) {
        if (!voice_kept[i]) continue;
        const void* id = impl_->traffic_voice_id[i];
        for (const auto& r : ranked) {
            if (r.src->id != id) continue;
            const TrafficSource& s = *r.src;
            ma_sound_set_position(&impl_->traffic_engine[i],
                                  s.position.x, s.position.y, s.position.z);
            float t = (s.max_speed_kmh > 0.f)
                        ? (s.speed_kmh / s.max_speed_kmh) : 0.f;
            if (t < 0.f) t = -t;
            if (t > 1.f) t = 1.f;
            // Same speed→pitch curve the player engine uses.
            ma_sound_set_pitch(&impl_->traffic_engine[i], 0.55f + t * 1.35f);
            break;
        }
    }
}

} // namespace pengine

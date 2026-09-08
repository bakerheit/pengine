#include <algorithm>
#include <cstdio>
#include <vector>

#include "audio/player_car_assets.h"
#include "app/player_car_catalog.h"
#include "audio/vehicle_audio.h"
#include "audio/traffic_idle_audio.h"
#include "physics/vehicle.h"
#include "traffic/crowd.h"
#include "road_fixture.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

double render_rms(VoiceMixer& mixer, int blocks = 12) {
    std::vector<float> out(256u * 2u, 0.0f);
    double level = 0.0;
    for (int i = 0; i < blocks; ++i) {
        mixer.render(out.data(), 256u);
        level = rms(out);
    }
    return level;
}

double render_peak_rms(VoiceMixer& mixer, int blocks) {
    std::vector<float> out(256u * 2u, 0.0f);
    double level = 0.0;
    for (int i = 0; i < blocks; ++i) {
        mixer.render(out.data(), 256u);
        level = std::max(level, rms(out));
    }
    return level;
}

// A known diagnostic tone measures playback pitch, not the complex recording's
// changing harmonic content. It is test-only; runtime still uses shipped WAVs.
PcmClip pitch_probe(float seconds = 1.0f) {
    PcmClip clip;
    clip.sample_rate = kDefaultSampleRate;
    clip.channels = 1;
    clip.samples.resize(static_cast<std::size_t>(seconds * kDefaultSampleRate));
    for (std::size_t i = 0; i < clip.samples.size(); ++i) {
        clip.samples[i] = static_cast<float>(0.2 * std::sin(6.283185307179586 * 200.0 *
                                       static_cast<double>(i) / kDefaultSampleRate));
    }
    return clip;
}

double rendered_pitch(VoiceMixer& mixer) {
    render_rms(mixer, 20); // Settle the existing eight-millisecond smoothing.
    std::vector<float> stereo(8192), mono(4096);
    mixer.render(stereo.data(), mono.size());
    REQUIRE(all_finite(stereo));
    for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = stereo[i * 2];
    return estimate_f0(mono, kDefaultSampleRate, 80.0, 350.0);
}

void catalog_models_have_distinct_rendered_engine_notes() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    bank.engine_idle = pitch_probe();
    bank.player_throttle_hold = pitch_probe();
    VehicleAudio audio;
    audio.set_model(player_car_definition(PlayerCarId::GlrZip).mesh_path);
    REQUIRE(audio.start(mixer, bank));
    VehicleAudioFrame frame;
    audio.update(frame);
    REQUIRE_NEAR(rendered_pitch(mixer), 224.0, 0.8);

    std::vector<float> previous(512), next(512);
    std::vector<float> pitches;
    for (const auto& car : kPlayerCars) {
        const auto profile = vehicle_sound_profile(car.mesh_path);
        REQUIRE(profile.model_key != "default");
        REQUIRE(profile.pitch >= 0.80f && profile.pitch <= 1.12f);
        REQUIRE(std::find(pitches.begin(), pitches.end(), profile.pitch) == pitches.end());
        pitches.push_back(profile.pitch);
        mixer.render(previous.data(), 256);
        audio.set_model(car.mesh_path);
        audio.update(frame);
        mixer.render(next.data(), 256);
        // A live model change must retain the loop's cursor and gain. This
        // bound is several times the largest normal slope of our probe tone.
        REQUIRE(std::abs(next.front() - previous[previous.size() - 2]) < .01f);
        REQUIRE(max_adjacent_step(next) < .01);
        REQUIRE_NEAR(rendered_pitch(mixer), 200.0 * profile.pitch, 0.8);
        REQUIRE(mixer.active_voices() == 2u);
    }
    REQUIRE_NEAR(vehicle_sound_profile("unknown/body.emesh").pitch, 1.0, 1e-6);
    REQUIRE_NEAR(vehicle_sound_profile("models/vehicles/car5_custom/body.emesh").pitch, 1.0, 1e-6);
    REQUIRE_NEAR(vehicle_sound_profile("/tmp/assets/models/vehicles/firetruck/body_surface.emesh").pitch, .80, 1e-6);
    REQUIRE(mixer.dropped_commands() == 0);
    pass("every catalog model has a distinct measured idle note; live changes retain smooth bounded voices");
}

void cinder_model_uses_its_rendered_engine_note() {
    const auto& car=player_car_definition(PlayerCarId::OrisonCinderGt);
    REQUIRE(vehicle_sound_profile(car.mesh_path).model_key=="orison_cinder");
    REQUIRE_NEAR(vehicle_sound_profile("orison_cinder").pitch,1.09f,1e-6f);
    REQUIRE(vehicle_sound_profile("orison_cinder_custom/body.emesh").model_key=="default");
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    bank.engine_idle=pitch_probe();
    bank.player_throttle_hold=pitch_probe();
    VehicleAudio audio;
    audio.set_model(car.mesh_path);
    REQUIRE(audio.start(mixer,bank));
    VehicleAudioFrame frame;
    audio.update(frame);
    REQUIRE_NEAR(rendered_pitch(mixer),218.0,.8);
    REQUIRE(mixer.active_voices()==2u);
    audio.set_model("car5");
    audio.update(frame);
    REQUIRE_NEAR(rendered_pitch(mixer),200.0,.8);
    audio.set_model(car.mesh_path);
    audio.update(frame);
    REQUIRE_NEAR(rendered_pitch(mixer),218.0,.8);
    REQUIRE(mixer.active_voices()==2u);
    REQUIRE(mixer.dropped_commands()==0);
    audio.stop();
    render_rms(mixer,40);
    REQUIRE(mixer.active_voices()==0);
    pass("Cinder uses its measured engine note and keeps bounded voices across model changes");
}

void model_pitch_preserves_rpm_response_and_attack_timing() {
    for (const auto key : {"firetruck", "glr_zip"}) {
        const float pitch = vehicle_sound_profile(key).pitch;
        VoiceMixer mixer;
        mixer.prepare(kDefaultSampleRate);
        SfxBank bank;
        bank.player_throttle_hold = pitch_probe();
        VehicleAudio audio;
        audio.set_model(key);
        REQUIRE(audio.start(mixer, bank));
        VehicleAudioFrame frame;
        frame.accelerating = true;
        frame.dt_seconds = .01f;
        std::vector<float> out(960);
        double source_time = 0;
        while (source_time < 2.0) {
            audio.update(frame);
            mixer.render(out.data(), 480);
            source_time += .01 * pitch;
        }
        REQUIRE(rms(out) < 1e-8); // No premature hold under a slowed attack.
        while (source_time < 2.8) {
            audio.update(frame);
            mixer.render(out.data(), 480);
            source_time += .01 * pitch;
        }
        REQUIRE(rms(out) > .005);
        for (float rpm : {900.0f, 4100.0f, 6800.0f}) {
            frame.engine_rpm = rpm;
            audio.update(frame);
            REQUIRE_NEAR(rendered_pitch(mixer),
                         200.0 * pitch * recorded_throttle_pitch(rpm), 0.8);
        }
        REQUIRE(mixer.active_voices() == 1u);
        REQUIRE(mixer.dropped_commands() == 0u);
    }
    pass("light/heavy engines preserve measured RPM response and pitch-adjusted attack-to-hold timing");
}

void model_pitch_applies_to_attack_release_and_ignition() {
    for (const auto key : {"firetruck", "glr_zip"}) {
        const float pitch = vehicle_sound_profile(key).pitch;
        VoiceMixer mixer;
        mixer.prepare(kDefaultSampleRate);
        SfxBank bank;
        bank.engine_start = pitch_probe();
        bank.player_throttle_attack = pitch_probe(3.0f);
        bank.player_throttle_release = pitch_probe(3.0f);
        VehicleAudio audio;
        audio.set_model(key);
        REQUIRE(audio.start(mixer, bank));
        VehicleAudioFrame frame;
        frame.accelerating = true;
        audio.update(frame);
        REQUIRE_NEAR(rendered_pitch(mixer), 200.0 * pitch, .8);
        frame.accelerating = false;
        audio.update(frame);
        REQUIRE_NEAR(rendered_pitch(mixer), 200.0 * .72 * pitch, .8);

        audio.enter_vehicle(false, {});
        audio.update(frame);
        REQUIRE_NEAR(rendered_pitch(mixer), 200.0 * pitch, .8);
        // Restart for synchronized controller/audio clocks. A profile switch
        // during ignition must not retune its one-shot or alter its duration.
        audio.enter_vehicle(false, {});
        audio.set_model("vesper_mistral");
        frame.dt_seconds = 1.0f / 120.0f;
        std::vector<float> out(800);
        float elapsed = 0;
        while (audio.starting()) {
            audio.update(frame);
            mixer.render(out.data(), 400);
            elapsed += frame.dt_seconds;
            REQUIRE(elapsed < 1.5f);
        }
        REQUIRE_NEAR(elapsed, 1.0 / pitch, 1.0 / 120.0 + 1e-5);
        REQUIRE(mixer.active_voices() == 0u);
        audio.enter_vehicle(true, {});
        REQUIRE(!audio.starting());
        REQUIRE(audio.startup_count() == 2);
        REQUIRE(mixer.dropped_commands() == 0);
    }
    pass("model pitch reaches attack/release/ignition; ignition duration matches PCM and traffic entry stays warm");
}

void traffic_model_pitch_is_stable_across_reordering() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    bank.engine_idle = pitch_probe();
    TrafficIdleAudio traffic;
    traffic.start(mixer, bank);
    for (const auto key : {"firetruck", "glr_zip"}) {
        for (float speed : {0.0f, 10.0f, -10.0f}) {
            traffic.begin_frame({});
            traffic.submit(27, 4, {}, speed, key);
            traffic.end_frame(true);
            REQUIRE_NEAR(rendered_pitch(mixer),
                         200.0 * traffic_idle_pitch(27, 4, speed, vehicle_sound_profile(key)), .8);
        }
    }
    VoiceMixer reference;
    reference.prepare(kDefaultSampleRate);
    TrafficIdleAudio reordered;
    reordered.start(reference, bank);
    traffic.stop();
    traffic.start(mixer, bank);
    std::vector<float> a(512), b(512);
    for (int frame = 0; frame < 20; ++frame) {
        traffic.begin_frame({});
        reordered.begin_frame({});
        traffic.submit(1, 2, {}, 3, "firetruck");
        traffic.submit(2, 3, {}, 6, "glr_zip");
        if (frame % 2) reordered.submit(2, 3, {}, 6, "glr_zip");
        reordered.submit(1, 2, {}, 3, "firetruck");
        if (!(frame % 2)) reordered.submit(2, 3, {}, 6, "glr_zip");
        traffic.end_frame(true);
        reordered.end_frame(true);
        mixer.render(a.data(), 256);
        reference.render(b.data(), 256);
        REQUIRE(a == b);
        REQUIRE(traffic.loop_count() == 2u);
    }
    REQUIRE(mixer.dropped_commands() == 0);
    REQUIRE(reference.dropped_commands() == 0);
    pass("traffic model/speed pitch is measured; reordered identities produce identical PCM");
}

void the_player_car_opens_a_bounded_voice_set() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;

    REQUIRE(audio.start(mixer, bank));
    REQUIRE(audio.loop_count() == 4u);

    VehicleAudioFrame frame;
    audio.update(frame);

    const double coasting = render_rms(mixer);
    REQUIRE_MSG(coasting > .005, "recorded idle is silent", "recorded-only runtime");

    frame.accelerating = true;
    audio.update(frame);
    const double sounding = render_peak_rms(mixer, 12);
    REQUIRE_MSG(sounding > 0.005, "the recorded acceleration is silent",
                "runtime mix");
    REQUIRE(mixer.dropped_commands() == 0u);

    std::printf("      (%zu persistent loops, acceleration RMS %.4f)\n",
                audio.loop_count(), sounding);
    pass("normal driving opens the recorded throttle and idle loops");
}

void coasting_player_keeps_the_idle_loop_audible() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    bank.engine_idle = pitch_probe();
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    VehicleAudioFrame frame;
    frame.accelerating = false;
    frame.engine_rpm = 4100.0f;
    audio.update(frame);
    REQUIRE_MSG(render_rms(mixer, 24) > .005,
                "coasting muted the idle loop at higher RPM",
                "player idle");

    frame.accelerating = true;
    audio.update(frame);
    REQUIRE_MSG(render_rms(mixer, 24) < 1e-8,
                "active throttle should still duck the idle bed at higher RPM",
                "player idle");
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("coasting player audio keeps the idle loop audible even above idle RPM");
}

void burnout_plays_at_rest_and_mutes() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    REQUIRE(load_wav_clip(player_car_audio_overrides().player_burnout, bank.player_burnout));
    REQUIRE(std::fabs(bank.player_burnout.duration_seconds() - 3.0f) < .0001f);
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));
    VehicleAudioFrame frame;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    frame.burnout_amount = 1.0f;
    audio.update(frame);
    REQUIRE(render_peak_rms(mixer, 600) > .005);
    frame.active = false;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    frame.active = true;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) > .005);
    audio.exit_vehicle();
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    audio.stop();
    REQUIRE(audio.loop_count() == 0u);
    pass("real burnout clip plays at rest and mutes on pause and exit");
}

void handbrake_and_drift_play_the_recorded_tyre_screech() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    bank.player_drift_tyres = pitch_probe();
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));
    REQUIRE(audio.loop_count() == 1u);

    VehicleAudioFrame frame;
    frame.speed_mps = 12.0f;
    frame.handbrake = 1.0f;
    audio.update(frame);
    REQUIRE_MSG(render_rms(mixer, 24) > .005,
                "handbrake at speed did not play tyre screech",
                "drift tyres");

    frame.handbrake = 0.0f;
    frame.drift_slip = 2.0f;
    audio.update(frame);
    REQUIRE_MSG(render_rms(mixer, 24) > .005,
                "rear slip did not keep tyre screech alive",
                "drift tyres");

    frame.speed_mps = 0.5f;
    frame.drift_slip = 2.0f;
    frame.handbrake = 1.0f;
    audio.update(frame);
    REQUIRE_MSG(render_rms(mixer, 40) < 1e-8,
                "parking-lot speed produced tyre screech",
                "drift tyres");
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("handbrake and drift slip play the recorded tyre screech loop");
}

void pause_stops_the_recording_without_retriggering_held_throttle() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    VehicleAudioFrame frame;
    frame.accelerating = true;
    audio.update(frame);
    const double driving = render_peak_rms(mixer, 12);
    REQUIRE(driving > 0.005);

    frame.active = false;
    audio.update(frame);
    const double paused = render_rms(mixer, 32);
    REQUIRE_MSG(paused < 1e-8, "pause left the car sounding",
                "pause mute");

    frame.active = true;
    audio.update(frame);
    render_rms(mixer);
    REQUIRE_MSG(mixer.active_voices() == audio.loop_count(),
                "resume retriggered held throttle", "modal edge");

    frame.accelerating = false;
    audio.update(frame);
    frame.accelerating = true;
    audio.update(frame);
    REQUIRE(render_peak_rms(mixer, 12) > 0.005);
    pass("pause stops audio and held throttle does not fake a new press");
}

void every_f1_car_sound_choice_is_real_and_audible() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    for (std::size_t use = 0; use < kCarSoundUseCount; ++use) {
        for (int variant = 0; variant < kCarSoundVariantCount; ++variant) {
            const VoiceMix preview = car_sound_audition_mix(
                bank, static_cast<CarSoundUse>(use), variant);
            REQUIRE(preview.clip != nullptr);
            REQUIRE(!preview.clip->empty());
            REQUIRE(preview.gain > 0.0f);
            REQUIRE(preview.pitch > 0.0f);
        }
    }

    audio.audition(CarSoundUse::Crash, 4);
    const double preview_level = render_peak_rms(mixer, 200);
    REQUIRE_MSG(preview_level > 0.005, "the F1 preview is silent",
                "sound audition");
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("all 25 F1 car sound choices point at audible PCM");
}

void acceleration_recording_plays_once_per_throttle_press() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));
    REQUIRE(acceleration_sound_for_gear(-1) == -1);
    REQUIRE(acceleration_sound_for_gear(1) == 0);
    REQUIRE(acceleration_sound_for_gear(2) == 1);
    REQUIRE(acceleration_sound_for_gear(5) == 4);
    REQUIRE(acceleration_sound_for_gear(6) == 4);
    REQUIRE_NEAR(recorded_throttle_pitch(900.0f), 0.78, 1e-6);
    REQUIRE_NEAR(recorded_throttle_pitch(6800.0f), 1.34, 1e-6);
    REQUIRE(recorded_throttle_pitch(6600.0f) >
            recorded_throttle_pitch(4100.0f) + 0.20f);
    REQUIRE_NEAR(recorded_shift_gain(0.0f), 0.18, 1e-6);
    REQUIRE_NEAR(recorded_shift_gain(0.18f), 1.0, 1e-6);

    VehicleAudioFrame frame;
    frame.active = true;
    frame.accelerating = false;
    audio.update(frame);
    std::vector<float> out(64u * 2u, 0.0f);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count());

    frame.accelerating = true;
    frame.engine_rpm = 1800.0f;
    audio.update(frame);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);

    // Holding the pedal updates the car but does not stack another copy.
    audio.update(frame);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);

    // Let the attack finish and the real static section take over.
    frame.dt_seconds = 0.1f;
    for (int i = 0; i < 30; ++i) audio.update(frame);
    for (int i = 0; i < 2200; ++i) mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count());

    // Gear changes alter simulated RPM, but do not restart the recorded ramp.
    frame.gear = 2;
    frame.engine_rpm = 4100.0f;
    audio.update(frame);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count());

    frame.accelerating = false;
    audio.update(frame);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);
    // The 2.05-second source is deliberately slowed to roughly 2.85 seconds,
    // so it must still be playing after its former native-duration cutoff.
    for (int i = 0; i < 1600; ++i) mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);
    for (int i = 0; i < 800; ++i) mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count());
    pass("attack, held throttle and release transition without shift restarts");
}

void sustained_throttle_never_reaches_a_quiet_source_tail() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    VehicleAudioFrame frame;
    frame.accelerating = true;
    frame.engine_rpm = 6800.0f;
    frame.gear = 5;
    frame.dt_seconds = 1.0f / 120.0f;
    std::vector<float> out(400u * 2u, 0.0f);

    // At redline the held clip wraps roughly every 3.7 seconds. Cover four
    // wraps after the attack-to-hold handoff: a full rise/release recording
    // used as a loop goes quiet near its release tail at this exact point.
    for (int tick = 0; tick < 2400; ++tick) {
        audio.update(frame);
        mixer.render(out.data(), 400u);
        if (tick >= 360) {
            REQUIRE_MSG(rms(out) > .005,
                        "held throttle reached a quiet source tail",
                        "sustained throttle");
        }
    }
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("held throttle remains audible across repeated high-RPM wraps");
}

void car_to_car_collision_uses_the_dedicated_recording() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    REQUIRE_NEAR(recorded_collision_gain(2.5f), 0.0, 1e-6);
    REQUIRE_NEAR(recorded_collision_gain(15.0f), 0.95, 1e-6);

    audio.play_car_collision(10.0f, {2.0f, 0.0f, 4.0f});
    REQUIRE(render_peak_rms(mixer, 24) > 0.005);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);

    audio.play_car_collision(1.0f, {2.0f, 0.0f, 4.0f});
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1u);
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("car-to-car impacts use the dedicated real crash recording");
}

void stolen_vehicle_contacts_reach_the_real_crash_recording() {
    const SfxBank bank=[] {
        auto result=synth_bank(kDefaultSampleRate);
        override_bank_from_wavs(result,player_car_audio_overrides());
        return result;
    }();
    const auto audible=[&](const VehicleState& car) {
        REQUIRE(car.car_contact_speed>2.5f);
        VoiceMixer mixer;
        mixer.prepare(kDefaultSampleRate);
        VehicleAudio audio;
        REQUIRE(audio.start(mixer,bank));
        Listener listener;
        listener.position=car.position;
        audio.set_listener(listener);
        audio.play_car_collision(car.car_contact_speed,car.position);
        REQUIRE(render_peak_rms(mixer,24)>0.005);
        REQUIRE(mixer.active_voices()==audio.loop_count()+1u);
        REQUIRE(mixer.dropped_commands()==0u);
    };

    // Taking an actor retires only that identity, not its vehicle kind. Hit
    // a different actor of the SAME kind, using the real traffic solver.
    RoadGraph roads;
    roads.build(make_test_spines(),RoadGraphParams{},GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads,GroundSampler{},LaneBuildParams{});
    Crowd crowd;
    CrowdTuning crowd_tuning;
    crowd_tuning.vehicle_activate_m=260;
    crowd_tuning.vehicle_retire_m=340;
    crowd_tuning.max_peds=0;
    crowd_tuning.police.patrol_fraction=0.0f;  // stolen civilian-car audio fixture
    crowd.build(lanes,0xA9C011ull,AmbientTuning{},crowd_tuning);
    crowd.refresh(0,{0,0});
    REQUIRE(crowd.vehicles().size()>8);
    const auto source=crowd.vehicles().front();
    VehicleAgent taken;
    REQUIRE(crowd.take_vehicle(source.lane_key,source.slot,taken));
    const auto kind=traffic_vehicle_kind(taken.lane_key,taken.slot);
    const auto match=std::find_if(crowd.vehicles().begin(),crowd.vehicles().end(),[&](const VehicleAgent& v) {
        return traffic_vehicle_kind(v.lane_key,v.slot)==kind;
    });
    REQUIRE(match!=crowd.vehicles().end());
    const auto target=*match;
    const auto fwd=glm::normalize(glm::vec3{target.fwd.x,0,target.fwd.z});
    const glm::vec3 right{-fwd.z,0,fwd.x};
    VehicleState stolen;
    stolen.body_damage=taken.body_damage;
    stolen.position=target.pos;
    stolen.orientation=glm::angleAxis(std::atan2(-fwd.x,-fwd.z),glm::vec3{0,1,0});
    stolen.velocity=target.fwd*target.speed_mps-right*10.0f;
    const auto footprint=traffic_vehicle_footprint(kind);
    REQUIRE(crowd.resolve_player_collision(stolen,footprint.half_width_m,footprint.half_length_m));
    audible(stolen);

    // Parked vehicles live in the world collider, so the traffic-only event
    // gate previously dropped this impact even though damage was registered.
    TerrainCollider collider{42};
    collider.add_static_ground_rect({0,0},200,{30,30},0);
    const auto parked=collider.add_kinematic_oriented_box({0,201,0},{1,1,2.5f},0);
    REQUIRE(collider.set_kinematic_vehicle(parked,true));
    REQUIRE(collider.set_kinematic_oriented_box(parked,{0,201,0},{1,1,2.5f},0));
    REQUIRE(collider.static_boxes()[parked].is_vehicle);
    REQUIRE(!collider.set_kinematic_vehicle(99999,true));
    VehicleTuning tuning;
    auto moving=spawn_vehicle(tuning,collider,2,0,0);
    // spawn_vehicle samples native terrain; place this test on the elevated
    // authored slab, where the parked body is actually sitting.
    moving.position.y=200.0f+static_ride_height(tuning);
    moving.orientation=glm::quat{1,0,0,0};
    moving.velocity={-10,0,0};
    auto hit=step_vehicle(moving,tuning,InputFrame{},collider,1.0f/120.0f);
    REQUIRE(hit.impact_count>moving.impact_count);
    audible(hit);
    // No repeated sound from the stale event on subsequent contact-free ticks.
    REQUIRE(collider.set_kinematic_enabled(parked,false));
    const auto clear=step_vehicle(hit,tuning,InputFrame{},collider,1.0f/120.0f);
    REQUIRE_NEAR(clear.car_contact_speed,0,1e-6);
    // Walls still use their own impact contract, not the car-crash recording.
    REQUIRE(collider.set_kinematic_enabled(parked,true));
    REQUIRE(collider.set_kinematic_vehicle(parked,false));
    const auto wall=step_vehicle(moving,tuning,InputFrame{},collider,1.0f/120.0f);
    REQUIRE(wall.impact_count>moving.impact_count);
    REQUIRE_NEAR(wall.car_contact_speed,0,1e-6);
    pass("stolen same-kind traffic and parked-car impacts produce audible real crash PCM; walls and stale events do not");
}

void shutdown_stops_the_active_recording() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank = synth_bank(kDefaultSampleRate);
    REQUIRE(override_bank_from_wavs(bank, player_car_audio_overrides()) ==
            11u + kCarSoundUseCount *
                     static_cast<std::size_t>(kCarSoundVariantCount));
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));

    VehicleAudioFrame frame;
    frame.accelerating = true;
    audio.update(frame);
    audio.play_car_collision(10.0f, frame.position);

    std::vector<float> out(64u * 2u, 0.0f);
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == audio.loop_count() + 2u);

    audio.stop();
    mixer.render(out.data(), 64u);
    REQUIRE(mixer.active_voices() == 0u);
    pass("shutdown stops active throttle and collision recordings");
}

void an_empty_bank_is_normal_silence() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    VehicleAudio audio;
    const SfxBank empty;
    REQUIRE(!audio.start(mixer, empty));
    REQUIRE(!audio.started());
    REQUIRE(audio.loop_count() == 0u);
    audio.update(VehicleAudioFrame{});
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("a failed playback device leaves vehicle audio safely inert");
}

void parked_ignition_and_idle_lifecycle() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    override_bank_from_wavs(bank, player_car_audio_overrides());
    REQUIRE(bank.engine_start.duration_seconds() > 1.0f);
    REQUIRE(bank.engine_start.duration_seconds() < 4.0f);
    REQUIRE(bank.engine_idle.duration_seconds() > 2.0f);
    REQUIRE(bank.engine_idle.channels == 1);
    const auto& samples = bank.engine_idle.samples;
    std::vector<float> steps;
    for (std::size_t i = 1; i < samples.size(); ++i) steps.push_back(std::fabs(samples[i] - samples[i-1]));
    std::sort(steps.begin(), steps.end());
    REQUIRE(std::fabs(samples.front() - samples.back()) < steps[steps.size() * 999 / 1000]);
    VehicleAudio audio;
    REQUIRE(audio.start(mixer, bank));
    VehicleAudioFrame frame;
    frame.engine_running = false;
    frame.dt_seconds = 1.0f / 60.0f;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    REQUIRE(audio.startup_count() == 0);

    audio.enter_vehicle(false, frame.position);
    frame.engine_running = true;
    audio.update(frame);
    REQUIRE(audio.starting());
    REQUIRE(audio.startup_count() == 1);
    REQUIRE(render_peak_rms(mixer, 24) > .005);
    std::vector<float> out(800 * 2);
    for (int i = 0; i < 240; ++i) {
        audio.update(frame);
        mixer.render(out.data(), 800);
    }
    REQUIRE(!audio.starting());
    REQUIRE(audio.startup_count() == 1);
    REQUIRE(render_rms(mixer) > .005);
    REQUIRE(mixer.active_voices() == audio.loop_count());

    audio.exit_vehicle();
    frame.engine_running = false;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    audio.enter_vehicle(true, frame.position); // Already-driven traffic car.
    frame.engine_running = true;
    audio.update(frame);
    REQUIRE(!audio.starting());
    REQUIRE(audio.startup_count() == 1);
    REQUIRE(render_rms(mixer) > .005);

    audio.enter_vehicle(false, frame.position);
    audio.update(frame);
    frame.active = false;
    audio.update(frame);
    REQUIRE(render_rms(mixer, 40) < 1e-8);
    frame.active = true;
    audio.update(frame);
    REQUIRE(!audio.starting());
    REQUIRE(audio.startup_count() == 2); // Resume did not replay ignition.

    audio.enter_vehicle(false, frame.position);
    frame.accelerating = true;
    for (int i = 0; i < 240; ++i) {
        audio.update(frame);
        mixer.render(out.data(), 800);
    }
    REQUIRE(!audio.starting());
    REQUIRE(mixer.active_voices() == audio.loop_count() + 1); // Held pedal catches after startup.
    REQUIRE(mixer.dropped_commands() == 0);
    audio.stop();
    mixer.render(out.data(), 800);
    REQUIRE(mixer.active_voices() == 0);
    pass("real idle seam, silent parked cars, one ignition per entry, traffic takeover, pause and held-pedal handoff");
}

void traffic_idle_is_spatial_and_bounded() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    SfxBank bank;
    override_bank_from_wavs(bank, player_car_audio_overrides());
    TrafficIdleAudio traffic;
    traffic.start(mixer, bank);
    traffic.begin_frame({0,0,0});
    for (uint32_t i = 0; i < 1000; ++i) traffic.submit(7, i, {static_cast<float>(i % 100), 0, 3}, 0);
    traffic.end_frame(true);
    REQUIRE(traffic.loop_count() == TrafficIdleAudio::kVoiceBudget);
    REQUIRE(render_rms(mixer) > .005);
    for (int frame = 0; frame < 120; ++frame) {
        traffic.begin_frame({0,0,0});
        for (int i = 999; i >= 0; --i) traffic.submit(7, static_cast<uint32_t>(i), {static_cast<float>(i % 100), 0, 3}, 0);
        traffic.end_frame(true);
        render_rms(mixer, 4);
        REQUIRE(mixer.active_voices() == TrafficIdleAudio::kVoiceBudget);
    }
    traffic.begin_frame({10000,0,0});
    traffic.submit(7, 1, {0,0,0}, 0);
    traffic.end_frame(true);
    REQUIRE(render_rms(mixer) < 1e-8);
    REQUIRE(traffic.loop_count() == 0);
    traffic.begin_frame({0,0,0});
    traffic.submit(7, 1, {0,0,3}, 0);
    traffic.end_frame(true);
    REQUIRE(render_rms(mixer) > .001);
    traffic.end_frame(false);
    REQUIRE(render_rms(mixer) < 1e-8);
    REQUIRE(mixer.dropped_commands() == 0);
    traffic.stop();
    pass("1000 traffic candidates share twelve spatial idle voices; distance and pause are silent");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--cinder") {
        cinder_model_uses_its_rendered_engine_note();
        return done("Cinder audio");
    }
    if (argc == 2 && std::string(argv[1]) == "--burnout") {
        burnout_plays_at_rest_and_mutes();
        the_player_car_opens_a_bounded_voice_set();
        handbrake_and_drift_play_the_recorded_tyre_screech();
        return done("burnout audio");
    }
    if (argc == 2 && std::string(argv[1]) == "--sustain") {
        sustained_throttle_never_reaches_a_quiet_source_tail();
        return done("sustained throttle audio");
    }
    std::printf("audio_vehicle_runtime_tests\n");
    cinder_model_uses_its_rendered_engine_note();
    catalog_models_have_distinct_rendered_engine_notes();
    model_pitch_preserves_rpm_response_and_attack_timing();
    model_pitch_applies_to_attack_release_and_ignition();
    traffic_model_pitch_is_stable_across_reordering();
    the_player_car_opens_a_bounded_voice_set();
    coasting_player_keeps_the_idle_loop_audible();
    burnout_plays_at_rest_and_mutes();
    handbrake_and_drift_play_the_recorded_tyre_screech();
    pause_stops_the_recording_without_retriggering_held_throttle();
    every_f1_car_sound_choice_is_real_and_audible();
    acceleration_recording_plays_once_per_throttle_press();
    sustained_throttle_never_reaches_a_quiet_source_tail();
    car_to_car_collision_uses_the_dedicated_recording();
    stolen_vehicle_contacts_reach_the_real_crash_recording();
    shutdown_stops_the_active_recording();
    an_empty_bank_is_normal_silence();
    parked_ignition_and_idle_lifecycle();
    traffic_idle_is_spatial_and_bounded();
    return done("audio_vehicle_runtime_tests");
}

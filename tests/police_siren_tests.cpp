#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

#include "audio/police_siren.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

double local_frequency(const PcmClip& clip, double time) {
    // Interpolated rising zero crossings measure the actual generated signal.
    const auto first = static_cast<std::size_t>((time - .02) * clip.sample_rate);
    const auto last = static_cast<std::size_t>((time + .02) * clip.sample_rate);
    double first_crossing = 0, last_crossing = 0;
    unsigned crossings = 0;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double a = clip.samples[i - 1], b = clip.samples[i];
        if (a <= 0 && b > 0) {
            const double crossing = static_cast<double>(i - 1) - a / (b - a);
            if (crossings == 0) first_crossing = crossing;
            last_crossing = crossing;
            ++crossings;
        }
    }
    REQUIRE(crossings > 10);
    return static_cast<double>(crossings - 1) * clip.sample_rate /
           (last_crossing - first_crossing);
}

void waveform_is_bounded_and_a_seamless_rising_falling_wail() {
    const auto& clip = police_siren_wail();
    REQUIRE(&clip == &police_siren_wail());
    REQUIRE(clip.channels == 1);
    REQUIRE(clip.sample_rate == 48000);
    REQUIRE(clip.frame_count() == 192000);
    REQUIRE(all_finite(clip.samples));
    REQUIRE(peak(clip.samples) <= .94f);
    REQUIRE(rms(clip.samples) > .4);
    REQUIRE(std::abs(dc_offset(clip.samples)) < 1e-5);
    const double low = local_frequency(clip, .05);
    const double rising = local_frequency(clip, 1.0);
    const double high = local_frequency(clip, 2.0);
    const double falling = local_frequency(clip, 3.0);
    REQUIRE(low > 700 && low < 705);
    REQUIRE_NEAR(rising, 1100, 2.0);
    REQUIRE(high > 1498 && high <= 1500);
    REQUIRE_NEAR(falling, 1100, 2.0);
    REQUIRE_NEAR(local_frequency(clip, 3.95), low, 1.0);
    REQUIRE(seam_step(clip.samples) <= max_adjacent_step(clip.samples));
    // Compare slopes on both sides of wrap, not just endpoint values.
    const double incoming = clip.samples.front() - clip.samples.back();
    const double outgoing = clip.samples[1] - clip.samples.front();
    REQUIRE_NEAR(incoming, outgoing, 1e-5);
    pass("deterministic 4-second wail: measured 700-1500 Hz sweep, finite bounded PCM, continuous wrap slope");
}

void toggle_pause_position_and_category_are_safe() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    PoliceSiren siren;
    siren.update(true, true, {});
    siren.stop();
    REQUIRE(siren.start(mixer));
    REQUIRE(siren.start(mixer));
    REQUIRE(siren.loop_count() == 1);
    std::vector<float> out(9600);
    mixer.render(out.data(), 4800);
    REQUIRE(rms(out) == 0);
    const auto* samples = police_siren_wail().samples.data();
    float last_left = 0;
    for (int frame = 0; frame < 160; ++frame) {
        const bool active = frame % 4 != 2;
        const bool enabled = frame % 4 != 3;
        siren.update(active, enabled, {});
        mixer.render(out.data(), 4800);
        REQUIRE(all_finite(out));
        REQUIRE(peak(out) < .31f);
        // A 1500 Hz three-harmonic signal has a bounded natural sample slope;
        // toggling uses the existing gain ramp, never a hard voice stop/start.
        REQUIRE(std::abs(out.front() - last_left) < .06f);
        REQUIRE(max_adjacent_step(out) < .06);
        last_left = out[out.size() - 2];
        if (active && enabled) REQUIRE(rms(out) > .08);
        else {
            std::vector<float> tail(out.end() - 512, out.end());
            REQUIRE(rms(tail) < 2e-6);
        }
        REQUIRE(mixer.active_voices() == 1);
        REQUIRE(samples == police_siren_wail().samples.data());
    }
    // Repeated unchanged updates do not fill the queue while callbacks pause.
    for (int i = 0; i < 10000; ++i) siren.update(false, true, {});
    REQUIRE(mixer.dropped_commands() == 0);
    siren.update(true, true, {1000, 0, 0});
    mixer.render(out.data(), 4800);
    REQUIRE(rms(out) < 1e-6);
    siren.update(true, true, {});
    mixer.render(out.data(), 4800);
    REQUIRE(rms(out) > .08);
    mixer.set_category(Category::Engine, 0);
    mixer.render(out.data(), 4800);
    mixer.render(out.data(), 4800);
    REQUIRE(rms(out) < 1e-6);
    mixer.set_category(Category::Engine, 1);
    siren.update(true, true, {std::numeric_limits<float>::quiet_NaN(), 0, 0});
    mixer.render(out.data(), 4800);
    REQUIRE(all_finite(out));
    REQUIRE(rms(out) < 1e-6);
    siren.stop();
    siren.stop();
    mixer.render(out.data(), 4800);
    REQUIRE(mixer.active_voices() == 0);
    REQUIRE(rms(out) == 0);
    REQUIRE(mixer.dropped_commands() == 0);
    pass("toggle/pause fade smoothly; one spatial Engine voice, stable PCM, safe repeated updates and teardown");
}

void queued_lifecycle_never_dangles_pcm_or_stacks_voices() {
    static_assert(!std::is_copy_constructible_v<PoliceSiren>);
    static_assert(!std::is_move_constructible_v<PoliceSiren>);
    VoiceMixer mixer;
    mixer.prepare(44100); // Exercise resampling across multiple loop seams.
    std::vector<float> out(8820);
    for (int i = 0; i < 30; ++i) {
        {
            PoliceSiren siren;
            REQUIRE(siren.start(mixer));
            siren.update(true, true, {});
            if (i % 2) mixer.render(out.data(), 4410);
        } // Pending Start/Update/Stop can be drained after controller death.
        mixer.render(out.data(), 4410);
        REQUIRE(rms(out) == 0);
        REQUIRE(mixer.active_voices() == 0);
    }
    PoliceSiren siren;
    REQUIRE(siren.start(mixer));
    siren.update(true, true, {});
    float last_left = 0;
    for (int i = 0; i < 100; ++i) {
        mixer.render(out.data(), 4410);
        REQUIRE(all_finite(out));
        REQUIRE(rms(out) > .08);
        REQUIRE(max_adjacent_step(out) < .065);
        REQUIRE(std::abs(out.front() - last_left) < .065f);
        last_left = out[out.size() - 2];
        REQUIRE(mixer.active_voices() == 1);
    }
    siren.stop();
    REQUIRE(mixer.dropped_commands() == 0);
    pass("destroy/restart with queued commands is safe; 44.1 kHz resampled playback crosses seams without gaps");
}

void silent_and_full_mixers_fail_without_leaking_slots() {
    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    PoliceSiren siren;
    mixer.set_silent(true);
    REQUIRE(!siren.start(mixer));
    REQUIRE(!siren.started());
    mixer.set_silent(false);
    std::vector<VoiceHandle> handles;
    std::vector<float> out(128);
    for (;;) {
        const auto handle = mixer.open_loop(&police_siren_wail(), VoiceParams{});
        if (!handle.valid()) break;
        handles.push_back(handle);
        mixer.render(out.data(), 64);
    }
    REQUIRE(!siren.start(mixer));
    REQUIRE(siren.loop_count() == 0);
    mixer.close_loop(handles.back());
    handles.pop_back();
    REQUIRE(siren.start(mixer));
    siren.stop();
    for (const auto handle : handles) mixer.close_loop(handle);
    mixer.render(out.data(), 64);
    REQUIRE(mixer.active_voices() == 0);
    REQUIRE(mixer.dropped_commands() == 0);
    pass("silent/full mixer startup fails cleanly and a released slot can be retried");
}

}  // namespace

int main() {
    waveform_is_bounded_and_a_seamless_rising_falling_wail();
    toggle_pause_position_and_category_are_safe();
    queued_lifecycle_never_dangles_pcm_or_stacks_voices();
    silent_and_full_mixers_fail_without_leaking_slots();
    return done("police_siren_tests");
}

// The respray booth's sounds (app/respray_audio.h): the hiss while the gun is
// on the car, and the three stingers that say how the respray ended.
//
// Nobody can hear this suite, so every claim is a number measured off the real
// synthesised samples and the real VoiceMixer: that each clip is playable,
// that the hiss is a steady unpitched hiss, that the three outcomes differ in
// the notes the player actually hears, and that cancelling a spray fades the
// hiss rather than cutting it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/respray_audio.h"
#include "audio_analysis.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

constexpr double kRate = static_cast<double>(kDefaultSampleRate);

std::vector<float> window(const PcmClip& clip, double from_s, double to_s) {
    const double sr = static_cast<double>(clip.sample_rate);
    const std::size_t n = clip.samples.size();
    const std::size_t a = std::min(n, static_cast<std::size_t>(from_s * sr));
    const std::size_t b = std::min(n, static_cast<std::size_t>(to_s * sr));
    return std::vector<float>(clip.samples.begin() + static_cast<std::ptrdiff_t>(a),
                              clip.samples.begin() + static_cast<std::ptrdiff_t>(b));
}

double correlation(const std::vector<float>& x, const std::vector<float>& y) {
    const std::size_t n = std::min(x.size(), y.size());
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double a = static_cast<double>(x[i]);
        const double b = static_cast<double>(y[i]);
        xy += a * b;
        xx += a * a;
        yy += b * b;
    }
    return (xx > 0.0 && yy > 0.0) ? xy / std::sqrt(xx * yy) : 0.0;
}

double tone(const std::vector<float>& x, double hz) { return goertzel(x, kRate, hz); }

std::vector<float> render(VoiceMixer& mixer, std::size_t frames) {
    std::vector<float> out(frames * 2u, 0.0f);
    mixer.render(out.data(), frames);
    return out;
}

void require_playable(const PcmClip& clip, const char* name, double min_s, double max_s) {
    REQUIRE_MSG(clip.channels == 1u, "respray clips are mono", name);
    REQUIRE_MSG(clip.sample_rate == kDefaultSampleRate, "wrong sample rate", name);
    REQUIRE_MSG(all_finite(clip.samples), "a sample is NaN or infinite", name);
    REQUIRE_MSG(peak(clip.samples) <= 1.0f, "peak above full scale", name);
    // Normalised, not pre-attenuated: the mixer owns gain.
    REQUIRE_MSG(peak(clip.samples) >= 0.75f, "clip is not normalised", name);
    REQUIRE_MSG(rms(clip.samples) > 0.05, "clip is near silence", name);
    REQUIRE_MSG(std::fabs(dc_offset(clip.samples)) < 0.005, "clip carries DC", name);
    const double s = static_cast<double>(clip.duration_seconds());
    REQUIRE_MSG(s >= min_s && s <= max_s, "clip length out of range", name);
    // Starting or ending on a non-zero sample is a click on every playback.
    REQUIRE_MSG(clip.samples.front() == 0.0f && clip.samples.back() == 0.0f,
                "clip does not start and end on silence", name);
}

void every_clip_is_playable() {
    require_playable(synth_respray_hiss(), "hiss", 0.99, 1.01);
    require_playable(synth_respray_done(false), "painted", 0.30, 1.50);
    require_playable(synth_respray_done(true), "cleared", 0.30, 1.50);
    require_playable(synth_respray_kept(), "kept", 0.30, 1.50);

    // The hiss is authored to the one-second spray, to the frame.
    REQUIRE(synth_respray_hiss().frame_count() ==
            static_cast<std::size_t>(static_cast<double>(kResprayHissSeconds) * kRate));

    // Another device rate still builds clean clips of the same duration.
    const PcmClip hiss44 = synth_respray_hiss(44100u);
    REQUIRE(hiss44.frame_count() == 44100u);
    REQUIRE(all_finite(hiss44.samples) && peak(hiss44.samples) <= 1.0f);
    const PcmClip kept44 = synth_respray_kept(44100u);
    REQUIRE(all_finite(kept44.samples) && peak(kept44.samples) <= 1.0f);

    REQUIRE(synth_respray_hiss(0u).empty());
    REQUIRE(synth_respray_done(true, 0u).empty());
    REQUIRE(synth_respray_kept(0u).empty());
    pass("every respray clip is finite, normalised, silent at both ends, sensibly long");
}

void clips_are_deterministic() {
    REQUIRE_MSG(synth_respray_hiss().samples == synth_respray_hiss().samples,
                "two builds of the hiss differ", "hiss");
    REQUIRE_MSG(synth_respray_done(false).samples == synth_respray_done(false).samples,
                "two builds differ", "painted");
    REQUIRE_MSG(synth_respray_done(true).samples == synth_respray_done(true).samples,
                "two builds differ", "cleared");
    REQUIRE_MSG(synth_respray_kept().samples == synth_respray_kept().samples,
                "two builds differ", "kept");
    pass("two builds of every respray clip are byte-identical");
}

void hiss_is_a_steady_unpitched_hiss() {
    const PcmClip hiss = synth_respray_hiss();
    const double mid = rms(window(hiss, 0.45, 0.55));
    REQUIRE(mid > 0.1);

    // Held, not a decaying burst: the level stays put across the spray.
    for (const double at : {0.10, 0.30, 0.65, 0.80}) {
        const double level = rms(window(hiss, at, at + 0.1));
        REQUIRE_MSG(level > 0.7 * mid && level < 1.3 * mid,
                    "hiss level wanders too far from the middle of the spray", "steady");
    }
    // The trigger opens and the gun closes off; neither end is a hard edge.
    REQUIRE(rms(window(hiss, 0.0, 0.004)) < 0.5 * mid);
    REQUIRE(rms(window(hiss, 0.985, 1.0)) < 0.25 * mid);

    // Bright and unpitched. None of the stinger notes is in it: the loudest of
    // them in the hiss is a tenth of what the plain-respray note carries.
    REQUIRE(spectral_centroid(hiss.samples, kRate) > 2400.0);
    REQUIRE(band_energy(hiss.samples, kRate, 2000.0, 9000.0) >
            2.0 * band_energy(hiss.samples, kRate, 40.0, 400.0));
    const auto body = window(hiss, 0.4, 0.5);
    const double note = tone(window(synth_respray_done(false), 0.10, 0.20), kResprayPaintedHz);
    for (const double hz : {kResprayKeptHz, kResprayClearedLowHz, kResprayPaintedHz,
                            kResprayClearedHighHz}) {
        REQUIRE_MSG(tone(body, hz) < 0.1 * note, "the hiss carries a pitch", "unpitched");
    }
    std::printf("      (hiss centroid %.0f Hz, mid rms %.3f)\n",
                spectral_centroid(hiss.samples, kRate), mid);
    pass("the hiss holds its level, has no pitch, and opens and closes without an edge");
}

void outcomes_are_told_apart_by_their_notes() {
    const PcmClip painted = synth_respray_done(false);
    const PcmClip cleared = synth_respray_done(true);
    const PcmClip kept = synth_respray_kept();
    REQUIRE(painted.samples != cleared.samples);

    // A plain respray: one bright note, and nothing at the others.
    const auto p = window(painted, 0.10, 0.25);
    const double p_note = tone(p, kResprayPaintedHz);
    REQUIRE(p_note > 10.0 * tone(p, kResprayClearedLowHz));
    REQUIRE(p_note > 10.0 * tone(p, kResprayClearedHighHz));
    REQUIRE(p_note > 10.0 * tone(p, kResprayKeptHz));

    // Cops lost: the pitch RISES. The low note owns the start and the high
    // note owns the ring-out.
    REQUIRE(kResprayClearedHighHz > kResprayClearedLowHz);
    const auto early = window(cleared, 0.10, kResprayClearedHighAtSeconds - 0.01);
    REQUIRE_MSG(tone(early, kResprayClearedLowHz) > 10.0 * tone(early, kResprayClearedHighHz),
                "the cleared stinger does not open on its low note", "cleared");
    const auto late = window(cleared, 0.30, 0.50);
    REQUIRE_MSG(tone(late, kResprayClearedHighHz) > 3.0 * tone(late, kResprayClearedLowHz),
                "the cleared stinger does not rise to its high note", "cleared");
    // And it is the bigger event: it rings on well past the plain respray.
    REQUIRE(cleared.duration_seconds() > painted.duration_seconds() + 0.3f);

    // Stars kept: one LOW note, more than an octave under both others, that
    // sags as it dies.
    const double k_f0 = estimate_f0(window(kept, 0.15, 0.30), kRate, 120.0, 400.0);
    REQUIRE_MSG(k_f0 > 0.92 * kResprayKeptHz && k_f0 < 1.02 * kResprayKeptHz,
                "the kept stinger is not at its note", "kept");
    REQUIRE(2.0 * k_f0 < kResprayPaintedHz && 2.0 * k_f0 < kResprayClearedLowHz);
    const double k_start = estimate_f0(window(kept, 0.10, 0.20), kRate, 120.0, 400.0);
    const double k_end = estimate_f0(window(kept, 0.50, 0.60), kRate, 120.0, 400.0);
    REQUIRE_MSG(k_end < 0.98 * k_start, "the kept note does not sag", "kept");
    const auto k = window(kept, 0.10, 0.50);
    REQUIRE(tone(k, kResprayPaintedHz) < 0.02 * tone(k, kResprayKeptHz));
    REQUIRE(spectral_centroid(k, kRate) <
            0.5 * spectral_centroid(window(painted, 0.10, 0.50), kRate));

    std::printf("      (kept %.1f Hz sagging to %.1f Hz; cleared late B5/E5 %.1fx)\n", k_start,
                k_end, tone(late, kResprayClearedHighHz) / tone(late, kResprayClearedLowHz));
    pass("painted is one bright note, cleared rises a fifth, kept is one low sagging note");
}

void all_three_open_on_the_same_clack() {
    // The first 60 ms is the gun going down, before any note starts. Each clip
    // is normalised on its own, so compare shape, not level.
    const auto painted = window(synth_respray_done(false), 0.0, 0.06);
    const auto cleared = window(synth_respray_done(true), 0.0, 0.06);
    const auto kept = window(synth_respray_kept(), 0.0, 0.06);
    REQUIRE(rms(painted) > 0.02);
    REQUIRE(correlation(painted, cleared) > 0.99);
    REQUIRE(correlation(painted, kept) > 0.99);
    pass("all three stingers open on the same clack");
}

void write_wav(const std::string& path, uint32_t rate, const std::vector<int16_t>& pcm) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    const auto u32 = [f](uint32_t v) {
        const unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu)};
        std::fwrite(b, 1, 4, f);
    };
    const auto u16 = [f](uint32_t v) {
        const unsigned char b[2] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu)};
        std::fwrite(b, 1, 2, f);
    };
    const uint32_t data = static_cast<uint32_t>(pcm.size() * 2u);
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16u);
    u16(1u);
    u16(1u);
    u32(rate);
    u32(rate * 2u);
    u16(2u);
    u16(16u);
    std::fwrite("data", 1, 4, f);
    u32(data);
    std::fwrite(pcm.data(), 1, data, f);
    std::fclose(f);
}

void overrides_replace_only_their_own_clip() {
    const PcmClip hiss = synth_respray_hiss();
    const PcmClip painted = synth_respray_done(false);
    const PcmClip cleared = synth_respray_done(true);
    const PcmClip kept = synth_respray_kept();

    // No paths, and paths to nothing: the normal state, since no recording
    // ships. Every clip stays synthesised.
    for (const ResprayClipPaths& paths :
         {ResprayClipPaths{}, ResprayClipPaths{"no/such/respray_hiss.wav",
                                               "no/such/respray_painted.wav",
                                               "no/such/respray_cleared.wav",
                                               "no/such/respray_kept.wav"}}) {
        const ResprayClips clips = build_respray_clips(paths);
        REQUIRE(clips.overrides_loaded == 0u);
        REQUIRE(clips.hiss.samples == hiss.samples);
        REQUIRE(clips.painted.samples == painted.samples);
        REQUIRE(clips.cleared.samples == cleared.samples);
        REQUIRE(clips.kept.samples == kept.samples);
        REQUIRE(&clips.done(true) == &clips.cleared);
        REQUIRE(&clips.done(false) == &clips.painted);
    }

    // A recording offered for ONE slot lands in that slot and nowhere else. A
    // crossed path is exactly the slip a missing-file test cannot see.
    const std::string path = "apricot_respray_test_kept.wav";
    std::vector<int16_t> pcm(2400);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(
            12000.0 * std::sin(6.283185307179586 * 220.0 * static_cast<double>(i) / 24000.0));
    }
    write_wav(path, 24000u, pcm);
    const ResprayClips clips =
        build_respray_clips(ResprayClipPaths{"", "no/such/painted.wav", "", path});
    std::remove(path.c_str());
    REQUIRE(clips.overrides_loaded == 1u);
    REQUIRE(clips.kept.sample_rate == 24000u);
    REQUIRE(clips.kept.frame_count() == pcm.size());
    REQUIRE(clips.hiss.samples == hiss.samples);
    REQUIRE(clips.painted.samples == painted.samples);
    REQUIRE(clips.cleared.samples == cleared.samples);
    pass("a missing recording keeps the synth clip; a present one replaces only its slot");
}

void booth_voices_ride_the_ui_category() {
    const VoiceParams p = respray_voice(0.5f);
    REQUIRE(p.category == Category::Ui);
    REQUIRE(!p.spatial && !p.looping);

    VoiceMixer mixer;
    mixer.prepare(kDefaultSampleRate);
    const ResprayClips clips = build_respray_clips(ResprayClipPaths{});
    RespraySound sound;

    REQUIRE(sound.start_hiss(mixer, clips));
    REQUIRE(sound.hiss.valid());
    for (int i = 0; i < 8; ++i) render(mixer, 256u);
    REQUIRE(rms(render(mixer, 256u)) > 0.01);

    // Muting another category leaves it; muting Ui silences it.
    mixer.set_category(Category::Engine, 0.0f);
    for (int i = 0; i < 8; ++i) render(mixer, 256u);
    REQUIRE(rms(render(mixer, 256u)) > 0.01);
    mixer.set_category(Category::Engine, 1.0f);
    mixer.set_category(Category::Ui, 0.0f);
    for (int i = 0; i < 20; ++i) render(mixer, 256u);
    REQUIRE(rms(render(mixer, 256u)) < 1e-4);
    mixer.set_category(Category::Ui, 1.0f);

    // A cancelled spray FADES the hiss: the block straight after the stop still
    // carries most of the level (a cut would be exact silence), and a few tens
    // of milliseconds later it is gone.
    REQUIRE(sound.start_hiss(mixer, clips));
    for (int i = 0; i < 8; ++i) render(mixer, 256u);
    const double before = rms(render(mixer, 256u));
    REQUIRE(before > 0.01);
    sound.stop_hiss(mixer);
    REQUIRE(!sound.hiss.valid());
    const double straight_after = rms(render(mixer, 64u));
    REQUIRE_MSG(straight_after > 0.25 * before, "the hiss was cut, not faded", "cancel");
    for (int i = 0; i < 30; ++i) render(mixer, 256u);
    REQUIRE_MSG(rms(render(mixer, 256u)) < 1e-4, "the hiss did not fade out", "cancel");

    // A completed spray: the stinger plays over whatever is left of the hiss.
    REQUIRE(sound.start_hiss(mixer, clips));
    for (int i = 0; i < 4; ++i) render(mixer, 256u);
    REQUIRE(sound.play_done(mixer, clips, true));
    REQUIRE(!sound.hiss.valid());
    REQUIRE(rms(render(mixer, 2048u)) > 0.01);
    REQUIRE(sound.play_kept(mixer, clips));
    REQUIRE(sound.play_done(mixer, clips, false));
    REQUIRE(all_finite(render(mixer, 4096u)));

    // Stopping a hiss that already ran out is harmless.
    REQUIRE(sound.start_hiss(mixer, clips));
    for (int i = 0; i < 200; ++i) render(mixer, 256u);
    sound.stop_hiss(mixer);
    sound.stop_hiss(mixer);
    REQUIRE(mixer.dropped_commands() == 0u);
    pass("booth voices ride Category::Ui, and a cancelled hiss fades instead of cutting");
}

}  // namespace

int main() {
    every_clip_is_playable();
    clips_are_deterministic();
    hiss_is_a_steady_unpitched_hiss();
    outcomes_are_told_apart_by_their_notes();
    all_three_open_on_the_same_clack();
    overrides_replace_only_their_own_clip();
    booth_voices_ride_the_ui_category();
    return done("respray_audio_tests");
}

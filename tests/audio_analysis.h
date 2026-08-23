#pragma once

// Measurement tools for the audio suites.
//
// NOBODY CAN HEAR A TEST. That is not a limitation to work around, it is the
// reason this header exists: every claim an audio test makes has to be a
// NUMBER. "the buffer is non-empty" proves nothing — a buffer full of NaN is
// non-empty, and so is one full of a 4 Hz square wave.
//
// So these are the properties that stand in for listening:
//
//   finite / in range     the buffer is playable at all
//   rms                   it is not silence dressed up as signal
//   dc_offset             it is centred, so it does not waste headroom or thump
//   seam_step             a loop wraps without a tick
//   estimate_f0           it is at the PITCH it claims to be at
//   spectral_centroid     it is as BRIGHT as it claims to be
//
// estimate_f0 is the important one. Asserting an engine buffer is non-silent
// says nothing about whether its pitch tracks rpm; measuring its fundamental
// and checking it against the firing frequency says exactly that.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace apricot_test {

inline bool all_finite(const std::vector<float>& x) {
    for (const float v : x) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

inline float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (const float v : x) p = std::max(p, std::fabs(v));
    return p;
}

inline double rms(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double acc = 0.0;
    for (const float v : x) acc += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(acc / static_cast<double>(x.size()));
}

inline double dc_offset(const std::vector<float>& x) {
    if (x.empty()) return 0.0;
    double acc = 0.0;
    for (const float v : x) acc += static_cast<double>(v);
    return acc / static_cast<double>(x.size());
}

// The largest jump between neighbouring samples. The yardstick a loop seam is
// judged against: a wrap is only a click if it is discontinuous COMPARED TO the
// signal's own slope. A 5 kHz tone legitimately moves a long way per sample,
// and an absolute threshold would either fail it or pass a genuine tick in a
// quiet bed.
inline double max_adjacent_step(const std::vector<float>& x) {
    double m = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(x[i]) -
                                  static_cast<double>(x[i - 1])));
    }
    return m;
}

// The jump from the last sample back to the first — what the ear hears once a
// second at every loop point.
inline double seam_step(const std::vector<float>& x) {
    if (x.size() < 2) return 0.0;
    return std::fabs(static_cast<double>(x.front()) -
                     static_cast<double>(x.back()));
}

// Goertzel: the energy at ONE frequency, without paying for a whole FFT.
// Hann-windowed, because the sidelobes of an unwindowed rectangular window
// smear a loud partial across the neighbouring probe frequencies and make a
// comb detector lock onto the smear.
inline double goertzel(const std::vector<float>& x, double sample_rate,
                       double hz) {
    const std::size_t n = x.size();
    if (n < 4 || hz <= 0.0 || hz >= sample_rate * 0.5) return 0.0;

    const double w = 6.283185307179586 * hz / sample_rate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double window =
            0.5 - 0.5 * std::cos(6.283185307179586 * static_cast<double>(i) /
                                 static_cast<double>(n - 1));
        const double s0 = static_cast<double>(x[i]) * window + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(power, 0.0)) / static_cast<double>(n);
}

// FUNDAMENTAL FREQUENCY, by normalised autocorrelation.
//
// The obvious alternative — pick the loudest bin, or score a harmonic comb —
// was tried first and MEASURABLY DOES NOT WORK on this material. A harmonic
// comb scored 900 rpm at 15 Hz and 5600 rpm at 46.7 Hz: octave errors, in both
// directions, on a signal whose pitch was in fact perfectly proportional to
// rpm. The reason is that an engine spectrum is dense — it has real energy at
// half orders, whole crank orders AND firing orders — so several candidate
// fundamentals genuinely explain it, and which one wins depends on where the
// formants happen to sit. A detector that reports a different ORDER at
// different rpm makes the tracking test look broken when it is not, which is
// the worst kind of test.
//
// Autocorrelation asks a blunter and better question: at what lag does the
// waveform most nearly repeat? Measured across a 7.5:1 rpm span and both load
// extremes it locked onto the firing period every single time, to within 0.11%.
//
// Two details that are doing real work:
//   * FIRST peak above the threshold, not the global maximum. The signal
//     correlates just as well at two and three times the period, and taking
//     the max would report a fundamental an octave or two down at random.
//   * Parabolic interpolation on the winning lag. Lag is an integer number of
//     samples, so at 200 Hz and 48 kHz the raw quantisation alone is 0.4% —
//     the same size as the error being measured. This pulls it under 0.05%.
inline double estimate_f0(const std::vector<float>& x, double sample_rate,
                          double lo_hz, double hi_hz, double threshold = 0.90) {
    const std::size_t n = x.size();
    if (n < 128 || !(hi_hz > lo_hz) || lo_hz <= 0.0) return 0.0;

    const std::size_t lag_min =
        std::max<std::size_t>(2, static_cast<std::size_t>(sample_rate / hi_hz));
    const std::size_t lag_max =
        std::min(n / 2, static_cast<std::size_t>(sample_rate / lo_hz));
    if (lag_max <= lag_min + 2) return 0.0;

    double energy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        energy += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    if (energy <= 0.0) return 0.0;

    std::vector<double> r(lag_max + 2, 0.0);
    for (std::size_t lag = lag_min; lag <= lag_max; ++lag) {
        double acc = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i) {
            acc += static_cast<double>(x[i]) * static_cast<double>(x[i + lag]);
        }
        r[lag] = acc / energy;
    }

    double best_r = 0.0;
    for (std::size_t lag = lag_min; lag <= lag_max; ++lag) {
        best_r = std::max(best_r, r[lag]);
    }
    if (best_r <= 0.0) return 0.0;

    for (std::size_t lag = lag_min + 1; lag + 1 <= lag_max; ++lag) {
        if (r[lag] < threshold * best_r) continue;
        if (!(r[lag] > r[lag - 1] && r[lag] >= r[lag + 1])) continue;

        const double y0 = r[lag - 1], y1 = r[lag], y2 = r[lag + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        double refined = static_cast<double>(lag);
        if (std::fabs(denom) > 1e-12) {
            refined += 0.5 * (y0 - y2) / denom;
        }
        return refined > 0.0 ? sample_rate / refined : 0.0;
    }
    return 0.0;
}

// Energy-weighted mean frequency: one number for "how bright is this".
// Sampled on a geometric grid rather than a linear one so the bottom two
// octaves are not represented by three points.
inline double spectral_centroid(const std::vector<float>& x, double sample_rate,
                                double lo_hz = 40.0, double hi_hz = 14000.0) {
    if (x.size() < 64) return 0.0;
    const double top = std::min(hi_hz, sample_rate * 0.45);
    if (!(top > lo_hz)) return 0.0;

    constexpr int kBands = 96;
    double num = 0.0, den = 0.0;
    for (int i = 0; i <= kBands; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(kBands);
        const double f = lo_hz * std::pow(top / lo_hz, u);
        const double m = goertzel(x, sample_rate, f);
        num += f * m;
        den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

// Total energy between two frequencies, on the same geometric grid.
inline double band_energy(const std::vector<float>& x, double sample_rate,
                          double lo_hz, double hi_hz, int bands = 24) {
    const double top = std::min(hi_hz, sample_rate * 0.45);
    if (!(top > lo_hz) || bands < 1) return 0.0;
    double acc = 0.0;
    for (int i = 0; i <= bands; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(bands);
        acc += goertzel(x, sample_rate, lo_hz * std::pow(top / lo_hz, u));
    }
    return acc;
}

// The nth percentile of the sample-to-sample step size. The right yardstick for
// a loop seam: the MAXIMUM step is set by one freak transient somewhere in the
// buffer, so comparing against it is a weak test that a genuinely ticking loop
// could pass. p99.9 is what the signal routinely does.
inline double adjacent_step_percentile(const std::vector<float>& x, double p) {
    if (x.size() < 3) return 0.0;
    std::vector<double> steps;
    steps.reserve(x.size() - 1);
    for (std::size_t i = 1; i < x.size(); ++i) {
        steps.push_back(std::fabs(static_cast<double>(x[i]) -
                                  static_cast<double>(x[i - 1])));
    }
    std::sort(steps.begin(), steps.end());
    const double clamped = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    const std::size_t idx =
        static_cast<std::size_t>(clamped * static_cast<double>(steps.size() - 1));
    return steps[idx];
}

// Energy in harmonics [hi_lo..hi_hi] over energy in harmonics [1..lo_hi].
// One number for "how bright is this harmonic sound".
//
// Deliberately sampled ON the harmonic grid rather than on an even or
// log-spaced one, and that is not a detail. A log-spaced centroid measured this
// engine's OVERRUN as brighter than its full-throttle tone — the exact opposite
// of the truth. The cause: with partials tens of Hz apart, half the low-end
// probe points of an even grid land in the gaps BETWEEN harmonics and read
// zero, while every high-end probe point lands on something. The grid was
// measuring its own spacing. Probing only where partials actually are removes
// the artifact entirely.
inline double harmonic_ratio(const std::vector<float>& x, double sample_rate,
                             double f0, int lo_hi, int hi_lo, int hi_hi) {
    if (!(f0 > 0.0)) return 0.0;
    double lo = 0.0, hi = 0.0;
    for (int n = 1; n <= lo_hi; ++n) {
        lo += goertzel(x, sample_rate, f0 * static_cast<double>(n));
    }
    for (int n = hi_lo; n <= hi_hi; ++n) {
        const double f = f0 * static_cast<double>(n);
        if (f >= sample_rate * 0.45) break;
        hi += goertzel(x, sample_rate, f);
    }
    return lo > 0.0 ? hi / lo : 0.0;
}

// APERIODIC CONTENT: energy sitting OFF the harmonic lattice, relative to
// energy on it.
//
// A steady harmonic stack puts energy at exact multiples of its fundamental
// and nowhere else, so this number is essentially window leakage for one.
// A transient — a pop, a click, a stone strike — is aperiodic by definition
// and smears energy into the gaps. That makes this the direct measurement of
// "does this sound contain events, or is it a drone".
//
// The 0.37 offset is not arbitrary. The engine model's partials live on a
// HALF-crank grid, so an offset of 0.5 harmonics would land on a real crank
// order and an offset of 0.25 on a real half order. 0.37 lands on neither.
//
// Needs a reasonably long window: the probes sit 0.37 of a harmonic spacing
// from real partials, and a short window's main lobe is wide enough to leak
// across that gap at low fundamentals.
inline double off_lattice_ratio(const std::vector<float>& x, double sample_rate,
                                double fundamental, int harmonics = 12) {
    if (!(fundamental > 0.0)) return 0.0;
    double on = 0.0, off = 0.0;
    for (int m = 1; m <= harmonics; ++m) {
        const double f_on = fundamental * static_cast<double>(m);
        const double f_off = fundamental * (static_cast<double>(m) + 0.37);
        if (f_off >= sample_rate * 0.45) break;
        on += goertzel(x, sample_rate, f_on);
        off += goertzel(x, sample_rate, f_off);
    }
    return on > 0.0 ? off / on : 0.0;
}

// Pull one channel out of an interleaved buffer, so the stereo render can be
// analysed a side at a time.
inline std::vector<float> deinterleave(const std::vector<float>& interleaved,
                                       std::size_t channels,
                                       std::size_t channel) {
    std::vector<float> out;
    if (channels == 0 || channel >= channels) return out;
    out.reserve(interleaved.size() / channels);
    for (std::size_t i = channel; i < interleaved.size(); i += channels) {
        out.push_back(interleaved[i]);
    }
    return out;
}

}  // namespace apricot_test

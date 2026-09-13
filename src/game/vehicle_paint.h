#pragma once
// Vehicle paint: the soft paint mask and the shading-preserving recolour a
// respray applies to the atlas a car is wearing.
//
// This is a port of tools/paint_lab.py (`gate`, `base_colour`, `recolour`),
// and tests/vehicle_paint_golden.inc holds it to that reference within one
// 8-bit level. Change the maths in both, in the same commit, and regenerate
// the golden with `tools/paint_profiles.py golden`.
//
// It lives sim-side on purpose. A recolour written in a shader would be a
// second derivation of the same maths that no headless suite can run; here the
// host layer only decodes the PNG, hands over pixels, and uploads the bytes
// that come back (docs/architecture.md, Rendering).
//
// Pipeline for one atlas a model wears, with params P:
//   mask, trust = build_paint_mask(atlas, P, region)   once per worn atlas
//   out         = recolour_paint(atlas, mask, target)  once per picked colour
// `region` is the stock atlas's mask when an alternate livery shares its UVs;
// the alternate's mask is then min(own gate, region).
//
// Rules the port keeps exactly (they are what "the same as the lab" means):
//  (i)   A texel is inside a rect when its centre in IMAGE space (top-left
//        origin) satisfies u0 <= (x+.5)/W < u1 and v0 <= cv < v1. A bottom-up
//        buffer (the engine's decode flips rows) tests the image row the
//        buffer row came from.
//  (ii)  The 3x3 median and max passes clamp at the edges.
//  (iii) A box reference is the per-channel median OKLab of the texels whose
//        centres fall in the box, in the atlas being gated; an even count
//        averages the two middle values.
//  (iv)  The base colour is a weighted per-channel median from 1024-bin
//        histograms (L over [0,1], a and b over [-.5,.5]), weights m where
//        m >= .5, else 0, falling back to m + 1e-9 when nothing reaches .5.
//  (v)   The recolour snaps a target within 0.008 of the base to the base,
//        carries lightness through a curve that is the identity when the
//        target is the base, and carries chroma as saturation ratio and hue
//        offset without clamps (a clamp breaks identity).
//  (vi)  Out of gamut, the chroma scale is bisected 14 times at fixed L.
//  (vii) All 8-bit rounding is round-half-to-even. Alpha is never written.
//
// The per-texel maths is double. The mask and trust are stored as float, so a
// result can differ from the float64 lab by one level; that is the golden's
// tolerance, not a licence for more.
//
// Speed comes from a memo that changes no result: gate, trust and recolour
// are functions of a texel's RGB alone for one atlas and target, so the mask
// keeps the atlas's unique colours and the recolour runs once per painted
// colour, not once per texel. vehicle_paint_tests holds the memo bit-exact to
// recolour_paint_per_texel().
#include <cstddef>
#include <cstdint>
#include <vector>

namespace apricot {

struct PaintColor {
    uint8_t r = 0, g = 0, b = 0;
};
constexpr bool operator==(PaintColor x, PaintColor y) {
    return x.r == y.r && x.g == y.g && x.b == y.b;
}
constexpr bool operator!=(PaintColor x, PaintColor y) { return !(x == y); }

// What the respray booth hands the respray rules: a colour, or "back to the
// factory paint". Shared by the picker model and the respray rules.
struct PaintOrder {
    bool factory = false;
    PaintColor colour{};
};
constexpr bool operator==(PaintOrder x, PaintOrder y) {
    return x.factory == y.factory && x.colour == y.colour;
}
constexpr bool operator!=(PaintOrder x, PaintOrder y) { return !(x == y); }

struct PaintLab {
    double L = 0, a = 0, b = 0;
};

// c and fc are distances in q = (a,b)/max(L, 0.10), OKLab chroma per unit
// lightness: full membership up to c, fading to 0 at c+fc. dark and light are
// how far L may sit below or above the reference, fading over fl.
struct PaintTol {
    float c, fc, dark, light, fl;
};

// Normalised image space of the PNG as authored: (0,0) is the top-left corner.
struct UvRect {
    float u0, v0, u1, v1;
};

struct PaintRef {
    enum class Kind : uint8_t { Srgb, Box };
    Kind kind;
    PaintColor rgb;  // Kind::Srgb
    UvRect box;      // Kind::Box
    PaintTol tol;
};

// Scalars used in double maths are double, so the port's per-texel arithmetic
// is the lab's float64 arithmetic. Float-typed tolerances carry an f suffix.
inline constexpr double kPaintLFloor = 0.10;
inline constexpr double kPaintSatWLo = 0.04, kPaintSatWHi = 0.12;
inline constexpr double kPaintHueKeepFar = 0.35;
inline constexpr double kPaintTargetNear = 0.03;
inline constexpr double kPaintTargetFar = 0.25;
inline constexpr double kPaintUntrustedLKeep = 0.5;
inline constexpr double kPaintTargetSnap = 0.008;
inline constexpr double kPaintAlphaLo = 250.0, kPaintAlphaHi = 255.0;
inline constexpr int kPaintGamutIters = 14;
inline constexpr int kPaintHistBins = 1024;
// The generator writes these into every ref it emits; the runtime reads the
// tolerance from the ref and never guesses one.
inline constexpr PaintTol kPaintDefaultTol{0.06f, 0.05f, 0.35f, 0.20f, 0.08f};
inline constexpr PaintTol kPaintDefaultExcludeTol{0.04f, 0.03f, 0.06f, 0.06f, 0.04f};

// Plain pointers and counts so a generated table can be constexpr. A list with
// no entries is {nullptr, 0}.
struct PaintMaskParams {
    const PaintRef* refs;
    uint16_t ref_count;
    const PaintRef* exclude_refs;
    uint16_t exclude_ref_count;
    const UvRect* include;  // if any: paint only inside them
    uint16_t include_count;
    const UvRect* exclude;  // never paint inside them
    uint16_t exclude_count;
    const UvRect* force;    // always paint, colour-agnostic; include and exclude still win
    uint16_t force_count;
    uint8_t clean;          // 3x3 median passes
    uint8_t grow_passes;    // dilate into neighbours whose L sits in [ref-dark, ref+light]
    float grow_dark, grow_light;
};

struct PaintImage {
    int width = 0, height = 0;
    bool bottom_up = true;  // row 0 is the image's bottom row, as the engine decodes
    std::vector<uint8_t> rgba;
};

struct PaintMask {
    int width = 0, height = 0;
    std::vector<float> weight;  // 0..1 per texel, buffer order
    std::vector<float> trust;   // 0..1 per texel: how well its own colour matched the refs
    // The unique-colour memo: packed 0xRRGGBB per colour, and each texel's
    // index into it. A 256x256 atlas has at most 65,536 colours, which is
    // exactly what a uint16_t index holds.
    std::vector<uint32_t> palette;
    std::vector<uint16_t> palette_index;
    PaintLab base;
    PaintColor base_srgb;          // base encoded per channel, as the lab's base8
    uint32_t painted_texels = 0;   // weight >= .5
    uint64_t weight_sum_q8 = 0;    // sum of nearbyint(weight * 255)
};

PaintLab paint_srgb8_to_oklab(PaintColor c);
// Gamut-clipped by bisecting chroma at fixed L (rule vi), then encoded.
PaintColor paint_oklab_to_srgb8(PaintLab lab);
// OKLab mix from a (t = 0) to b (t = 1), gamut-clipped; t is clamped to [0,1].
PaintColor paint_mix_oklab(PaintColor a, PaintColor b, float t);

// Builds the mask, trust, memo and base colour of `atlas` under `p`, bounded
// by `region` when given. Returns false, leaving `out` untouched, for an empty
// or mis-sized image, a region of another size, a list with a count but no
// pointer, a box reference that covers no texel centre, or more unique colours
// than a uint16_t index holds.
bool build_paint_mask(const PaintImage& atlas, const PaintMaskParams& p,
                      const PaintMask* region, PaintMask& out);

// Recolours `atlas` to `target` under `mask`. Returns false for a size
// mismatch, for a mask that was not built from these pixels, or when `out` is
// `atlas`: a respray always starts from the pristine decoded atlas, never from
// one that was already painted.
bool recolour_paint(const PaintImage& atlas, const PaintMask& mask, PaintColor target,
                    PaintImage& out);

// The same result without the memo: every painted texel converted and
// recoloured on its own. It exists so a test can hold the memo to the
// definition. Never call it per frame; it is several times slower.
bool recolour_paint_per_texel(const PaintImage& atlas, const PaintMask& mask,
                              PaintColor target, PaintImage& out);

}  // namespace apricot

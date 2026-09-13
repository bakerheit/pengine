// vehicle_paint_tests: the recolour core a respray runs (game/vehicle_paint.h).
//
// tests/vehicle_paint_golden.inc is tools/paint_lab.py's float64 output on
// synthetic atlases, written by `python3 tools/paint_profiles.py golden`. The
// port stores mask and trust as float, so it may sit one 8-bit level from the
// lab and no further. Every other case is a property the lab holds exactly.
#include "game/vehicle_paint.h"
#include "test_assert.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <vector>

using namespace apricot;

namespace {

struct PaintGoldenPixels {
    const uint8_t* top_down;
    const uint8_t* bottom_up;
};

// One synthetic atlas and what the lab made of it.
struct PaintGoldenCase {
    const char* name;
    int width, height;
    const PaintMaskParams* params;
    const PaintGoldenCase* region;  // the stock case whose mask bounds this alternate
    PaintGoldenPixels rgba, mask_q8, trust_q8;
    PaintColor base_srgb;
    uint32_t painted_texels;
    uint64_t weight_sum_q8;
    PaintColor targets[3];          // the lab's base colour, deep blue, white
    PaintGoldenPixels recolours[3];
};

#include "vehicle_paint_golden.inc"

constexpr PaintColor kDeepBlue{20, 40, 140};
constexpr PaintColor kWhite{240, 240, 236};
constexpr PaintColor kBlack{18, 18, 20};
constexpr PaintColor kLime{120, 210, 40};

static_assert(PaintColor{1, 2, 3} == PaintColor{1, 2, 3} && PaintColor{1, 2, 3} != PaintColor{1, 2, 4});
static_assert(PaintOrder{} == PaintOrder{false, {0, 0, 0}} && PaintOrder{true, {}} != PaintOrder{});

std::size_t texel_count(int w, int h) {
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
}

PaintImage make_image(const uint8_t* rgba, int w, int h, bool bottom_up) {
    PaintImage image;
    image.width = w;
    image.height = h;
    image.bottom_up = bottom_up;
    image.rgba.assign(rgba, rgba + texel_count(w, h) * 4);
    return image;
}

const uint8_t* pick(const PaintGoldenPixels& pixels, bool bottom_up) {
    return bottom_up ? pixels.bottom_up : pixels.top_down;
}

// The lab's q8: np.clip(np.round(m * 255), 0, 255).
int q8(float v) {
    return static_cast<int>(std::clamp(std::nearbyint(static_cast<double>(v) * 255.0), 0.0, 255.0));
}

int level_diff(uint8_t a, uint8_t b) { return std::abs(int{a} - int{b}); }

uint64_t count_diff(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

// A golden case's mask, built after its region case's mask when it has one.
bool build_case(const PaintGoldenCase& gc, bool bottom_up, PaintImage& atlas, PaintMask& mask) {
    atlas = make_image(pick(gc.rgba, bottom_up), gc.width, gc.height, bottom_up);
    if (gc.region == nullptr) return build_paint_mask(atlas, *gc.params, nullptr, mask);
    PaintImage stock;
    PaintMask region;
    return build_case(*gc.region, bottom_up, stock, region) &&
           build_paint_mask(atlas, *gc.params, &region, mask);
}

// A 16x2 lightness ramp of one paint, every texel inside a generous reference.
constexpr PaintColor kRampPaint{130, 60, 40};
constexpr PaintRef kRampRefs[] = {
    {PaintRef::Kind::Srgb, kRampPaint, {0.0f, 0.0f, 0.0f, 0.0f}, {0.30f, 0.10f, 1.00f, 1.00f, 0.10f}},
};
constexpr PaintMaskParams kRampParams = {
    kRampRefs, static_cast<uint16_t>(std::size(kRampRefs)), nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
    0, 0, 0.0f, 0.0f,
};

PaintImage ramp_atlas(bool bottom_up) {
    PaintImage image;
    image.width = 16;
    image.height = 2;
    image.bottom_up = bottom_up;
    image.rgba.resize(texel_count(16, 2) * 4);
    for (std::size_t i = 0; i < texel_count(16, 2); ++i) {
        const double f = 0.40 + 0.07 * static_cast<double>(i % 16);
        const auto scaled = [f](uint8_t c) { return static_cast<uint8_t>(std::lround(c * f)); };
        image.rgba[i * 4] = scaled(kRampPaint.r);
        image.rgba[i * 4 + 1] = scaled(kRampPaint.g);
        image.rgba[i * 4 + 2] = scaled(kRampPaint.b);
        image.rgba[i * 4 + 3] = 255;
    }
    return image;
}

// A 64x64 atlas with thousands of colours and every gate feature switched on.
constexpr PaintColor kNoisyPaint{150, 70, 50};
constexpr PaintColor kNoisyTrim{60, 90, 160};
constexpr PaintRef kNoisyRefs[] = {
    {PaintRef::Kind::Srgb, kNoisyPaint, {0.0f, 0.0f, 0.0f, 0.0f}, {0.06f, 0.05f, 0.20f, 0.20f, 0.05f}},
    {PaintRef::Kind::Box, {0, 0, 0}, {0.50f, 0.50f, 0.75f, 0.75f}, {0.04f, 0.03f, 0.15f, 0.15f, 0.05f}},
};
// A lamp in the paint's hue but well above its lightness, so the exclude takes
// the lamps and leaves the paint alone.
constexpr PaintColor kNoisyLamp{230, 110, 70};
constexpr PaintRef kNoisyExcludeRefs[] = {
    {PaintRef::Kind::Srgb, kNoisyLamp, {0.0f, 0.0f, 0.0f, 0.0f}, kPaintDefaultExcludeTol},
};
constexpr UvRect kNoisyExclude[] = {{0.25f, 0.00f, 0.3125f, 1.00f}};
constexpr UvRect kNoisyForce[] = {{0.50f, 0.90f, 0.625f, 0.95f}};
constexpr PaintMaskParams kNoisyParams = {
    kNoisyRefs,    static_cast<uint16_t>(std::size(kNoisyRefs)),
    kNoisyExcludeRefs, static_cast<uint16_t>(std::size(kNoisyExcludeRefs)),
    nullptr,       0,
    kNoisyExclude, static_cast<uint16_t>(std::size(kNoisyExclude)),
    kNoisyForce,   static_cast<uint16_t>(std::size(kNoisyForce)),
    1,             1,
    0.04f,         0.20f,
};

PaintImage noisy_atlas(bool bottom_up) {
    const int n = 64;
    PaintImage image;
    image.width = n;
    image.height = n;
    image.bottom_up = bottom_up;
    image.rgba.resize(texel_count(n, n) * 4);
    for (int y = 0; y < n; ++y) {
        const int buffer_row = bottom_up ? n - 1 - y : y;
        for (int x = 0; x < n; ++x) {
            uint8_t* t = &image.rgba[texel_count(n, buffer_row) * 4 + static_cast<std::size_t>(x) * 4];
            PaintColor c{static_cast<uint8_t>(kNoisyPaint.r + (x * 37 + y * 11) % 31 - 15),
                         static_cast<uint8_t>(kNoisyPaint.g + (x * 17 + y * 29) % 23 - 11),
                         static_cast<uint8_t>(kNoisyPaint.b + (x * 5 + y * 41) % 19 - 9)};
            if ((x + 2 * y) % 9 == 0) c = kNoisyTrim;              // single texels the clean pass fills
            if (x >= 40 && x < 56 && y >= 4 && y < 13) c = kNoisyTrim;  // a block it cannot
            if (x % 13 == 7 && y % 5 == 2) c = kNoisyLamp;
            t[0] = c.r;
            t[1] = c.g;
            t[2] = c.b;
            t[3] = x < 3 ? 150 : (x == 3 ? 252 : 255);
        }
    }
    return image;
}

// The noisy atlas with its trim painted over: an alternate livery whose own
// gate reaches texels the stock atlas's mask does not.
PaintImage noisy_alternate(bool bottom_up) {
    PaintImage image = noisy_atlas(bottom_up);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        if (PaintColor{image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]} != kNoisyTrim) continue;
        image.rgba[i] = kNoisyPaint.r;
        image.rgba[i + 1] = kNoisyPaint.g;
        image.rgba[i + 2] = kNoisyPaint.b;
    }
    return image;
}

bool same_mask(const PaintMask& a, const PaintMask& b) {
    return a.width == b.width && a.height == b.height && a.weight == b.weight && a.trust == b.trust &&
           a.palette == b.palette && a.palette_index == b.palette_index && a.base.L == b.base.L &&
           a.base.a == b.base.a && a.base.b == b.base.b && a.base_srgb == b.base_srgb &&
           a.painted_texels == b.painted_texels && a.weight_sum_q8 == b.weight_sum_q8;
}

// ---------------------------------------------------------------------------
// 1. The port equals the lab within one 8-bit level, in both row orders.
void golden_matches_lab() {
    int worst_mask = 0, worst_trust = 0, worst_base = 0, worst_recolour = 0;
    for (const PaintGoldenCase* gc : kPaintGoldenCases) {
        const std::size_t n = texel_count(gc->width, gc->height);
        for (const bool bottom_up : {false, true}) {
            PaintImage atlas;
            PaintMask mask;
            REQUIRE_MSG(build_case(*gc, bottom_up, atlas, mask), "the mask builds", gc->name);
            const uint8_t* want_mask = pick(gc->mask_q8, bottom_up);
            const uint8_t* want_trust = pick(gc->trust_q8, bottom_up);
            uint64_t mask_off = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const int dm = std::abs(q8(mask.weight[i]) - int{want_mask[i]});
                const int dt = std::abs(q8(mask.trust[i]) - int{want_trust[i]});
                REQUIRE_MSG(dm <= 1, "mask within one 8-bit level of the lab", gc->name);
                REQUIRE_MSG(dt <= 1, "trust within one 8-bit level of the lab", gc->name);
                mask_off += dm != 0 ? 1u : 0u;
                worst_mask = std::max(worst_mask, dm);
                worst_trust = std::max(worst_trust, dt);
            }
            const int db = std::max({level_diff(mask.base_srgb.r, gc->base_srgb.r),
                                     level_diff(mask.base_srgb.g, gc->base_srgb.g),
                                     level_diff(mask.base_srgb.b, gc->base_srgb.b)});
            REQUIRE_MSG(db <= 1, "base colour within one 8-bit level of the lab", gc->name);
            worst_base = std::max(worst_base, db);
            // A texel can only change the painted count or the q8 sum by
            // crossing a level, and every such texel is already counted.
            REQUIRE_MSG(count_diff(mask.painted_texels, gc->painted_texels) <= mask_off,
                        "painted texel count matches the lab", gc->name);
            REQUIRE_MSG(count_diff(mask.weight_sum_q8, gc->weight_sum_q8) <= mask_off,
                        "q8 weight sum matches the lab", gc->name);
            for (std::size_t k = 0; k < std::size(gc->targets); ++k) {
                PaintImage out;
                REQUIRE_MSG(recolour_paint(atlas, mask, gc->targets[k], out), "the recolour runs", gc->name);
                const uint8_t* want = pick(gc->recolours[k], bottom_up);
                for (std::size_t i = 0; i < n * 4; ++i) {
                    if (i % 4 == 3) {
                        REQUIRE_MSG(out.rgba[i] == want[i], "alpha equals the lab's", gc->name);
                        continue;
                    }
                    const int dr = level_diff(out.rgba[i], want[i]);
                    REQUIRE_MSG(dr <= 1, "recolour within one 8-bit level of the lab", gc->name);
                    worst_recolour = std::max(worst_recolour, dr);
                }
            }
        }
    }
    std::printf("      worst |port - lab| in 8-bit levels: mask %d, trust %d, base %d, recolour %d\n",
                worst_mask, worst_trust, worst_base, worst_recolour);
    apricot_test::pass("golden_matches_lab");
}

// 2. Recolouring to the atlas's own base colour is a no-op.
void identity_is_a_no_op() {
    const auto check = [](const PaintImage& atlas, const PaintMask& mask, const char* label) {
        REQUIRE_MSG(mask.weight_sum_q8 > 0, "the case paints something", label);
        PaintImage out;
        REQUIRE_MSG(recolour_paint(atlas, mask, mask.base_srgb, out), "the recolour runs", label);
        int worst = 0;
        for (std::size_t i = 0; i < out.rgba.size(); ++i)
            worst = std::max(worst, level_diff(out.rgba[i], atlas.rgba[i]));
        REQUIRE_MSG(worst <= 1, "the atlas's own base colour moves no texel by more than one level", label);
    };
    for (const bool bottom_up : {false, true}) {
        for (const PaintGoldenCase* gc : kPaintGoldenCases) {
            PaintImage atlas;
            PaintMask mask;
            REQUIRE_MSG(build_case(*gc, bottom_up, atlas, mask), "the mask builds", gc->name);
            check(atlas, mask, gc->name);
        }
        const PaintImage ramp = ramp_atlas(bottom_up), noisy = noisy_atlas(bottom_up);
        PaintMask ramp_mask, noisy_mask;
        REQUIRE(build_paint_mask(ramp, kRampParams, nullptr, ramp_mask));
        REQUIRE(build_paint_mask(noisy, kNoisyParams, nullptr, noisy_mask));
        check(ramp, ramp_mask, "ramp");
        check(noisy, noisy_mask, "noisy");
    }
    apricot_test::pass("identity_is_a_no_op");
}

constexpr PaintColor kFarTargets[] = {kDeepBlue, kWhite, kBlack, kLime, {0, 255, 0}, {255, 0, 255}};

template <typename Fn>
void for_each_real_mask(Fn fn) {
    for (const bool bottom_up : {false, true}) {
        for (const PaintGoldenCase* gc : kPaintGoldenCases) {
            PaintImage atlas;
            PaintMask mask;
            REQUIRE_MSG(build_case(*gc, bottom_up, atlas, mask), "the mask builds", gc->name);
            fn(atlas, mask, gc->name);
        }
        const PaintImage noisy = noisy_atlas(bottom_up);
        PaintMask noisy_mask;
        REQUIRE(build_paint_mask(noisy, kNoisyParams, nullptr, noisy_mask));
        fn(noisy, noisy_mask, "noisy");
    }
}

// 3. A texel with no paint weight comes out bit for bit.
void unpainted_texels_are_bit_exact() {
    for_each_real_mask([](const PaintImage& atlas, const PaintMask& mask, const char* label) {
        std::size_t unpainted = 0, changed = 0;
        for (const PaintColor target : kFarTargets) {
            PaintImage out;
            REQUIRE_MSG(recolour_paint(atlas, mask, target, out), "the recolour runs", label);
            for (std::size_t i = 0; i < mask.weight.size(); ++i) {
                const uint8_t* s = &atlas.rgba[i * 4];
                const uint8_t* o = &out.rgba[i * 4];
                if (mask.weight[i] > 0.0f) {
                    changed += (o[0] != s[0] || o[1] != s[1] || o[2] != s[2]) ? 1u : 0u;
                    continue;
                }
                ++unpainted;
                REQUIRE_MSG(std::equal(s, s + 4, o), "an unpainted texel is copied bit for bit", label);
            }
        }
        REQUIRE_MSG(unpainted > 0 && changed > 0, "the case has painted and unpainted texels", label);
    });
    apricot_test::pass("unpainted_texels_are_bit_exact");
}

// 4. Alpha is never written, including on glass and part-alpha texels.
void alpha_is_never_written() {
    for_each_real_mask([](const PaintImage& atlas, const PaintMask& mask, const char* label) {
        bool saw_part_alpha = false;
        for (const PaintColor target : kFarTargets) {
            PaintImage out;
            REQUIRE_MSG(recolour_paint(atlas, mask, target, out), "the recolour runs", label);
            for (std::size_t i = 3; i < out.rgba.size(); i += 4) {
                REQUIRE_MSG(out.rgba[i] == atlas.rgba[i], "alpha is never written", label);
                saw_part_alpha = saw_part_alpha || atlas.rgba[i] != 255;
            }
        }
        REQUIRE_MSG(saw_part_alpha, "the case carries glass or part alpha", label);
    });
    apricot_test::pass("alpha_is_never_written");
}

void require_lightness_order(const PaintImage& out, const char* label) {
    const std::size_t w = static_cast<std::size_t>(out.width);
    double first = 0.0, prev = -1.0;
    for (std::size_t x = 0; x < w; ++x) {
        const uint8_t* t = &out.rgba[x * 4];
        const double L = paint_srgb8_to_oklab({t[0], t[1], t[2]}).L;
        if (x == 0) first = L;
        REQUIRE_MSG(L >= prev - 0.005, "a lightness ramp stays in order, within one 8-bit level", label);
        prev = L;
    }
    REQUIRE_MSG(prev > first + 0.05, "the ramp still spans a visible range of lightness", label);
}

// 5. Shading is monotonic: a lightness ramp stays ordered after the recolour.
void shading_order_survives_a_recolour() {
    for (const bool bottom_up : {false, true}) {
        const PaintImage ramp = ramp_atlas(bottom_up);
        PaintMask mask;
        REQUIRE(build_paint_mask(ramp, kRampParams, nullptr, mask));
        for (const float w : mask.weight) REQUIRE(w == 1.0f);
        require_lightness_order(ramp, "the source ramp");
        for (const PaintColor target : {mask.base_srgb, kWhite, kBlack, kDeepBlue, kLime}) {
            PaintImage out;
            REQUIRE(recolour_paint(ramp, mask, target, out));
            require_lightness_order(out, "recoloured ramp");
        }
    }
    apricot_test::pass("shading_order_survives_a_recolour");
}

// 6. Identical inputs give identical bytes.
void same_inputs_same_bytes() {
    for (const bool bottom_up : {false, true}) {
        const PaintImage atlas = noisy_atlas(bottom_up);
        const PaintImage copy = noisy_atlas(bottom_up);
        PaintMask a, b;
        REQUIRE(build_paint_mask(atlas, kNoisyParams, nullptr, a));
        REQUIRE(build_paint_mask(copy, kNoisyParams, nullptr, b));
        REQUIRE(same_mask(a, b));
        for (const PaintColor target : kFarTargets) {
            PaintImage x, y;
            REQUIRE(recolour_paint(atlas, a, target, x));
            REQUIRE(recolour_paint(copy, b, target, y));
            REQUIRE(x.rgba == y.rgba && x.width == y.width && x.height == y.height && x.bottom_up == y.bottom_up);
        }
    }
    apricot_test::pass("same_inputs_same_bytes");
}

// 7. Bad input is refused, and a refused call leaves its output alone.
void bad_input_is_refused() {
    const PaintImage ramp = ramp_atlas(true);
    PaintMask untouched;
    untouched.width = 7;
    const auto refused = [&untouched](const PaintImage& atlas, const PaintMaskParams& params,
                                      const PaintMask* region) {
        return !build_paint_mask(atlas, params, region, untouched) && untouched.width == 7;
    };

    REQUIRE(refused(PaintImage{}, kRampParams, nullptr));
    PaintImage short_buffer = ramp;
    short_buffer.rgba.pop_back();
    REQUIRE(refused(short_buffer, kRampParams, nullptr));
    PaintImage negative = ramp;
    negative.width = -16;
    negative.height = -2;
    REQUIRE(refused(negative, kRampParams, nullptr));

    PaintMask ramp_mask;
    REQUIRE(build_paint_mask(ramp, kRampParams, nullptr, ramp_mask));
    REQUIRE(refused(noisy_atlas(true), kNoisyParams, &ramp_mask));  // region of another size

    PaintMaskParams dangling = kRampParams;
    dangling.include_count = 1;  // a count with no list behind it
    REQUIRE(refused(ramp, dangling, nullptr));

    constexpr PaintRef kEmptyBox[] = {
        {PaintRef::Kind::Box, {0, 0, 0}, {0.0f, 0.0f, 0.01f, 0.01f}, {0.06f, 0.05f, 0.35f, 0.20f, 0.08f}},
    };
    PaintMaskParams empty_box = kRampParams;
    empty_box.refs = kEmptyBox;  // covers no texel centre of a 16x2 atlas
    REQUIRE(refused(ramp, empty_box, nullptr));

    // One colour more than a uint16_t palette index holds.
    PaintImage too_many;
    too_many.width = 257;
    too_many.height = 256;
    too_many.rgba.resize(texel_count(257, 256) * 4);
    for (std::size_t i = 0; i < texel_count(257, 256); ++i) {
        too_many.rgba[i * 4] = static_cast<uint8_t>(i >> 16);
        too_many.rgba[i * 4 + 1] = static_cast<uint8_t>(i >> 8);
        too_many.rgba[i * 4 + 2] = static_cast<uint8_t>(i);
        too_many.rgba[i * 4 + 3] = 255;
    }
    REQUIRE(refused(too_many, kRampParams, nullptr));

    PaintImage out;
    out.width = 9;
    const auto recolour_refused = [&out](const PaintImage& atlas, const PaintMask& mask) {
        return !recolour_paint(atlas, mask, kDeepBlue, out) &&
               !recolour_paint_per_texel(atlas, mask, kDeepBlue, out) && out.width == 9;
    };
    REQUIRE(recolour_refused(noisy_atlas(true), ramp_mask));  // mask of another size
    PaintImage repainted = ramp;
    repainted.rgba[0] = static_cast<uint8_t>(repainted.rgba[0] + 1);
    REQUIRE(recolour_refused(repainted, ramp_mask));  // mask built from other pixels
    PaintMask short_trust = ramp_mask;
    short_trust.trust.pop_back();
    REQUIRE(recolour_refused(ramp, short_trust));
    PaintImage self = ramp;
    REQUIRE(!recolour_paint(self, ramp_mask, kDeepBlue, self) && self.rgba == ramp.rgba);
    REQUIRE(!recolour_paint_per_texel(self, ramp_mask, kDeepBlue, self) && self.rgba == ramp.rgba);
    apricot_test::pass("bad_input_is_refused");
}

// 8. A region bounds the mask texel by texel, and nothing else changes.
void region_bounds_the_mask() {
    for (const bool bottom_up : {false, true}) {
        const PaintImage stock = noisy_atlas(bottom_up), alternate = noisy_alternate(bottom_up);
        PaintMask region, own, bounded;
        REQUIRE(build_paint_mask(stock, kNoisyParams, nullptr, region));
        REQUIRE(build_paint_mask(alternate, kNoisyParams, nullptr, own));
        REQUIRE(build_paint_mask(alternate, kNoisyParams, &region, bounded));
        std::size_t cut = 0;
        for (std::size_t i = 0; i < own.weight.size(); ++i) {
            REQUIRE(bounded.weight[i] == std::min(own.weight[i], region.weight[i]));
            REQUIRE(bounded.trust[i] == own.trust[i]);
            cut += own.weight[i] > region.weight[i] ? 1u : 0u;
        }
        REQUIRE(cut > 100);
        REQUIRE(bounded.painted_texels < own.painted_texels);
    }
    apricot_test::pass("region_bounds_the_mask");
}

// 9. Out of gamut, the chroma gives way and the lightness does not.
void out_of_gamut_keeps_lightness() {
    for (const PaintLab lab : {PaintLab{0.70, 0.30, 0.20}, PaintLab{0.45, -0.25, 0.30},
                               PaintLab{0.90, 0.10, -0.35}, PaintLab{0.30, 0.00, -0.40}}) {
        const PaintColor c = paint_oklab_to_srgb8(lab);
        const PaintLab back = paint_srgb8_to_oklab(c);
        REQUIRE_NEAR(back.L, lab.L, 0.006);
        REQUIRE(std::hypot(back.a, back.b) < std::hypot(lab.a, lab.b));
    }
    for (const bool bottom_up : {false, true}) {
        const PaintImage ramp = ramp_atlas(bottom_up);
        PaintMask mask;
        REQUIRE(build_paint_mask(ramp, kRampParams, nullptr, mask));
        for (const PaintColor target : {PaintColor{0, 255, 0}, PaintColor{255, 0, 0}, PaintColor{0, 0, 255},
                                        PaintColor{255, 0, 255}, PaintColor{0, 255, 255}}) {
            PaintImage out;
            REQUIRE(recolour_paint(ramp, mask, target, out));
            require_lightness_order(out, "ramp recoloured to a gamut-edge target");
        }
    }
    apricot_test::pass("out_of_gamut_keeps_lightness");
}

// 10. The unique-colour memo is exactly the per-texel definition.
void memo_equals_per_texel_recolour() {
    std::size_t most_colours = 0;
    for_each_real_mask([&most_colours](const PaintImage& atlas, const PaintMask& mask, const char* label) {
        most_colours = std::max(most_colours, mask.palette.size());
        for (const PaintColor target : {mask.base_srgb, kDeepBlue, kWhite, kBlack, kLime, PaintColor{255, 0, 255}}) {
            PaintImage memo, per_texel;
            REQUIRE_MSG(recolour_paint(atlas, mask, target, memo), "the memo recolour runs", label);
            REQUIRE_MSG(recolour_paint_per_texel(atlas, mask, target, per_texel), "the reference runs", label);
            REQUIRE_MSG(memo.rgba == per_texel.rgba, "memo and per-texel recolour agree bit for bit", label);
        }
    });
    REQUIRE(most_colours > 1000);
    apricot_test::pass("memo_equals_per_texel_recolour");
}

// 11. The OKLab mix the spray reveal uses lands on both ends.
void oklab_mix_hits_both_ends() {
    const PaintColor pairs[][2] = {
        {kDeepBlue, kWhite}, {{255, 0, 0}, {0, 255, 0}}, {kBlack, kLime}, {{0, 0, 0}, {255, 255, 255}},
        {{200, 30, 160}, {200, 30, 160}}, {{35, 86, 201}, {138, 63, 209}},
    };
    const auto near = [](PaintColor x, PaintColor y) {
        return level_diff(x.r, y.r) <= 1 && level_diff(x.g, y.g) <= 1 && level_diff(x.b, y.b) <= 1;
    };
    for (const auto& pair : pairs) {
        const PaintColor a = pair[0], b = pair[1];
        REQUIRE(near(paint_mix_oklab(a, b, 0.0f), a));
        REQUIRE(near(paint_mix_oklab(a, b, 1.0f), b));
        REQUIRE(paint_mix_oklab(a, b, -2.0f) == paint_mix_oklab(a, b, 0.0f));
        REQUIRE(paint_mix_oklab(a, b, 3.0f) == paint_mix_oklab(a, b, 1.0f));
        REQUIRE(paint_mix_oklab(a, b, std::numeric_limits<float>::quiet_NaN()) == paint_mix_oklab(a, b, 0.0f));
        const double la = paint_srgb8_to_oklab(a).L, lb = paint_srgb8_to_oklab(b).L;
        double prev = la;
        for (int step = 1; step <= 16; ++step) {
            const PaintColor mid = paint_mix_oklab(a, b, static_cast<float>(step) / 16.0f);
            const double L = paint_srgb8_to_oklab(mid).L;
            REQUIRE(L >= std::min(la, lb) - 0.006 && L <= std::max(la, lb) + 0.006);
            REQUIRE(lb >= la ? L >= prev - 0.006 : L <= prev + 0.006);
            prev = L;
        }
    }
    apricot_test::pass("oklab_mix_hits_both_ends");
}

// The conversions every rule above stands on.
void colour_space_round_trips() {
    for (int r = 0; r <= 255; r += 15)
        for (int g = 0; g <= 255; g += 15)
            for (int b = 0; b <= 255; b += 15) {
                const PaintColor c{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
                const PaintColor back = paint_oklab_to_srgb8(paint_srgb8_to_oklab(c));
                REQUIRE(level_diff(back.r, c.r) <= 1 && level_diff(back.g, c.g) <= 1 &&
                        level_diff(back.b, c.b) <= 1);
            }
    for (int v = 0; v <= 255; ++v) {
        const uint8_t u = static_cast<uint8_t>(v);
        const PaintLab grey = paint_srgb8_to_oklab({u, u, u});
        REQUIRE(std::abs(grey.a) < 1e-4 && std::abs(grey.b) < 1e-4);
        REQUIRE((paint_oklab_to_srgb8(grey) == PaintColor{u, u, u}));
    }
    REQUIRE_NEAR(paint_srgb8_to_oklab({255, 255, 255}).L, 1.0, 1e-4);
    REQUIRE_NEAR(paint_srgb8_to_oklab({0, 0, 0}).L, 0.0, 1e-9);
    apricot_test::pass("colour_space_round_trips");
}

}  // namespace

int main() {
    golden_matches_lab();
    identity_is_a_no_op();
    unpainted_texels_are_bit_exact();
    alpha_is_never_written();
    shading_order_survives_a_recolour();
    same_inputs_same_bytes();
    bad_input_is_refused();
    region_bounds_the_mask();
    out_of_gamut_keeps_lightness();
    memo_equals_per_texel_recolour();
    oklab_mix_hits_both_ends();
    colour_space_round_trips();
    return apricot_test::done("vehicle_paint_tests");
}

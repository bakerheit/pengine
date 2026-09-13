#include "game/vehicle_paint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <unordered_map>

namespace apricot {
namespace {

using Vec3 = std::array<double, 3>;

// Ottosson's OKLab matrices, verbatim from tools/paint_lab.py.
constexpr double kM1[3][3] = {{0.4122214708, 0.5363325363, 0.0514459929},
                              {0.2119034982, 0.6806995451, 0.1073969566},
                              {0.0883024619, 0.2817188376, 0.6299787005}};
constexpr double kM2[3][3] = {{0.2104542553, 0.7936177850, -0.0040720468},
                              {1.9779984951, -2.4285922050, 0.4505937099},
                              {0.0259040371, 0.7827717662, -0.8086757660}};
constexpr double kM2Inv[3][3] = {{1.0, 0.3963377774, 0.2158037573},
                                 {1.0, -0.1055613458, -0.0638541728},
                                 {1.0, -0.0894841775, -1.2914855480}};
constexpr double kM1Inv[3][3] = {{4.0767416621, -3.3077115913, 0.2309699292},
                                 {-1.2684380046, 2.6097574011, -0.3413193965},
                                 {-0.0041960863, -0.7034186147, 1.7076147010}};
constexpr uint16_t kMaxPaletteIndex = 0xFFFF;

Vec3 mul(const double m[3][3], const Vec3& x) {
    return {m[0][0] * x[0] + m[0][1] * x[1] + m[0][2] * x[2],
            m[1][0] * x[0] + m[1][1] * x[1] + m[1][2] * x[2],
            m[2][0] * x[0] + m[2][1] * x[1] + m[2][2] * x[2]};
}

double srgb_channel_to_linear(uint8_t v) {
    const double c = static_cast<double>(v) / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

Vec3 linear_to_oklab(const Vec3& lin) {
    Vec3 lms = mul(kM1, lin);
    for (double& v : lms) v = std::cbrt(v);
    return mul(kM2, lms);
}

Vec3 oklab_to_linear(const Vec3& lab) {
    Vec3 lms = mul(kM2Inv, lab);
    for (double& v : lms) v = v * v * v;
    return mul(kM1Inv, lms);
}

// The 256 sRGB levels decoded once per call. Local, so nothing here holds
// state between calls.
struct SrgbDecode {
    std::array<double, 256> linear{};
    SrgbDecode() {
        for (std::size_t v = 0; v < linear.size(); ++v)
            linear[v] = srgb_channel_to_linear(static_cast<uint8_t>(v));
    }
    Vec3 oklab(uint8_t r, uint8_t g, uint8_t b) const {
        return linear_to_oklab({linear[std::size_t{r}], linear[std::size_t{g}],
                                linear[std::size_t{b}]});
    }
    Vec3 oklab_packed(uint32_t rgb) const {
        return oklab(static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
                     static_cast<uint8_t>(rgb));
    }
};

uint8_t linear_to_srgb8(double lin) {
    const double c = std::clamp(lin, 0.0, 1.0);
    const double s = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<uint8_t>(std::clamp(std::nearbyint(s * 255.0), 0.0, 255.0));
}

// Per-channel clamp in linear light, no chroma bisection: the lab's
// oklab_to_srgb8, which is what its base8 and every recolour end with.
PaintColor encode_clamped(const Vec3& lab) {
    const Vec3 lin = oklab_to_linear(lab);
    return {linear_to_srgb8(lin[0]), linear_to_srgb8(lin[1]), linear_to_srgb8(lin[2])};
}

bool in_gamut(const Vec3& lin) {
    for (double v : lin)
        if (v < -1e-6 || v > 1.0 + 1e-6) return false;
    return true;
}

// Rule (vi).
Vec3 gamut_clip(const Vec3& lab) {
    if (in_gamut(oklab_to_linear(lab))) return lab;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < kPaintGamutIters; ++i) {
        const double mid = (lo + hi) * 0.5;
        if (in_gamut(oklab_to_linear({lab[0], lab[1] * mid, lab[2] * mid})))
            lo = mid;
        else
            hi = mid;
    }
    return {lab[0], lab[1] * lo, lab[2] * lo};
}

double smoothstep(double e0, double e1, double x) {
    // A zero-width fade is a step. The lab would divide by zero and produce
    // NaN; a profile never asks for it, and a step is the honest limit.
    if (!(e1 > e0)) return x < e0 ? 0.0 : 1.0;
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Tol {
    double c, fc, dark, light, fl;
};

Tol to_double(const PaintTol& t) { return {t.c, t.fc, t.dark, t.light, t.fl}; }

double lightness_window(double L, double ref_l, double dark, double light, double fl) {
    const double dl = L - ref_l;
    return dl < 0.0 ? 1.0 - smoothstep(dark, dark + fl, -dl)
                    : 1.0 - smoothstep(light, light + fl, dl);
}

struct RefPoint {
    Vec3 lab{};
    double qa = 0, qb = 0;
    Tol tol{};
};

double chroma_weight(double qa, double qb, const RefPoint& ref) {
    const double da = qa - ref.qa, db = qb - ref.qb;
    return 1.0 - smoothstep(ref.tol.c, ref.tol.c + ref.tol.fc, std::sqrt(da * da + db * db));
}

double gate_one(double L, double qa, double qb, const RefPoint& ref) {
    return chroma_weight(qa, qb, ref) *
           lightness_window(L, ref.lab[0], ref.tol.dark, ref.tol.light, ref.tol.fl);
}

std::size_t texel_count(const PaintImage& image) {
    return static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
}

bool image_valid(const PaintImage& image) {
    return image.width > 0 && image.height > 0 && image.rgba.size() == texel_count(image) * 4;
}

bool list_valid(const void* items, uint16_t count) { return count == 0 || items != nullptr; }

bool params_valid(const PaintMaskParams& p) {
    return list_valid(p.refs, p.ref_count) && list_valid(p.exclude_refs, p.exclude_ref_count) &&
           list_valid(p.include, p.include_count) && list_valid(p.exclude, p.exclude_count) &&
           list_valid(p.force, p.force_count);
}

uint32_t pack_rgb(const uint8_t* texel) {
    return (uint32_t{texel[0]} << 16) | (uint32_t{texel[1]} << 8) | uint32_t{texel[2]};
}

bool build_palette(const PaintImage& atlas, std::vector<uint32_t>& palette,
                   std::vector<uint16_t>& index) {
    const std::size_t n = texel_count(atlas);
    palette.clear();
    index.assign(n, 0);
    std::unordered_map<uint32_t, uint16_t> lookup;
    lookup.reserve(4096);
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t key = pack_rgb(&atlas.rgba[i * 4]);
        const auto found = lookup.find(key);
        if (found != lookup.end()) {
            index[i] = found->second;
            continue;
        }
        if (palette.size() > kMaxPaletteIndex) return false;
        const auto id = static_cast<uint16_t>(palette.size());
        lookup.emplace(key, id);
        palette.push_back(key);
        index[i] = id;
    }
    return true;
}

// numpy's median: the middle value, or the mean of the two middle values.
double median(std::vector<double>& values) {
    const std::size_t mid = values.size() / 2;
    const auto mid_it = values.begin() + static_cast<std::ptrdiff_t>(mid);
    std::nth_element(values.begin(), mid_it, values.end());
    const double upper = *mid_it;
    if (values.size() % 2 == 1) return upper;
    const double lower = *std::max_element(values.begin(), mid_it);
    return (lower + upper) / 2.0;
}

// Rule (ii): 3x3 neighbourhoods clamp at the edges. Symmetric, so the row
// order of the buffer does not change the result.
template <typename Combine>
std::vector<double> neighbourhood(const std::vector<double>& m, int w, int h, Combine combine) {
    std::vector<double> out(m.size());
    std::array<double, 9> k{};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::size_t j = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const auto yy = static_cast<std::size_t>(std::clamp(y + dy, 0, h - 1));
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto xx = static_cast<std::size_t>(std::clamp(x + dx, 0, w - 1));
                    k[j++] = m[yy * static_cast<std::size_t>(w) + xx];
                }
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                static_cast<std::size_t>(x)] = combine(k);
        }
    }
    return out;
}

std::vector<double> median3(const std::vector<double>& m, int w, int h) {
    return neighbourhood(m, w, h, [](std::array<double, 9>& k) {
        std::nth_element(k.begin(), k.begin() + 4, k.end());
        return k[4];
    });
}

std::vector<double> max3(const std::vector<double>& m, int w, int h) {
    return neighbourhood(m, w, h,
                         [](std::array<double, 9>& k) { return *std::max_element(k.begin(), k.end()); });
}

// Image-space texel centres for rule (i).
struct TexelCentres {
    std::vector<double> u, v;  // u per column; v per BUFFER row
    TexelCentres(const PaintImage& image)
        : u(static_cast<std::size_t>(image.width)), v(static_cast<std::size_t>(image.height)) {
        for (std::size_t x = 0; x < u.size(); ++x)
            u[x] = (static_cast<double>(x) + 0.5) / static_cast<double>(image.width);
        for (int y = 0; y < image.height; ++y) {
            const int image_row = image.bottom_up ? image.height - 1 - y : y;
            v[static_cast<std::size_t>(y)] =
                (static_cast<double>(image_row) + 0.5) / static_cast<double>(image.height);
        }
    }
    bool inside(const UvRect& r, std::size_t x, std::size_t y) const {
        return u[x] >= r.u0 && u[x] < r.u1 && v[y] >= r.v0 && v[y] < r.v1;
    }
    bool inside_any(const UvRect* rects, uint16_t count, std::size_t x, std::size_t y) const {
        for (uint16_t i = 0; i < count; ++i)
            if (inside(rects[i], x, y)) return true;
        return false;
    }
};

// Rule (iii).
bool resolve_ref(const PaintRef& ref, const PaintImage& atlas, const TexelCentres& centres,
                 const std::vector<uint16_t>& index, const std::vector<Vec3>& lab,
                 const SrgbDecode& decode, RefPoint& out) {
    Vec3 colour{};
    if (ref.kind == PaintRef::Kind::Box) {
        std::array<std::vector<double>, 3> channels;
        const auto w = static_cast<std::size_t>(atlas.width);
        for (std::size_t y = 0; y < centres.v.size(); ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                if (!centres.inside(ref.box, x, y)) continue;
                const Vec3& texel = lab[index[y * w + x]];
                for (std::size_t c = 0; c < 3; ++c) channels[c].push_back(texel[c]);
            }
        }
        if (channels[0].empty()) return false;
        for (std::size_t c = 0; c < 3; ++c) colour[c] = median(channels[c]);
    } else {
        colour = decode.oklab(ref.rgb.r, ref.rgb.g, ref.rgb.b);
    }
    const double lf = std::max(colour[0], kPaintLFloor);
    out.lab = colour;
    out.qa = colour[1] / lf;
    out.qb = colour[2] / lf;
    out.tol = to_double(ref.tol);
    return true;
}

// Rule (iv). Iterates in IMAGE row order so the float sums accumulate exactly
// as the lab's do, whichever way up the buffer is.
double histogram_median(const PaintImage& atlas, const std::vector<uint16_t>& index,
                        const std::vector<Vec3>& lab, const std::vector<double>& weights,
                        std::size_t channel, double lo, double hi) {
    std::vector<double> bins(static_cast<std::size_t>(kPaintHistBins), 0.0);
    const auto w = static_cast<std::size_t>(atlas.width);
    for (int image_row = 0; image_row < atlas.height; ++image_row) {
        const int buffer_row = atlas.bottom_up ? atlas.height - 1 - image_row : image_row;
        const std::size_t row = static_cast<std::size_t>(buffer_row) * w;
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t i = row + x;
            const double scaled = (lab[index[i]][channel] - lo) / (hi - lo) * kPaintHistBins;
            const long bin = std::clamp(static_cast<long>(scaled), 0L, long{kPaintHistBins - 1});
            bins[static_cast<std::size_t>(bin)] += weights[i];
        }
    }
    std::vector<double> cumulative(bins.size());
    double sum = 0.0;
    for (std::size_t k = 0; k < bins.size(); ++k) cumulative[k] = sum = sum + bins[k];
    const double half = cumulative.back() * 0.5;
    const auto first = std::lower_bound(cumulative.begin(), cumulative.end(), half);
    const auto k = static_cast<double>(std::distance(cumulative.begin(), first));
    return lo + (k + 0.5) * (hi - lo) / kPaintHistBins;
}

// Everything about a target that does not depend on the texel.
struct TargetFrame {
    Vec3 base{}, target{};
    double qr0 = 0, qr1 = 0, qt0 = 0, qt1 = 0;
    double cr = 0, ct = 0;
    double far_weight = 0, wsat = 0, hue_keep = 1;
    double ur0 = 0, ur1 = 0, ut0 = 0, ut1 = 0;
};

TargetFrame make_frame(const PaintLab& base, const Vec3& target_lab) {
    TargetFrame f;
    f.base = {base.L, base.a, base.b};
    const double d0 = target_lab[0] - f.base[0], d1 = target_lab[1] - f.base[1],
                 d2 = target_lab[2] - f.base[2];
    f.target = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2) < kPaintTargetSnap ? f.base : target_lab;
    const double base_lf = std::max(f.base[0], kPaintLFloor);
    const double target_lf = std::max(f.target[0], kPaintLFloor);
    f.qr0 = f.base[1] / base_lf;
    f.qr1 = f.base[2] / base_lf;
    f.qt0 = f.target[1] / target_lf;
    f.qt1 = f.target[2] / target_lf;
    f.cr = std::sqrt(f.qr0 * f.qr0 + f.qr1 * f.qr1);
    f.ct = std::sqrt(f.qt0 * f.qt0 + f.qt1 * f.qt1);
    const double dq0 = f.qt0 - f.qr0, dq1 = f.qt1 - f.qr1;
    f.far_weight = smoothstep(kPaintTargetNear, kPaintTargetFar, std::sqrt(dq0 * dq0 + dq1 * dq1));
    f.wsat = smoothstep(kPaintSatWLo, kPaintSatWHi, f.cr);
    f.hue_keep = 1.0 - (1.0 - kPaintHueKeepFar) * f.far_weight;
    if (f.cr > 1e-6) {
        f.ur0 = f.qr0 / f.cr;
        f.ur1 = f.qr1 / f.cr;
        f.ut0 = f.ct > 1e-6 ? f.qt0 / f.ct : f.ur0;
        f.ut1 = f.ct > 1e-6 ? f.qt1 / f.ct : f.ur1;
    }
    return f;
}

double remap_lightness(double L, double lr, double lt) {
    const double eps = 1e-4, dl = L - lr;
    if (dl >= 0.0) {
        const double hs = std::max(1.0 - lr, eps), ht = std::max(1.0 - lt, eps);
        const double x = std::clamp(dl / hs, 0.0, 1.0), r = hs / ht;
        return lt + ht * (r * x / (1.0 + (r - 1.0) * x));
    }
    const double ss = std::max(lr, eps), st = std::max(lt, eps);
    const double y = std::clamp(-dl / ss, 0.0, 1.0), r = ss / st;
    return lt - st * (r * y / (1.0 + (r - 1.0) * y));
}

// Rule (v): the lab's recolour_lab for one colour, then (vi) and (vii).
PaintColor recolour_colour(const Vec3& lab, const TargetFrame& f, double trust) {
    const double L = lab[0], lf = std::max(L, kPaintLFloor);
    const double q0 = lab[1] / lf, q1 = lab[2] / lf;
    const double t = 1.0 - f.far_weight * (1.0 - trust);
    const double l_src =
        f.base[0] + (L - f.base[0]) * (kPaintUntrustedLKeep + (1.0 - kPaintUntrustedLKeep) * t);
    const double l_out = remap_lightness(l_src, f.base[0], f.target[0]);
    const double n0 = f.qt0 + (q0 - f.qr0) * t, n1 = f.qt1 + (q1 - f.qr1) * t;
    double s0 = n0, s1 = n1;
    if (f.cr > 1e-6) {
        double along = (q0 * f.ur0 + q1 * f.ur1) / f.cr;
        double across = (q0 * -f.ur1 + q1 * f.ur0) / f.cr;
        along = 1.0 + (along - 1.0) * t;
        across = across * t * f.hue_keep;
        s0 = f.qt0 * along + -f.ut1 * (across * f.ct);
        s1 = f.qt1 * along + f.ut0 * (across * f.ct);
    }
    const double o0 = f.wsat * s0 + (1.0 - f.wsat) * n0;
    const double o1 = f.wsat * s1 + (1.0 - f.wsat) * n1;
    const double out_lf = std::max(l_out, kPaintLFloor);
    return encode_clamped(gamut_clip({l_out, o0 * out_lf, o1 * out_lf}));
}

uint8_t composite_channel(uint8_t src, uint8_t rec, float weight) {
    const double s = src;
    return static_cast<uint8_t>(std::clamp(
        std::nearbyint(s + (static_cast<double>(rec) - s) * static_cast<double>(weight)), 0.0,
        255.0));
}

// The checks both recolour paths share: sizes, the memo belongs to these
// pixels, and trust is a function of colour, which is what makes the memo
// exact. Fills the trust of each painted colour.
bool painted_colours(const PaintImage& atlas, const PaintMask& mask, const PaintImage& out,
                     std::vector<uint8_t>& painted, std::vector<double>& trust) {
    if (&out == &atlas || !image_valid(atlas)) return false;
    const std::size_t n = texel_count(atlas);
    if (mask.width != atlas.width || mask.height != atlas.height || mask.weight.size() != n ||
        mask.trust.size() != n || mask.palette_index.size() != n)
        return false;
    const std::size_t colours = mask.palette.size();
    painted.assign(colours, 0);
    trust.assign(colours, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t id = mask.palette_index[i];
        if (id >= colours || mask.palette[id] != pack_rgb(&atlas.rgba[i * 4])) return false;
        if (!(mask.weight[i] > 0.0f)) continue;
        if (!painted[id]) {
            painted[id] = 1;
            trust[id] = static_cast<double>(mask.trust[i]);
        } else if (trust[id] != static_cast<double>(mask.trust[i])) {
            return false;
        }
    }
    return true;
}

void prepare_output(const PaintImage& atlas, PaintImage& out) {
    out.width = atlas.width;
    out.height = atlas.height;
    out.bottom_up = atlas.bottom_up;
    out.rgba.resize(atlas.rgba.size());
}

}  // namespace

PaintLab paint_srgb8_to_oklab(PaintColor c) {
    const Vec3 lab = linear_to_oklab(
        {srgb_channel_to_linear(c.r), srgb_channel_to_linear(c.g), srgb_channel_to_linear(c.b)});
    return {lab[0], lab[1], lab[2]};
}

PaintColor paint_oklab_to_srgb8(PaintLab lab) {
    return encode_clamped(gamut_clip({lab.L, lab.a, lab.b}));
}

PaintColor paint_mix_oklab(PaintColor a, PaintColor b, float t) {
    const double k = std::isnan(t) ? 0.0 : std::clamp(static_cast<double>(t), 0.0, 1.0);
    const PaintLab x = paint_srgb8_to_oklab(a), y = paint_srgb8_to_oklab(b);
    if (k >= 1.0) return paint_oklab_to_srgb8(y);
    return paint_oklab_to_srgb8({x.L + (y.L - x.L) * k, x.a + (y.a - x.a) * k, x.b + (y.b - x.b) * k});
}

bool build_paint_mask(const PaintImage& atlas, const PaintMaskParams& p, const PaintMask* region,
                      PaintMask& out) {
    if (!image_valid(atlas) || !params_valid(p)) return false;
    const int w = atlas.width, h = atlas.height;
    const auto wu = static_cast<std::size_t>(w), hu = static_cast<std::size_t>(h);
    const std::size_t n = texel_count(atlas);
    if (region != nullptr &&
        (region->width != w || region->height != h || region->weight.size() != n))
        return false;

    PaintMask mask;
    mask.width = w;
    mask.height = h;
    if (!build_palette(atlas, mask.palette, mask.palette_index)) return false;
    const std::size_t colours = mask.palette.size();
    const std::vector<uint16_t>& index = mask.palette_index;

    const SrgbDecode decode;
    std::vector<Vec3> lab(colours);
    for (std::size_t c = 0; c < colours; ++c) lab[c] = decode.oklab_packed(mask.palette[c]);

    const TexelCentres centres(atlas);
    std::vector<RefPoint> refs(p.ref_count), exclude_refs(p.exclude_ref_count);
    for (std::size_t r = 0; r < refs.size(); ++r)
        if (!resolve_ref(p.refs[r], atlas, centres, index, lab, decode, refs[r])) return false;
    for (std::size_t r = 0; r < exclude_refs.size(); ++r)
        if (!resolve_ref(p.exclude_refs[r], atlas, centres, index, lab, decode, exclude_refs[r]))
            return false;

    // The colour gate, per unique colour.
    const bool grow = p.grow_passes > 0;
    const double grow_dark = p.grow_dark, grow_light = p.grow_light;
    std::vector<double> colour_w(colours), trust_w(colours), grow_w(grow ? colours : 0);
    for (std::size_t c = 0; c < colours; ++c) {
        const double L = lab[c][0], lf = std::max(L, kPaintLFloor);
        const double qa = lab[c][1] / lf, qb = lab[c][2] / lf;
        double colour = 0.0, chroma = 0.0, window = 0.0, ex = 0.0;
        for (const RefPoint& ref : refs) {
            const double wc = chroma_weight(qa, qb, ref);
            colour = std::max(colour, wc * lightness_window(L, ref.lab[0], ref.tol.dark,
                                                            ref.tol.light, ref.tol.fl));
            chroma = std::max(chroma, wc);
            if (grow)
                window = std::max(window, lightness_window(L, ref.lab[0], grow_dark, grow_light,
                                                           ref.tol.fl));
        }
        for (const RefPoint& ref : exclude_refs) ex = std::max(ex, gate_one(L, qa, qb, ref));
        colour_w[c] = colour * (1.0 - ex);
        trust_w[c] = std::clamp(chroma * (1.0 - ex), 0.0, 1.0);
        if (grow) grow_w[c] = window * (1.0 - ex);
    }

    // Rects: keep is include-and-not-exclude; force is its own set.
    std::vector<uint8_t> keep(n, 1), forced(n, 0);
    for (std::size_t y = 0; y < hu; ++y) {
        for (std::size_t x = 0; x < wu; ++x) {
            const std::size_t i = y * wu + x;
            bool k = p.include_count == 0 || centres.inside_any(p.include, p.include_count, x, y);
            if (k && centres.inside_any(p.exclude, p.exclude_count, x, y)) k = false;
            keep[i] = k ? 1 : 0;
            forced[i] = centres.inside_any(p.force, p.force_count, x, y) ? 1 : 0;
        }
    }
    const auto apply_rects = [&](std::vector<double>& m) {
        for (std::size_t i = 0; i < n; ++i)
            if (!keep[i]) m[i] = 0.0;
    };

    std::vector<double> m(n);
    for (std::size_t i = 0; i < n; ++i) m[i] = colour_w[index[i]];
    apply_rects(m);
    for (int pass = 0; pass < p.clean; ++pass) {
        m = median3(m, w, h);
        apply_rects(m);
    }
    for (int pass = 0; pass < p.grow_passes; ++pass) {
        const std::vector<double> grown = max3(m, w, h);
        for (std::size_t i = 0; i < n; ++i) m[i] = std::max(m[i], grown[i] * grow_w[index[i]]);
        apply_rects(m);
    }
    if (p.force_count > 0) {
        for (std::size_t i = 0; i < n; ++i)
            if (forced[i]) m[i] = std::max(m[i], 1.0);
        apply_rects(m);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double alpha = atlas.rgba[i * 4 + 3];
        const double glass =
            std::clamp((alpha - kPaintAlphaLo) / (kPaintAlphaHi - kPaintAlphaLo), 0.0, 1.0);
        m[i] = std::clamp(m[i] * glass, 0.0, 1.0);
        if (region != nullptr) m[i] = std::min(m[i], static_cast<double>(region->weight[i]));
    }

    mask.weight.resize(n);
    mask.trust.resize(n);
    std::vector<double> base_w(n);
    bool any_half = false;
    for (std::size_t i = 0; i < n; ++i) {
        mask.weight[i] = static_cast<float>(m[i]);
        mask.trust[i] = static_cast<float>(trust_w[index[i]]);
        base_w[i] = m[i] >= 0.5 ? m[i] : 0.0;
        any_half = any_half || base_w[i] > 0.0;
    }
    if (!any_half)
        for (std::size_t i = 0; i < n; ++i) base_w[i] = m[i] + 1e-9;
    const Vec3 base{histogram_median(atlas, index, lab, base_w, 0, 0.0, 1.0),
                    histogram_median(atlas, index, lab, base_w, 1, -0.5, 0.5),
                    histogram_median(atlas, index, lab, base_w, 2, -0.5, 0.5)};
    mask.base = {base[0], base[1], base[2]};
    mask.base_srgb = encode_clamped(base);

    for (float weight : mask.weight) {
        if (weight >= 0.5f) ++mask.painted_texels;
        mask.weight_sum_q8 +=
            static_cast<uint64_t>(std::nearbyint(static_cast<double>(weight) * 255.0));
    }
    out = std::move(mask);
    return true;
}

bool recolour_paint(const PaintImage& atlas, const PaintMask& mask, PaintColor target,
                    PaintImage& out) {
    std::vector<uint8_t> painted;
    std::vector<double> trust;
    if (!painted_colours(atlas, mask, out, painted, trust)) return false;

    const SrgbDecode decode;
    const TargetFrame frame = make_frame(mask.base, decode.oklab(target.r, target.g, target.b));
    std::vector<PaintColor> recoloured(mask.palette.size());
    for (std::size_t c = 0; c < recoloured.size(); ++c)
        if (painted[c])
            recoloured[c] = recolour_colour(decode.oklab_packed(mask.palette[c]), frame, trust[c]);

    prepare_output(atlas, out);
    const std::size_t n = texel_count(atlas);
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t* s = &atlas.rgba[i * 4];
        uint8_t* o = &out.rgba[i * 4];
        const float weight = mask.weight[i];
        if (!(weight > 0.0f)) {
            std::copy(s, s + 4, o);
            continue;
        }
        const PaintColor rec = recoloured[mask.palette_index[i]];
        o[0] = composite_channel(s[0], rec.r, weight);
        o[1] = composite_channel(s[1], rec.g, weight);
        o[2] = composite_channel(s[2], rec.b, weight);
        o[3] = s[3];
    }
    return true;
}

bool recolour_paint_per_texel(const PaintImage& atlas, const PaintMask& mask, PaintColor target,
                              PaintImage& out) {
    std::vector<uint8_t> painted;
    std::vector<double> trust;
    if (!painted_colours(atlas, mask, out, painted, trust)) return false;

    const PaintLab target_lab = paint_srgb8_to_oklab(target);
    const TargetFrame frame = make_frame(mask.base, {target_lab.L, target_lab.a, target_lab.b});
    prepare_output(atlas, out);
    const std::size_t n = texel_count(atlas);
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t* s = &atlas.rgba[i * 4];
        uint8_t* o = &out.rgba[i * 4];
        const float weight = mask.weight[i];
        if (!(weight > 0.0f)) {
            std::copy(s, s + 4, o);
            continue;
        }
        const PaintLab texel = paint_srgb8_to_oklab({s[0], s[1], s[2]});
        const PaintColor rec = recolour_colour({texel.L, texel.a, texel.b}, frame,
                                               static_cast<double>(mask.trust[i]));
        o[0] = composite_channel(s[0], rec.r, weight);
        o[1] = composite_channel(s[1], rec.g, weight);
        o[2] = composite_channel(s[2], rec.b, weight);
        o[3] = s[3];
    }
    return true;
}

}  // namespace apricot

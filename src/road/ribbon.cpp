#include "road/ribbon.h"
#include "road/sidewalk_clipping.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace apricot {

const char* road_layer_name(RoadLayer l) {
    switch (l) {
        case RoadLayer::Carriageway: return "carriageway";
        case RoadLayer::Unpaved: return "unpaved";
        case RoadLayer::Walk: return "walk";
        case RoadLayer::Kerb: return "kerb";
        case RoadLayer::Plate: return "plate";
        case RoadLayer::Crosswalk: return "crosswalk";
        case RoadLayer::WhiteMarking: return "white-mark";
        case RoadLayer::YellowMarking: return "yellow-mark";
        case RoadLayer::Structure: return "structure";
    }
    return "?";
}

AABB RibbonBake::bounds() const {
    AABB b;
    for (std::size_t i = 0; i < kRoadLayerCount; ++i) b.expand(layers[i].bounds);
    return b;
}

std::size_t RibbonBake::total_triangles() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < kRoadLayerCount; ++i) n += layers[i].triangle_count();
    return n;
}

namespace {

// Material splat weights, in terrain::Surface order (rock, gravel, grass,
// sand). road_surface() picks the row; this turns it into the vector the one
// engine vertex format carries. Roads are drawn with a road material by the
// host layer, but the weights still have to MEAN something — a zero vector
// through the terrain shader is a black road, and "it went black" is a much
// worse bug report than "the asphalt looks like rock".
glm::vec4 splat_for(Surface s) {
    glm::vec4 w{0.0f};
    w[static_cast<int>(surface_index(s))] = 1.0f;
    return w;
}

glm::vec2 perp(glm::vec2 d) { return glm::vec2{-d.y, d.x}; }

glm::vec2 safe_normalize(glm::vec2 v, glm::vec2 fallback = {1.0f, 0.0f}) {
    const float l = glm::length(v);
    return l > 1e-6f ? v / l : fallback;
}

uint32_t push_vertex(RoadMesh& m, glm::vec3 pos, glm::vec3 nrm, glm::vec2 uv,
                     glm::vec4 w) {
    TerrainVertex v;
    v.position = pos;
    v.normal = nrm;
    v.uv = uv;
    v.material_weights = w;
    const uint32_t i = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back(v);
    return i;
}

uint32_t push_flat(RoadMesh& m, const RoadSurface& d, glm::vec2 xz, float lift,
                   glm::vec2 uv, glm::vec4 w) {
    return push_vertex(m, glm::vec3{xz.x, d.at(xz) + lift, xz.y},
                       glm::vec3{0.0f, 1.0f, 0.0f}, uv, w);
}

// Emit one triangle with the winding that makes its face normal point the way
// `want` does.
//
// FRONT FACES ARE COUNTER-CLOCKWISE in this engine, and getting that backwards
// on a road surface does not look wrong — it looks like the road is MISSING,
// from above only, once culling is on. Rather than reason about which corner
// order is CCW for a band whose direction changes every segment, the winding
// is derived from the geometry that was just emitted.
void push_tri_facing(RoadMesh& m, uint32_t i0, uint32_t i1, uint32_t i2,
                     glm::vec3 want) {
    const glm::vec3& a = m.vertices[i0].position;
    const glm::vec3& b = m.vertices[i1].position;
    const glm::vec3& c = m.vertices[i2].position;
    const glm::vec3 n = glm::cross(b - a, c - a);
    m.indices.push_back(i0);
    if (glm::dot(n, want) >= 0.0f) {
        m.indices.push_back(i1);
        m.indices.push_back(i2);
    } else {
        m.indices.push_back(i2);
        m.indices.push_back(i1);
    }
}

constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};

void push_tri_up(RoadMesh& m, uint32_t i0, uint32_t i1, uint32_t i2) {
    push_tri_facing(m, i0, i1, i2, kUp);
}

// A draped ribbon spanning lateral offsets [off_a, off_b] about `pts`, lifted
// `lift` above the surface. Most layers use world XZ UVs so road surfaces and
// plates line up. Sidewalk paving uses distance along the road and offset
// across it: world UVs made the grout run crooked against angled curbs.
//
// `miter_first` / `miter_last`, when set, replace the segment perpendicular at
// the very first / last cross-section with a shared bisector, so two ribbons
// meeting at a bend land on the same line instead of leaving a notch on the
// outside of the corner.
void bake_band_profiled(RoadMesh& m, const RoadSurface& d,
               const std::vector<glm::vec2>& pts,
               float off_a_start, float off_a_end,
               float off_b_start, float off_b_end,
               float lift, float tile, glm::vec4 w, float step_m,
               const glm::vec2* miter_first, const glm::vec2* miter_last,
               bool path_aligned_uv = false) {
    bool have_prev = false;
    float total = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i)
        total += glm::length(pts[i] - pts[i - 1]);
    total = std::max(total, 1e-6f);
    float run = 0.0f;
    const float widest = std::max(std::fabs(off_b_start - off_a_start),
                                  std::fabs(off_b_end - off_a_end));
    const int lateral_steps = std::max(
        1, static_cast<int>(std::ceil(widest / step_m)));
    std::vector<uint32_t> previous(static_cast<std::size_t>(lateral_steps + 1));
    std::vector<uint32_t> current(static_cast<std::size_t>(lateral_steps + 1));

    for (std::size_t s = 0; s + 1 < pts.size(); ++s) {
        const glm::vec2 a = pts[s];
        const glm::vec2 b = pts[s + 1];
        const glm::vec2 delta = b - a;
        const float seg_len = glm::length(delta);
        if (seg_len < 1e-3f) continue;
        const glm::vec2 n = perp(delta / seg_len);

        const int steps = std::max(1, static_cast<int>(std::ceil(seg_len / step_m)));
        // Later segments start at sub-step 1: sub-step 0 is the joint the
        // previous segment already emitted, and duplicating it leaves a
        // hairline crack that only shows at grazing angles.
        const int s0 = (s == 0) ? 0 : 1;
        for (int i = s0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float profile_t = (run + seg_len * t) / total;
            const glm::vec2 p = a + delta * t;
            glm::vec2 ncs = n;
            if (miter_first && s == 0 && i == 0) ncs = *miter_first;
            else if (miter_last && s + 2 == pts.size() && i == steps) ncs = *miter_last;

            for (int lateral = 0; lateral <= lateral_steps; ++lateral) {
                const float u = static_cast<float>(lateral) /
                                static_cast<float>(lateral_steps);
                const float off_a = glm::mix(off_a_start, off_a_end, profile_t);
                const float off_b = glm::mix(off_b_start, off_b_end, profile_t);
                const glm::vec2 q = p + ncs * glm::mix(off_a, off_b, u);
                const glm::vec2 uv = path_aligned_uv
                    ? glm::vec2{(run + seg_len * t) / tile,
                                glm::mix(off_a, off_b, u) / tile}
                    : q / tile;
                current[static_cast<std::size_t>(lateral)] =
                    push_flat(m, d, q, lift, uv, w);
            }
            if (have_prev) {
                for (int lateral = 0; lateral < lateral_steps; ++lateral) {
                    const std::size_t left = static_cast<std::size_t>(lateral);
                    const std::size_t right = left + 1u;
                    push_tri_up(m, previous[left], previous[right], current[right]);
                    push_tri_up(m, previous[left], current[right], current[left]);
                }
            }
            previous.swap(current);
            have_prev = true;
        }
        run += seg_len;
    }
}

void bake_band(RoadMesh& m, const RoadSurface& d, const std::vector<glm::vec2>& pts,
               float off_a, float off_b, float lift, float tile, glm::vec4 w,
               float step_m, const glm::vec2* miter_first,
               const glm::vec2* miter_last, bool path_aligned_uv = false) {
    bake_band_profiled(m, d, pts, off_a, off_a, off_b, off_b, lift, tile, w,
                       step_m, miter_first, miter_last, path_aligned_uv);
}

// The vertical face closing one edge of a raised slab. Stepped and mitered
// exactly like bake_band so its top edge coincides with the slab's edge — a
// riser baked on a different subdivision leaves a gap at every bend.
void bake_kerb_profiled(RoadMesh& m, const RoadSurface& d,
               const std::vector<glm::vec2>& pts, float off_start, float off_end,
               float top_lift, float foot, float outward_sign,
               float slab_m, float step_m, const glm::vec2* miter_first,
               const glm::vec2* miter_last) {
    const float v_top = (top_lift - foot) / slab_m;
    float run = 0.0f;
    bool have_prev = false;
    uint32_t prev_b = 0;
    uint32_t prev_t = 0;
    float total = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i)
        total += glm::length(pts[i] - pts[i - 1]);
    total = std::max(total, 1e-6f);

    for (std::size_t s = 0; s + 1 < pts.size(); ++s) {
        const glm::vec2 a = pts[s];
        const glm::vec2 b = pts[s + 1];
        const glm::vec2 delta = b - a;
        const float seg_len = glm::length(delta);
        if (seg_len < 1e-3f) continue;
        const glm::vec2 dir = delta / seg_len;
        const glm::vec2 n = perp(dir);

        const int steps = std::max(1, static_cast<int>(std::ceil(seg_len / step_m)));
        const int s0 = (s == 0) ? 0 : 1;
        for (int i = s0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float profile_t = (run + seg_len * t) / total;
            const glm::vec2 p = a + delta * t;
            glm::vec2 ncs = n;
            if (miter_first && s == 0 && i == 0) ncs = *miter_first;
            else if (miter_last && s + 2 == pts.size() && i == steps) ncs = *miter_last;

            const glm::vec2 pe = p + ncs * glm::mix(off_start, off_end, profile_t);
            const float base = d.at(pe);
            const float along = run + seg_len * t;
            const glm::vec2 od = safe_normalize(ncs) * outward_sign;
            const glm::vec3 nrm{od.x, 0.0f, od.y};

            const uint32_t ib = push_vertex(
                m, glm::vec3{pe.x, base + foot, pe.y}, nrm,
                glm::vec2{along / slab_m, 0.0f}, glm::vec4{0.0f});
            const uint32_t it = push_vertex(
                m, glm::vec3{pe.x, base + top_lift, pe.y}, nrm,
                glm::vec2{along / slab_m, v_top}, glm::vec4{0.0f});
            if (have_prev) {
                push_tri_facing(m, prev_b, prev_t, it, nrm);
                push_tri_facing(m, prev_b, it, ib, nrm);
            }
            prev_b = ib;
            prev_t = it;
            have_prev = true;
        }
        run += seg_len;
    }
}

void bake_kerb(RoadMesh& m, const RoadSurface& d, const std::vector<glm::vec2>& pts,
               float off, float top_lift, float foot, float outward_sign,
               float slab_m, float step_m, const glm::vec2* miter_first,
               const glm::vec2* miter_last) {
    bake_kerb_profiled(m, d, pts, off, off, top_lift, foot, outward_sign,
                       slab_m, step_m, miter_first, miter_last);
}

// A flat quad, wound upward, world-UV'd.
void push_quad_up(RoadMesh& m, const RoadSurface& d, glm::vec2 p0, glm::vec2 p1,
                  glm::vec2 p2, glm::vec2 p3, float lift, float tile,
                  glm::vec4 w) {
    const uint32_t i0 = push_flat(m, d, p0, lift, p0 / tile, w);
    const uint32_t i1 = push_flat(m, d, p1, lift, p1 / tile, w);
    const uint32_t i2 = push_flat(m, d, p2, lift, p2 / tile, w);
    const uint32_t i3 = push_flat(m, d, p3, lift, p3 / tile, w);
    push_tri_up(m, i0, i1, i2);
    push_tri_up(m, i0, i2, i3);
}

// A junction plate can be tens of metres wide. One triangle from its centre
// to each hull edge is only correct over a planar ground field; over a shaped
// grade that chord cuts straight through the terrain and the ground wins the
// depth test in large green islands. Tessellate the fan and sample the same
// terrain mesh at every small cell, just like the road ribbons do.
void push_draped_triangle(RoadMesh& m, const RoadSurface& d, glm::vec2 centre,
                          glm::vec2 p0, glm::vec2 p1, float lift, float tile,
                          glm::vec4 w, float step_m) {
    const float longest = std::max({glm::length(p0 - centre),
                                    glm::length(p1 - centre),
                                    glm::length(p1 - p0)});
    const int steps = std::max(1, static_cast<int>(std::ceil(longest / step_m)));
    std::vector<uint32_t> previous;
    std::vector<uint32_t> current;
    for (int row = 0; row <= steps; ++row) {
        current.clear();
        current.reserve(static_cast<std::size_t>(row + 1));
        const float edge = static_cast<float>(row) / static_cast<float>(steps);
        for (int column = 0; column <= row; ++column) {
            const float across = row > 0
                ? static_cast<float>(column) / static_cast<float>(row)
                : 0.0f;
            const glm::vec2 q = glm::mix(centre, glm::mix(p0, p1, across), edge);
            current.push_back(push_flat(m, d, q, lift, q / tile, w));
        }
        if (row > 0) {
            for (int column = 0; column < row; ++column) {
                const std::size_t left = static_cast<std::size_t>(column);
                const std::size_t right = left + 1u;
                push_tri_up(m, previous[left], current[left], current[right]);
                if (column + 1 < row)
                    push_tri_up(m, previous[left], current[right], previous[right]);
            }
        }
        previous.swap(current);
    }
}

// A vertical face from p0 to p1, from `foot` to `top_lift` above the surface,
// with its normal pointing along `out`.
void push_kerb_quad(RoadMesh& m, const RoadSurface& d, glm::vec2 p0, glm::vec2 p1,
                    glm::vec2 out, float top_lift, float foot, float slab_m) {
    const float len = glm::length(p1 - p0);
    if (len < 1e-4f) return;
    const glm::vec2 od = safe_normalize(out);
    const glm::vec3 nrm{od.x, 0.0f, od.y};
    const float v_top = (top_lift - foot) / slab_m;
    const float y0 = d.at(p0);
    const float y1 = d.at(p1);
    const uint32_t b0 = push_vertex(m, {p0.x, y0 + foot, p0.y}, nrm, {0.0f, 0.0f}, {});
    const uint32_t t0 = push_vertex(m, {p0.x, y0 + top_lift, p0.y}, nrm, {0.0f, v_top}, {});
    const uint32_t t1 = push_vertex(m, {p1.x, y1 + top_lift, p1.y}, nrm, {len / slab_m, v_top}, {});
    const uint32_t b1 = push_vertex(m, {p1.x, y1 + foot, p1.y}, nrm, {len / slab_m, 0.0f}, {});
    push_tri_facing(m, b0, t0, t1, nrm);
    push_tri_facing(m, b0, t1, b1, nrm);
}

// `pts` with `cut_front` metres removed from the start and `cut_back` from the
// end, measured along arc length. Interior points survive. A road that lived
// entirely inside its junctions keeps a sliver rather than vanishing — an
// erased road is a hole in the network that reads as a bug in the map.
std::vector<glm::vec2> trim_polyline(const std::vector<glm::vec2>& pts,
                                     float cut_front, float cut_back) {
    if (pts.size() < 2) return {};
    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        total += glm::length(pts[i + 1] - pts[i]);

    const float max_cut = total * 0.9f;
    if (cut_front + cut_back > max_cut && cut_front + cut_back > 0.0f) {
        const float scale = max_cut / (cut_front + cut_back);
        cut_front *= scale;
        cut_back *= scale;
    }
    const float a0 = cut_front;
    const float a1 = total - cut_back;
    if (a1 - a0 < 1e-2f) return {};

    auto point_at = [&](float arc) {
        float run = 0.0f;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const float seg = glm::length(pts[i + 1] - pts[i]);
            if (run + seg >= arc) {
                const float t = seg > 1e-6f ? (arc - run) / seg : 0.0f;
                return pts[i] + (pts[i + 1] - pts[i]) * t;
            }
            run += seg;
        }
        return pts.back();
    };

    std::vector<glm::vec2> out;
    out.push_back(point_at(a0));
    float run = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        run += glm::length(pts[i + 1] - pts[i]);
        if (run > a0 + 1e-3f && run < a1 - 1e-3f) out.push_back(pts[i + 1]);
    }
    out.push_back(point_at(a1));
    return out;
}

float polyline_length(const std::vector<glm::vec2>& pts) {
    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        total += glm::length(pts[i + 1] - pts[i]);
    return total;
}

// Exact arc-length slice, used for paint dashes. trim_polyline intentionally
// preserves a sliver when cuts consume most of a road; a dash needs the
// opposite rule or the last gap in a short edge gets accidentally painted.
std::vector<glm::vec2> slice_polyline(const std::vector<glm::vec2>& pts,
                                      float from_m, float to_m) {
    if (pts.size() < 2 || to_m - from_m < 1e-3f) return {};
    const float total = polyline_length(pts);
    from_m = std::clamp(from_m, 0.0f, total);
    to_m = std::clamp(to_m, from_m, total);
    if (to_m - from_m < 1e-3f) return {};

    auto point_at = [&](float arc) {
        float run = 0.0f;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const float seg = glm::length(pts[i + 1] - pts[i]);
            if (run + seg >= arc) {
                const float t = seg > 1e-6f ? (arc - run) / seg : 0.0f;
                return pts[i] + (pts[i + 1] - pts[i]) * t;
            }
            run += seg;
        }
        return pts.back();
    };

    std::vector<glm::vec2> out{point_at(from_m)};
    float run = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        run += glm::length(pts[i + 1] - pts[i]);
        if (run > from_m + 1e-3f && run < to_m - 1e-3f)
            out.push_back(pts[i + 1]);
    }
    out.push_back(point_at(to_m));
    return out;
}

void bake_solid_marking(RoadMesh& m, const RoadSurface& d,
                        const std::vector<glm::vec2>& pts, float offset,
                        float width, float lift, float step_m, glm::vec4 w,
                        const glm::vec2* miter_first = nullptr,
                        const glm::vec2* miter_last = nullptr) {
    bake_band(m, d, pts, offset - width * 0.5f, offset + width * 0.5f,
              lift, 1.0f, w, step_m, miter_first, miter_last);
}

void bake_solid_marking_profiled(RoadMesh& m, const RoadSurface& d,
                        const std::vector<glm::vec2>& pts,
                        float offset_start, float offset_end,
                        float width, float lift, float step_m, glm::vec4 w,
                        const glm::vec2* miter_first = nullptr,
                        const glm::vec2* miter_last = nullptr) {
    bake_band_profiled(m, d, pts,
        offset_start - width * 0.5f, offset_end - width * 0.5f,
        offset_start + width * 0.5f, offset_end + width * 0.5f,
        lift, 1.0f, w, step_m, miter_first, miter_last);
}

void bake_dashed_marking(RoadMesh& m, const RoadSurface& d,
                         const std::vector<glm::vec2>& pts, float offset,
                         float width, float lift, float dash_m, float gap_m,
                         float step_m, glm::vec4 w) {
    const float total = polyline_length(pts);
    const float period = dash_m + gap_m;
    if (dash_m <= 0.0f || period <= 0.0f) return;

    // Centre the pattern's leftover distance so neither road end gets a tiny
    // accidental paint chip. The phase is deterministic for this edge.
    const int count = std::max(1, static_cast<int>(
        std::floor((total + gap_m) / period)));
    const float painted_span = static_cast<float>(count) * dash_m +
                               static_cast<float>(count - 1) * gap_m;
    const float phase = std::max(0.0f, (total - painted_span) * 0.5f);
    for (int i = 0; i < count; ++i) {
        const float start = phase + static_cast<float>(i) * period;
        const float end = std::min(start + dash_m, total);
        const std::vector<glm::vec2> dash = slice_polyline(pts, start, end);
        if (dash.size() >= 2)
            bake_solid_marking(m, d, dash, offset, width, lift, step_m, w);
    }
}

void bake_dashed_marking_profiled(RoadMesh& m, const RoadSurface& d,
                         const std::vector<glm::vec2>& pts,
                         float offset_start, float offset_end,
                         float width, float lift, float dash_m, float gap_m,
                         float step_m, glm::vec4 w) {
    const float total = polyline_length(pts);
    const float period = dash_m + gap_m;
    if (dash_m <= 0.0f || period <= 0.0f || total <= 1e-6f) return;
    const int count = std::max(1, static_cast<int>(
        std::floor((total + gap_m) / period)));
    const float painted_span = static_cast<float>(count) * dash_m +
                               static_cast<float>(count - 1) * gap_m;
    const float phase = std::max(0.0f, (total - painted_span) * 0.5f);
    for (int i = 0; i < count; ++i) {
        const float start = phase + static_cast<float>(i) * period;
        const float end = std::min(start + dash_m, total);
        const std::vector<glm::vec2> dash = slice_polyline(pts, start, end);
        if (dash.size() < 2) continue;
        bake_solid_marking_profiled(
            m, d, dash,
            glm::mix(offset_start, offset_end, start / total),
            glm::mix(offset_start, offset_end, end / total),
            width, lift, step_m, w);
    }
}

// 2D convex hull, monotone chain, without the duplicated closing point.
std::vector<glm::vec2> convex_hull(std::vector<glm::vec2> p) {
    std::sort(p.begin(), p.end(), [](glm::vec2 a, glm::vec2 b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    p.erase(std::unique(p.begin(), p.end(),
                        [](glm::vec2 a, glm::vec2 b) {
                            return glm::length(a - b) < 1e-4f;
                        }),
            p.end());
    if (p.size() < 3) return p;
    auto turn = [](glm::vec2 o, glm::vec2 a, glm::vec2 b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    auto build = [&](auto first, auto last) {
        std::vector<glm::vec2> chain;
        for (auto it = first; it != last; ++it) {
            while (chain.size() >= 2 &&
                   turn(chain[chain.size() - 2], chain.back(), *it) <= 0.0f)
                chain.pop_back();
            chain.push_back(*it);
        }
        chain.pop_back();
        return chain;
    };
    std::vector<glm::vec2> lower = build(p.begin(), p.end());
    const std::vector<glm::vec2> upper = build(p.rbegin(), p.rend());
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

// Replace the flat placeholder normals with area-weighted smooth ones, so a
// road over a grade lights like a road over a grade instead of like a decal.
// Only called on the horizontal layers — the kerb risers carry a deliberate
// horizontal normal and averaging it against the slab top would round the step
// off into a ramp.
void smooth_normals(RoadMesh& m) {
    for (TerrainVertex& v : m.vertices) v.normal = glm::vec3{0.0f};
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const uint32_t i0 = m.indices[i];
        const uint32_t i1 = m.indices[i + 1];
        const uint32_t i2 = m.indices[i + 2];
        const glm::vec3& a = m.vertices[i0].position;
        const glm::vec3& b = m.vertices[i1].position;
        const glm::vec3& c = m.vertices[i2].position;
        const glm::vec3 fn = glm::cross(b - a, c - a);  // area weighted
        m.vertices[i0].normal += fn;
        m.vertices[i1].normal += fn;
        m.vertices[i2].normal += fn;
    }
    for (TerrainVertex& v : m.vertices) {
        const float l = glm::length(v.normal);
        v.normal = l > 1e-8f ? v.normal / l : kUp;
    }
}

// The same oriented box goes to the renderer and body collider.
void solid_box(RibbonBake& bake, glm::vec3 centre, glm::vec3 half, float yaw) {
    bake.solids.push_back({centre, half, yaw});
    RoadMesh& mesh = bake.layer(RoadLayer::Structure);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p{(i & 1) ? half.x : -half.x,
                          (i & 2) ? half.y : -half.y,
                          (i & 4) ? half.z : -half.z};
        push_vertex(mesh, centre + glm::vec3{c*p.x+s*p.z,p.y,-s*p.x+c*p.z},
                    {0,1,0}, {p.x,p.z}, splat_for(Surface::Rock));
    }
    const uint32_t faces[][4] = {{0,4,6,2},{1,3,7,5},{0,1,5,4},
                                 {2,6,7,3},{0,2,3,1},{4,5,7,6}};
    for (const auto& face : faces) {
        const glm::vec3 mid = (mesh.vertices[base+face[0]].position +
                              mesh.vertices[base+face[2]].position)*0.5f;
        push_tri_facing(mesh,base+face[0],base+face[1],base+face[2],mid-centre);
        push_tri_facing(mesh,base+face[0],base+face[2],base+face[3],mid-centre);
    }
}

void finalise(RoadMesh& m) {
    m.bounds = AABB{};
    for (const TerrainVertex& v : m.vertices) m.bounds.expand(v.position);
}

// One road end arriving at a node.
struct Approach {
    uint32_t edge = 0;
    bool at_front = false;
    glm::vec2 dir{1.0f, 0.0f};  // unit, pointing away from the node
    float hw = 0.0f;            // carriageway half-width
    float ext = 0.0f;           // outer sidewalk extent (== hw when none)
    bool walk = false;
    bool paved = true;
    float angle = 0.0f;
};

// A narrow access road terminating on one side of a straight sidewalk road.
// The main road stays whole; only this short throat replaces the near walk.
struct CurbCutTee {
    uint32_t node = 0;
    Approach spur;
    Approach main;
    float inner_distance_m = 0.0f;
    float outer_distance_m = 0.0f;
};

}  // namespace

RibbonBake bake_ribbons(const RoadGraph& graph, const GroundSampler& ground,
                        const RibbonParams& params) {
    RibbonBake out;
    const std::size_t ncount = graph.node_count();
    const std::size_t ecount = graph.edge_count();
    if (ecount == 0) return out;

    // --- approaches, gathered from the edges so a loop contributes both ends
    std::vector<std::vector<Approach>> app(ncount);
    for (uint32_t ei = 0; ei < ecount; ++ei) {
        const RoadEdge& e = graph.edge(ei);
        const std::size_t n = e.points.size();
        const bool walk = e.sidewalks();
        const float front_hw = e.half_width_at(0.0f);
        const float back_hw = e.half_width_at(1.0f);
        const bool paved = road_is_paved(e.cls);

        Approach front;
        front.edge = ei;
        front.at_front = true;
        front.dir = safe_normalize(e.points[1] - e.points[0]);
        front.hw = front_hw;
        front.ext = walk ? front_hw + kSidewalkWidthM : front_hw;
        front.walk = walk;
        front.paved = paved;
        front.angle = std::atan2(front.dir.y, front.dir.x);
        app[e.node_a].push_back(front);

        Approach back = front;
        back.at_front = false;
        back.dir = safe_normalize(e.points[n - 2] - e.points[n - 1]);
        back.hw = back_hw;
        back.ext = walk ? back_hw + kSidewalkWidthM : back_hw;
        back.angle = std::atan2(back.dir.y, back.dir.x);
        app[e.node_b].push_back(back);
    }

    // --- classify every node: nothing, a miter, or a plate -----------------
    std::vector<float> trim_front(ecount, 0.0f);
    std::vector<float> trim_back(ecount, 0.0f);
    std::vector<glm::vec2> miter_front(ecount, glm::vec2{0.0f});
    std::vector<glm::vec2> miter_back(ecount, glm::vec2{0.0f});
    std::vector<uint8_t> has_mf(ecount, 0);
    std::vector<uint8_t> has_mb(ecount, 0);
    std::vector<uint32_t> plate_nodes;
    std::vector<float> plate_trim(ncount, 0.0f);
    std::vector<CurbCutTee> curb_cut_tees;

    for (uint32_t n = 0; n < ncount; ++n) {
        std::vector<Approach>& a = app[n];
        if (a.size() < 2) continue;  // dead end: the square cap is correct

        float max_hw = 0.0f;
        for (const Approach& ap : a) max_hw = std::max(max_hw, ap.hw);
        float trim = max_hw + params.junction_margin_m;

        // A private access meeting the side of a straight through-road is a
        // curb cut, not a full three-way crossing. The old convex-hull plate
        // pulled both halves of the main sidewalk back by a full road width,
        // erased the opposite frontage, and left triangular grass gores at
        // the mouth. Honor the authored hint only when the topology proves it
        // is safe: one narrow no-walk spur and two matching, straight, paved
        // sidewalk approaches.
        if (a.size() == 3) {
            int spur_index = -1;
            for (int i = 0; i < 3; ++i) {
                if (graph.edge(a[static_cast<std::size_t>(i)].edge).curb_cut_tee)
                    spur_index = spur_index == -1 ? i : -2;
            }
            if (spur_index >= 0) {
                const Approach& spur = a[static_cast<std::size_t>(spur_index)];
                const Approach& m0 = a[static_cast<std::size_t>((spur_index + 1) % 3)];
                const Approach& m1 = a[static_cast<std::size_t>((spur_index + 2) % 3)];
                glm::vec2 normal = perp(m0.dir);
                if (glm::dot(normal, spur.dir) < 0.0f) normal = -normal;
                const float facing = glm::dot(normal, spur.dir);
                const bool straight = glm::dot(m0.dir, m1.dir) < -0.985f;
                const bool matching = std::fabs(m0.hw - m1.hw) < 0.25f &&
                                      m0.walk && m1.walk && m0.paved && m1.paved;
                const bool narrow_spur = !spur.walk && spur.paved &&
                                         spur.hw + 0.5f < m0.hw;
                const bool ground_level =
                    !road_structure_is_decked(graph.edge(spur.edge).structure) &&
                    !road_structure_is_decked(graph.edge(m0.edge).structure) &&
                    !road_structure_is_decked(graph.edge(m1.edge).structure);
                if (straight && matching && narrow_spur && ground_level && facing > 0.94f) {
                    const float inner = m0.hw / facing;
                    const float outer = m0.ext / facing;
                    if (spur.at_front) trim_front[spur.edge] = outer;
                    else trim_back[spur.edge] = outer;
                    curb_cut_tees.push_back({n, spur, m0, inner, outer});
                    continue;
                }
            }
        }

        if (a.size() == 2) {
            // Outward dirs: dot -1 is a straight continuation, 0 a right
            // angle, +1 a hairpin.
            const float c = glm::dot(a[0].dir, a[1].dir);
            const bool width_step = std::fabs(a[0].hw - a[1].hw) > 0.25f;
            const bool walk_step = a[0].walk != a[1].walk;
            const bool surface_step = a[0].paved != a[1].paved;
            const bool step = width_step || walk_step || surface_step;

            if (!step && c < -0.985f) continue;  // straight: the ends meet flush
            if (!step && c < -0.5f) {
                // Gentle bend. Both ribbons cap on the shared bisector so the
                // surface flows through the corner with no plate and no
                // overlap. A SHARPER corner falls through to the plate below:
                // mitering a hairpin folds the inner sidewalk over itself.
                const glm::vec2 dd = a[1].dir - a[0].dir;
                const float denom = std::max(1.0f - c, 0.40f);
                const glm::vec2 mit = perp(dd) / denom;
                for (const Approach& ap : a) {
                    if (ap.at_front) {
                        miter_front[ap.edge] = mit;
                        has_mf[ap.edge] = 1;
                    } else {
                        miter_back[ap.edge] = mit;
                        has_mb[ap.edge] = 1;
                    }
                }
                continue;
            }
            // Sharp corner, or a change of road: tight trim, so the surface
            // runs right up to the corner and the plate is one road wide.
            trim = max_hw;
        }

        if(a.size()>=3) {
            // A narrow street can meet a freeway at an acute angle. Half the
            // widest road is enough at 90 degrees, but not there: its raised
            // sidewalk otherwise continues several metres into a freeway lane
            // (Route 1 / Kestrel Close). Pull back to the actual intersection
            // of the sidewalk outer edge and the freeway carriageway edge.
            // Keep ordinary street junctions and degree-two bends unchanged.
            float limit=4.f*max_hw;
            for(const Approach& ap:a)
                limit=std::min(limit,std::max(trim,graph.edge(ap.edge).length_m*.45f));
            for(const Approach& walk:a) if(walk.walk)
                for(const Approach& freeway:a) {
                    if(graph.edge(freeway.edge).cls!=RoadClass::Freeway) continue;
                    const float cosine=glm::dot(walk.dir,freeway.dir);
                    const float sine=std::fabs(walk.dir.x*freeway.dir.y-walk.dir.y*freeway.dir.x);
                    if(cosine<=0.f || sine<1e-4f) continue;
                    const float meet=(freeway.hw+walk.ext*cosine)/sine;
                    trim=std::max(trim,std::min(meet+params.junction_margin_m,limit));
                }
        }

        for (const Approach& ap : a) {
            if (ap.at_front) trim_front[ap.edge] = trim;
            else trim_back[ap.edge] = trim;
        }
        plate_trim[n] = trim;
        plate_nodes.push_back(n);

        // Angular order, so "the corner between these two approaches" is well
        // defined. Ties broken on the edge index so the sort is total and the
        // bake is reproducible.
        std::sort(a.begin(), a.end(), [](const Approach& x, const Approach& y) {
            if (x.angle != y.angle) return x.angle < y.angle;
            return x.edge < y.edge;
        });
    }

    // --- bake the trimmed ribbons -----------------------------------------
    const float walk_lift = kDrapeEpsM + kKerbHeightM;
    std::vector<uint32_t> road_owners,walk_owners,kerb_owners;
    for (uint32_t ei = 0; ei < ecount; ++ei) {
        const RoadEdge& e = graph.edge(ei);
        std::vector<glm::vec2> pts =
            trim_polyline(e.points, trim_front[ei], trim_back[ei]);
        if (pts.size() < 2) continue;

        // Paint starts at the authored traffic seam. Only the asphalt and its
        // collision need to overlap the freeway; extending the ramp's edge
        // lines as well draws a long white wedge across the auxiliary lane.
        const std::vector<glm::vec2> marking_pts = pts;

        // lane_connect_* joins traffic at a freeway lane endpoint away from
        // the road-centre graph node. The authored point is therefore the SIM
        // seam, not the visible end of the ramp. A normal ribbon caps there
        // with a square cross-section; its outside corner sticks past the
        // freeway shoulder and reads as a detached rectangular platform. Run
        // the visual/collision ribbon into the broad auxiliary deck instead,
        // burying that cap under same-height asphalt. Parapet placement already
        // rejects pieces overlapping another road, so the throat stays open.
        const float overlap = std::max(0.0f, params.lane_connect_overlap_m);
        if (e.one_way && overlap > 0.0f && e.lane_connect_start &&
            trim_front[ei] <= 1e-4f) {
            const glm::vec2 dir = safe_normalize(pts[1] - pts[0]);
            pts.insert(pts.begin(), pts.front() - dir * overlap);
        }
        if (e.one_way && overlap > 0.0f && e.lane_connect_end &&
            trim_back[ei] <= 1e-4f) {
            const std::size_t last = pts.size() - 1;
            const glm::vec2 dir = safe_normalize(pts[last] - pts[last - 1]);
            pts.push_back(pts.back() + dir * overlap);
        }

        const RoadSurface d = RoadSurface::of(e, ground);

        // Orient each shared bisector to THIS ribbon's own perpendicular, so
        // the band does not flip inside out while both ribbons still land on
        // the same line.
        glm::vec2 mf{0.0f};
        glm::vec2 mb{0.0f};
        const glm::vec2* pmf = nullptr;
        const glm::vec2* pmb = nullptr;
        if (has_mf[ei]) {
            const glm::vec2 nn = perp(safe_normalize(pts[1] - pts[0]));
            mf = glm::dot(miter_front[ei], nn) >= 0.0f ? miter_front[ei]
                                                       : -miter_front[ei];
            pmf = &mf;
        }
        if (has_mb[ei]) {
            const std::size_t n = pts.size();
            const glm::vec2 nn = perp(safe_normalize(pts[n - 1] - pts[n - 2]));
            mb = glm::dot(miter_back[ei], nn) >= 0.0f ? miter_back[ei]
                                                      : -miter_back[ei];
            pmb = &mb;
        }

        const float profile_start = e.length_m > 1e-6f
            ? std::clamp(trim_front[ei] / e.length_m, 0.0f, 1.0f) : 0.0f;
        const float profile_end = e.length_m > 1e-6f
            ? std::clamp(1.0f - trim_back[ei] / e.length_m, 0.0f, 1.0f) : 1.0f;
        const float hw_start = e.half_width_at(profile_start);
        const float hw_end = e.half_width_at(profile_end);
        const float hw = std::max(hw_start, hw_end);
        const glm::vec4 w = splat_for(road_surface(e.cls));
        RoadMesh& surf = road_is_paved(e.cls) ? out.layer(RoadLayer::Carriageway)
                                              : out.layer(RoadLayer::Unpaved);
        bake_band_profiled(surf, d, pts, -hw_start, -hw_end, hw_start, hw_end,
                  kDrapeEpsM, params.uv_tile_m, w, params.step_m, pmf, pmb);
        road_owners.resize(out.layer(RoadLayer::Carriageway).triangle_count(),ei);

        if (e.structure == RoadStructure::Bridge &&
            e.bridge_detail_style != BridgeDetailStyle::None) {
            const bool municipal =
                e.bridge_detail_style == BridgeDetailStyle::Municipal;
            const float deck_depth = municipal ? 1.15f : 0.9f;
            // Close the deck: a bridge is not a one-sided floating decal.
            RoadMesh& concrete = out.layer(RoadLayer::Structure);
            const std::size_t first_index = concrete.indices.size();
            const std::size_t first_vertex = concrete.vertices.size();
            bake_band_profiled(concrete, d, pts, -hw_start, -hw_end,
                      hw_start, hw_end, kDrapeEpsM - deck_depth,
                      params.uv_tile_m, w, params.step_m, pmf, pmb);
            for (std::size_t i = first_index; i + 2 < concrete.indices.size(); i += 3)
                std::swap(concrete.indices[i + 1], concrete.indices[i + 2]);
            for (std::size_t i = first_vertex; i < concrete.vertices.size(); ++i)
                concrete.vertices[i].normal = {0.0f, -1.0f, 0.0f};
            bake_kerb_profiled(concrete, d, pts, -hw_start, -hw_end, kDrapeEpsM,
                      kDrapeEpsM - deck_depth,
                      -1.0f, params.slab_m, params.step_m, pmf, pmb);
            bake_kerb_profiled(concrete, d, pts, hw_start, hw_end, kDrapeEpsM,
                      kDrapeEpsM - deck_depth,
                      1.0f, params.slab_m, params.step_m, pmf, pmb);
            float bridge_run = 0.0f;
            const float bridge_length = std::max(polyline_length(pts), 1e-6f);
            for (std::size_t si = 1; si < pts.size(); ++si) {
                const glm::vec2 delta = pts[si] - pts[si - 1];
                const float len = glm::length(delta);
                if (len < 1.0f) continue;
                const glm::vec2 tangent = delta / len, side = perp(tangent);
                const float yaw = std::atan2(tangent.x, tangent.y);
                // The creek bridge uses a small-town public-works kit: a low
                // concrete crash wall, regular steel posts and one slim upper
                // rail. It is maintained and credible without reading as a
                // landmark. The viaduct keeps its heavier solid parapet.
                const float rail_step = municipal
                    ? 5.5f
                    : (e.deck_heights.empty() ? 12.0f : 2.0f);
                const int pieces = std::max(1, static_cast<int>(std::ceil(len / rail_step)));
                const float pitch = len / static_cast<float>(pieces);
                const float end_clearance = municipal ? 2.5f : 22.0f;
                for (int k = 0; k < pieces; ++k) {
                    const float along = (static_cast<float>(k) + 0.5f) * pitch;
                    const float local_t = (bridge_run + along) / bridge_length;
                    const float local_hw = glm::mix(hw_start, hw_end, local_t);
                    // Keep merge mouths open, including the shallow-angle gore.
                    if ((si == 1 && along < end_clearance) ||
                        (si + 1 == pts.size() &&
                         len - along < end_clearance)) continue;
                    const glm::vec2 p = pts[si - 1] + tangent * along;
                    for (float sign : {-1.0f, 1.0f}) {
                        const float rail_offset = local_hw +
                                                  (municipal ? 0.28f : 0.35f);
                        const glm::vec2 q = p + side * (sign * rail_offset);
                        bool merge = false;
                        for (const RoadEdge& other : graph.edges()) {
                            if (&other == &e ||
                                std::fabs(RoadSurface::of(other,ground).at(q)-d.at(q))>1.0f) continue;
                            for (std::size_t j=1;j<other.points.size();++j) {
                                const glm::vec2 a=other.points[j-1], v=other.points[j]-a;
                                const float t=std::clamp(glm::dot(q-a,v)/glm::dot(v,v),0.0f,1.0f);
                                if (glm::length(q-a-v*t)<other.half_width_m()+pitch*0.5f+1.0f)
                                    merge=true;
                            }
                        }
                        if (merge) continue;
                        if (municipal) {
                            solid_box(out, {q.x, d.at(q) + 0.34f, q.y},
                                      {0.28f, 0.34f, pitch * 0.5f + 0.04f},
                                      yaw);
                            solid_box(out, {q.x, d.at(q) + 1.02f, q.y},
                                      {0.10f, 0.09f, pitch * 0.5f + 0.04f},
                                      yaw);
                            solid_box(out, {q.x, d.at(q) + 0.78f, q.y},
                                      {0.13f, 0.50f, 0.13f}, yaw);
                        } else {
                            solid_box(out, {q.x,d.at(q)+0.55f,q.y},
                                      {0.35f,0.55f,pitch*0.5f+0.05f},yaw);
                        }
                    }
                }
                if (!e.deck_heights.empty()) continue;
                const glm::vec2 mid = (pts[si]+pts[si-1])*0.5f;
                // Volume beneath the already-drawn asphalt and fascia.
                out.solids.push_back(
                    {{mid.x, d.at(mid) + kDrapeEpsM - deck_depth * 0.5f,
                      mid.y},
                     {glm::mix(hw_start, hw_end,
                               (bridge_run + len * 0.5f) / bridge_length),
                      deck_depth * 0.5f, len * 0.5f}, yaw});
                const int piers = municipal
                    ? std::max(1, static_cast<int>(len / 110.0f))
                    : static_cast<int>(len / 65.0f);
                for (int k = 0; k < piers; ++k) {
                    const glm::vec2 at = pts[si-1] + tangent *
                        (len * (static_cast<float>(k)+0.5f)/static_cast<float>(piers));
                    const float at_t = (bridge_run + len *
                        (static_cast<float>(k)+0.5f)/static_cast<float>(piers)) /
                        bridge_length;
                    const float at_hw = glm::mix(hw_start, hw_end, at_t);
                    bool placed_pier = false;
                    for (float sign : {-1.0f,1.0f}) {
                        const glm::vec2 q = at + side*(sign*at_hw*0.62f);
                        const float bottom = ground.at(q.x,q.y);
                        const float top = d.at(q) - deck_depth;
                        if (top-bottom < 3.0f) continue;
                        bool blocked = false;
                        for (const RoadEdge& lower : graph.edges()) {
                            if (RoadSurface::of(lower,ground).at(q) > top-3.0f) continue;
                            for (std::size_t j=1;j<lower.points.size();++j) {
                                const glm::vec2 a=lower.points[j-1], v=lower.points[j]-a;
                                const float t=std::clamp(glm::dot(q-a,v)/glm::dot(v,v),0.0f,1.0f);
                                if (glm::length(q-a-v*t) < lower.half_width_m()+7.0f) blocked=true;
                            }
                        }
                        if (!blocked) {
                            const glm::vec3 pier_half = municipal
                                ? glm::vec3{1.05f, (top-bottom)*0.5f, 1.45f}
                                : glm::vec3{0.9f, (top-bottom)*0.5f, 1.2f};
                            solid_box(out, {q.x,(top+bottom)*0.5f,q.y},
                                      pier_half, yaw);
                            placed_pier = true;
                        }
                    }
                    if (municipal && placed_pier) {
                        const float underside = d.at(at) - deck_depth;
                        solid_box(out, {at.x, underside - 0.28f, at.y},
                                  {at_hw * 0.72f, 0.28f, 0.72f}, yaw);
                    }
                }
                if (municipal) {
                    // Shallow end diaphragms read as abutment seats from the
                    // creek without filling the channel or touching traffic.
                    for (float along : {1.2f, len - 1.2f}) {
                        const glm::vec2 at = pts[si-1] + tangent * along;
                        const float underside = d.at(at) - deck_depth;
                        solid_box(out, {at.x, underside - 0.30f, at.y},
                                  {hw * 0.94f, 0.30f, 0.65f}, yaw);
                    }
                }
                bridge_run += len;
            }
        }

        if (e.one_way) {
            const float edge = hw - 0.25f;
            for (float side : {-edge, edge})
                bake_solid_marking(out.layer(RoadLayer::WhiteMarking), d,
                                   marking_pts,
                                   side, params.marking_width_m,
                                   kDrapeEpsM + params.marking_lift_m,
                                   params.step_m, w, pmf, pmb);
        }

        if (road_is_paved(e.cls) && e.cls != RoadClass::Alley) {
            RoadMesh& white = out.layer(RoadLayer::WhiteMarking);
            RoadMesh& yellow = out.layer(RoadLayer::YellowMarking);
            const glm::vec4 paint_w = splat_for(Surface::Rock);
            const float lift = kDrapeEpsM + params.marking_lift_m;
            const float paint_step = std::min(params.step_m, 1.5f);
            const int lanes_per_dir = std::max<int>(
                e.lanes_start_per_dir, e.lanes_end_per_dir);
            // A same-count width profile is shoulder/gore space, matching the
            // lane graph. Keep its paint on the narrower live carriageway so
            // the shoulder line does not sweep across an entry/exit ramp and
            // draw a giant X through the gore.
            const bool shoulder_profile =
                e.lanes_start_per_dir == e.lanes_end_per_dir;
            const float paint_hw = std::min(hw_start, hw_end);
            const float paint_hw_start = shoulder_profile ? paint_hw : hw_start;
            const float paint_hw_end = shoulder_profile ? paint_hw : hw_end;

            // Major roads get solid white shoulders. Local streets use the
            // kerb itself as their edge, which is cleaner and less highway-ish.
            if (road_is_major(e.cls)) {
                const float edge_start = std::max(0.0f, paint_hw_start - 0.30f);
                const float edge_end = std::max(0.0f, paint_hw_end - 0.30f);
                bake_solid_marking_profiled(white, d, pts, -edge_start,
                                   -edge_end, params.marking_width_m, lift,
                                   paint_step, paint_w, pmf, pmb);
                bake_solid_marking_profiled(white, d, pts, edge_start,
                                   edge_end, params.marking_width_m, lift,
                                   paint_step, paint_w, pmf, pmb);
            }

            // Same-direction lane boundaries are white and broken. Their
            // offsets come from the same equal half-road shares as LaneGraph.
            const auto divider_at = [](float half_width, int count, int i) {
                count = std::max(1, count);
                if (i < count)
                    return half_width * static_cast<float>(i) /
                           static_cast<float>(count);
                // The extra boundary converges on the old outer lane centre,
                // matching the born/dying lane rather than the parapet edge.
                return half_width *
                       (static_cast<float>(count) - 0.5f) /
                       static_cast<float>(count);
            };
            for (int i = 1; i < lanes_per_dir; ++i) {
                const float divider_start = divider_at(
                    paint_hw_start, std::max<int>(1, e.lanes_start_per_dir), i);
                const float divider_end = divider_at(
                    paint_hw_end, std::max<int>(1, e.lanes_end_per_dir), i);
                bake_dashed_marking_profiled(
                    white, d, pts, -divider_start, -divider_end,
                    params.marking_width_m, lift, params.dash_length_m,
                    params.dash_gap_m, paint_step, paint_w);
                bake_dashed_marking_profiled(
                    white, d, pts, divider_start, divider_end,
                    params.marking_width_m, lift, params.dash_length_m,
                    params.dash_gap_m, paint_step, paint_w);
            }

            if (road_is_major(e.cls)) {
                // Two solid yellows make the opposing-flow boundary obvious
                // on wide roads without pretending the thin plate is a median.
                const float split = params.marking_width_m * 1.15f;
                bake_solid_marking(yellow, d, pts, -split,
                                   params.marking_width_m, lift, paint_step,
                                   paint_w, pmf, pmb);
                bake_solid_marking(yellow, d, pts, split,
                                   params.marking_width_m, lift, paint_step,
                                   paint_w, pmf, pmb);
            } else {
                bake_dashed_marking(yellow, d, pts, 0.0f,
                                    params.marking_width_m, lift,
                                    params.dash_length_m, params.dash_gap_m,
                                    paint_step, paint_w);
            }
        }

        if (!e.sidewalks()) continue;
        // The inner edge overlaps the asphalt by a few centimetres so no strip
        // of terrain shows through at the kerb line.
        const float inner = hw - 0.05f;
        const float outer = hw + kSidewalkWidthM;
        const glm::vec4 cw = splat_for(Surface::Rock);
        RoadMesh& walk = out.layer(RoadLayer::Walk);
        RoadMesh& kerb = out.layer(RoadLayer::Kerb);
        bake_band(walk, d, pts, -outer, -inner, walk_lift, params.walk_tile_m, cw,
                  params.step_m, pmf, pmb, true);
        bake_band(walk, d, pts, inner, outer, walk_lift, params.walk_tile_m, cw,
                  params.step_m, pmf, pmb, true);
        // Four risers per road: each slab's road-facing edge (the visible
        // kerb) and its grass-facing edge, so you can never see under a slab.
        bake_kerb(kerb, d, pts, -inner, walk_lift, params.kerb_foot_m, 1.0f,
                  params.slab_m, params.step_m, pmf, pmb);
        bake_kerb(kerb, d, pts, -outer, walk_lift, params.kerb_foot_m, -1.0f,
                  params.slab_m, params.step_m, pmf, pmb);
        bake_kerb(kerb, d, pts, inner, walk_lift, params.kerb_foot_m, -1.0f,
                  params.slab_m, params.step_m, pmf, pmb);
        bake_kerb(kerb, d, pts, outer, walk_lift, params.kerb_foot_m, 1.0f,
                  params.slab_m, params.step_m, pmf, pmb);
        walk_owners.resize(walk.triangle_count(),ei);
        kerb_owners.resize(kerb.triangle_count(),ei);
    }

    // --- plates, sidewalk corners and crosswalks --------------------------
    for (uint32_t n : plate_nodes) {
        const std::vector<Approach>& a = app[n];
        const glm::vec2 centre = graph.node(n).pos;
        const float trim = plate_trim[n];
        ++out.plates_baked;

        bool all_unpaved = true;
        bool any_walk = false;
        bool all_walk = true;
        bool all_decked = true;
        for (const Approach& ap : a) {
            if (ap.paved) all_unpaved = false;
            if (ap.walk) any_walk = true;
            if (!ap.walk) all_walk = false;
            if (!road_structure_is_decked(graph.edge(ap.edge).structure))
                all_decked = false;
        }

        // A junction in the middle of a bridge is flat at the deck height the
        // author gave it; every other junction drapes, because a plate held
        // flat across a slope buries its uphill corner in the hill.
        RoadSurface flat;
        flat.ground = &ground;
        flat.decked = all_decked;
        flat.deck_y_m = graph.node(n).y_m;

        // Plate: the convex hull of the trimmed carriageway corners, fanned
        // from the node centre.
        std::vector<glm::vec2> corners;
        corners.reserve(a.size() * 2 + 2);
        for (const Approach& ap : a) {
            const glm::vec2 pp = perp(ap.dir);
            const glm::vec2 end = centre + ap.dir * trim;
            corners.push_back(end + pp * ap.hw);
            corners.push_back(end - pp * ap.hw);
        }
        if (a.size() == 2) {
            // A two-road corner's trimmed ends alone hull into a chamfered
            // diamond that cuts across the carriageway. Add the points where
            // the two road EDGES actually meet so the hull squares off.
            const Approach& x = a[0];
            const Approach& y = a[1];
            const float den = x.dir.x * y.dir.y - x.dir.y * y.dir.x;
            if (std::fabs(den) > 1e-4f) {
                glm::vec2 px = perp(x.dir);
                glm::vec2 py = perp(y.dir);
                if (glm::dot(px, y.dir) < 0.0f) px = -px;
                if (glm::dot(py, x.dir) < 0.0f) py = -py;
                auto isect = [&](glm::vec2 p0, glm::vec2 q0) {
                    const glm::vec2 dd = q0 - p0;
                    const float t = (dd.x * y.dir.y - dd.y * y.dir.x) / den;
                    return p0 + x.dir * t;
                };
                // Nearly parallel edges at a width change can intersect far
                // outside this junction. Keep those as a bevel between the
                // trimmed ends: an unlimited miter made the West Ramp plate
                // stretch 500 m and form a raised collision wedge on Route 1.
                const float max_miter_m = 4.0f * trim;
                for (float side : {-1.0f, 1.0f}) {
                    const glm::vec2 p = isect(centre + px * (side * x.hw),
                                              centre + py * (side * y.hw));
                    if (glm::length(p - centre) <= max_miter_m)
                        corners.push_back(p);
                }
            }
        }

        RoadMesh& plate = all_unpaved ? out.layer(RoadLayer::Unpaved)
                                      : out.layer(RoadLayer::Plate);
        const glm::vec4 pw = splat_for(all_unpaved ? Surface::Gravel : Surface::Rock);
        const std::vector<glm::vec2> hull = convex_hull(corners);
        if (hull.size() >= 3) {
            glm::vec2 c{0.0f};
            for (glm::vec2 v : hull) c += v;
            c /= static_cast<float>(hull.size());
            for (std::size_t i = 0; i < hull.size(); ++i) {
                const glm::vec2 p0 = hull[i];
                const glm::vec2 p1 = hull[(i + 1) % hull.size()];
                push_draped_triangle(plate, flat, c, p0, p1, kDrapeEpsM,
                                     params.uv_tile_m, pw, params.step_m);
            }
        }

        // Sidewalk corner fills: without them a junction is a ring of grass
        // between the strips, and the eye reads that as the pavement having
        // holes in it rather than as a missing feature.
        if (any_walk && a.size() >= 2) {
            RoadMesh& walk = out.layer(RoadLayer::Walk);
            RoadMesh& kerb = out.layer(RoadLayer::Kerb);
            const glm::vec4 cw = splat_for(Surface::Rock);
            const std::size_t count = a.size();
            const std::size_t pairs = (count == 2) ? 2 : count;
            for (std::size_t i = 0; i < pairs; ++i) {
                const Approach& x = a[i];
                const Approach& y = a[(i + 1) % count];
                if (!x.walk && !y.walk) continue;
                glm::vec2 px = perp(x.dir);
                glm::vec2 py = perp(y.dir);
                if (glm::dot(px, y.dir) < 0.0f) px = -px;
                if (glm::dot(py, x.dir) < 0.0f) py = -py;
                // Degenerate for a straight-through pair; the strips already
                // meet there and a fill would be a zero-area sliver.
                if (std::fabs(glm::dot(x.dir, y.dir)) > 0.999f) continue;
                if (count == 2 && i == 1) {
                    px = -px;
                    py = -py;
                }
                const glm::vec2 ex = centre + x.dir * trim;
                const glm::vec2 ey = centre + y.dir * trim;
                const glm::vec2 x_in = ex + px * (x.hw - 0.05f);
                const glm::vec2 x_out = ex + px * x.ext;
                const glm::vec2 y_in = ey + py * (y.hw - 0.05f);
                const glm::vec2 y_out = ey + py * y.ext;
                push_quad_up(walk, flat, x_in, x_out, y_out, y_in, walk_lift,
                             params.walk_tile_m, cw);
                // The diagonal quad above bridges the two trimmed strips, but
                // alone it leaves a triangular hole at each true corner. The
                // paving edges meet where their offset lines intersect, not
                // along the straight chord from x to y. Grass showed through
                // those holes even before the patterned paving exposed them.
                const float den = x.dir.x * y.dir.y - x.dir.y * y.dir.x;
                auto intersection = [&](glm::vec2 from, glm::vec2 to) {
                    const glm::vec2 delta = to - from;
                    const float t = (delta.x * y.dir.y - delta.y * y.dir.x) / den;
                    return from + x.dir * t;
                };
                const float max_miter = 4.0f * trim;
                const bool miter_ok = std::fabs(den) > 0.2f;
                const glm::vec2 inner_corner = miter_ok
                    ? intersection(centre + px * (x.hw - 0.05f),
                                   centre + py * (y.hw - 0.05f))
                    : (x_in + y_in) * 0.5f;
                const glm::vec2 outer_corner = miter_ok
                    ? intersection(centre + px * x.ext, centre + py * y.ext)
                    : (x_out + y_out) * 0.5f;
                const bool fill_corner = miter_ok &&
                    glm::length(inner_corner - centre) <= max_miter &&
                    glm::length(outer_corner - centre) <= max_miter;
                if (fill_corner) {
                    const auto add_wedge = [&](glm::vec2 tip, glm::vec2 p0,
                                               glm::vec2 p1) {
                        const glm::vec2 u = p0 - tip, v = p1 - tip;
                        if (std::fabs(u.x * v.y - u.y * v.x) > 0.01f)
                            push_draped_triangle(walk, flat, tip, p0, p1,
                                                 walk_lift, params.walk_tile_m,
                                                 cw, params.step_m);
                    };
                    add_wedge(outer_corner, x_out, y_out);
                    add_wedge(inner_corner, y_in, x_in);
                }
                // Kerb walls follow the same exposed perimeter as the top.
                const glm::vec2 mid = (x_in + x_out + y_in + y_out) * 0.25f;
                const auto add_kerb = [&](glm::vec2 p0, glm::vec2 p1) {
                    if (glm::length(p1 - p0) < 0.01f) return;
                    push_kerb_quad(kerb, flat, p0, p1,
                                   (p0 + p1) * 0.5f - mid, walk_lift,
                                   params.kerb_foot_m, params.slab_m);
                };
                if (fill_corner) {
                    add_kerb(x_out, outer_corner);
                    add_kerb(outer_corner, y_out);
                    add_kerb(y_in, inner_corner);
                    add_kerb(inner_corner, x_in);
                } else {
                    add_kerb(x_out, y_out);
                    add_kerb(y_in, x_in);
                }
            }
        }

        // Zebra bands. Only a real crossing gets them: a two-road bend is not
        // a pedestrian crossing and stripes there read as a mistake.
        if (a.size() < 3 || !all_walk) continue;
        RoadMesh& cross = out.layer(RoadLayer::Crosswalk);
        const float lift = kDrapeEpsM + 0.03f;
        const glm::vec4 xw = splat_for(Surface::Rock);
        for (const Approach& ap : a) {
            const glm::vec2 pp = perp(ap.dir);
            const glm::vec2 base = centre + ap.dir * trim;
            const glm::vec2 far = base + ap.dir * params.crosswalk_depth_m;
            // Individual paint bars, not a pale rectangle with stripes hidden
            // in its texture. This keeps the asphalt between bars continuous
            // with the plate and reads correctly from a moving chase camera.
            constexpr float kBarWidthM = 0.55f;
            constexpr float kBarGapM = 0.55f;
            const float inner_hw = std::max(0.0f, ap.hw - 0.45f);
            for (float across = -inner_hw; across < inner_hw;
                 across += kBarWidthM + kBarGapM) {
                const float next = std::min(across + kBarWidthM, inner_hw);
                const glm::vec2 p0 = base + pp * across;
                const glm::vec2 p1 = base + pp * next;
                const glm::vec2 p2 = far + pp * next;
                const glm::vec2 p3 = far + pp * across;
                push_quad_up(cross, flat, p0, p1, p2, p3, lift,
                             params.crosswalk_tile_m, xw);
            }
            ++out.crosswalks_baked;
        }
    }

    // Compact curb-cut T throats. Short trapezoids replace only the
    // entrance-side sidewalk strip. Their eased-width progression gives the
    // near kerbs a readable return radius without an asphalt plate spreading
    // across the through lanes or the opposite frontage.
    for (const CurbCutTee& tee : curb_cut_tees) {
        ++out.plates_baked;
        RoadSurface flat;
        flat.ground = &ground;
        flat.deck_y_m = graph.node(tee.node).y_m;
        const glm::vec2 centre = graph.node(tee.node).pos;
        const glm::vec2 away = tee.spur.dir;
        const glm::vec2 side = tee.main.dir;
        glm::vec2 spur_side = perp(away);
        if (glm::dot(spur_side, side) < 0.0f) spur_side = -spur_side;
        const float flare = std::min(2.0f, kSidewalkWidthM * 0.67f);
        constexpr int kReturnSections = 6;
        const auto cross_section=[&](float t) {
            const float eased=t*t*(3.0f-2.0f*t);
            return std::pair{
                glm::mix(tee.outer_distance_m,tee.inner_distance_m,t),
                tee.spur.hw+flare*eased};
        };
        const auto cross_direction=[&](float t) {
            const float eased=t*t*(3.0f-2.0f*t);
            return safe_normalize(glm::mix(spur_side,side,eased),side);
        };
        RoadMesh& plate = out.layer(RoadLayer::Plate);
        const glm::vec4 asphalt = splat_for(Surface::Rock);
        for (int i=0;i<kReturnSections;++i) {
            const float t0=static_cast<float>(i)/kReturnSections;
            const float t1=static_cast<float>(i+1)/kReturnSections;
            const auto [d0,h0]=cross_section(t0);
            const auto [d1,h1]=cross_section(t1);
            const glm::vec2 s0=cross_direction(t0);
            const glm::vec2 s1=cross_direction(t1);
            const glm::vec2 a = centre + away * d0;
            const glm::vec2 b = centre + away * d1;
            push_quad_up(plate, flat,
                         a + s0 * h0, a - s0 * h0,
                         b - s1 * h1, b + s1 * h1,
                         kDrapeEpsM, params.uv_tile_m, asphalt);
        }
    }

    detail::clip_walks_from_roads(out,road_owners,walk_owners,kerb_owners,ground);

    // Add the two exposed concrete cheek faces after clipping. If these were
    // present during subtraction, their zero-width XZ projection could be
    // consumed by the throat that they are meant to border.
    for (const CurbCutTee& tee : curb_cut_tees) {
        RoadSurface flat;
        flat.ground = &ground;
        flat.deck_y_m = graph.node(tee.node).y_m;
        const glm::vec2 centre = graph.node(tee.node).pos;
        const glm::vec2 away = tee.spur.dir;
        const glm::vec2 side = tee.main.dir;
        glm::vec2 spur_side = perp(away);
        if (glm::dot(spur_side, side) < 0.0f) spur_side = -spur_side;
        const float flare = std::min(2.0f, kSidewalkWidthM * 0.67f);
        constexpr int kReturnSections = 6;
        const auto cross_section=[&](float t) {
            const float eased=t*t*(3.0f-2.0f*t);
            return std::pair{
                glm::mix(tee.outer_distance_m,tee.inner_distance_m,t),
                tee.spur.hw+flare*eased};
        };
        const auto cross_direction=[&](float t) {
            const float eased=t*t*(3.0f-2.0f*t);
            return safe_normalize(glm::mix(spur_side,side,eased),side);
        };
        RoadMesh& kerb = out.layer(RoadLayer::Kerb);
        for (float sign : {-1.0f, 1.0f}) {
            for (int i=0;i<kReturnSections;++i) {
                const float t0=static_cast<float>(i)/kReturnSections;
                const float t1=static_cast<float>(i+1)/kReturnSections;
                const auto [d0,h0]=cross_section(t0);
                const auto [d1,h1]=cross_section(t1);
                const glm::vec2 p0 = centre + away*d0 +
                                     cross_direction(t0)*(sign*h0);
                const glm::vec2 p1 = centre + away*d1 +
                                     cross_direction(t1)*(sign*h1);
                push_kerb_quad(kerb, flat, p0, p1, -side * sign, walk_lift,
                               params.kerb_foot_m, params.slab_m);
            }
        }
    }
    for (std::size_t i = 0; i < kRoadLayerCount; ++i) {
        if (static_cast<RoadLayer>(i) != RoadLayer::Kerb)
            smooth_normals(out.layers[i]);
        finalise(out.layers[i]);
    }
    return out;
}

RoadCollision build_road_collision(const RibbonBake& bake) {
    auto collidable = [](RoadLayer layer) {
        return layer != RoadLayer::Kerb && layer != RoadLayer::Structure &&
               layer != RoadLayer::Crosswalk &&
               layer != RoadLayer::WhiteMarking &&
               layer != RoadLayer::YellowMarking;
    };
    RoadCollision out;
    out.solids = bake.solids;
    std::size_t reserve = 0;
    for (std::size_t i = 0; i < kRoadLayerCount; ++i)
        if (collidable(static_cast<RoadLayer>(i)))
            reserve += bake.layers[i].triangle_count();
    out.triangles.reserve(reserve);

    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadLayer layer = static_cast<RoadLayer>(li);
        if (!collidable(layer)) continue;  // paint is visual; kerbs are vertical
        const Surface mat =
            layer == RoadLayer::Unpaved ? Surface::Gravel : Surface::Rock;

        const RoadMesh& m = bake.layers[li];
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            RoadCollisionTri t;
            t.geom.a = m.vertices[m.indices[i]].position;
            t.geom.b = m.vertices[m.indices[i + 1]].position;
            t.geom.c = m.vertices[m.indices[i + 2]].position;

            // The FACE normal, from the drawn corners. Not the vertex normals:
            // those are smoothed for shading, and a contact plane tilted to a
            // shading normal is a plane the geometry does not have.
            glm::vec3 fn = glm::cross(t.geom.b - t.geom.a, t.geom.c - t.geom.a);
            const float l = glm::length(fn);
            if (l < 1e-12f) continue;  // degenerate sliver: not a surface
            fn /= l;
            if (fn.y < 0.0f) fn = -fn;
            t.geom.normal = fn;
            t.layer = layer;
            t.material = mat;
            out.triangles.push_back(t);
            out.bounds.expand(t.geom.a);
            out.bounds.expand(t.geom.b);
            out.bounds.expand(t.geom.c);
        }
    }
    return out;
}

}  // namespace apricot

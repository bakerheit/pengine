#include "road/ribbon.h"

#include <algorithm>
#include <cmath>

namespace apricot {

const char* road_layer_name(RoadLayer l) {
    switch (l) {
        case RoadLayer::Carriageway: return "carriageway";
        case RoadLayer::Unpaved: return "unpaved";
        case RoadLayer::Walk: return "walk";
        case RoadLayer::Kerb: return "kerb";
        case RoadLayer::Plate: return "plate";
        case RoadLayer::Crosswalk: return "crosswalk";
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
// `lift` above the surface. UVs are world XZ over `tile`, so every band tiles
// against every other band and against the junction fills, which are also
// world-keyed — mismatched UV frames show up as a visible texture seam exactly
// where a straight sidewalk meets a corner.
//
// `miter_first` / `miter_last`, when set, replace the segment perpendicular at
// the very first / last cross-section with a shared bisector, so two ribbons
// meeting at a bend land on the same line instead of leaving a notch on the
// outside of the corner.
void bake_band(RoadMesh& m, const RoadSurface& d, const std::vector<glm::vec2>& pts,
               float off_a, float off_b, float lift, float tile, glm::vec4 w,
               float step_m, const glm::vec2* miter_first,
               const glm::vec2* miter_last) {
    bool have_prev = false;
    uint32_t prev_a = 0;
    uint32_t prev_b = 0;

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
            const glm::vec2 p = a + delta * t;
            glm::vec2 ncs = n;
            if (miter_first && s == 0 && i == 0) ncs = *miter_first;
            else if (miter_last && s + 2 == pts.size() && i == steps) ncs = *miter_last;

            const glm::vec2 pa = p + ncs * off_a;
            const glm::vec2 pb = p + ncs * off_b;
            const uint32_t ia = push_flat(m, d, pa, lift, pa / tile, w);
            const uint32_t ib = push_flat(m, d, pb, lift, pb / tile, w);
            if (have_prev) {
                push_tri_up(m, prev_a, prev_b, ib);
                push_tri_up(m, prev_a, ib, ia);
            }
            prev_a = ia;
            prev_b = ib;
            have_prev = true;
        }
    }
}

// The vertical face closing one edge of a raised slab. Stepped and mitered
// exactly like bake_band so its top edge coincides with the slab's edge — a
// riser baked on a different subdivision leaves a gap at every bend.
void bake_kerb(RoadMesh& m, const RoadSurface& d, const std::vector<glm::vec2>& pts,
               float off, float top_lift, float foot, float outward_sign,
               float slab_m, float step_m, const glm::vec2* miter_first,
               const glm::vec2* miter_last) {
    const float v_top = (top_lift - foot) / slab_m;
    float run = 0.0f;
    bool have_prev = false;
    uint32_t prev_b = 0;
    uint32_t prev_t = 0;

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
            const glm::vec2 p = a + delta * t;
            glm::vec2 ncs = n;
            if (miter_first && s == 0 && i == 0) ncs = *miter_first;
            else if (miter_last && s + 2 == pts.size() && i == steps) ncs = *miter_last;

            const glm::vec2 pe = p + ncs * off;
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
        const float hw = e.half_width_m();
        const float ext = walk ? hw + kSidewalkWidthM : hw;
        const bool paved = road_is_paved(e.cls);

        Approach front;
        front.edge = ei;
        front.at_front = true;
        front.dir = safe_normalize(e.points[1] - e.points[0]);
        front.hw = hw;
        front.ext = ext;
        front.walk = walk;
        front.paved = paved;
        front.angle = std::atan2(front.dir.y, front.dir.x);
        app[e.node_a].push_back(front);

        Approach back = front;
        back.at_front = false;
        back.dir = safe_normalize(e.points[n - 2] - e.points[n - 1]);
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

    for (uint32_t n = 0; n < ncount; ++n) {
        std::vector<Approach>& a = app[n];
        if (a.size() < 2) continue;  // dead end: the square cap is correct

        float max_hw = 0.0f;
        for (const Approach& ap : a) max_hw = std::max(max_hw, ap.hw);
        float trim = max_hw + params.junction_margin_m;

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
    for (uint32_t ei = 0; ei < ecount; ++ei) {
        const RoadEdge& e = graph.edge(ei);
        const std::vector<glm::vec2> pts =
            trim_polyline(e.points, trim_front[ei], trim_back[ei]);
        if (pts.size() < 2) continue;

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

        const float hw = e.half_width_m();
        const glm::vec4 w = splat_for(road_surface(e.cls));
        RoadMesh& surf = road_is_paved(e.cls) ? out.layer(RoadLayer::Carriageway)
                                              : out.layer(RoadLayer::Unpaved);
        bake_band(surf, d, pts, -hw, hw, kDrapeEpsM, params.uv_tile_m, w,
                  params.step_m, pmf, pmb);

        if (!e.sidewalks()) continue;
        // The inner edge overlaps the asphalt by a few centimetres so no strip
        // of terrain shows through at the kerb line.
        const float inner = hw - 0.05f;
        const float outer = hw + kSidewalkWidthM;
        const glm::vec4 cw = splat_for(Surface::Rock);
        RoadMesh& walk = out.layer(RoadLayer::Walk);
        RoadMesh& kerb = out.layer(RoadLayer::Kerb);
        bake_band(walk, d, pts, -outer, -inner, walk_lift, params.slab_m, cw,
                  params.step_m, pmf, pmb);
        bake_band(walk, d, pts, inner, outer, walk_lift, params.slab_m, cw,
                  params.step_m, pmf, pmb);
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
    }

    // --- plates, sidewalk corners and crosswalks --------------------------
    for (uint32_t n : plate_nodes) {
        const std::vector<Approach>& a = app[n];
        const glm::vec2 centre = graph.node(n).pos;
        const float trim = plate_trim[n];
        ++out.plates_baked;

        bool all_unpaved = true;
        bool any_walk = false;
        bool all_decked = true;
        for (const Approach& ap : a) {
            if (ap.paved) all_unpaved = false;
            if (ap.walk) any_walk = true;
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
                corners.push_back(isect(centre + px * x.hw, centre + py * y.hw));
                corners.push_back(isect(centre - px * x.hw, centre - py * y.hw));
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
            const uint32_t ci =
                push_flat(plate, flat, c, kDrapeEpsM, c / params.uv_tile_m, pw);
            for (std::size_t i = 0; i < hull.size(); ++i) {
                const glm::vec2 p0 = hull[i];
                const glm::vec2 p1 = hull[(i + 1) % hull.size()];
                const uint32_t i0 = push_flat(plate, flat, p0, kDrapeEpsM,
                                              p0 / params.uv_tile_m, pw);
                const uint32_t i1 = push_flat(plate, flat, p1, kDrapeEpsM,
                                              p1 / params.uv_tile_m, pw);
                push_tri_up(plate, ci, i0, i1);
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
                const glm::vec2 x_in = ex + px * x.hw;
                const glm::vec2 x_out = ex + px * x.ext;
                const glm::vec2 y_in = ey + py * y.hw;
                const glm::vec2 y_out = ey + py * y.ext;
                push_quad_up(walk, flat, x_in, x_out, y_out, y_in, walk_lift,
                             params.slab_m, cw);
                // Two exposed edges: the outer (grass) one and the road-facing
                // one. The other two abut the strips arriving here.
                const glm::vec2 mid = (x_in + x_out + y_in + y_out) * 0.25f;
                push_kerb_quad(kerb, flat, x_out, y_out,
                               (x_out + y_out) * 0.5f - mid, walk_lift,
                               params.kerb_foot_m, params.slab_m);
                push_kerb_quad(kerb, flat, y_in, x_in,
                               (x_in + y_in) * 0.5f - mid, walk_lift,
                               params.kerb_foot_m, params.slab_m);
            }
        }

        // Zebra bands. Only a real crossing gets them: a two-road bend is not
        // a pedestrian crossing and stripes there read as a mistake.
        if (a.size() < 3 || !any_walk) continue;
        RoadMesh& cross = out.layer(RoadLayer::Crosswalk);
        const float lift = kDrapeEpsM + 0.03f;
        const glm::vec4 xw = splat_for(Surface::Rock);
        for (const Approach& ap : a) {
            const glm::vec2 pp = perp(ap.dir);
            const glm::vec2 base = centre + ap.dir * trim;
            const glm::vec2 far = base + ap.dir * params.crosswalk_depth_m;
            const float u = (2.0f * ap.hw) / params.crosswalk_tile_m;
            const uint32_t i0 = push_flat(cross, flat, base - pp * ap.hw, lift,
                                          {0.0f, 0.0f}, xw);
            const uint32_t i1 = push_flat(cross, flat, base + pp * ap.hw, lift,
                                          {u, 0.0f}, xw);
            const uint32_t i2 = push_flat(cross, flat, far + pp * ap.hw, lift,
                                          {u, 1.0f}, xw);
            const uint32_t i3 = push_flat(cross, flat, far - pp * ap.hw, lift,
                                          {0.0f, 1.0f}, xw);
            push_tri_up(cross, i0, i1, i2);
            push_tri_up(cross, i0, i2, i3);
            ++out.crosswalks_baked;
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
    RoadCollision out;
    std::size_t reserve = 0;
    for (std::size_t i = 0; i < kRoadLayerCount; ++i)
        if (static_cast<RoadLayer>(i) != RoadLayer::Kerb)
            reserve += bake.layers[i].triangle_count();
    out.triangles.reserve(reserve);

    for (std::size_t li = 0; li < kRoadLayerCount; ++li) {
        const RoadLayer layer = static_cast<RoadLayer>(li);
        if (layer == RoadLayer::Kerb) continue;  // vertical: nothing rests on it
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

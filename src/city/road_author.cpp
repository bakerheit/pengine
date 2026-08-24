#include "city/road_author.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>


namespace apricot {

namespace {

constexpr std::uint32_t kMagic   = 0x47444152u;  // "RADG"
// v2 adds a per-edge `sidewalks` byte. v1 blobs lack it and load with the field
// defaulted ON (see deserialize), so existing maps gain sidewalks transparently.
// v4 marks widths as post-PCG-177: older blobs get the one-time +2 m global
// widen applied at load (see deserialize), v4 blobs are already widened.
constexpr std::uint32_t kVersion = 4u;  // v3 adds per-edge traffic/ped density

// PCG-177 — the founder's "slightly wider roads": every carriageway grew by
// this delta (matching the +2 in ROAD_TYPES and the grid's STREET_WIDTH).
// Applied to pre-v4 blobs only, and gated on the FILE version rather than the
// value, so a load->save->load cycle can't compound it. A pure shift: a
// custom-width road authored with the editor's width slider keeps its
// identity (9 m alley -> 11 m alley), it is NEVER snapped to a canonical type
// width. The .roadgraph on disk is untouched by a load — the widened value is
// only written back if the editor saves (which also stamps v4).
constexpr float kWidenDeltaM = 2.f;

glm::vec2 lerp2(glm::vec2 a, glm::vec2 b, float t) { return a + (b - a) * t; }

// Quadratic Bezier A->B with control C.
glm::vec2 bezier2(glm::vec2 a, glm::vec2 c, glm::vec2 b, float t) {
    float u = 1.f - t;
    return u * u * a + 2.f * u * t * c + t * t * b;
}

float dist2(glm::vec2 a, glm::vec2 b) {
    glm::vec2 d = a - b;
    return d.x * d.x + d.y * d.y;
}

// Clamped projection parameter of p onto segment a->b, in [0,1].
float project_param(glm::vec2 a, glm::vec2 b, glm::vec2 p) {
    glm::vec2 ab = b - a;
    float l2 = ab.x * ab.x + ab.y * ab.y;
    if (l2 < 1e-12f) return 0.f;
    float u = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2;
    return std::clamp(u, 0.f, 1.f);
}

struct Nearest {
    glm::vec2 point{0.f};
    float     dist2 = FLT_MAX;
    float     t     = 0.f;   // arc-length fraction along the polyline [0,1]
};

Nearest nearest_on_polyline(const std::vector<glm::vec2>& poly, glm::vec2 q) {
    Nearest best;
    if (poly.size() < 2) {
        if (!poly.empty()) { best.point = poly[0]; best.dist2 = dist2(poly[0], q); }
        return best;
    }
    float total = 0.f;
    std::vector<float> cum(poly.size(), 0.f);
    for (std::size_t i = 1; i < poly.size(); ++i) {
        total += glm::length(poly[i] - poly[i - 1]);
        cum[i] = total;
    }
    for (std::size_t i = 1; i < poly.size(); ++i) {
        glm::vec2 a = poly[i - 1], b = poly[i];
        float u = project_param(a, b, q);
        glm::vec2 pt = a + (b - a) * u;
        float d2 = dist2(pt, q);
        if (d2 < best.dist2) {
            best.dist2 = d2;
            best.point = pt;
            float along = cum[i - 1] + glm::length(b - a) * u;
            best.t = total > 1e-6f ? along / total : 0.f;
        }
    }
    return best;
}

// Approximate the Bezier parameter t nearest to q by sampling.
float nearest_bezier_t(glm::vec2 a, glm::vec2 c, glm::vec2 b, glm::vec2 q) {
    constexpr int kN = 64;
    float best_t = 0.f, best_d2 = FLT_MAX;
    for (int i = 0; i <= kN; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kN);
        float d2 = dist2(bezier2(a, c, b, t), q);
        if (d2 < best_d2) { best_d2 = d2; best_t = t; }
    }
    return best_t;
}

// nearest_road_type(width) lived here. Its only caller was migrate_roads(),
// which did not come across, so it went with it rather than sitting in the
// tree as a function nobody calls. It is four lines to write back if the
// legacy-format migration ever matters again.

template <class T>
void put(std::string& s, const T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "POD only");
    const char* p = reinterpret_cast<const char*>(&v);
    s.append(p, sizeof(T));
}

template <class T>
bool get(const std::string& s, std::size_t& off, T& v) {
    static_assert(std::is_trivially_copyable<T>::value, "POD only");
    if (off + sizeof(T) > s.size()) return false;
    std::memcpy(&v, s.data() + off, sizeof(T));
    off += sizeof(T);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Topology authoring
// ---------------------------------------------------------------------------

RoadNodeId RoadGraphAuthor::node_at(glm::vec2 pos, float merge_radius) {
    RoadNodeId hit = pick_node(pos, merge_radius);
    if (hit != kInvalidRoadId) return hit;

    glm::vec2 proj{0.f};
    float     t = 0.f;
    RoadEdgeId e = pick_edge(pos, merge_radius, proj, t);
    if (e != kInvalidRoadId) return split_edge_at(e, proj);

    RoadNodeId id = static_cast<RoadNodeId>(nodes_.size());
    nodes_.push_back(Node{pos, {}});
    dirty_ = true;
    return id;
}

RoadEdgeId RoadGraphAuthor::add_edge(RoadNodeId a, RoadNodeId b, RoadType type,
                                     float width, Shape shape,
                                     std::vector<glm::vec2> controls,
                                     bool sidewalks) {
    if (a == b || a >= nodes_.size() || b >= nodes_.size()) return kInvalidRoadId;
    Edge e;
    e.a = a;
    e.b = b;
    e.type = type;
    e.width = width;
    e.shape = shape;
    e.sidewalks = sidewalks;
    e.controls = std::move(controls);
    RoadEdgeId id = static_cast<RoadEdgeId>(edges_.size());
    edges_.push_back(std::move(e));
    nodes_[a].edges.push_back(id);
    nodes_[b].edges.push_back(id);
    dirty_ = true;
    return id;
}

void RoadGraphAuthor::set_edge_type(RoadEdgeId e, RoadType type, float width) {
    if (e >= edges_.size() || !edge_alive(e)) return;
    edges_[e].type = type;
    edges_[e].width = width;
    dirty_ = true;
}

void RoadGraphAuthor::set_edge_density(RoadEdgeId e, float traffic_density,
                                       float ped_density) {
    if (e >= edges_.size() || !edge_alive(e)) return;
    edges_[e].traffic_density = traffic_density;
    edges_[e].ped_density     = ped_density;
    dirty_ = true;
}

void RoadGraphAuthor::remove_edge(RoadEdgeId e) {
    if (e >= edges_.size() || !edge_alive(e)) return;
    edges_[e].a = kInvalidRoadId;
    edges_[e].b = kInvalidRoadId;
    compact();
    dirty_ = true;
}

void RoadGraphAuthor::remove_node(RoadNodeId n) {
    if (n >= nodes_.size()) return;
    for (RoadEdgeId e : nodes_[n].edges) {
        if (e < edges_.size()) {
            edges_[e].a = kInvalidRoadId;
            edges_[e].b = kInvalidRoadId;
        }
    }
    compact();
    dirty_ = true;
}

void RoadGraphAuthor::remove_last_edge() {
    for (std::size_t i = edges_.size(); i-- > 0;) {
        RoadEdgeId e = static_cast<RoadEdgeId>(i);
        if (edge_alive(e)) {
            edges_[e].a = kInvalidRoadId;
            edges_[e].b = kInvalidRoadId;
            compact();
            dirty_ = true;
            return;
        }
    }
}

void RoadGraphAuthor::clear() {
    nodes_.clear();
    edges_.clear();
    dirty_ = true;
}

RoadNodeId RoadGraphAuthor::split_edge_at(RoadEdgeId e, glm::vec2 pos) {
    Edge old = edges_[e];  // copy before mutating the slot
    glm::vec2 A = nodes_[old.a].pos;
    glm::vec2 B = nodes_[old.b].pos;

    glm::vec2              M{0.f};
    std::vector<glm::vec2> c1, c2;
    Shape                  sh1 = old.shape, sh2 = old.shape;

    if (old.shape == Shape::Bezier && !old.controls.empty()) {
        glm::vec2 C = old.controls[0];
        float t = nearest_bezier_t(A, C, B, pos);
        M = bezier2(A, C, B, t);
        c1 = {lerp2(A, C, t)};
        c2 = {lerp2(C, B, t)};
    } else if (old.shape == Shape::Freeform && !old.controls.empty()) {
        std::vector<glm::vec2> poly;
        poly.push_back(A);
        for (glm::vec2 c : old.controls) poly.push_back(c);
        poly.push_back(B);
        // Split at the nearest interior vertex so each half stays a polyline.
        int vi = -1;
        float best = FLT_MAX;
        for (int i = 1; i + 1 < static_cast<int>(poly.size()); ++i) {
            float d2 = dist2(poly[static_cast<std::size_t>(i)], pos);
            if (d2 < best) { best = d2; vi = i; }
        }
        if (vi < 0) {
            float u = project_param(A, B, pos);
            M = A + (B - A) * u;
        } else {
            M = poly[static_cast<std::size_t>(vi)];
            for (int i = 1; i < vi; ++i) c1.push_back(poly[static_cast<std::size_t>(i)]);
            for (int i = vi + 1; i + 1 < static_cast<int>(poly.size()); ++i)
                c2.push_back(poly[static_cast<std::size_t>(i)]);
        }
    } else {
        float u = project_param(A, B, pos);
        M = A + (B - A) * u;
        sh1 = sh2 = Shape::Straight;
    }

    RoadNodeId mid = static_cast<RoadNodeId>(nodes_.size());
    nodes_.push_back(Node{M, {}});

    // First half reuses slot e (old.a -> mid); second half is a fresh edge.
    edges_[e].b = mid;
    edges_[e].shape = sh1;
    edges_[e].controls = std::move(c1);

    Edge ne;
    ne.a = mid;
    ne.b = old.b;
    ne.type = old.type;
    ne.width = old.width;
    ne.shape = sh2;
    ne.sidewalks = old.sidewalks;
    ne.traffic_density = old.traffic_density;  // both halves inherit the density
    ne.ped_density     = old.ped_density;
    ne.controls = std::move(c2);
    edges_.push_back(std::move(ne));

    rebuild_adjacency();
    dirty_ = true;
    return mid;
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

RoadNodeId RoadGraphAuthor::pick_node(glm::vec2 pos, float radius) const {
    RoadNodeId best = kInvalidRoadId;
    float best_d2 = radius * radius;
    for (RoadNodeId n = 0; n < nodes_.size(); ++n) {
        if (nodes_[n].edges.empty()) continue;  // skip transient/orphan nodes
        float d2 = dist2(nodes_[n].pos, pos);
        if (d2 <= best_d2) { best_d2 = d2; best = n; }
    }
    return best;
}

RoadEdgeId RoadGraphAuthor::pick_edge(glm::vec2 pos, float radius,
                                      glm::vec2& out_proj, float& out_t) const {
    RoadEdgeId best = kInvalidRoadId;
    float best_d2 = radius * radius;
    for (RoadEdgeId e = 0; e < edges_.size(); ++e) {
        if (!edge_alive(e)) continue;
        std::vector<glm::vec2> poly = tessellate(e, 4.f);
        Nearest nr = nearest_on_polyline(poly, pos);
        if (nr.dist2 <= best_d2) {
            best_d2 = nr.dist2;
            best = e;
            out_proj = nr.point;
            out_t = nr.t;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

std::vector<glm::vec2> RoadGraphAuthor::tessellate(RoadEdgeId e, float step_m) const {
    const Edge& ed = edges_[e];
    glm::vec2 A = nodes_[ed.a].pos;
    glm::vec2 B = nodes_[ed.b].pos;
    std::vector<glm::vec2> out;

    if (ed.shape == Shape::Bezier && !ed.controls.empty()) {
        glm::vec2 C = ed.controls[0];
        float approx = 0.5f * glm::length(B - A) +
                       0.5f * (glm::length(C - A) + glm::length(B - C));
        int segs = std::clamp(static_cast<int>(std::ceil(approx / std::max(step_m, 0.5f))),
                              2, 256);
        out.push_back(A);
        for (int i = 1; i <= segs; ++i)
            out.push_back(bezier2(A, C, B, static_cast<float>(i) / static_cast<float>(segs)));
    } else if (ed.shape == Shape::Freeform && !ed.controls.empty()) {
        out.push_back(A);
        for (glm::vec2 c : ed.controls) out.push_back(c);
        out.push_back(B);
    } else {
        out = {A, B};
    }
    return out;
}

std::vector<RoadGraphAuthor::Polyline> RoadGraphAuthor::to_polylines(float step_m) const {
    std::vector<Polyline> out;
    for (RoadEdgeId e = 0; e < edges_.size(); ++e) {
        if (!edge_alive(e)) continue;
        std::vector<glm::vec2> pts = tessellate(e, step_m);
        if (pts.size() < 2) continue;
        float len = 0.f;
        for (std::size_t i = 1; i < pts.size(); ++i) len += glm::length(pts[i] - pts[i - 1]);
        if (len < 0.01f) continue;  // drop degenerate edges before consumers see them
        out.push_back(Polyline{std::move(pts), edges_[e].width, edges_[e].type,
                               edges_[e].sidewalks, edges_[e].traffic_density,
                               edges_[e].ped_density});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Compaction / adjacency
// ---------------------------------------------------------------------------

void RoadGraphAuthor::rebuild_adjacency() {
    for (Node& n : nodes_) n.edges.clear();
    for (RoadEdgeId e = 0; e < edges_.size(); ++e) {
        if (!edge_alive(e)) continue;
        const Edge& ed = edges_[e];
        if (ed.a < nodes_.size()) nodes_[ed.a].edges.push_back(e);
        if (ed.b < nodes_.size()) nodes_[ed.b].edges.push_back(e);
    }
}

void RoadGraphAuthor::build_compacted(std::vector<Node>& out_nodes,
                                      std::vector<Edge>& out_edges) const {
    out_nodes.clear();
    out_edges.clear();
    std::vector<char> referenced(nodes_.size(), 0);
    for (RoadEdgeId e = 0; e < edges_.size(); ++e) {
        if (!edge_alive(e)) continue;
        referenced[edges_[e].a] = 1;
        referenced[edges_[e].b] = 1;
    }
    std::vector<RoadNodeId> remap(nodes_.size(), kInvalidRoadId);
    for (RoadNodeId n = 0; n < nodes_.size(); ++n) {
        if (!referenced[n]) continue;
        remap[n] = static_cast<RoadNodeId>(out_nodes.size());
        out_nodes.push_back(Node{nodes_[n].pos, {}});
    }
    for (RoadEdgeId e = 0; e < edges_.size(); ++e) {
        if (!edge_alive(e)) continue;
        Edge ne = edges_[e];
        ne.a = remap[ne.a];
        ne.b = remap[ne.b];
        out_edges.push_back(std::move(ne));
    }
    for (RoadEdgeId e = 0; e < out_edges.size(); ++e) {
        out_nodes[out_edges[e].a].edges.push_back(e);
        out_nodes[out_edges[e].b].edges.push_back(e);
    }
}

void RoadGraphAuthor::compact() {
    std::vector<Node> nn;
    std::vector<Edge> ne;
    build_compacted(nn, ne);
    nodes_ = std::move(nn);
    edges_ = std::move(ne);
}

std::size_t RoadGraphAuthor::live_edge_count() const {
    std::size_t c = 0;
    for (RoadEdgeId e = 0; e < edges_.size(); ++e)
        if (edge_alive(e)) ++c;
    return c;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

std::string RoadGraphAuthor::serialize() const {
    std::vector<Node> nn;
    std::vector<Edge> ne;
    build_compacted(nn, ne);

    std::string s;
    put(s, kMagic);
    put(s, kVersion);
    put(s, static_cast<std::uint32_t>(nn.size()));
    for (const Node& n : nn) {
        put(s, n.pos.x);
        put(s, n.pos.y);
    }
    put(s, static_cast<std::uint32_t>(ne.size()));
    for (const Edge& e : ne) {
        put(s, e.a);
        put(s, e.b);
        put(s, static_cast<std::uint8_t>(e.type));
        put(s, static_cast<std::uint8_t>(e.shape));
        put(s, static_cast<std::uint8_t>(e.sidewalks ? 1 : 0));
        put(s, e.width);
        put(s, e.traffic_density);   // v3
        put(s, e.ped_density);       // v3
        put(s, static_cast<std::uint32_t>(e.controls.size()));
        for (glm::vec2 c : e.controls) {
            put(s, c.x);
            put(s, c.y);
        }
    }
    return s;
}

bool RoadGraphAuthor::deserialize(const std::string& blob) {
    std::size_t off = 0;
    std::uint32_t magic = 0, ver = 0;
    if (!get(blob, off, magic) || magic != kMagic) return false;
    if (!get(blob, off, ver) || ver < 1u || ver > kVersion) return false;
    const bool has_sidewalk_byte = ver >= 2u;
    const bool has_density       = ver >= 3u;
    const bool pre_widen         = ver <  4u;   // PCG-177 +2 m not yet applied

    nodes_.clear();
    edges_.clear();

    std::uint32_t nn = 0;
    if (!get(blob, off, nn)) return false;
    nodes_.resize(nn);
    for (std::uint32_t i = 0; i < nn; ++i) {
        float x = 0.f, y = 0.f;
        if (!get(blob, off, x) || !get(blob, off, y)) return false;
        nodes_[i].pos = {x, y};
    }

    std::uint32_t ec = 0;
    if (!get(blob, off, ec)) return false;
    edges_.reserve(ec);
    for (std::uint32_t i = 0; i < ec; ++i) {
        std::uint32_t a = 0, b = 0, nctrl = 0;
        std::uint8_t type = 0, shape = 0, sidewalks = 1;
        float width = 12.f, traffic_density = 1.f, ped_density = 1.f;
        if (!get(blob, off, a) || !get(blob, off, b) || !get(blob, off, type) ||
            !get(blob, off, shape))
            return false;
        if (has_sidewalk_byte && !get(blob, off, sidewalks)) return false;
        if (!get(blob, off, width)) return false;
        if (has_density &&
            (!get(blob, off, traffic_density) || !get(blob, off, ped_density)))
            return false;
        if (!get(blob, off, nctrl)) return false;
        if (a >= nn || b >= nn || nctrl > 1000000u) return false;
        if (type >= static_cast<std::uint8_t>(RoadType::Count)) type = 0;
        if (shape > static_cast<std::uint8_t>(Shape::Freeform)) shape = 0;
        Edge e;
        e.a = a;
        e.b = b;
        e.type = static_cast<RoadType>(type);
        e.shape = static_cast<Shape>(shape);
        e.sidewalks = sidewalks != 0;
        // PCG-177: pre-v4 widths get the one-time +2 m global widen. Pure
        // delta — custom widths keep their identity, no type-canonical snap.
        e.width = pre_widen ? width + kWidenDeltaM : width;
        e.traffic_density = traffic_density;
        e.ped_density     = ped_density;
        e.controls.resize(nctrl);
        for (std::uint32_t k = 0; k < nctrl; ++k) {
            float cx = 0.f, cy = 0.f;
            if (!get(blob, off, cx) || !get(blob, off, cy)) return false;
            e.controls[k] = {cx, cy};
        }
        edges_.push_back(std::move(e));
    }
    rebuild_adjacency();
    return true;
}

// ---------------------------------------------------------------------------
// NOT LIFTED (PENG-29): load(), save() and migrate_roads().
//
// They were the .roadgraph file layer: fopen/fwrite against a path the editor
// held, plus a one-time migration of a legacy .roads format. They stayed behind
// for a specific reason rather than a general one.
//
// probablecause's CI carries an EXCLUDE list, and its entries are excluded
// because they WRITE INTO LIVE WORLD DATA. tools/ci.sh has no such list and
// docs/architecture.md says it must never grow one — which makes "the editor
// writes to a temp directory by construction, not by convention" a design
// requirement of whatever file layer replaces these, not a follow-up.
//
// What survives is the part that has no opinion about disk: serialize() and
// deserialize() move the whole graph through an in-memory blob. A file layer
// is fifteen lines on top of them, written once, by somebody who has decided
// where it is allowed to write.
// ---------------------------------------------------------------------------

}  // namespace apricot

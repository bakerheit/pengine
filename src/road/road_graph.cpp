#include "road/road_graph.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "terrain/chunk.h"

namespace apricot {

float TerrainGround::sample(const void* ctx, float x, float z) {
    // mesh_height_at, NOT height_at. The first is the height of the DRAWN
    // triangle; the second is the continuous field underneath it, and they
    // differ by centimetres on a grade. A ribbon draped on the field while the
    // terrain draws the mesh is a road that sinks into the hill it is visibly
    // lying on, and it is invisible until somebody drives there. Same rule as
    // TerrainCollider: what you touch is what you see.
    return mesh_height_at(static_cast<const TerrainGround*>(ctx)->seed, x, z);
}

namespace {

constexpr float kDegenerateSegM = 1e-3f;

float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

int64_t cell_key(int32_t cx, int32_t cz) {
    return (static_cast<int64_t>(cx) << 32) ^
           static_cast<int64_t>(static_cast<uint32_t>(cz));
}

int32_t floor_div(float v, float cell) {
    return static_cast<int32_t>(std::floor(v / cell));
}

// Positions welded to a tolerance, first-come-wins.
//
// Deterministic because insertion order is spine order then vertex order, and
// nothing here ever iterates the bucket map to produce an output — it is a
// lookup structure only. (A std::unordered_map would be just as correct here
// for that reason; std::map is used because "no hash-order in the build" is
// cheaper to keep true than to keep checking.)
class WeldGrid {
public:
    explicit WeldGrid(float tolerance_m)
        : tol_(tolerance_m), tol2_(tolerance_m * tolerance_m) {}

    uint32_t weld(glm::vec2 p) {
        const int32_t cx = floor_div(p.x, tol_);
        const int32_t cz = floor_div(p.y, tol_);
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                auto it = buckets_.find(cell_key(cx + dx, cz + dz));
                if (it == buckets_.end()) continue;
                for (uint32_t id : it->second) {
                    const glm::vec2 d = pos_[id] - p;
                    if (glm::dot(d, d) <= tol2_) return id;
                }
            }
        }
        const uint32_t id = static_cast<uint32_t>(pos_.size());
        pos_.push_back(p);
        buckets_[cell_key(cx, cz)].push_back(id);
        return id;
    }

    glm::vec2 position(uint32_t id) const { return pos_[id]; }
    std::size_t size() const { return pos_.size(); }

private:
    float tol_;
    float tol2_;
    std::vector<glm::vec2> pos_;
    std::map<int64_t, std::vector<uint32_t>> buckets_;
};

struct Seg {
    uint32_t spine = 0;   // index into the input vector
    uint32_t index = 0;   // segment index within that spine
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
};

// Broadphase: bucket segments by the cells their AABB covers, so the crossing
// pass looks at neighbours instead of at everything. The pairwise test itself
// is still exact.
//
// Cell size is a compromise and it is stated rather than tuned: 64 m matches a
// terrain chunk, which is the scale district streets are laid out at. A single
// long span (the 900 m causeway) lands in ~15 cells, which is fine; a map made
// entirely of 3 km straights would degenerate back toward O(n^2) and that is a
// measurement to take when such a map exists, not a knob to add now.
constexpr float kBroadphaseCellM = 64.0f;

void collect_crossings(const std::vector<Seg>& segs, float tol,
                       std::vector<std::vector<float>>& splits) {
    std::map<int64_t, std::vector<uint32_t>> grid;
    for (uint32_t i = 0; i < segs.size(); ++i) {
        const Seg& s = segs[i];
        const int32_t x0 = floor_div(std::min(s.a.x, s.b.x) - tol, kBroadphaseCellM);
        const int32_t x1 = floor_div(std::max(s.a.x, s.b.x) + tol, kBroadphaseCellM);
        const int32_t z0 = floor_div(std::min(s.a.y, s.b.y) - tol, kBroadphaseCellM);
        const int32_t z1 = floor_div(std::max(s.a.y, s.b.y) + tol, kBroadphaseCellM);
        for (int32_t cz = z0; cz <= z1; ++cz)
            for (int32_t cx = x0; cx <= x1; ++cx)
                grid[cell_key(cx, cz)].push_back(i);
    }

    // A split parameter is only worth recording when it is at least `tol` away
    // from both ends of the segment; anything closer welds onto the existing
    // endpoint anyway, and recording it would make a zero-length stub edge.
    auto record = [&](uint32_t seg, float t) {
        const Seg& s = segs[seg];
        const float len = glm::length(s.b - s.a);
        if (len < kDegenerateSegM) return;
        const float margin = tol / len;
        if (t <= margin || t >= 1.0f - margin) return;
        splits[seg].push_back(t);
    };

    std::vector<uint32_t> cand;
    for (uint32_t i = 0; i < segs.size(); ++i) {
        const Seg& si = segs[i];
        const glm::vec2 r = si.b - si.a;
        if (glm::length(r) < kDegenerateSegM) continue;

        cand.clear();
        const int32_t x0 = floor_div(std::min(si.a.x, si.b.x) - tol, kBroadphaseCellM);
        const int32_t x1 = floor_div(std::max(si.a.x, si.b.x) + tol, kBroadphaseCellM);
        const int32_t z0 = floor_div(std::min(si.a.y, si.b.y) - tol, kBroadphaseCellM);
        const int32_t z1 = floor_div(std::max(si.a.y, si.b.y) + tol, kBroadphaseCellM);
        for (int32_t cz = z0; cz <= z1; ++cz) {
            for (int32_t cx = x0; cx <= x1; ++cx) {
                auto it = grid.find(cell_key(cx, cz));
                if (it == grid.end()) continue;
                for (uint32_t j : it->second)
                    if (j > i) cand.push_back(j);
            }
        }
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());

        for (uint32_t j : cand) {
            const Seg& sj = segs[j];
            // Consecutive segments of one spine already share an endpoint.
            if (si.spine == sj.spine &&
                (sj.index == si.index + 1 || si.index == sj.index + 1))
                continue;

            const glm::vec2 s = sj.b - sj.a;
            if (glm::length(s) < kDegenerateSegM) continue;

            const glm::vec2 qp = sj.a - si.a;
            const float denom = cross2(r, s);
            if (std::fabs(denom) > 1e-6f) {
                const float t = cross2(qp, s) / denom;
                const float u = cross2(qp, r) / denom;
                if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
                    record(i, t);
                    record(j, u);
                    continue;
                }
            }

            // Not a proper crossing. The other case that still makes a
            // junction is an endpoint LANDING on the interior of the other
            // segment — a street running into an arterial. Without this a T
            // junction is two roads that overlap without connecting, and
            // traffic drives through itself at full speed.
            auto foot = [&](glm::vec2 p, const Seg& on, uint32_t on_idx) {
                const glm::vec2 d = on.b - on.a;
                const float l2 = glm::dot(d, d);
                if (l2 < kDegenerateSegM) return;
                const float t = std::clamp(glm::dot(p - on.a, d) / l2, 0.0f, 1.0f);
                const glm::vec2 c = on.a + d * t;
                if (glm::length(c - p) <= tol) record(on_idx, t);
            };
            foot(si.a, sj, j);
            foot(si.b, sj, j);
            foot(sj.a, si, i);
            foot(sj.b, si, i);
        }
    }
}

}  // namespace

void RoadGraph::clear() {
    nodes_.clear();
    edges_.clear();
    junctions_.clear();
    bounds_ = AABB{};
}

void RoadGraph::build(const std::vector<RoadSpine>& spines,
                      const RoadGraphParams& params,
                      const GroundSampler& ground) {
    clear();
    const float tol = std::max(params.weld_tolerance_m, 1e-3f);

    // --- 1. flatten every spine into segments ------------------------------
    std::vector<Seg> segs;
    std::vector<std::vector<uint32_t>> spine_segs(spines.size());
    for (uint32_t s = 0; s < spines.size(); ++s) {
        const RoadSpine& sp = spines[s];
        for (std::size_t k = 0; k + 1 < sp.points.size(); ++k) {
            if (glm::length(sp.points[k + 1] - sp.points[k]) < kDegenerateSegM)
                continue;
            spine_segs[s].push_back(static_cast<uint32_t>(segs.size()));
            segs.push_back(Seg{s, static_cast<uint32_t>(k), sp.points[k],
                               sp.points[k + 1]});
        }
    }
    if (segs.empty()) return;

    // --- 2. where do spines meet? ------------------------------------------
    std::vector<std::vector<float>> splits(segs.size());
    if (params.split_crossings) collect_crossings(segs, tol, splits);
    for (auto& v : splits) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
                            [](float a, float b) { return std::fabs(a - b) < 1e-5f; }),
                v.end());
    }

    // --- 3. augmented polyline per spine, welded to shared vertices ---------
    WeldGrid weld(tol);
    std::vector<std::vector<uint32_t>> vert(spines.size());  // welded ids
    std::vector<std::vector<glm::vec2>> vpos(spines.size());
    for (uint32_t s = 0; s < spines.size(); ++s) {
        if (spine_segs[s].empty()) continue;
        bool first = true;
        for (uint32_t si : spine_segs[s]) {
            const Seg& sg = segs[si];
            if (first) {
                vert[s].push_back(weld.weld(sg.a));
                vpos[s].push_back(sg.a);
                first = false;
            }
            for (float t : splits[si]) {
                const glm::vec2 p = sg.a + (sg.b - sg.a) * t;
                vert[s].push_back(weld.weld(p));
                vpos[s].push_back(p);
            }
            vert[s].push_back(weld.weld(sg.b));
            vpos[s].push_back(sg.b);
        }
        // A split that welded straight back onto its neighbour contributes
        // nothing but a zero-length edge later. Drop the duplicate here, where
        // it is one comparison, rather than downstream where it is a lane of
        // length zero that pose() has to survive.
        std::vector<uint32_t> cv;
        std::vector<glm::vec2> cp;
        for (std::size_t k = 0; k < vert[s].size(); ++k) {
            if (!cv.empty() && cv.back() == vert[s][k]) continue;
            cv.push_back(vert[s][k]);
            cp.push_back(vpos[s][k]);
        }
        vert[s].swap(cv);
        vpos[s].swap(cp);
    }

    // --- 4. which welded vertices are edge boundaries? ---------------------
    const std::size_t nweld = weld.size();
    std::vector<uint32_t> ends(nweld, 0);       // segment ends landing here
    std::vector<uint32_t> owner(nweld, 0xFFFFFFFFu);
    std::vector<uint8_t> multi(nweld, 0);       // touched by >1 spine
    std::vector<uint8_t> terminal(nweld, 0);    // a spine starts or ends here
    for (uint32_t s = 0; s < spines.size(); ++s) {
        const std::size_t n = vert[s].size();
        if (n < 2) continue;
        for (std::size_t k = 0; k < n; ++k) {
            const uint32_t v = vert[s][k];
            ends[v] += (k == 0 || k + 1 == n) ? 1u : 2u;
            if (owner[v] == 0xFFFFFFFFu) owner[v] = s;
            else if (owner[v] != s) multi[v] = 1;
        }
        terminal[vert[s].front()] = 1;
        terminal[vert[s].back()] = 1;
    }

    // A vertex breaks the polyline into a new edge when it is anything other
    // than a plain shape point of one spine: a crossing, a dead end, a meeting
    // of two spines (which may have different classes), or a spine's own end
    // — that last one is what stops a closed loop from walking forever.
    std::vector<uint8_t> is_break(nweld, 0);
    for (std::size_t v = 0; v < nweld; ++v)
        is_break[v] = (ends[v] != 2u || multi[v] || terminal[v]) ? 1u : 0u;

    // --- 5. walk each spine, emitting edges --------------------------------
    std::vector<uint32_t> node_of(nweld, 0xFFFFFFFFu);
    auto node_for = [&](uint32_t v) {
        if (node_of[v] != 0xFFFFFFFFu) return node_of[v];
        const uint32_t idx = static_cast<uint32_t>(nodes_.size());
        RoadNode n;
        n.pos = weld.position(v);
        nodes_.push_back(std::move(n));
        node_of[v] = idx;
        return idx;
    };

    for (uint32_t s = 0; s < spines.size(); ++s) {
        const RoadSpine& sp = spines[s];
        const std::size_t n = vert[s].size();
        if (n < 2) continue;

        const float width = sp.width_m > 0.0f
                                ? sp.width_m
                                : road_class_def(sp.cls).carriageway_width_m;
        uint32_t run = 0;
        std::size_t start = 0;
        for (std::size_t k = 1; k < n; ++k) {
            if (!is_break[vert[s][k]] && k + 1 != n) continue;

            RoadEdge e;
            e.node_a = node_for(vert[s][start]);
            e.node_b = node_for(vert[s][k]);
            e.cls = sp.cls;
            e.structure = sp.structure;
            e.deck_y_m = sp.deck_y_m;
            e.width_m = width;
            e.block_quality = sp.block_quality;
            e.traffic_density = sp.traffic_density;
            e.ped_density = sp.ped_density;
            e.spine_id = sp.id;
            e.spine_run = run++;

            e.points.reserve(k - start + 1);
            for (std::size_t q = start; q <= k; ++q) e.points.push_back(vpos[s][q]);
            // Endpoints sit EXACTLY on their nodes. Two edges meeting at a
            // junction must agree on the meeting point to the bit, or the
            // ribbon bake leaves a hairline of terrain showing through the
            // seam and the lane graph's turn links start from the wrong place.
            e.points.front() = nodes_[e.node_a].pos;
            e.points.back() = nodes_[e.node_b].pos;
            for (std::size_t q = 1; q < e.points.size(); ++q)
                e.length_m += glm::length(e.points[q] - e.points[q - 1]);

            if (e.length_m >= kDegenerateSegM) {
                const uint32_t ei = static_cast<uint32_t>(edges_.size());
                nodes_[e.node_a].edges.push_back(ei);
                nodes_[e.node_b].edges.push_back(ei);
                edges_.push_back(std::move(e));
            }
            start = k;
        }
    }

    // --- 6. node kinds, heights and the junction list ----------------------
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        RoadNode& n = nodes_[i];
        const std::size_t deg = n.edges.size();
        n.kind = deg >= 3 ? NodeKind::Junction
                          : (deg == 2 ? NodeKind::Continuation : NodeKind::DeadEnd);

        // Ground height, unless EVERY incident road is on a deck — a node in
        // the middle of a bridge belongs to the bridge. A bridgehead, where a
        // decked edge meets a ground one, stays on the ground: that is where
        // the deck is supposed to come back down, and if it does not, the
        // authored deck height is wrong and should look wrong.
        bool all_decked = !n.edges.empty();
        float deck = 0.0f;
        for (uint32_t ei : n.edges) {
            if (!road_structure_is_decked(edges_[ei].structure)) {
                all_decked = false;
                break;
            }
            deck = edges_[ei].deck_y_m;
        }
        n.y_m = all_decked ? deck : ground.at(n.pos.x, n.pos.y);

        if (n.kind == NodeKind::Junction) junctions_.push_back(i);
    }

    for (const RoadEdge& e : edges_) {
        const bool decked = road_structure_is_decked(e.structure);
        for (glm::vec2 p : e.points) {
            const float y = decked ? e.deck_y_m : ground.at(p.x, p.y);
            bounds_.expand(glm::vec3{p.x, y, p.y});
        }
    }
}

float RoadGraph::junction_trim_m(uint32_t n, float margin_m) const {
    float widest = 0.0f;
    for (uint32_t ei : nodes_[n].edges)
        widest = std::max(widest, edges_[ei].half_width_m());
    return widest + margin_m;
}

RoadClass RoadGraph::dominant_class(uint32_t n) const {
    RoadClass best = RoadClass::Dirt;
    bool any = false;
    for (uint32_t ei : nodes_[n].edges) {
        const RoadClass c = edges_[ei].cls;
        // The enum is ordered widest-first, so "lowest index wins" IS
        // "highest class wins" and stays true when a class is appended.
        if (!any || road_class_index(c) < road_class_index(best)) {
            best = c;
            any = true;
        }
    }
    return best;
}

}  // namespace apricot

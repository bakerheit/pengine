#include "road/lane_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

#include "core/rng.h"

namespace apricot {

const std::vector<TurnLink> LaneGraph::kNoLinks{};

const char* turn_kind_name(TurnKind k) {
    switch (k) {
        case TurnKind::Straight: return "straight";
        case TurnKind::Left: return "left";
        case TurnKind::Right: return "right";
        case TurnKind::UTurn: return "u-turn";
    }
    return "?";
}

const char* junction_control_name(JunctionControl c) {
    switch (c) {
        case JunctionControl::None: return "none";
        case JunctionControl::Signal: return "signal";
        case JunctionControl::Stop: return "stop";
    }
    return "?";
}

namespace {

constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};

glm::vec2 safe_normalize(glm::vec2 v, glm::vec2 fallback = {1.0f, 0.0f}) {
    const float l = glm::length(v);
    return l > 1e-6f ? v / l : fallback;
}

// The lane's right vector, in XZ. This is cross(up, tangent) with the Y
// component dropped, and it is written out rather than called through glm so
// that pose(), project_onto() and the lane offsets provably share ONE sign
// convention. When they do not, "lateral" means one thing to the follower and
// the opposite to the overtake logic, and cars steer into oncoming to avoid
// oncoming.
glm::vec2 lane_right(glm::vec2 tangent) {
    return glm::vec2{tangent.y, -tangent.x};
}

// Per-vertex right-offset of an XZ polyline, lifted onto the drawn road
// surface. Interior tangents use a central difference so the offset line does
// not kink at every shape point.
std::vector<glm::vec3> offset_polyline(const std::vector<glm::vec2>& pts,
                                       float offset, const RoadSurface& surf) {
    std::vector<glm::vec3> out;
    out.reserve(pts.size());
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec2 t;
        if (i == 0) t = pts[1] - pts[0];
        else if (i + 1 == n) t = pts[n - 1] - pts[n - 2];
        else t = pts[i + 1] - pts[i - 1];
        const glm::vec2 p = pts[i] + lane_right(safe_normalize(t)) * offset;
        // + kDrapeEpsM: the lane rides the CARRIAGEWAY, which is drawn that
        // far above the terrain. A lane on bare terrain puts every car 6 cm
        // inside the road it is driving on.
        out.push_back(glm::vec3{p.x, surf.at(p) + kDrapeEpsM, p.y});
    }
    return out;
}

glm::vec2 lane_start_dir(const Lane& l) {
    if (l.centreline.size() < 2) return {1.0f, 0.0f};
    return glm::vec2{l.centreline[1].x - l.centreline[0].x,
                     l.centreline[1].z - l.centreline[0].z};
}

glm::vec2 lane_end_dir(const Lane& l) {
    const std::size_t n = l.centreline.size();
    if (n < 2) return {1.0f, 0.0f};
    return glm::vec2{l.centreline[n - 1].x - l.centreline[n - 2].x,
                     l.centreline[n - 1].z - l.centreline[n - 2].z};
}

// Heading-based turn classification, XZ only.
//
// The handedness is pinned by lane_right(): for a heading of +X the right
// vector is -Z, so a turn from +X to -Z is a RIGHT turn, and the 2D cross
// product of those two is negative. Hence cross > 0 is Left. Get this backwards
// and every junction politely gives way to the wrong side of the road.
TurnKind classify_turn(glm::vec2 in_dir, glm::vec2 out_dir) {
    const glm::vec2 a = safe_normalize(in_dir);
    const glm::vec2 b = safe_normalize(out_dir);
    const float d = glm::dot(a, b);
    if (d < -0.5f) return TurnKind::UTurn;
    if (d > 0.5f) return TurnKind::Straight;
    return (a.x * b.y - a.y * b.x) > 0.0f ? TurnKind::Left : TurnKind::Right;
}

float turn_weight(TurnKind k) {
    // Straight dominates, U-turns are a last resort. Carried over from the
    // reference unchanged: these are feel numbers somebody already tuned by
    // watching traffic, and there is nothing to gain by re-guessing them.
    switch (k) {
        case TurnKind::Straight: return 56.0f;
        case TurnKind::Right: return 25.0f;
        case TurnKind::Left: return 18.0f;
        case TurnKind::UTurn: return 1.0f;
    }
    return 1.0f;
}

JunctionControl control_for(const RoadGraph& g, uint32_t n) {
    const RoadNode& nd = g.node(n);
    if (nd.edges.size() < 3) return JunctionControl::None;

    bool any_freeway = false;
    bool any_unpaved = false;
    bool any_major = false;
    bool all_alley = true;
    for (uint32_t ei : nd.edges) {
        const RoadClass c = g.edge(ei).cls;
        if (road_is_grade_separated(c)) any_freeway = true;
        if (road_uses_stop_signs(c)) any_unpaved = true;
        if (road_is_major(c)) any_major = true;
        if (c != RoadClass::Alley) all_alley = false;
    }

    // Order matters, and each step is a rule from pinatty.md §3:
    //  * a freeway meets other roads at RAMPS. There is no at-grade freeway
    //    crossing in the design, so there is no light for one here either.
    //  * dirt takes stop signs, never lights, and the stop wins even against
    //    an arterial.
    //  * alleys are uncontrolled at any degree.
    //  * an arterial or better in the mix makes it a signalled crossing.
    //  * a four-way of plain streets is signalled; a T of plain streets is not.
    if (any_freeway) return JunctionControl::None;
    if (any_unpaved) return JunctionControl::Stop;
    if (all_alley) return JunctionControl::None;
    if (any_major) return JunctionControl::Signal;
    if (nd.edges.size() >= 4) return JunctionControl::Signal;
    return JunctionControl::None;
}

int64_t cell_key(int32_t cx, int32_t cz) {
    return (static_cast<int64_t>(cx) << 32) ^
           static_cast<int64_t>(static_cast<uint32_t>(cz));
}

}  // namespace

void LaneGraph::clear() {
    lanes_.clear();
    junctions_.clear();
    out_links_.clear();
    edge_lanes_.clear();
    index_.clear();
}

LaneRef LaneGraph::add_lane(Lane&& lane) {
    lane.cum.assign(lane.centreline.size(), 0.0f);
    for (std::size_t k = 1; k < lane.centreline.size(); ++k)
        lane.cum[k] = lane.cum[k - 1] +
                      glm::length(lane.centreline[k] - lane.centreline[k - 1]);
    lane.length_m = lane.cum.empty() ? 0.0f : lane.cum.back();
    const LaneRef r = static_cast<LaneRef>(lanes_.size());
    lanes_.push_back(std::move(lane));
    out_links_.emplace_back();
    return r;
}

void LaneGraph::build(const RoadGraph& graph, const GroundSampler& ground,
                      const LaneBuildParams& params) {
    clear();
    index_cell_m_ = std::max(params.index_cell_m, 1.0f);

    junctions_.resize(graph.node_count());
    for (uint32_t n = 0; n < graph.node_count(); ++n) {
        const RoadNode& nd = graph.node(n);
        junctions_[n].pos = glm::vec3{nd.pos.x, nd.y_m, nd.pos.y};
        junctions_[n].degree = static_cast<uint32_t>(nd.edges.size());
        junctions_[n].control = control_for(graph, n);
    }

    edge_lanes_.assign(graph.edge_count(), EdgeLanes{});
    const float sign = params.drive_on_right ? 1.0f : -1.0f;

    for (uint32_t ei = 0; ei < graph.edge_count(); ++ei) {
        const RoadEdge& e = graph.edge(ei);
        const RoadClassDef& def = road_class_def(e.cls);
        const int per_dir = std::max(1, def.lanes_per_dir);
        const RoadSurface surf = RoadSurface::of(e, ground);

        edge_lanes_[ei].base = static_cast<uint32_t>(lanes_.size());
        edge_lanes_[ei].per_dir = static_cast<uint8_t>(per_dir);

        std::vector<glm::vec2> reversed(e.points.rbegin(), e.points.rend());

        // Forward lanes first, then the returning ones. opposing() and
        // neighbour() index off this layout, so it is a contract: base + i is
        // forward lane i, base + per_dir + i is its mirror.
        for (int pass = 0; pass < 2; ++pass) {
            const bool forward = pass == 0;
            for (int i = 0; i < per_dir; ++i) {
                const float off = sign * lane_centre_offset_m(e.width_m, per_dir, i);
                Lane l;
                l.centreline =
                    offset_polyline(forward ? e.points : reversed, off, surf);
                l.junction_from = forward ? e.node_a : e.node_b;
                l.junction_to = forward ? e.node_b : e.node_a;
                l.edge = ei;
                // Packed, not hashed, so it reads in a debugger:
                //   spine id (32) | run (24) | direction (1) | lane index (7)
                l.key = (static_cast<uint64_t>(e.spine_id) << 32) |
                        ((static_cast<uint64_t>(e.spine_run) & 0xFFFFFFull) << 8) |
                        (static_cast<uint64_t>(forward ? 0u : 1u) << 7) |
                        static_cast<uint64_t>(i & 0x7F);
                l.cls = e.cls;
                l.index = static_cast<uint8_t>(i);
                l.forward = forward;
                l.lateral_offset_m = off;
                l.width_m = e.width_m;
                l.speed_limit_mps = def.speed_limit_mps;
                l.traffic_density = e.traffic_density;
                l.ped_density = e.ped_density;
                l.block_quality = e.block_quality;
                add_lane(std::move(l));
            }
        }
    }

    for (LaneRef r = 0; r < lanes_.size(); ++r) {
        junctions_[lanes_[r].junction_from].outgoing.push_back(r);
        junctions_[lanes_[r].junction_to].incoming.push_back(r);
    }

    link_junctions(graph, params.drive_on_right);
    build_index(index_cell_m_);
}

void LaneGraph::link_junctions(const RoadGraph& graph, bool drive_on_right) {
    // Which turn crosses opposing traffic, and which hugs the kerb.
    const TurnKind crossing = drive_on_right ? TurnKind::Left : TurnKind::Right;
    const TurnKind kerbside = drive_on_right ? TurnKind::Right : TurnKind::Left;

    struct Cand {
        LaneRef to = kInvalidLane;
        TurnKind kind = TurnKind::Straight;
        bool preferred = false;
    };
    std::vector<Cand> cand;

    for (uint32_t j = 0; j < junctions_.size(); ++j) {
        const LaneJunction& jn = junctions_[j];
        if (jn.incoming.empty() || jn.outgoing.empty()) continue;
        const RoadClass dominant = graph.dominant_class(j);

        for (LaneRef in_r : jn.incoming) {
            const Lane& in_l = lanes_[in_r];
            const glm::vec2 in_dir = lane_end_dir(in_l);
            const int in_n = std::max<int>(1, edge_lanes_[in_l.edge].per_dir);

            cand.clear();
            for (LaneRef out_r : jn.outgoing) {
                if (out_r == in_r) continue;
                const Lane& out_l = lanes_[out_r];
                const int out_n = std::max<int>(1, edge_lanes_[out_l.edge].per_dir);
                const TurnKind kind =
                    classify_turn(in_dir, lane_start_dir(out_l));

                // Lane discipline: a through movement stays in its own lane, a
                // kerbside turn is made from the kerbside lane, and a crossing
                // turn is made from the lane nearest the centreline. Without
                // this, a multi-lane junction offers every lane to every lane
                // and cars change lanes diagonally across the middle of it.
                bool preferred;
                if (kind == TurnKind::Straight) {
                    preferred = out_l.index ==
                                std::min<int>(in_l.index, out_n - 1);
                } else if (kind == kerbside) {
                    preferred = in_l.index == in_n - 1 && out_l.index == out_n - 1;
                } else {
                    preferred = in_l.index == 0 && out_l.index == 0;
                }
                cand.push_back(Cand{out_r, kind, preferred});
            }
            if (cand.empty()) continue;

            auto prio = [&](TurnKind k) {
                if (k == TurnKind::UTurn) return TurnPriority::Yield;
                if (k == crossing) return TurnPriority::Yield;
                if (in_l.cls != dominant) return TurnPriority::Minor;
                if (k == TurnKind::Straight) return TurnPriority::Major;
                return TurnPriority::Normal;
            };
            auto emit = [&](const Cand& c) {
                out_links_[in_r].push_back(TurnLink{in_r, c.to, j, c.kind,
                                                    prio(c.kind),
                                                    turn_weight(c.kind)});
            };

            // Preferred, non-U-turn movements are the normal case.
            bool any = false;
            for (const Cand& c : cand)
                if (c.preferred && c.kind != TurnKind::UTurn) {
                    emit(c);
                    any = true;
                }
            // A LANE WITH NO SUCCESSOR IS A CAR PARKED FOREVER IN A LIVE LANE,
            // so the discipline above is a preference and never a wall. Fall
            // back to any non-U-turn movement, and to a U-turn only when the
            // junction genuinely offers nothing else (a dead end).
            if (!any)
                for (const Cand& c : cand)
                    if (c.kind != TurnKind::UTurn) {
                        emit(c);
                        any = true;
                    }
            if (!any)
                for (const Cand& c : cand) emit(c);
        }
    }
}

void LaneGraph::build_index(float cell_m) {
    index_.clear();
    index_cell_m_ = cell_m;
    const float step = cell_m * 0.5f;

    for (LaneRef r = 0; r < lanes_.size(); ++r) {
        const Lane& l = lanes_[r];
        for (std::size_t k = 0; k + 1 < l.centreline.size(); ++k) {
            const glm::vec3 a = l.centreline[k];
            const glm::vec3 b = l.centreline[k + 1];
            const float len = glm::length(glm::vec2{b.x - a.x, b.z - a.z});
            const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
            for (int i = 0; i <= n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                const float x = a.x + (b.x - a.x) * t;
                const float z = a.z + (b.z - a.z) * t;
                index_[cell_key(static_cast<int32_t>(std::floor(x / cell_m)),
                                static_cast<int32_t>(std::floor(z / cell_m)))]
                    .push_back(r);
            }
        }
    }
    for (auto& kv : index_) {
        std::sort(kv.second.begin(), kv.second.end());
        kv.second.erase(std::unique(kv.second.begin(), kv.second.end()),
                        kv.second.end());
    }
}

void LaneGraph::gather_candidates(glm::vec2 xz, float radius_m,
                                  std::vector<LaneRef>& out) const {
    out.clear();
    const int32_t x0 = static_cast<int32_t>(std::floor((xz.x - radius_m) / index_cell_m_));
    const int32_t x1 = static_cast<int32_t>(std::floor((xz.x + radius_m) / index_cell_m_));
    const int32_t z0 = static_cast<int32_t>(std::floor((xz.y - radius_m) / index_cell_m_));
    const int32_t z1 = static_cast<int32_t>(std::floor((xz.y + radius_m) / index_cell_m_));
    for (int32_t cz = z0; cz <= z1; ++cz) {
        for (int32_t cx = x0; cx <= x1; ++cx) {
            auto it = index_.find(cell_key(cx, cz));
            if (it == index_.end()) continue;
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

LanePose LaneGraph::pose(LaneRef r, float d) const { return pose(r, d, 0.0f); }

LanePose LaneGraph::pose(LaneRef r, float d, float lateral_m) const {
    LanePose out;
    if (r >= lanes_.size()) return out;
    const Lane& l = lanes_[r];
    if (l.centreline.empty()) return out;
    if (l.centreline.size() < 2) {
        out.position = l.centreline[0];
        return out;
    }

    // Clamped, never wrapped. See the header: running past the end of a lane
    // is a routing bug, and wrapping hides it as a teleport.
    const float t_d = std::clamp(d, 0.0f, l.length_m);
    // Binary search, because a police chase asks this for every car every step
    // and a linear walk down a 600 m bridge lane is a lot of nothing.
    auto it = std::upper_bound(l.cum.begin() + 1, l.cum.end(), t_d);
    std::size_t s = static_cast<std::size_t>(it - l.cum.begin()) - 1;
    if (s + 2 > l.centreline.size()) s = l.centreline.size() - 2;

    const glm::vec3 a = l.centreline[s];
    const glm::vec3 b = l.centreline[s + 1];
    const float seg = l.cum[s + 1] - l.cum[s];
    const float u = seg > 1e-6f ? (t_d - l.cum[s]) / seg : 0.0f;

    const glm::vec3 tangent = b - a;
    const float tl = glm::length(tangent);
    out.tangent = tl > 1e-6f ? tangent / tl : glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 right = glm::cross(kUp, out.tangent);
    const float rl = glm::length(right);
    out.right = rl > 1e-6f ? right / rl : glm::vec3{0.0f, 0.0f, -1.0f};
    out.position = a + (b - a) * u + out.right * lateral_m;
    return out;
}

LaneProjection LaneGraph::project_onto(LaneRef r, glm::vec2 xz) const {
    LaneProjection best;
    if (r >= lanes_.size()) return best;
    const Lane& l = lanes_[r];
    if (l.centreline.empty()) return best;
    if (l.centreline.size() < 2) {
        const glm::vec2 a{l.centreline[0].x, l.centreline[0].z};
        best.lane = r;
        best.dist2 = glm::dot(xz - a, xz - a);
        return best;
    }

    float best_d2 = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < l.centreline.size(); ++i) {
        const glm::vec2 a{l.centreline[i - 1].x, l.centreline[i - 1].z};
        const glm::vec2 b{l.centreline[i].x, l.centreline[i].z};
        const glm::vec2 ab = b - a;
        const float l2 = glm::dot(ab, ab);
        const float u = l2 > 1e-9f
                            ? std::clamp(glm::dot(xz - a, ab) / l2, 0.0f, 1.0f)
                            : 0.0f;
        const glm::vec2 p = a + ab * u;
        const glm::vec2 dp = xz - p;
        const float d2 = glm::dot(dp, dp);
        if (d2 >= best_d2) continue;
        best_d2 = d2;
        const float seg_len = std::sqrt(l2);
        best.lane = r;
        best.dist_along_m = l.cum[i - 1] + seg_len * u;
        best.dist2 = d2;
        // Same right vector pose() uses, so "lateral" means one thing.
        best.lateral_m =
            seg_len > 1e-6f ? glm::dot(dp, lane_right(ab / seg_len)) : 0.0f;
    }
    return best;
}

LaneProjection LaneGraph::nearest_lane(glm::vec2 xz, float max_radius_m) const {
    std::vector<LaneRef> cand;
    gather_candidates(xz, max_radius_m, cand);
    LaneProjection best;
    float best_d2 = max_radius_m * max_radius_m;
    for (LaneRef r : cand) {
        const LaneProjection p = project_onto(r, xz);
        if (!p.valid() || p.dist2 >= best_d2) continue;
        best_d2 = p.dist2;
        best = p;
    }
    return best;
}

LaneProjection LaneGraph::nearest_lane_along(glm::vec2 xz, glm::vec2 heading,
                                             float max_radius_m) const {
    std::vector<LaneRef> cand;
    gather_candidates(xz, max_radius_m, cand);
    const glm::vec2 h = safe_normalize(heading);
    LaneProjection best;
    float best_d2 = max_radius_m * max_radius_m;
    for (LaneRef r : cand) {
        const LaneProjection p = project_onto(r, xz);
        if (!p.valid() || p.dist2 >= best_d2) continue;
        const LanePose lp = pose(r, p.dist_along_m);
        if (glm::dot(glm::vec2{lp.tangent.x, lp.tangent.z}, h) <= 0.0f) continue;
        best_d2 = p.dist2;
        best = p;
    }
    return best;
}

const std::vector<TurnLink>& LaneGraph::outgoing(LaneRef r) const {
    if (r >= out_links_.size()) return kNoLinks;
    return out_links_[r];
}

LaneRef LaneGraph::choose_next(LaneRef r, uint64_t seed,
                               uint32_t decision_index) const {
    if (r >= out_links_.size()) return kInvalidLane;
    const std::vector<TurnLink>& links = out_links_[r];
    if (links.empty()) return kInvalidLane;

    float total = 0.0f;
    for (const TurnLink& t : links) total += t.weight;
    if (!(total > 0.0f)) return links.front().to;

    const uint64_t key = lanes_[r].key;
    Rng rng{hash_coord3(seed, static_cast<int32_t>(key & 0xFFFFFFFFull),
                        static_cast<int32_t>(key >> 32), decision_index)};
    float x = rng.next_float() * total;
    for (const TurnLink& t : links) {
        x -= t.weight;
        if (x <= 0.0f) return t.to;
    }
    return links.back().to;
}

std::vector<LaneRef> LaneGraph::plan_route(LaneRef from, LaneRef to) const {
    if (from >= lanes_.size() || to >= lanes_.size()) return {};
    if (from == to) return {from};

    const glm::vec3 goal = lanes_[to].centreline.front();
    auto heuristic = [&](LaneRef r) {
        const glm::vec3 p = lanes_[r].centreline.front();
        return glm::length(glm::vec2{goal.x - p.x, goal.z - p.z});
    };

    const float kInf = std::numeric_limits<float>::max();
    std::vector<float> g(lanes_.size(), kInf);
    std::vector<LaneRef> came(lanes_.size(), kInvalidLane);
    std::vector<uint8_t> closed(lanes_.size(), 0);

    struct Open {
        float f;
        LaneRef lane;
    };
    // Tie-break on the lane index so two equally good frontiers always expand
    // in the same order. A route that depends on heap order is a route that
    // differs between builds, and a police chase that differs between builds
    // is a replay that desyncs.
    auto worse = [](const Open& a, const Open& b) {
        if (a.f != b.f) return a.f > b.f;
        return a.lane > b.lane;
    };
    std::priority_queue<Open, std::vector<Open>, decltype(worse)> open(worse);

    g[from] = 0.0f;
    open.push(Open{heuristic(from), from});

    while (!open.empty()) {
        const Open cur = open.top();
        open.pop();
        if (closed[cur.lane]) continue;
        closed[cur.lane] = 1;
        if (cur.lane == to) break;

        const float base = g[cur.lane] + lanes_[cur.lane].length_m;
        for (const TurnLink& t : out_links_[cur.lane]) {
            if (t.to >= lanes_.size() || closed[t.to]) continue;
            if (base >= g[t.to]) continue;
            g[t.to] = base;
            came[t.to] = cur.lane;
            open.push(Open{base + heuristic(t.to), t.to});
        }
    }

    if (g[to] == kInf) return {};
    std::vector<LaneRef> route;
    for (LaneRef r = to; r != kInvalidLane; r = came[r]) {
        route.push_back(r);
        if (r == from) break;
    }
    if (route.empty() || route.back() != from) return {};
    std::reverse(route.begin(), route.end());
    return route;
}

bool LaneGraph::approach_group_a(uint32_t junction, LaneRef incoming) const {
    if (junction >= junctions_.size()) return true;
    const LaneJunction& jn = junctions_[junction];
    if (jn.incoming.empty() || incoming >= lanes_.size()) return true;

    // A road's AXIS: its heading at the junction folded to a half circle, so
    // an approach and the one opposite it land in the same group and a street
    // gets one green for both of its ends.
    auto axis = [&](LaneRef r) {
        glm::vec2 a = safe_normalize(lane_end_dir(lanes_[r]));
        if (a.x < 0.0f || (a.x == 0.0f && a.y < 0.0f)) a = -a;
        return a;
    };
    const glm::vec2 ref = axis(jn.incoming.front());
    const float align = std::fabs(glm::dot(ref, axis(incoming)));
    return align >= 0.70710678f;  // cos 45 degrees
}

LaneRef LaneGraph::opposing(LaneRef r) const {
    if (r >= lanes_.size()) return kInvalidLane;
    const Lane& l = lanes_[r];
    const EdgeLanes& el = edge_lanes_[l.edge];
    if (el.per_dir == 0) return kInvalidLane;
    const uint32_t per = el.per_dir;
    return el.base + (l.forward ? per + l.index : uint32_t{l.index});
}

LaneRef LaneGraph::neighbour(LaneRef r, int delta) const {
    if (r >= lanes_.size()) return kInvalidLane;
    const Lane& l = lanes_[r];
    const EdgeLanes& el = edge_lanes_[l.edge];
    const int per = el.per_dir;
    const int want = static_cast<int>(l.index) + delta;
    if (per == 0 || want < 0 || want >= per) return kInvalidLane;
    return el.base + (l.forward ? 0u : static_cast<uint32_t>(per)) +
           static_cast<uint32_t>(want);
}

std::vector<LaneRef> LaneGraph::lanes_of_edge(uint32_t edge) const {
    std::vector<LaneRef> out;
    if (edge >= edge_lanes_.size()) return out;
    const EdgeLanes& el = edge_lanes_[edge];
    const uint32_t n = static_cast<uint32_t>(el.per_dir) * 2u;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) out.push_back(el.base + i);
    return out;
}

}  // namespace apricot

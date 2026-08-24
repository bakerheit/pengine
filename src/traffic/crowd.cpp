#include "traffic/crowd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "city/city_rng.h"
#include "city/pedestrian_separation.h"
#include "core/fixed_step.h"

namespace apricot {
namespace {

constexpr float kSimDtF = static_cast<float>(kSimDt);
constexpr float kInf = std::numeric_limits<float>::infinity();

// The two halves of an agent's identity, packed for ordering only. Ordering is
// lexicographic on (lane key, slot) and never on a hash of them: a hash
// collision would put two distinct agents in an undefined relative order, and
// "undefined" is exactly the property this ordering exists to remove.
struct Ident {
    uint64_t key;
    uint32_t slot;
};

bool ident_less(const Ident& a, const Ident& b) {
    if (a.key != b.key) return a.key < b.key;
    return a.slot < b.slot;
}

uint64_t mix_bits(uint64_t h, uint64_t v) {
    return splitmix64_mix(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2)));
}

uint64_t mix_f32(uint64_t h, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return mix_bits(h, bits);
}

int32_t floor_div(float v, float cell) {
    return static_cast<int32_t>(std::floor(v / cell));
}

// Cell hash for the pedestrian neighbour grid. Only ever used to pick a bucket;
// the real cell coordinates are compared on the way out, so a collision costs
// wasted distance tests and never a wrong neighbour set.
uint64_t cell_hash(int32_t cx, int32_t cz) {
    return hash_coord(0x9E3779B97F4A7C15ull, cx, cz);
}

}  // namespace

// ---------------------------------------------------------------------------
//  build
// ---------------------------------------------------------------------------

void Crowd::build(const LaneGraph& graph, uint64_t map_seed,
                  const AmbientTuning& ambient, const CrowdTuning& tuning) {
    clear();
    graph_ = &graph;
    map_seed_ = map_seed;
    ambient_ = ambient;
    tuning_ = tuning;

    const std::size_t n = graph.lane_count();
    veh_sched_.resize(n);
    ped_sched_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Lane& l = graph.lane(static_cast<LaneRef>(i));
        veh_sched_[i] = vehicle_schedule(map_seed_, l, ambient_);
        ped_sched_[i] = ped_schedule(map_seed_, l, ambient_);
    }

    lane_buckets_.assign(n, {});
    build_lane_index();
}

void Crowd::clear() {
    graph_ = nullptr;
    veh_sched_.clear();
    ped_sched_.clear();
    index_cells_.clear();
    index_nx_ = index_nz_ = 0;
    vehicles_.clear();
    peds_.clear();
    retired_.clear();
    lane_buckets_.clear();
    touched_lanes_.clear();
    leader_gap_.clear();
    ped_cells_.clear();
    ped_cell_keys_.clear();
    ped_pos_frozen_.clear();
    dead_nodes_.clear();
    stats_ = CrowdStats{};
}

void Crowd::build_lane_index() {
    index_cells_.clear();
    if (!graph_ || graph_->lane_count() == 0) return;

    glm::vec2 lo{kInf, kInf};
    glm::vec2 hi{-kInf, -kInf};
    for (const Lane& l : graph_->lanes()) {
        for (const glm::vec3& p : l.centreline) {
            lo = glm::min(lo, glm::vec2{p.x, p.z});
            hi = glm::max(hi, glm::vec2{p.x, p.z});
        }
    }
    if (!(hi.x >= lo.x)) return;

    index_min_ = lo - glm::vec2{index_cell_m_};
    const glm::vec2 span = (hi - lo) + glm::vec2{2.0f * index_cell_m_};
    index_nx_ = std::max(1, static_cast<int>(span.x / index_cell_m_) + 1);
    index_nz_ = std::max(1, static_cast<int>(span.y / index_cell_m_) + 1);
    index_cells_.assign(static_cast<std::size_t>(index_nx_) *
                            static_cast<std::size_t>(index_nz_),
                        {});

    // A lane goes into every cell its bounding box touches. Conservative rather
    // than exact: a rasterised segment walk would put fewer lanes in fewer
    // cells, and gather_lanes() distance-filters anyway, so the exactness buys
    // nothing and the walk is one more thing to get wrong at a corner.
    for (std::size_t i = 0; i < graph_->lane_count(); ++i) {
        const Lane& l = graph_->lane(static_cast<LaneRef>(i));
        if (l.centreline.empty()) continue;
        glm::vec2 a{kInf, kInf};
        glm::vec2 b{-kInf, -kInf};
        for (const glm::vec3& p : l.centreline) {
            a = glm::min(a, glm::vec2{p.x, p.z});
            b = glm::max(b, glm::vec2{p.x, p.z});
        }
        const int x0 = std::max(0, floor_div(a.x - index_min_.x, index_cell_m_));
        const int x1 = std::min(index_nx_ - 1,
                                floor_div(b.x - index_min_.x, index_cell_m_));
        const int z0 = std::max(0, floor_div(a.y - index_min_.y, index_cell_m_));
        const int z1 = std::min(index_nz_ - 1,
                                floor_div(b.y - index_min_.y, index_cell_m_));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t c = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(index_nx_) +
                                      static_cast<std::size_t>(x);
                index_cells_[c].push_back(static_cast<LaneRef>(i));
            }
        }
    }
}

void Crowd::gather_lanes(glm::vec2 xz, float radius_m,
                         std::vector<LaneRef>& out) const {
    out.clear();
    if (index_cells_.empty()) return;
    const int x0 = std::max(0, floor_div(xz.x - radius_m - index_min_.x, index_cell_m_));
    const int x1 = std::min(index_nx_ - 1,
                            floor_div(xz.x + radius_m - index_min_.x, index_cell_m_));
    const int z0 = std::max(0, floor_div(xz.y - radius_m - index_min_.y, index_cell_m_));
    const int z1 = std::min(index_nz_ - 1,
                            floor_div(xz.y + radius_m - index_min_.y, index_cell_m_));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const std::size_t c = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(index_nx_) +
                                  static_cast<std::size_t>(x);
            const std::vector<LaneRef>& cell = index_cells_[c];
            out.insert(out.end(), cell.begin(), cell.end());
        }
    }
    // A lane spanning several cells arrives several times. Sorting and uniquing
    // is what makes the candidate list a function of the QUERY and not of the
    // cell walk order.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// ---------------------------------------------------------------------------
//  sub-rate scheduling
// ---------------------------------------------------------------------------

int Crowd::sub_rate_phase(uint64_t lane_key, uint32_t slot, uint32_t index,
                          uint32_t spawn_ordinal, int k) const {
    if (k <= 1) return 0;
    const uint32_t uk = static_cast<uint32_t>(k);
    switch (tuning_.policy) {
        case SubRatePolicy::Keyed: {
            // The phase travels with the agent, so it is the same in every run
            // that contains this agent, regardless of what else is active.
            const uint64_t h = phantom_key(map_seed_, lane_key, slot, 0x2500u);
            return static_cast<int>(h % uk);
        }
        case SubRatePolicy::ContainerIndex:
            // Safe here, and only because the vector is sorted by identity.
            return static_cast<int>(index % uk);
        case SubRatePolicy::SpawnOrdinal:
            // The bug, on purpose.
            return static_cast<int>(spawn_ordinal % uk);
    }
    return 0;
}

bool Crowd::is_retired(uint64_t lane_key, uint32_t slot) const {
    const RetiredId want{lane_key, slot};
    const auto it = std::lower_bound(
        retired_.begin(), retired_.end(), want,
        [](const RetiredId& a, const RetiredId& b) {
            return ident_less(Ident{a.key, a.slot}, Ident{b.key, b.slot});
        });
    return it != retired_.end() && it->key == lane_key && it->slot == slot;
}

void Crowd::retire(uint64_t lane_key, uint32_t slot) {
    const RetiredId want{lane_key, slot};
    const auto it = std::lower_bound(
        retired_.begin(), retired_.end(), want,
        [](const RetiredId& a, const RetiredId& b) {
            return ident_less(Ident{a.key, a.slot}, Ident{b.key, b.slot});
        });
    if (it == retired_.end() || it->key != lane_key || it->slot != slot)
        retired_.insert(it, want);
}

// ---------------------------------------------------------------------------
//  refresh: instantiate what came into range, retire what left
// ---------------------------------------------------------------------------

void Crowd::refresh(int64_t step, glm::vec2 player_xz) {
    if (!graph_) return;

    const float retire_r = std::max(tuning_.vehicle_retire_m, tuning_.ped_retire_m);
    gather_lanes(player_xz, retire_r, lane_scratch_);
    stats_.lanes_scanned = lane_scratch_.size();

    const float va2 = tuning_.vehicle_activate_m * tuning_.vehicle_activate_m;
    const float vr2 = tuning_.vehicle_retire_m * tuning_.vehicle_retire_m;
    const float pa2 = tuning_.ped_activate_m * tuning_.ped_activate_m;
    const float pr2 = tuning_.ped_retire_m * tuning_.ped_retire_m;

    // --- retire ------------------------------------------------------------
    // std::remove_if is stable, so the survivors keep their sorted order and
    // the merge below only has to splice the newcomers in.
    {
        const auto dead = std::remove_if(
            vehicles_.begin(), vehicles_.end(), [&](const VehicleAgent& v) {
                const glm::vec2 d{v.pos.x - player_xz.x, v.pos.z - player_xz.y};
                if (d.x * d.x + d.y * d.y <= vr2) return false;
                if (v.node != kInvalidId) dead_nodes_.push_back(v.node);
                retire(v.lane_key, v.slot);
                ++stats_.retired;
                return true;
            });
        vehicles_.erase(dead, vehicles_.end());
    }
    {
        const auto dead = std::remove_if(
            peds_.begin(), peds_.end(), [&](const PedAgent& p) {
                const glm::vec2 d{p.pos.x - player_xz.x, p.pos.z - player_xz.y};
                if (d.x * d.x + d.y * d.y <= pr2) return false;
                if (p.node != kInvalidId) dead_nodes_.push_back(p.node);
                retire(p.lane_key, p.slot);
                ++stats_.retired;
                return true;
            });
        peds_.erase(dead, peds_.end());
    }

    // --- instantiate -------------------------------------------------------
    const std::size_t veh_have = vehicles_.size();
    const std::size_t ped_have = peds_.size();

    for (std::size_t li = 0; li < lane_scratch_.size(); ++li) {
        const LaneRef lr = tuning_.reverse_scan_order
                               ? lane_scratch_[lane_scratch_.size() - 1 - li]
                               : lane_scratch_[li];
        const Lane& lane = graph_->lane(lr);

        const LaneSchedule& vs = veh_sched_[lr];
        for (uint32_t slot = 0; slot < vs.slots; ++slot) {
            if (vehicles_.size() >= tuning_.max_vehicles) break;
            const Ident id{lane.key, slot};
            const auto lo = std::lower_bound(
                vehicles_.begin(), vehicles_.begin() + static_cast<long>(veh_have),
                id, [](const VehicleAgent& a, const Ident& b) {
                    return ident_less(Ident{a.lane_key, a.slot}, b);
                });
            if (lo != vehicles_.begin() + static_cast<long>(veh_have) &&
                lo->lane_key == id.key && lo->slot == id.slot)
                continue;
            if (is_retired(id.key, id.slot)) continue;

            const PhantomState ph =
                phantom_vehicle(map_seed_, lane, vs, slot, step, ambient_);
            const LanePose pose = graph_->pose(lr, ph.dist_along_m);
            const glm::vec2 d{pose.position.x - player_xz.x,
                              pose.position.z - player_xz.y};
            if (d.x * d.x + d.y * d.y > va2) continue;

            VehicleAgent v;
            v.lane_key = lane.key;
            v.slot = slot;
            v.lane = lr;
            v.dist_along_m = ph.dist_along_m;
            v.speed_mps = ph.speed_mps;
            v.cruise_mps = ph.speed_mps;
            v.last_dist_m = ph.dist_along_m;
            v.mode = AgentMode::Analytic;
            // The driver comes off the agent's identity, not off a stream. The
            // lane key is split across the two coordinate axes exactly as
            // lane_graph.h prescribes, so the same car has the same driver
            // forever regardless of approach order.
            v.profile = driver_profile_for(
                map_seed_,
                static_cast<int32_t>(static_cast<uint32_t>(lane.key)),
                static_cast<int32_t>(static_cast<uint32_t>(lane.key >> 32)), slot);
            v.pos = pose.position;
            v.fwd = pose.tangent;
            v.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
            vehicles_.push_back(v);
            ++stats_.activated;
        }

        const LaneSchedule& ps = ped_sched_[lr];
        for (uint32_t slot = 0; slot < ps.slots; ++slot) {
            if (peds_.size() >= tuning_.max_peds) break;
            const Ident id{lane.key, slot};
            const auto lo = std::lower_bound(
                peds_.begin(), peds_.begin() + static_cast<long>(ped_have), id,
                [](const PedAgent& a, const Ident& b) {
                    return ident_less(Ident{a.lane_key, a.slot}, b);
                });
            if (lo != peds_.begin() + static_cast<long>(ped_have) &&
                lo->lane_key == id.key && lo->slot == id.slot)
                continue;
            if (is_retired(id.key, id.slot)) continue;

            const PhantomState ph =
                phantom_ped(map_seed_, lane, ps, slot, step, ambient_);
            const LanePose pose =
                graph_->pose(lr, ph.dist_along_m, ph.lateral_m);
            const glm::vec2 d{pose.position.x - player_xz.x,
                              pose.position.z - player_xz.y};
            if (d.x * d.x + d.y * d.y > pa2) continue;

            PedAgent p;
            p.lane_key = lane.key;
            p.slot = slot;
            p.lane = lr;
            p.dist_along_m = ph.dist_along_m;
            p.speed_mps = ph.speed_mps;
            p.base_lateral_m = ph.lateral_m;
            p.lateral_m = ph.lateral_m;
            p.last_dist_m = ph.dist_along_m;
            p.mode = AgentMode::Analytic;
            p.pos = pose.position;
            p.fwd = pose.tangent;
            p.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
            peds_.push_back(p);
            ++stats_.activated;
        }
    }

    // Newcomers were appended in lane-scan order, which is a fact about the
    // scan and not about the population. Sort them and splice: the resulting
    // vector is ordered by identity alone, so two runs that instantiated the
    // same agents in different orders end up byte-identical here.
    if (vehicles_.size() > veh_have) {
        const auto mid = vehicles_.begin() + static_cast<long>(veh_have);
        std::sort(mid, vehicles_.end(),
                  [](const VehicleAgent& a, const VehicleAgent& b) {
                      return ident_less(Ident{a.lane_key, a.slot},
                                        Ident{b.lane_key, b.slot});
                  });
        std::inplace_merge(vehicles_.begin(), mid, vehicles_.end(),
                           [](const VehicleAgent& a, const VehicleAgent& b) {
                               return ident_less(Ident{a.lane_key, a.slot},
                                                 Ident{b.lane_key, b.slot});
                           });
    }
    if (peds_.size() > ped_have) {
        const auto mid = peds_.begin() + static_cast<long>(ped_have);
        std::sort(mid, peds_.end(), [](const PedAgent& a, const PedAgent& b) {
            return ident_less(Ident{a.lane_key, a.slot},
                              Ident{b.lane_key, b.slot});
        });
        std::inplace_merge(peds_.begin(), mid, peds_.end(),
                           [](const PedAgent& a, const PedAgent& b) {
                               return ident_less(Ident{a.lane_key, a.slot},
                                                 Ident{b.lane_key, b.slot});
                           });
    }

    stats_.vehicles = vehicles_.size();
    stats_.peds = peds_.size();
}

// ---------------------------------------------------------------------------
//  rebuild_buckets: freeze every cross-agent read for this step
// ---------------------------------------------------------------------------

void Crowd::rebuild_buckets() {
    if (!graph_) return;

    for (LaneRef lr : touched_lanes_) lane_buckets_[lr].clear();
    touched_lanes_.clear();

    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        const VehicleAgent& v = vehicles_[i];
        if (!graph_->valid(v.lane)) continue;
        std::vector<BucketEntry>& b = lane_buckets_[v.lane];
        if (b.empty()) touched_lanes_.push_back(v.lane);
        b.push_back(BucketEntry{v.dist_along_m, i});
    }

    leader_gap_.assign(vehicles_.size(), kInf);
    for (LaneRef lr : touched_lanes_) {
        std::vector<BucketEntry>& b = lane_buckets_[lr];
        // Ties broken on agent index, which is itself ordered by identity, so
        // two cars stopped at exactly the same distance still have a defined
        // leader-follower relationship rather than whichever std::sort picked.
        std::sort(b.begin(), b.end(), [](const BucketEntry& a, const BucketEntry& c) {
            if (a.dist != c.dist) return a.dist < c.dist;
            return a.agent < c.agent;
        });
        for (std::size_t j = 0; j + 1 < b.size(); ++j)
            leader_gap_[b[j].agent] = b[j + 1].dist - b[j].dist;
        // The last car on a lane looks into the lane it is about to enter. One
        // extra bucket probe, and without it every car at the head of a queue
        // accelerates into the back of the queue on the far side of the
        // junction.
        if (!b.empty()) {
            const BucketEntry& head = b.back();
            const VehicleAgent& v = vehicles_[head.agent];
            const std::vector<TurnLink>& outs = graph_->outgoing(v.lane);
            if (!outs.empty()) {
                const LaneRef nxt = graph_->choose_next(v.lane, map_seed_,
                                                        v.decisions);
                if (graph_->valid(nxt) && !lane_buckets_[nxt].empty()) {
                    const float remain = graph_->lane(v.lane).length_m - head.dist;
                    leader_gap_[head.agent] =
                        remain + lane_buckets_[nxt].front().dist;
                }
            }
        }
    }

    // --- pedestrian neighbour grid ----------------------------------------
    // A spatial hash sized to the POPULATION, not to the area: a dense grid
    // over the active box is fine at a 160 m radius and is nine megabytes of
    // memset per step at a kilometre.
    const std::size_t np = peds_.size();
    ped_pos_frozen_.resize(np);
    for (std::size_t i = 0; i < np; ++i)
        ped_pos_frozen_[i] = glm::vec2{peds_[i].pos.x, peds_[i].pos.z};

    std::size_t table = 64;
    while (table < np * 2u) table <<= 1;
    ped_cells_.assign(table, {});
    ped_cell_keys_.assign(np, 0);
    for (std::size_t i = 0; i < np; ++i) {
        const int32_t cx = floor_div(ped_pos_frozen_[i].x, PED_SEPARATION_RADIUS);
        const int32_t cz = floor_div(ped_pos_frozen_[i].y, PED_SEPARATION_RADIUS);
        ped_cell_keys_[i] = (static_cast<int64_t>(cx) << 32) |
                            static_cast<int64_t>(static_cast<uint32_t>(cz));
        // Appended in index order, and index order IS identity order, so a
        // bucket's contents are ordered by identity and the float sum in
        // ped_separation() is reproducible.
        ped_cells_[cell_hash(cx, cz) & (table - 1u)].push_back(
            static_cast<uint32_t>(i));
    }
}

// ---------------------------------------------------------------------------
//  step_vehicles
// ---------------------------------------------------------------------------

void Crowd::step_vehicles(int64_t step) {
    if (!graph_) return;
    const int k = std::max(1, tuning_.vehicle_sub_rate);
    const float dt = static_cast<float>(k) * kSimDtF;
    const float min_gap_floor = tuning_.car_length_m + 0.6f;
    const int64_t half_cycle = std::max<int64_t>(1, tuning_.signal_period_steps / 2);

    stats_.vehicles_stepped = 0;
    stats_.vehicles_analytic = 0;

    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        VehicleAgent& v = vehicles_[i];
        if (v.mode == AgentMode::Analytic) ++stats_.vehicles_analytic;
        if (!graph_->valid(v.lane)) continue;
        if (static_cast<int>(step % k) !=
            sub_rate_phase(v.lane_key, v.slot, i, v.spawn_ordinal, k))
            continue;
        ++stats_.vehicles_stepped;

        const Lane& lane = graph_->lane(v.lane);
        const float to_end = lane.length_m - v.dist_along_m;

        // --- what would slow me down ---------------------------------------
        const float gap = leader_gap_[i] - tuning_.car_length_m;
        DriverProfile prof = v.profile;
        prof.min_gap = effective_min_gap(prof, min_gap_floor);
        float target = std::min(v.cruise_mps,
                                traffic_follow_speed_for_gap(gap, prof));

        // Signals. Which half of the cycle is green is a pure function of the
        // STEP, like every other clock in this engine; approach_group_a() is
        // the lane graph's own answer for which approaches share a phase, and
        // the AI and the signal head must both call it or the bulb disagrees
        // with the stop decision.
        bool hold = false;
        const uint32_t jn = lane.junction_to;
        if (jn < graph_->junction_count()) {
            const JunctionControl ctrl = graph_->junction_control(jn);
            if (ctrl == JunctionControl::Signal) {
                const bool phase_a = ((step / half_cycle) & 1) == 0;
                hold = graph_->approach_group_a(jn, v.lane) != phase_a;
            } else if (ctrl == JunctionControl::Stop) {
                hold = v.speed_mps > 0.4f;
            }
        }
        if (hold) {
            const float slack = to_end - tuning_.stop_line_m;
            // Same shape as the maneuver governor: approach speed proportional
            // to remaining distance, so a car eases onto the line rather than
            // discovering it.
            target = std::min(target, std::max(0.0f, slack * 0.8f));
        }

        const bool perturbed = target < v.cruise_mps - 1e-4f;

        if (v.mode == AgentMode::Analytic && !perturbed) {
            // REPRODUCED, NOT ADVANCED. The closed form is evaluated at the
            // absolute step, so this agent's state does not remember the step
            // it was instantiated at — which is the entire claim analytic
            // ambient traffic is making.
            const PhantomState ph = phantom_vehicle(map_seed_, lane,
                                                    veh_sched_[v.lane], v.slot,
                                                    step, ambient_);
            if (ph.dist_along_m + 1e-3f < v.last_dist_m) {
                // The schedule wrapped. A phantom may teleport to the start of
                // its lane because nobody is looking at it; an ACTIVE car may
                // not, so this is where it stops being a phantom and starts
                // being a car with a history.
                v.mode = AgentMode::Integrating;
            } else {
                v.dist_along_m = ph.dist_along_m;
                v.speed_mps = ph.speed_mps;
                v.last_dist_m = ph.dist_along_m;
                const LanePose p = graph_->pose(v.lane, v.dist_along_m);
                v.pos = p.position;
                v.fwd = p.tangent;
                continue;
            }
        }

        v.mode = AgentMode::Integrating;

        const float dv = target - v.speed_mps;
        const float rate = dv >= 0.0f ? prof.accel : prof.brake;
        v.speed_mps += std::clamp(dv, -rate * dt, rate * dt);
        v.speed_mps = std::max(0.0f, v.speed_mps);

        v.dist_along_m += v.speed_mps * dt;
        if (v.dist_along_m >= lane.length_m) {
            const LaneRef nxt = graph_->choose_next(v.lane, map_seed_, v.decisions);
            ++v.decisions;
            if (graph_->valid(nxt)) {
                v.dist_along_m -= lane.length_m;
                v.lane = nxt;
                v.cruise_mps = std::min(v.cruise_mps,
                                        graph_->lane(nxt).speed_limit_mps);
            } else {
                // Nowhere to go. Sit on the end rather than run off it: a
                // silent wrap would teleport the car to the start of the
                // street it was leaving.
                v.dist_along_m = lane.length_m;
                v.speed_mps = 0.0f;
            }
        }
        v.last_dist_m = v.dist_along_m;

        const LanePose p = graph_->pose(v.lane, v.dist_along_m);
        v.pos = p.position;
        v.fwd = p.tangent;
    }
}

// ---------------------------------------------------------------------------
//  step_peds
// ---------------------------------------------------------------------------

void Crowd::step_peds(int64_t step) {
    if (!graph_) return;
    const int k = std::max(1, tuning_.ped_sub_rate);
    const float dt = static_cast<float>(k) * kSimDtF;
    const std::size_t table = ped_cells_.size();

    stats_.peds_stepped = 0;
    stats_.peds_analytic = 0;
    stats_.ped_neighbour_tests = 0;

    for (uint32_t i = 0; i < peds_.size(); ++i) {
        PedAgent& p = peds_[i];
        if (p.mode == AgentMode::Analytic) ++stats_.peds_analytic;
        if (!graph_->valid(p.lane)) continue;
        if (static_cast<int>(step % k) !=
            sub_rate_phase(p.lane_key, p.slot, i, p.spawn_ordinal, k))
            continue;
        ++stats_.peds_stepped;

        // --- neighbours, from the frozen grid -------------------------------
        const glm::vec2 self = ped_pos_frozen_[i];
        const int32_t cx = floor_div(self.x, PED_SEPARATION_RADIUS);
        const int32_t cz = floor_div(self.y, PED_SEPARATION_RADIUS);
        ped_scratch_.clear();
        if (table != 0) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int32_t qx = cx + dx;
                    const int32_t qz = cz + dz;
                    const int64_t want = (static_cast<int64_t>(qx) << 32) |
                                         static_cast<int64_t>(static_cast<uint32_t>(qz));
                    const std::vector<uint32_t>& bucket =
                        ped_cells_[cell_hash(qx, qz) & (table - 1u)];
                    for (uint32_t j : bucket) {
                        // The bucket is a SUPERSET of the cell — two cells can
                        // hash to it. Comparing the real cell coordinates is
                        // what stops a colliding cell being counted twice, and
                        // a double-counted neighbour is a ped that sidesteps
                        // twice as hard for no visible reason.
                        if (ped_cell_keys_[j] != want) continue;
                        if (j == i) continue;
                        ++stats_.ped_neighbour_tests;
                        ped_scratch_.push_back(ped_pos_frozen_[j]);
                    }
                }
            }
        }

        const LanePose base = graph_->pose(p.lane, p.dist_along_m);
        const glm::vec2 fwd{base.tangent.x, base.tangent.z};

        // Per-ped variation, keyed on identity. Without the preferred offset
        // every uncrowded ped targets the same line and the crowd walks single
        // file; without the space scale they all defend the same bubble.
        const int32_t kx = static_cast<int32_t>(static_cast<uint32_t>(p.lane_key));
        const int32_t kz =
            static_cast<int32_t>(static_cast<uint32_t>(p.lane_key >> 32));
        const float pref =
            (city_unit_roll(map_seed_, kx, kz, p.slot, kChannelPedPreferred) *
                 2.0f - 1.0f) * PED_PREFERRED_OFFSET_MAX;
        const float space =
            PED_SPACE_SCALE_MIN +
            city_unit_roll(map_seed_, kx, kz, p.slot, kChannelPedSpace) *
                (PED_SPACE_SCALE_MAX - PED_SPACE_SCALE_MIN);

        const PedSeparation sep =
            ped_separation(self, fwd, ped_scratch_.data(), ped_scratch_.size(),
                           pref, space);
        p.blocked = sep.blocked;
        p.lateral_m = p.base_lateral_m + sep.lateral_target;

        if (p.mode == AgentMode::Analytic && !sep.blocked) {
            const PhantomState ph = phantom_ped(map_seed_, graph_->lane(p.lane),
                                                ped_sched_[p.lane], p.slot, step,
                                                ambient_);
            if (ph.dist_along_m + 1e-3f < p.last_dist_m) {
                p.mode = AgentMode::Integrating;
            } else {
                p.dist_along_m = ph.dist_along_m;
                p.speed_mps = ph.speed_mps;
                p.last_dist_m = ph.dist_along_m;
                const LanePose pose =
                    graph_->pose(p.lane, p.dist_along_m, p.lateral_m);
                p.pos = pose.position;
                p.fwd = pose.tangent;
                continue;
            }
        }

        // A blocked ped stops advancing, and a ped that has stopped is behind
        // its schedule for good. One way, like every other demotion here.
        p.mode = AgentMode::Integrating;
        if (!sep.blocked) p.dist_along_m += p.speed_mps * dt;

        const Lane& lane = graph_->lane(p.lane);
        if (p.dist_along_m >= lane.length_m) {
            const LaneRef nxt = graph_->choose_next(p.lane, map_seed_, p.slot);
            if (graph_->valid(nxt)) {
                p.dist_along_m -= lane.length_m;
                p.lane = nxt;
                p.base_lateral_m = (p.slot & 1u) ? -1.0f : 1.0f;
                p.base_lateral_m *= graph_->lane(nxt).width_m * 0.5f +
                                    ambient_.sidewalk_offset_m;
            } else {
                p.dist_along_m = lane.length_m;
            }
        }
        p.last_dist_m = p.dist_along_m;

        const LanePose pose = graph_->pose(p.lane, p.dist_along_m, p.lateral_m);
        p.pos = pose.position;
        p.fwd = pose.tangent;
    }
}

// ---------------------------------------------------------------------------
//  publish
// ---------------------------------------------------------------------------

namespace {

Transform agent_transform(const glm::vec3& pos, const glm::vec3& fwd) {
    Transform t;
    t.position = pos;
    // Transform::forward() is -Z, so the yaw that maps -Z onto `fwd` is
    // atan2(-fwd.x, -fwd.z). Getting the signs wrong here puts every car in
    // the city in reverse, which is exactly the sort of thing that looks like
    // a physics bug for a day.
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    t.rotation = glm::quat(glm::vec3{0.0f, yaw, 0.0f});
    return t;
}

const AABB kCarBounds{glm::vec3{-0.9f, 0.0f, -2.2f}, glm::vec3{0.9f, 1.5f, 2.2f}};
const AABB kPedBounds{glm::vec3{-0.3f, 0.0f, -0.3f}, glm::vec3{0.3f, 1.8f, 0.3f}};

}  // namespace

void Crowd::publish(Scene& scene) {
    if (!dead_nodes_.empty()) {
        scene.remove_many(dead_nodes_);
        dead_nodes_.clear();
    }
    for (VehicleAgent& v : vehicles_) {
        const Transform t = agent_transform(v.pos, v.fwd);
        if (v.node == kInvalidId) {
            Renderable r;
            r.mesh = 1;
            r.material = 1;
            v.node = scene.create(r, t, kCarBounds);
        } else {
            scene.set_transform(v.node, t);
        }
    }
    for (PedAgent& p : peds_) {
        const Transform t = agent_transform(p.pos, p.fwd);
        if (p.node == kInvalidId) {
            Renderable r;
            r.mesh = 2;
            r.material = 2;
            p.node = scene.create(r, t, kPedBounds);
        } else {
            scene.set_transform(p.node, t);
        }
    }
}

// ---------------------------------------------------------------------------
//  digests
// ---------------------------------------------------------------------------

uint64_t Crowd::population_hash() const {
    // THE LANE GOES IN AS ITS KEY, NEVER AS ITS LaneRef. A LaneRef is an index
    // into one build; hashing it makes this digest report a divergence every
    // time the spine table is reordered, which is precisely the case the
    // digest exists to prove is FINE. (It did exactly that, once, and the
    // failure read as "state diverged under reordering" for twenty minutes.)
    uint64_t h = 0xA5A5A5A5DEADBEEFull;
    for (const VehicleAgent& v : vehicles_) {
        h = mix_bits(h, v.lane_key);
        h = mix_bits(h, v.slot);
        h = mix_bits(h, static_cast<uint64_t>(v.mode));
        h = mix_bits(h, graph_ && graph_->valid(v.lane)
                            ? graph_->lane(v.lane).key
                            : 0xFFFFFFFFFFFFFFFFull);
        h = mix_f32(h, v.dist_along_m);
        h = mix_f32(h, v.speed_mps);
        h = mix_f32(h, v.pos.x);
        h = mix_f32(h, v.pos.y);
        h = mix_f32(h, v.pos.z);
    }
    for (const PedAgent& p : peds_) {
        h = mix_bits(h, p.lane_key);
        h = mix_bits(h, p.slot);
        h = mix_bits(h, static_cast<uint64_t>(p.mode));
        h = mix_bits(h, graph_ && graph_->valid(p.lane)
                            ? graph_->lane(p.lane).key
                            : 0xFFFFFFFFFFFFFFFFull);
        h = mix_f32(h, p.dist_along_m);
        h = mix_f32(h, p.lateral_m);
        h = mix_f32(h, p.pos.x);
        h = mix_f32(h, p.pos.y);
        h = mix_f32(h, p.pos.z);
    }
    return h;
}

uint64_t Crowd::membership_hash() const {
    uint64_t h = 0x1234567898765432ull;
    for (const VehicleAgent& v : vehicles_) {
        h = mix_bits(h, v.lane_key);
        h = mix_bits(h, v.slot);
    }
    for (const PedAgent& p : peds_) {
        h = mix_bits(h, p.lane_key ^ 0xFFull);
        h = mix_bits(h, p.slot);
    }
    return h;
}

}  // namespace apricot

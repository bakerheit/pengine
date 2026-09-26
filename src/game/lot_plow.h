#pragma once

// LOT PLOW CREWS. While snow is down, a few pickups with plows work the
// commercial lots: push a pass the length of the lot with the blade down,
// lift it, back up and over to the next pass, and push again, until the lot is
// done. Then they drop the blade and sit.
//
// Everything here is sim-side and a pure function of authored data, the run
// seed and the steps it is handed:
//   * which lots get a crew comes from hash_coord(seed, lot), never a stream;
//   * the passes come from the lot's authored pavement and its solid parts, so
//     a truck never plans a pass through a pump island or a wall;
//   * the truck is a kinematic bicycle stepped by dt, and it reads no clock.
//
// A truck stops for anything in the box its blade (or tailgate) is about to
// sweep: people, cars, the player. It waits there; a pass that stays blocked
// for long enough is abandoned rather than pushed through somebody.
//
// What it does NOT do, stated so nobody has to find out: trucks are kinematic
// and do not respond to being rammed (the player bounces off a moving
// obstacle), there is no driver in the cab, and a finished lot is not replowed
// if a new storm fills it in the same session.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "city/building_access.h"
#include "core/rng.h"
#include "game/plow_blade.h"
#include "game/snow_clearance.h"
#include "physics/terrain_collider.h"

namespace apricot {

struct LotPlowPass {
    glm::vec2 start{0.0f};  // blade edge, world xz
    glm::vec2 end{0.0f};
};

struct LotPlowPlan {
    const char* lot = nullptr;
    std::size_t lot_index = 0;
    std::vector<LotPlowPass> passes;
};

struct LotPlowObstacle {
    glm::vec2 xz{0.0f};
    float radius_m = 0.4f;
};

// Commercial lots a contractor plows: the downtown businesses whose buildings
// are authored as parts, so the planner can see what it must miss. Civic lots,
// the airport, homes and the imported restaurant lots (bare pavement records
// whose buildings live elsewhere, invisible to a planner) are not candidates.
inline bool is_lot_plow_candidate(const city::BuildingAccessLot& lot) {
    static constexpr const char* kLots[] = {"gas lot", "motel lot", "apartment lot",
        "restaurant lot", "bank parking lot", "laundry lot", "pawn lot", "bar lot",
        "gun store lot"};
    if (lot.use != city::BuildingAccessUse::PublicParking || lot.name == nullptr) return false;
    for (const char* name : kLots)
        if (std::strcmp(lot.name, name) == 0) return true;
    return false;
}

namespace lot_plow_detail {

inline glm::vec2 site_to_world(const city::StartSite& s, glm::vec2 p) {
    return {s.origin.x + s.cos_yaw * p.x + s.sin_yaw * p.y,
            s.origin.z - s.sin_yaw * p.x + s.cos_yaw * p.y};
}

struct Rect {
    glm::vec2 lo;
    glm::vec2 hi;
};

// Footprints in site space a truck cannot drive through or over: every solid
// part a truck would strike (walls, pumps, columns), and every raised kerb,
// however it is flagged. A pump island is authored non-solid, because a person
// steps onto it, but it is a 25 cm kerb and no plow driver mounts one; it
// cost a truck plowing straight across the pumps before this said so. Flat
// paint, roofs overhead and the lot's own pavement do not count.
inline std::vector<Rect> lot_obstacles(const city::BuildingAccessLot& lot) {
    std::vector<Rect> out;
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    for (const auto& part : lot.parts) {
        const float top = part.bottom_m + part.height_m;
        const bool struck = part.solid && part.height_m >= 0.12f && part.bottom_m <= 2.4f;
        const bool kerb = !part.solid && part.bottom_m <= 0.35f && top >= 0.15f &&
                          part.width_m * part.depth_m < 400.0f;
        if (!struck && !kerb) continue;
        if (part.name != nullptr && lot.name != nullptr && std::strcmp(part.name, lot.name) == 0) continue;
        const float c = std::fabs(std::cos(part.yaw_deg * kDegToRad));
        const float s = std::fabs(std::sin(part.yaw_deg * kDegToRad));
        const glm::vec2 half{0.5f * (c * part.width_m + s * part.depth_m),
                             0.5f * (s * part.width_m + c * part.depth_m)};
        const glm::vec2 centre{part.centre.x, part.centre.z};
        out.push_back({centre - half, centre + half});
    }
    return out;
}

}  // namespace lot_plow_detail

// THE PLAN: the biggest clear rectangle of the lot, plowed in lanes.
//
// Lanes run the lot's long way, one swath apart with a hand's width of
// overlap. For each lane the planner finds every stretch whose whole swept
// corridor (truck and blade, with room to spare) misses every solid part. It
// then takes the run of ADJACENT lanes whose clear stretches share the longest
// common span, and plows exactly that rectangle. Two things follow from that
// choice and both were learned the hard way in lot_plow_tests: a truck only
// ever backs over to the lane next door, so it never reverses across a
// building to reach a lane on the far side of it; and every lane's run covers
// the next one's, so the diagonal back-up between them stays in cleared,
// obstacle-free pavement. A lot too cluttered to hold a truck-and-a-half of
// run in two adjacent lanes gets no plan.
inline LotPlowPlan plan_lot_plow(const city::BuildingAccessLot& lot, std::size_t lot_index,
                                 const LotPlowTruckSpec& truck, bool push_positive) {
    using namespace lot_plow_detail;
    LotPlowPlan plan;
    plan.lot = lot.name;
    plan.lot_index = lot_index;
    const glm::vec2 centre{lot.pavement.centre.x, lot.pavement.centre.z};
    const bool along_x = lot.pavement.width_m >= lot.pavement.depth_m;
    const float length = along_x ? lot.pavement.width_m : lot.pavement.depth_m;
    const float breadth = along_x ? lot.pavement.depth_m : lot.pavement.width_m;
    const glm::vec2 axis = along_x ? glm::vec2{1, 0} : glm::vec2{0, 1};
    const glm::vec2 across = along_x ? glm::vec2{0, 1} : glm::vec2{1, 0};
    const auto obstacles = lot_obstacles(lot);
    const float corridor = std::max(truck.half_width_m, truck.blade.half_width_m) + 0.45f;
    const float swath = 2.0f * truck.blade.half_width_m - 0.30f;
    const float reach = truck.blade.edge_forward_m + 0.25f;  // edge ahead of the origin
    const float tail = truck.rear_m + 0.4f;
    const float margin = 0.6f;  // stay off the pavement edge and its kerb
    const float min_run = 2.5f * (reach + tail);
    const int lanes = std::max(0, static_cast<int>((breadth - 2.0f * corridor) / swath) + 1);
    using Spans = std::vector<glm::vec2>;
    std::vector<Spans> free(static_cast<std::size_t>(lanes));
    const auto offset_of = [&](int l) {
        return -0.5f * breadth + corridor + static_cast<float>(l) * swath;
    };
    for (int l = 0; l < lanes; ++l) {
        const float offset = offset_of(l);
        Spans blocked;
        for (const auto& r : obstacles) {
            const float a0 = glm::dot(r.lo - centre, across), a1 = glm::dot(r.hi - centre, across);
            if (std::min(a0, a1) > offset + corridor || std::max(a0, a1) < offset - corridor) continue;
            const float b0 = glm::dot(r.lo - centre, axis), b1 = glm::dot(r.hi - centre, axis);
            blocked.push_back({std::min(b0, b1), std::max(b0, b1)});
        }
        std::sort(blocked.begin(), blocked.end(),
                  [](glm::vec2 p, glm::vec2 q) { return p.x < q.x || (p.x == q.x && p.y < q.y); });
        float cursor = -0.5f * length + margin;
        const float limit = 0.5f * length - margin;
        Spans& spans = free[static_cast<std::size_t>(l)];
        for (const auto& b : blocked) {
            if (b.x > cursor) spans.push_back({cursor, std::min(b.x, limit)});
            cursor = std::max(cursor, b.y);
        }
        if (cursor < limit) spans.push_back({cursor, limit});
    }
    const auto intersect = [](const Spans& a, const Spans& b) {
        Spans out;
        for (const auto& p : a)
            for (const auto& q : b) {
                const float lo = std::max(p.x, q.x), hi = std::min(p.y, q.y);
                if (hi > lo) out.push_back({lo, hi});
            }
        return out;
    };
    const auto longest = [](const Spans& spans) {
        glm::vec2 best{0.0f, -1.0f};
        for (const auto& p : spans)
            if (p.y - p.x > best.y - best.x) best = p;
        return best;
    };
    int best_first = 0, best_count = 0;
    glm::vec2 best_run{0.0f, -1.0f};
    for (int first = 0; first < lanes; ++first) {
        Spans common = free[static_cast<std::size_t>(first)];
        for (int last = first; last < lanes; ++last) {
            if (last > first) common = intersect(common, free[static_cast<std::size_t>(last)]);
            const glm::vec2 run = longest(common);
            if (run.y - run.x < min_run) break;
            const int count = last - first + 1;
            if (static_cast<float>(count) * (run.y - run.x) >
                static_cast<float>(best_count) * (best_run.y - best_run.x)) {
                best_first = first;
                best_count = count;
                best_run = run;
            }
        }
    }
    if (best_count < 2) return plan;
    for (int l = best_first; l < best_first + best_count; ++l) {
        const float offset = offset_of(l);
        const float start = push_positive ? best_run.x + tail + reach : best_run.y - tail - reach;
        const float end = push_positive ? best_run.y - 0.2f : best_run.x + 0.2f;
        const glm::vec2 s = centre + axis * start + across * offset;
        const glm::vec2 e = centre + axis * end + across * offset;
        plan.passes.push_back({site_to_world(lot.site, s), site_to_world(lot.site, e)});
    }
    return plan;
}

// One kinematic plow truck working one plan.
struct LotPlowTruck {
    enum class Phase : uint8_t { Pushing, Lifting, Backing, Lowering, Done };

    LotPlowPlan plan;
    LotPlowTruckSpec spec;
    std::size_t model = 0;     // which of the specs plan() was handed
    glm::vec3 position{0.0f};  // chassis origin, on its springs
    float heading = 0.0f;      // forward is (-sin, 0, -cos), as VehicleState
    float speed_mps = 0.0f;    // signed: negative backing up
    float steer_rad = 0.0f;
    float wheel_spin = 0.0f;
    PlowBladeState blade;
    PlowSweep sweep;
    Phase phase = Phase::Pushing;
    std::size_t pass = 0;
    float blocked_s = 0.0f;
    bool blocked = false;
    std::size_t collider = static_cast<std::size_t>(-1);

    glm::vec3 forward() const { return {-std::sin(heading), 0.0f, -std::cos(heading)}; }
    glm::quat orientation() const { return glm::angleAxis(heading, glm::vec3{0, 1, 0}); }
    bool working() const { return phase != Phase::Done; }
    glm::vec3 edge() const {
        return position + orientation() * spec.blade.edge_local(blade.raised);
    }
};

// Tuning the crew drives with. Walking pace in a lot, a little slower backing.
struct LotPlowTuning {
    float push_mps = 2.6f;
    float back_mps = 2.0f;
    float accel_mps2 = 1.2f;
    float brake_mps2 = 3.0f;
    float lookahead_m = 4.5f;
    float give_up_s = 8.0f;          // abandon a pass blocked this long
    float start_depth_m = 0.03f;     // crews come out once there is this much down
    std::size_t crews = 3;
};

class LotPlowCrew {
public:
    // Pick crews and plan their lots. candidates are indices into lots; the
    // session seed decides which of them get a truck and which way each lot
    // is pushed. Crew k drives specs[k % specs.size()].
    void plan(const std::vector<city::BuildingAccessLot>& lots,
              const std::vector<std::size_t>& candidates, uint64_t seed,
              const std::vector<LotPlowTruckSpec>& specs, const LotPlowTuning& tuning = {}) {
        tuning_ = tuning;
        trucks_.clear();
        struct Pick { uint64_t key; std::size_t lot; };
        std::vector<Pick> picks;
        for (const std::size_t index : candidates) {
            if (index >= lots.size()) continue;
            picks.push_back({hash_coord(seed ^ 0x504C4F57435245ull, static_cast<int32_t>(index), 17), index});
        }
        std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
            return a.key != b.key ? a.key < b.key : a.lot < b.lot;
        });
        for (const auto& pick : picks) {
            if (trucks_.size() >= tuning_.crews || specs.empty()) break;
            LotPlowTruck truck;
            truck.model = trucks_.size() % specs.size();
            truck.spec = specs[truck.model];
            truck.plan = plan_lot_plow(lots[pick.lot], pick.lot, truck.spec, (pick.key >> 32) & 1u);
            if (truck.plan.passes.size() < 2) continue;
            trucks_.push_back(std::move(truck));
        }
        dispatched_ = false;
    }

    const std::vector<LotPlowTruck>& trucks() const { return trucks_; }
    std::vector<LotPlowTruck>& trucks() { return trucks_; }
    bool dispatched() const { return dispatched_; }

    // Put every truck at the start of its first pass, on the ground.
    void dispatch(const TerrainCollider& ground) {
        for (auto& truck : trucks_) {
            const LotPlowPass& first = truck.plan.passes.front();
            const glm::vec2 dir = glm::normalize(first.end - first.start);
            truck.heading = std::atan2(-dir.x, -dir.y);
            const glm::vec2 origin = first.start - dir * truck.spec.blade.edge_forward_m;
            truck.position = {origin.x, ground_y(ground, origin) + truck.spec.ride_height_m, origin.y};
            truck.phase = LotPlowTruck::Phase::Pushing;
            truck.pass = 0;
            truck.speed_mps = 0.0f;
            truck.blade = {};
            truck.sweep.reset();
        }
        dispatched_ = true;
    }

    // One sim step. depth_m is the lying snow; obstacles are everything a
    // truck must not drive into this step.
    void step(float dt, float depth_m, const TerrainCollider& ground,
              const std::vector<LotPlowObstacle>& obstacles, SnowClearanceField& field) {
        if (!(dt > 0.0f) || !std::isfinite(dt)) return;
        if (!dispatched_) {
            if (depth_m < tuning_.start_depth_m || trucks_.empty()) return;
            dispatch(ground);
        }
        for (auto& truck : trucks_) step_truck(truck, dt, depth_m, ground, obstacles, field);
    }

private:
    // The paved surface under a point. Probed from just above the terrain so
    // a canopy or roof overhead is never mistaken for the ground.
    static float ground_y(const TerrainCollider& ground, glm::vec2 xz) {
        const float terrain = ground.height(xz.x, xz.y);
        const TerrainCollider::GroundHit hit = ground.probe_down(
            {xz.x, terrain + 1.2f, xz.y}, 3.0f, TerrainCollider::ProbeVehicles::Exclude);
        return hit.hit ? hit.point.y : terrain;
    }

    // The box a truck is about to sweep, forward from the blade or back from
    // the tailgate: a stopping distance deep, but never past where this move
    // ends, so people on the pavement beyond the lot do not stall a push.
    bool path_blocked(const LotPlowTruck& truck, bool backwards, float remaining_m,
                      const std::vector<LotPlowObstacle>& obstacles) const {
        const glm::vec3 f3 = truck.forward();
        const glm::vec2 fwd{f3.x, f3.z};
        const glm::vec2 right{-fwd.y, fwd.x};
        const glm::vec2 at{truck.position.x, truck.position.z};
        const float near = backwards ? -truck.spec.rear_m : truck.spec.blade.edge_forward_m + 0.2f;
        const float depth = std::clamp(remaining_m + 0.8f, 0.8f, 3.5f);
        const float far = near + (backwards ? -depth : depth);
        const float half = (backwards ? truck.spec.half_width_m : truck.spec.blade.half_width_m) + 0.5f;
        for (const auto& o : obstacles) {
            const glm::vec2 d = o.xz - at;
            const float along = glm::dot(d, fwd);
            const float side = glm::dot(d, right);
            const float lo = std::min(near, far) - o.radius_m, hi = std::max(near, far) + o.radius_m;
            if (along >= lo && along <= hi && std::fabs(side) <= half + o.radius_m) return true;
        }
        return false;
    }

    void drive(LotPlowTruck& truck, float dt, glm::vec2 target, float want_mps, bool backwards) {
        const glm::vec2 at{truck.position.x, truck.position.z};
        const glm::vec3 f3 = truck.forward();
        glm::vec2 facing{f3.x, f3.z};
        if (backwards) facing = -facing;
        const glm::vec2 to = target - at;
        float turn = 0.0f;
        if (glm::length(to) > 1e-3f) {
            const glm::vec2 want = glm::normalize(to);
            turn = std::atan2(facing.x * want.y - facing.y * want.x, glm::dot(facing, want));
        }
        // Pure pursuit on the travel direction: curvature 2 sin(a) / L.
        const float distance = std::max(glm::length(to), 0.5f);
        float curvature = 2.0f * std::sin(turn) / distance;
        const float max_curvature = 1.0f / truck.spec.min_turn_radius_m;
        curvature = std::clamp(curvature, -max_curvature, max_curvature);
        const float step_speed = want_mps - std::fabs(truck.speed_mps);
        const float rate = step_speed >= 0.0f ? tuning_.accel_mps2 : tuning_.brake_mps2;
        const float magnitude = std::max(0.0f, std::fabs(truck.speed_mps) +
            std::clamp(step_speed, -rate * dt, rate * dt));
        truck.speed_mps = backwards ? -magnitude : magnitude;
        // Heading changes by curvature * distance travelled; a reversing truck
        // turns its tail toward the target, which is the same sign flip.
        // The facing vector (x, z) turns by +turn when heading decreases.
        truck.heading -= curvature * magnitude * dt;
        const glm::vec3 fwd = truck.forward();
        truck.position += fwd * (truck.speed_mps * dt);
        truck.steer_rad = std::atan(curvature * truck.spec.wheelbase_m) * (backwards ? -1.0f : 1.0f);
        truck.wheel_spin += truck.speed_mps * dt / 0.4f;
    }

    void step_truck(LotPlowTruck& truck, float dt, float depth_m, const TerrainCollider& ground,
                    const std::vector<LotPlowObstacle>& obstacles, SnowClearanceField& field) {
        using Phase = LotPlowTruck::Phase;
        if (truck.phase != Phase::Done && depth_m < SnowClearanceField::kResidualDepth) {
            // The snow has gone. Nothing left to push: drop the blade and sit.
            truck.phase = Phase::Done;
        }
        const LotPlowPass& pass = truck.plan.passes[std::min(truck.pass, truck.plan.passes.size() - 1)];
        const glm::vec2 dir = glm::normalize(pass.end - pass.start);
        const glm::vec2 at{truck.position.x, truck.position.z};
        truck.blocked = false;
        switch (truck.phase) {
            case Phase::Pushing: {
                truck.blade.lowered = true;
                const glm::vec2 edge_xz = at + glm::vec2{truck.forward().x, truck.forward().z} *
                                                   truck.spec.blade.edge_forward_m;
                const float remaining = glm::dot(pass.end - edge_xz, dir);
                truck.blocked = path_blocked(truck, false, remaining, obstacles);
                if (remaining <= 0.05f) {
                    stop(truck, dt);
                    if (std::fabs(truck.speed_mps) < 0.02f) truck.phase = Phase::Lifting;
                    break;
                }
                if (truck.blocked) {
                    stop(truck, dt);
                    truck.blocked_s += dt;
                    if (truck.blocked_s > tuning_.give_up_s) {
                        truck.blocked_s = 0.0f;
                        truck.phase = Phase::Lifting;
                    }
                    break;
                }
                truck.blocked_s = 0.0f;
                // Chase a point on the lane ahead of the truck's projection.
                const float along = glm::dot(at - pass.start, dir);
                const glm::vec2 target = pass.start + dir * (along + tuning_.lookahead_m +
                                                             truck.spec.blade.edge_forward_m);
                const float want = std::min(tuning_.push_mps, std::max(0.4f, remaining * 1.2f));
                if (truck.blade.scraping()) drive(truck, dt, target, want, false);
                else stop(truck, dt);
                break;
            }
            case Phase::Lifting:
                truck.blade.lowered = false;
                stop(truck, dt);
                if (truck.blade.raised >= 1.0f) {
                    if (truck.pass + 1 >= truck.plan.passes.size()) truck.phase = Phase::Done;
                    else {
                        ++truck.pass;
                        truck.phase = Phase::Backing;
                    }
                }
                break;
            case Phase::Backing: {
                truck.blade.lowered = false;
                // Back up the NEXT lane: steer the tail onto its line while
                // reversing, the way a driver backs into position, so the
                // truck is square to the lane when the blade drops and the
                // push is one straight strip, not a wiggle of short ones.
                const float along = glm::dot(at - pass.start, dir);
                const float goal = -truck.spec.blade.edge_forward_m;
                truck.blocked = path_blocked(truck, true, along - goal, obstacles);
                if (along <= goal + 0.05f) {
                    stop(truck, dt);
                    if (std::fabs(truck.speed_mps) < 0.02f) truck.phase = Phase::Lowering;
                    break;
                }
                if (truck.blocked) {
                    stop(truck, dt);
                    break;
                }
                const glm::vec2 target = pass.start + dir * std::max(along - tuning_.lookahead_m, goal - 0.5f);
                const float want = std::min(tuning_.back_mps, std::max(0.4f, (along - goal) * 1.0f));
                drive(truck, dt, target, want, true);
                break;
            }
            case Phase::Lowering:
                truck.blade.lowered = true;
                stop(truck, dt);
                if (truck.blade.scraping()) truck.phase = Phase::Pushing;
                break;
            case Phase::Done:
                truck.blade.lowered = true;
                stop(truck, dt);
                break;
        }
        truck.blade.step(dt);
        const glm::vec2 xz{truck.position.x, truck.position.z};
        truck.position.y = ground_y(ground, xz) + truck.spec.ride_height_m;
        truck.sweep.step(truck.edge(), truck.speed_mps,
                         truck.phase != Phase::Done && truck.blade.scraping(),
                         truck.spec.blade.half_width_m, field, depth_m);
    }

    void stop(LotPlowTruck& truck, float dt) {
        const float magnitude = std::max(0.0f, std::fabs(truck.speed_mps) - tuning_.brake_mps2 * dt);
        truck.speed_mps = truck.speed_mps < 0.0f ? -magnitude : magnitude;
        truck.position += truck.forward() * (truck.speed_mps * dt);
        truck.wheel_spin += truck.speed_mps * dt / 0.4f;
    }

    LotPlowTuning tuning_;
    std::vector<LotPlowTruck> trucks_;
    bool dispatched_ = false;
};

}  // namespace apricot

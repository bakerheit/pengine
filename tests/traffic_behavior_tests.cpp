#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

#include "app/road_fixture_layout.h"
#include "app/road_sign_mesh.h"
#include "city/map.h"
#include "city/spines.h"
#include "terrain/heightmap.h"
#include "traffic/crowd.h"
#include "test_assert.h"

using namespace apricot;
namespace {
constexpr uint64_t kSeed = 523;
struct Tee {
    RoadGraph roads;
    LaneGraph lanes;
    LaneRef minor = kInvalidLane, main = kInvalidLane;
    uint32_t junction = 0;
    explicit Tee(RoadClass side = RoadClass::Street, bool reverse = false) {
        RoadSpine through; through.id = 1; through.cls = RoadClass::Street;
        through.points = {{-240, 0}, {240, 0}};
        RoadSpine spur; spur.id = 2; spur.cls = side;
        spur.points = {{0, 0}, {0, 180}};
        std::vector<RoadSpine> spines{through, spur};
        if (reverse) std::reverse(spines.begin(), spines.end());
        roads.build(spines, {}, {}); lanes.build(roads, {});
        for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
            const auto& lane = lanes.lane(r);
            if (glm::length(lanes.junction(lane.junction_to).pos) > 0.1f) continue;
            junction = lane.junction_to;
            const auto start = lanes.pose(r, 0).position;
            if (start.z > 100) minor = r;
            if (start.x < -100) main = r;
        }
        REQUIRE(lanes.valid(minor) && lanes.valid(main));
    }
};
VehicleAgent car(const LaneGraph& lanes, LaneRef lane, float distance_to_end,
                 float speed, TurnKind turn, uint64_t seed_value = kSeed) {
    VehicleAgent v;
    v.lane = lane; v.lane_key = lanes.lane(lane).key;
    v.dist_along_m = lanes.length(lane) - distance_to_end;
    v.last_dist_m = v.dist_along_m;
    v.speed_mps = speed; v.cruise_mps = 14;
    v.mode = AgentMode::Integrating;
    const auto pose = lanes.pose(lane, v.dist_along_m);
    v.pos = pose.position; v.fwd = pose.tangent;
    LaneRef exit = kInvalidLane;
    for (const auto& link : lanes.outgoing(lane)) if (link.kind == turn) exit = link.to;
    REQUIRE(lanes.valid(exit));
    bool selected = false;
    for (uint32_t d = 0; d < 1000; ++d) {
        if (lanes.choose_next(lane, seed_value, d) != exit) continue;
        v.decisions = d; selected = true; break;
    }
    REQUIRE(selected);
    return v;
}
void seed(Crowd& crowd, std::vector<VehicleAgent> cars) {
    std::sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) {
        return std::tie(a.lane_key, a.slot) < std::tie(b.lane_key, b.slot);
    });
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()) = std::move(cars);
    crowd.rebuild_buckets();
}
void step(Crowd& crowd, int64_t s) { crowd.rebuild_buckets(); crowd.step_vehicles(s); }
void controls_match_priority() {
    for (RoadClass side : {RoadClass::Street, RoadClass::Alley, RoadClass::Dirt}) {
        Tee n(side);
        REQUIRE(n.lanes.approach_control(n.main) == JunctionControl::None);
        REQUIRE(n.lanes.approach_control(n.minor) == (side == RoadClass::Street
            ? JunctionControl::Yield : JunctionControl::Stop));
        for (const auto& main : n.lanes.outgoing(n.main)) {
            if (main.kind == TurnKind::UTurn) continue;
            for (const auto& minor : n.lanes.outgoing(n.minor))
                REQUIRE(turn_priority_rank(main.priority) > turn_priority_rank(minor.priority));
        }
        Tee reversed(side, true);
        for (const auto& a : n.lanes.lanes()) for (const auto& b : reversed.lanes.lanes())
            if (a.key == b.key) REQUIRE(a.approach_control == b.approach_control);
    }
    apricot_test::pass("T geometry and road hierarchy give priority to the continuing road in either build order");
}
void rolling_yield_and_actual_stop() {
    for (RoadClass side : {RoadClass::Street, RoadClass::Alley}) {
        Tee n(side); CrowdTuning tuning; tuning.max_peds = 0;
        const float clear = traffic_junction_clearance(n.lanes, n.junction, tuning);
        Crowd crowd; crowd.build(n.lanes, kSeed, {}, tuning);
        seed(crowd, {car(n.lanes, n.minor, clear + 25, 8, TurnKind::Left)});
        bool crossed = false; int stopped = 0; float slowest = 100;
        for (int64_t s = 0; s < 2400; ++s) {
            step(crowd, s);
            const auto& v = crowd.vehicles().front();
            if (v.speed_mps < 0.35f) ++stopped;
            slowest = std::min(slowest, v.speed_mps);
            if (v.lane != n.minor && v.committed_junction != n.junction) {
                crossed = true; break;
            }
        }
        REQUIRE(crossed);
        if (side == RoadClass::Street) REQUIRE(slowest > 2.0f);
        else REQUIRE(stopped >= tuning.stop_dwell_steps);
    }
    apricot_test::pass("open yield approach rolls through; a side-road STOP still requires a complete dwell");
}
void gap_scan_and_clearance() {
    Tee n; CrowdTuning tuning; tuning.max_peds = 0;
    const float clear = traffic_junction_clearance(n.lanes, n.junction, tuning);
    for (float other_distance : {65.0f, 160.0f}) {
        Crowd crowd; crowd.build(n.lanes, kSeed, {}, tuning);
        Crowd reverse; reverse.build(n.lanes, kSeed, {}, tuning);
        auto minor = car(n.lanes, n.minor, clear + 0.1f, 0, TurnKind::Left);
        auto major = car(n.lanes, n.main, other_distance, 14, TurnKind::Straight);
        seed(crowd, {minor, major}); seed(reverse, {major, minor});
        bool minor_entered = false, major_entered = false;
        bool minor_cleared = false, major_cleared = false;
        int64_t minor_first = -1, major_first = -1;
        for (int64_t s = 0; s < 2400; ++s) {
            step(crowd, s); step(reverse, s);
            REQUIRE(crowd.population_hash() == reverse.population_hash());
            REQUIRE(crowd.stats().ai_collisions == 0u);
            for (const auto& v : crowd.vehicles()) {
                if (v.lane_key == minor.lane_key) {
                    if (v.committed_junction == n.junction && !minor_entered) {
                        minor_entered = true; minor_first = s;
                    }
                    if (minor_entered && v.committed_junction != n.junction) minor_cleared = true;
                } else {
                    if (v.committed_junction == n.junction && !major_entered) {
                        major_entered = true; major_first = s;
                    }
                    if (major_entered && v.committed_junction != n.junction) major_cleared = true;
                }
            }
            if (minor_cleared && major_cleared) break;
        }
        REQUIRE(minor_cleared && major_cleared);
        if (other_distance < 100) REQUIRE(major_first < minor_first);
        else REQUIRE(minor_first < major_first);
        std::printf("  priority car %.0fm away: minor entry %.2fs, major entry %.2fs, both clear, 0 contacts\n",
            other_distance, static_cast<double>(minor_first) / 120.0,
            static_cast<double>(major_first) / 120.0);
    }
    apricot_test::pass("yield sees fast priority traffic beyond 18m but uses an open gap; both movements clear deterministically");
}
void patience_keeps_safety() {
    const auto cautious = make_driver_profile(DriverProfileKind::Cautious);
    const auto impatient = make_driver_profile(DriverProfileKind::Impatient);
    const float calm = traffic_gap_margin_seconds(cautious, 0);
    const float waited = traffic_gap_margin_seconds(impatient, 30);
    REQUIRE(waited < calm); REQUIRE(waited >= 0.9f);
    const auto adapted = traffic_driver_after_wait(impatient, 30);
    REQUIRE(adapted.accel > impatient.accel);
    REQUIRE(adapted.headway < impatient.headway);
    REQUIRE(adapted.min_gap == impatient.min_gap);
    REQUIRE(adapted.brake == impatient.brake);
    REQUIRE(adapted.speed_mul == impatient.speed_mul);
    TrafficApproachView mine, other;
    mine.valid = other.valid = true; mine.priority = 0; other.priority = 3;
    mine.clearance_seconds = traffic_travel_seconds(30, 0, 4.5f, 8) + waited;
    other.eta_seconds = mine.clearance_seconds - 0.1f;
    REQUIRE(traffic_approach_yields(mine, other, false, 0.3f));
    other.eta_seconds = mine.clearance_seconds + 0.1f;
    REQUIRE(!traffic_approach_yields(mine, other, false, 0.3f));
    other.committed = true;
    REQUIRE(traffic_approach_yields(mine, other, false, 0.3f));
    REQUIRE_NEAR(traffic_travel_seconds(20, 10, 5, 10), 2, 0.0001);
    REQUIRE_NEAR(traffic_travel_seconds(10, 0, 5, 10), 2, 0.0001);
    apricot_test::pass("impatience narrows comfort gaps and quickens launch while preserving stopping and physical clearance");
}
void impact_caution_fades_without_changing_legal_limits() {
    const auto baseline = make_driver_profile(DriverProfileKind::Impatient);
    const auto shaken = traffic_driver_after_wait(baseline, 30.0f, 12.0f);
    const auto settling = traffic_driver_after_wait(baseline, 30.0f, 6.0f);
    const auto recovered = traffic_driver_after_wait(baseline, 30.0f, 0.0f);
    REQUIRE(shaken.headway > settling.headway);
    REQUIRE(settling.headway > recovered.headway);
    REQUIRE(shaken.accel < settling.accel);
    REQUIRE(settling.accel < recovered.accel);
    REQUIRE(shaken.min_gap == baseline.min_gap);
    REQUIRE(shaken.brake == baseline.brake);
    REQUIRE(shaken.speed_mul == baseline.speed_mul);
    REQUIRE(shaken.yellow_bias == baseline.yellow_bias);
    REQUIRE(shaken.patience_seconds == baseline.patience_seconds);
    REQUIRE(traffic_gap_margin_seconds(baseline, 30.0f, 12.0f) >
            traffic_gap_margin_seconds(baseline, 30.0f, 6.0f));
    REQUIRE(traffic_gap_margin_seconds(baseline, 30.0f, 6.0f) >
            traffic_gap_margin_seconds(baseline, 30.0f, 0.0f));
    REQUIRE_NEAR(recovered.headway,
        traffic_driver_after_wait(baseline, 30.0f).headway, 0.0001f);
    REQUIRE_NEAR(traffic_gap_margin_seconds(baseline, 30.0f, 0.0f),
        traffic_gap_margin_seconds(baseline, 30.0f), 0.0001f);
    const float ordinary_stop = traffic_comfort_stop_speed(25.0f, baseline);
    const float cautious_stop = traffic_comfort_stop_speed(25.0f, baseline, 12.0f);
    REQUIRE(cautious_stop < ordinary_stop);
    REQUIRE(cautious_stop > 0.0f);
    REQUIRE(traffic_comfort_stop_speed(0.0f, baseline, 12.0f) == 0.0f);
    REQUIRE_NEAR(traffic_comfort_stop_speed(25.0f, baseline, 0.0f),
                 ordinary_stop, 0.0001f);
    REQUIRE(traffic_pass_wait_credit(3.0f, 12.0f) <
            traffic_pass_wait_credit(3.0f, 6.0f));
    REQUIRE_NEAR(traffic_pass_wait_credit(3.0f, 0.0f), 3.0f, 0.0001f);
    apricot_test::pass("a driver eases off after impact, then smoothly returns to the waiting profile");
}
void everyday_driver_style_changes_braking_and_lane_choice() {
    const auto cautious = make_driver_profile(DriverProfileKind::Cautious);
    const auto normal = make_driver_profile(DriverProfileKind::Normal);
    const auto impatient = make_driver_profile(DriverProfileKind::Impatient);
    const auto aggressive = make_driver_profile(DriverProfileKind::AggressiveLite);
    REQUIRE(traffic_comfort_brake(cautious) < cautious.brake);
    REQUIRE(traffic_comfort_brake(normal) < normal.brake);
    REQUIRE(traffic_comfort_brake(aggressive) == aggressive.brake);
    REQUIRE(traffic_comfort_stop_speed(30.0f, cautious) <
            traffic_comfort_stop_speed(30.0f, normal));
    REQUIRE(traffic_comfort_stop_speed(30.0f, normal) <
            traffic_comfort_stop_speed(30.0f, impatient));
    REQUIRE(traffic_lane_change_wait(1.0f, cautious) >
            traffic_lane_change_wait(1.0f, normal));
    REQUIRE(traffic_lane_change_wait(1.0f, normal) >
            traffic_lane_change_wait(1.0f, impatient));
    REQUIRE(traffic_lane_change_gain(2.0f, cautious) >
            traffic_lane_change_gain(2.0f, normal));
    REQUIRE(traffic_lane_change_gain(2.0f, normal) >
            traffic_lane_change_gain(2.0f, aggressive));
    REQUIRE(traffic_lane_change_gain(0.0f, aggressive) >= 0.5f);
    apricot_test::pass("ordinary drivers brake and choose useful lane changes by profile without an impact");
}
void signs_match_the_authored_map() {
    TerrainGround ground{city::kMapSeed}; RoadGraph roads; LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler()); lanes.build(roads, ground.sampler());
    const auto fixtures = build_road_control_layouts(lanes);
    for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
        const auto& lane = lanes.lane(r);
        const auto junction = lanes.junction(lane.junction_to).pos;
        if (glm::distance(glm::vec2{junction.x, junction.z}, {820, 460}) > 0.1f) continue;
        REQUIRE(lanes.approach_control(r) == ((lane.key >> 32) == 127u
            ? JunctionControl::Yield : JunctionControl::None));
    }
    std::array<int, 3> count{};
    for (const auto& f : fixtures) {
        const auto& lane = lanes.lane(f.incoming);
        REQUIRE(f.control == lanes.approach_control(f.incoming));
        if (!road_control_pole_clear(lanes, lane.junction_to, f.pole_ground))
            std::printf("  blocked sign: spine %llu, junction %u, post %.2f %.2f\n",
                static_cast<unsigned long long>(lane.key >> 32), lane.junction_to,
                f.pole_ground.x, f.pole_ground.z);
        REQUIRE(road_control_pole_clear(lanes, lane.junction_to, f.pole_ground));
        const std::size_t kind = f.control == JunctionControl::Yield ? 2u : (f.all_way ? 1u : 0u);
        ++count[kind];
        const auto pose = lanes.pose(f.incoming,
            lanes.project_onto(f.incoming, {f.marking_centre.x, f.marking_centre.z}).dist_along_m);
        const auto facing = f.rotation * glm::vec3{0, 0, 1};
        REQUIRE(glm::dot(facing, pose.tangent) < -0.95f);
        REQUIRE_NEAR(glm::determinant(glm::mat3_cast(f.rotation)), 1, 0.0001);
        if (count[kind] <= 3) {
            const auto j = lanes.junction(lane.junction_to).pos;
            std::printf("  %s%s: spine %llu, junction %.2f %.2f, sign %.2f %.2f, facing %.2f %.2f\n",
                junction_control_name(f.control), f.all_way ? " all-way" : "",
                static_cast<unsigned long long>(lane.key >> 32), j.x, j.z,
                f.pole_ground.x, f.pole_ground.z, facing.x, facing.z);
        }
    }
    REQUIRE(count[0] > 0 && count[1] > 0 && count[2] > 0);
    std::printf("  authored controls: %d side-road stops, %d all-way stop approaches, %d yields\n",
                count[0], count[1], count[2]);
    for (std::size_t kind = 0; kind < 3; ++kind) {
        const auto meshes = make_road_sign_mesh(kind == 2, kind == 1);
        for (const auto& mesh : meshes) {
            REQUIRE(!mesh.vertices.empty() && !mesh.indices.empty());
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto& a = mesh.vertices[mesh.indices[i]];
                const auto& b = mesh.vertices[mesh.indices[i + 1]];
                const auto& c = mesh.vertices[mesh.indices[i + 2]];
                REQUIRE(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0);
            }
        }
    }
    const auto teeth = make_yield_marking();
    REQUIRE(teeth.indices.size() == 15u);
    apricot_test::pass("authored stop/yield signs match driver controls, have front-facing lettering and correct mesh winding");
}

void authored_approaches_clear() {
    TerrainGround ground{city::kMapSeed}; RoadGraph roads; LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler()); lanes.build(roads, ground.sampler());
    for (glm::vec2 focus : {glm::vec2{-263.58f, -386.76f},
                            glm::vec2{-1050, 110}, glm::vec2{-1300, -190}}) {
        uint32_t j = UINT32_MAX;
        for (uint32_t candidate = 0; candidate < lanes.junction_count(); ++candidate) {
            const auto pos = lanes.junction(candidate).pos;
            if (glm::distance(glm::vec2{pos.x, pos.z}, focus) < 0.1f) j = candidate;
        }
        REQUIRE(j != UINT32_MAX);
        CrowdTuning tuning; tuning.max_peds = 0;
        Crowd crowd; crowd.build(lanes, city::kMapSeed, {}, tuning);
        Crowd reversed; reversed.build(lanes, city::kMapSeed, {}, tuning);
        const float clear = traffic_junction_clearance(lanes, j, tuning);
        std::vector<VehicleAgent> cars;
        std::map<uint64_t, LaneRef> origins;
        for (LaneRef approach : lanes.junction(j).incoming) {
            const auto& links = lanes.outgoing(approach);
            REQUIRE(!links.empty());
            auto movement = std::find_if(links.begin(), links.end(), [](const auto& link) {
                return link.kind == TurnKind::Straight;
            });
            if (movement == links.end()) movement = links.begin();
            auto v = car(lanes, approach, std::min(lanes.length(approach) - 1.0f,
                clear + 12.0f), 4.0f, movement->kind, city::kMapSeed);
            v.profile = make_driver_profile(cars.size() % 2 == 0
                ? DriverProfileKind::Cautious : DriverProfileKind::Impatient);
            origins[v.lane_key] = approach; cars.push_back(v);
        }
        seed(crowd, cars); std::reverse(cars.begin(), cars.end()); seed(reversed, cars);
        std::map<uint64_t, bool> entered, cleared;
        std::map<uint64_t, int> waits;
        int longest = 0; int64_t final_step = 0; std::size_t contacts = 0;
        for (int64_t s = 0; s < 120 * 120; ++s) {
            step(crowd, s); step(reversed, s); final_step = s;
            REQUIRE(crowd.population_hash() == reversed.population_hash());
            contacts += crowd.stats().ai_collisions;
            for (const auto& v : crowd.vehicles()) {
                if (v.committed_junction == j) entered[v.lane_key] = true;
                if (entered[v.lane_key] && v.committed_junction != j)
                    cleared[v.lane_key] = true;
                const bool waiting = !cleared[v.lane_key] &&
                    v.lane == origins[v.lane_key] && v.speed_mps < 0.35f;
                waits[v.lane_key] = waiting ? waits[v.lane_key] + 1 : 0;
                longest = std::max(longest, waits[v.lane_key]);
            }
            if (std::all_of(origins.begin(), origins.end(), [&](const auto& origin) {
                return cleared[origin.first];
            })) break;
        }
        const auto cleared_count = std::count_if(cleared.begin(), cleared.end(),
            [](const auto& entry) { return entry.second; });
        std::printf("  authored %.2f %.2f (%s), seed %llu: %zu/%zu approaches clear in %.2fs, longest stop %.2fs, %zu contacts\n",
            focus.x, focus.y, junction_control_name(lanes.junction_control(j)),
            static_cast<unsigned long long>(city::kMapSeed),
            static_cast<std::size_t>(cleared_count), origins.size(),
            static_cast<double>(final_step) / 120.0, static_cast<double>(longest) / 120.0, contacts);
        REQUIRE(static_cast<std::size_t>(cleared_count) == origins.size());
        REQUIRE(contacts == 0u);
    }
    apricot_test::pass("every seeded approach clears the authored Briar/Tenth, Rimway Slip and Quay junctions");
}

void authored_streaming_order_is_stable() {
    TerrainGround ground{city::kMapSeed}; RoadGraph roads; LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler()); lanes.build(roads, ground.sampler());
    bool saw_waiting_driver = false;
    for (glm::vec2 focus : {glm::vec2{-263.58f, -386.76f}, glm::vec2{-1050, 110}}) {
        CrowdTuning tuning; tuning.max_peds = 0;
        CrowdTuning reverse_tuning = tuning; reverse_tuning.reverse_scan_order = true;
        Crowd a, b; a.build(lanes, city::kMapSeed, {}, tuning);
        b.build(lanes, city::kMapSeed, {}, reverse_tuning);
        for (int64_t s = 0; s < 3600; ++s) {
            if (s % tuning.refresh_every_steps == 0) {
                a.refresh(s, focus); b.refresh(s, focus);
            }
            step(a, s); step(b, s);
            REQUIRE(a.population_hash() == b.population_hash());
            for (const auto& v : a.vehicles())
                saw_waiting_driver = saw_waiting_driver || v.delay_seconds > 2.0f;
        }
    }
    REQUIRE(saw_waiting_driver);
    apricot_test::pass("30s of real-map stop and yield traffic stays bit-equal under reversed activation order, including patience state");
}
// THE CAR THAT SPUN ON THE SPOT.
//
// Ordinary lane-following traffic used to reverse its heading inside a single
// 120 Hz step -- 180 degrees, 21,600 deg/s. Not a turn taken badly: a snap. The
// agent was on one lane, not turning, not in a maneuver, four centimetres
// further along than it had been. Two things in the lane geometry did it, and
// both are geometry, not driving:
//
//   1. offset_polyline() offset each vertex independently, so at a corner
//      tighter than the lane is wide the inner vertices swapped order and the
//      centreline doubled back for a few centimetres.
//   2. pose() read its tangent straight off the polyline segment, which makes
//      the heading a STEP function -- crossing any shape point rotated the car
//      by the whole authored deflection at once, up to 132 degrees on spine 100.
//
// So this runs the REAL authored map, on the REAL draped ground, through the
// REAL Crowd, and watches what the cars' own published `fwd` does. Both numbers
// are printed, because the bound that matters is not "did it pass" but "how
// close is it": a car may corner briskly, it may not pirouette.
void authored_traffic_never_snaps_its_heading() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads; LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler());
    lanes.build(roads, ground.sampler());

    // The geometry first, because if the lanes are continuous the drivers are
    // reading a continuous thing. Walk every lane at 2 cm -- finer than any car
    // moves in one step, so a discontinuity cannot hide between samples.
    double worst_lane_turn = 0.0;
    LaneRef worst_lane = kInvalidLane;
    for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
        const float length = lanes.lane(r).length_m;
        glm::vec3 previous = lanes.pose(r, 0.0f).tangent;
        for (float d = 0.02f; d <= length; d += 0.02f) {
            const glm::vec3 t = lanes.pose(r, d).tangent;
            const double turn = std::acos(glm::clamp(
                glm::dot(glm::normalize(previous), glm::normalize(t)),
                -1.0f, 1.0f)) * 57.2957795;
            if (turn > worst_lane_turn) { worst_lane_turn = turn; worst_lane = r; }
            previous = t;
        }
    }
    std::printf("      worst lane heading turn per 2 cm: %.2f deg (lane %d)\n",
                worst_lane_turn, static_cast<int>(worst_lane));
    REQUIRE_MSG(worst_lane_turn < 12.0,
                "a lane's heading jumps within 2 cm of station", "lane geometry");

    // Now the cars. Identity is (lane key, slot) and BOTH get recycled, so a
    // retired departure and the fresh one that takes its pair look like one car
    // teleporting. Filter on travel: at 120 Hz nothing legitimately moves 0.5 m
    // in a step, and a car that did is a different car. Without this the suite
    // measures respawns and reports 9,000 deg/s of nothing.
    CrowdTuning tuning; tuning.max_peds = 0;
    Crowd crowd; crowd.build(lanes, city::kMapSeed, {}, tuning);
    std::map<std::pair<uint64_t, uint32_t>, std::pair<glm::vec3, glm::vec3>> seen;
    double worst_yaw = 0.0;
    int64_t samples = 0;
    for (glm::vec2 focus : {glm::vec2{-263.58f, -386.76f}, glm::vec2{-1050, 110},
                            glm::vec2{-1300, -190}}) {
        for (int64_t s = 0; s < 3600; ++s) {
            if (s % tuning.refresh_every_steps == 0) crowd.refresh(s, focus);
            step(crowd, s);
            for (const VehicleAgent& v : crowd.vehicles()) {
                const std::pair<uint64_t, uint32_t> id{v.lane_key, v.slot};
                const auto it = seen.find(id);
                if (it != seen.end() &&
                    glm::length(v.pos - it->second.second) < 0.5f) {
                    ++samples;
                    worst_yaw = std::max(worst_yaw, std::acos(glm::clamp(
                        glm::dot(glm::normalize(it->second.first),
                                 glm::normalize(v.fwd)), -1.0f, 1.0f)) * 57.2957795);
                }
                seen[id] = {v.fwd, v.pos};
            }
        }
    }
    std::printf("      worst traffic yaw: %.2f deg/step = %.0f deg/s "
                "over %lld steps of driving\n",
                worst_yaw, worst_yaw * 120.0, static_cast<long long>(samples));
    REQUIRE_MSG(samples > 100000, "the crowd barely drove, so this proved nothing",
                "coverage");
    // 25 deg/step is 3,000 deg/s: still brisk, and still nothing like the
    // 78-to-180 degrees a single step used to be able to produce. What is left
    // under this bound is a real defect and not this one -- see the suite notes.
    REQUIRE_MSG(worst_yaw < 25.0,
                "traffic snapped its heading inside one step", "crowd");
    apricot_test::pass("authored-map traffic turns its heading instead of snapping it");
}

} // namespace
int main() {
    controls_match_priority(); rolling_yield_and_actual_stop();
    gap_scan_and_clearance(); patience_keeps_safety();
    impact_caution_fades_without_changing_legal_limits();
    everyday_driver_style_changes_braking_and_lane_choice();
    signs_match_the_authored_map();
    authored_approaches_clear();
    authored_streaming_order_is_stable();
    authored_traffic_never_snaps_its_heading();
    return apricot_test::done("traffic_behavior_tests");
}

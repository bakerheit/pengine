// What a pedestrian does besides walk, and whether any of it reached the
// result by accident.
//
// The activity machine (traffic/crowd.h, PedActivity) is the first thing in
// this module that gives a person state which is neither their position nor
// their schedule, so it is the first thing that can leak instantiation order
// into the street. Every claim below is therefore paired with the thing that
// would make it vacuous:
//
//   * the states occur AND the population still moves (a city of statues
//     satisfies "Idling occurs" perfectly)
//   * the machine is scan-order free AND the states it produced were varied
//     (a population that is 100% Walking is trivially order-free)
//   * the nerve is a property of the person AND the two crowds compared had
//     wildly different spawn counters
//   * a knocked-over person gets up AND somebody was actually knocked over
//
// Everything here drives a REAL Crowd over a REAL LaneGraph built from a real
// RoadGraph. Nothing hand-sets an agent's state: if the machine cannot be
// driven into a state from outside, that state does not exist.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "city/map.h"
#include "city/spines.h"
#include "core/fixed_step.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "scene/scene.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

// One sim step, as a float. core/fixed_step.h holds it as a double because the
// accumulator needs the precision; everything here is float geometry.
constexpr float kStepSeconds = static_cast<float>(kSimDt);

namespace {

constexpr uint64_t kSeed = 0x50454421ull;

struct Net {
    RoadGraph roads;
    LaneGraph lanes;
};

void build(Net& net, int n, float pitch, bool reversed = false) {
    std::vector<RoadSpine> spines = make_grid_spines(n, pitch);
    if (reversed) std::reverse(spines.begin(), spines.end());
    net.roads.build(spines, RoadGraphParams{}, GroundSampler{});
    net.lanes.build(net.roads, GroundSampler{});
}

CrowdTuning walkable_tuning() {
    CrowdTuning t;
    // One district's worth of pavement held resident for the whole run, so
    // nobody is retired half way through a decision and the counts below are
    // about behaviour rather than about membership churn.
    t.ped_activate_m = 400.0f;
    t.ped_retire_m = 600.0f;
    t.refresh_every_steps = 8;
    return t;
}

// Somebody on their feet and walking, who has not been used yet. Chosen
// IMMEDIATELY before each encounter and never in a batch up front: an earlier
// encounter can knock a bystander over, and a person who is already on the
// floor when their own test starts makes the test report a knockdown that the
// run under test did not cause. That is exactly how the first draft of this
// file failed, and it failed only after an unrelated density change moved who
// was standing where.
const PedAgent* pick_walker(
    const Crowd& crowd,
    const std::vector<std::pair<uint64_t, uint32_t>>& used,
    ped_react::Disposition want, bool any_disposition) {
    for (const PedAgent& p : crowd.peds()) {
        if (p.activity != PedActivity::Walking) continue;
        if (!any_disposition && p.disposition != want) continue;
        if (std::find(used.begin(), used.end(),
                      std::make_pair(p.lane_key, p.slot)) != used.end())
            continue;
        return &p;
    }
    return nullptr;
}

// Where in the population is this identity now? The active set is sorted by
// identity and refresh() splices newcomers into it, so an index taken on one
// step means nothing on the next.
const PedAgent* find_ped(const Crowd& crowd, uint64_t key, uint32_t slot) {
    for (const PedAgent& p : crowd.peds())
        if (p.lane_key == key && p.slot == slot) return &p;
    return nullptr;
}

// A car pointed at a point, moving at `speed`. This is the same VehicleState
// step_vehicle() produces and the same one World hands the crowd; only the
// integration is replaced, because what is under test is what the CROWD does
// about a car, not what the car does.
VehicleState car_aimed_at(glm::vec3 from, glm::vec3 at, float speed) {
    VehicleState v;
    glm::vec3 delta = at - from;
    delta.y = 0.0f;
    const float len = glm::length(delta);
    const glm::vec3 fwd = len > 1e-4f ? delta / len : glm::vec3{0.0f, 0.0f, -1.0f};
    v.position = from;
    v.velocity = fwd * speed;
    // Transform::forward() is -Z, so the yaw that maps -Z onto `fwd` is
    // atan2(-fwd.x, -fwd.z) — the identical convention Crowd::publish uses.
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    v.orientation = glm::quat(glm::vec3{0.0f, yaw, 0.0f});
    return v;
}

// ---------------------------------------------------------------------------
//  1. the states exist, and the street still works
// ---------------------------------------------------------------------------

void quiet_street_produces_idling_and_kerb_waiting() {
    Net net;
    build(net, 4, 62.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;  // nobody to wait for but the kerb itself
    const CrowdTuning tune = walkable_tuning();

    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, tune);
    Scene scene;
    crowd.refresh(0, {93.0f, 93.0f});
    REQUIRE_MSG(crowd.peds().size() > 40, "the fixture spawned almost nobody",
                "vacuity");

    std::map<std::pair<uint64_t, uint32_t>, float> travelled;
    std::map<std::pair<uint64_t, uint32_t>, glm::vec3> last;
    for (const PedAgent& p : crowd.peds()) last[{p.lane_key, p.slot}] = p.pos;

    std::size_t saw_walking = 0, saw_idling = 0, saw_waiting = 0;
    int64_t worst_wait = 0;
    std::map<std::pair<uint64_t, uint32_t>, int64_t> waiting_run;

    for (int64_t step = 1; step <= 9000; ++step) {
        crowd.rebuild_buckets();
        crowd.step_peds(step);
        crowd.publish(scene);
        const CrowdStats& s = crowd.stats();
        saw_walking = std::max(saw_walking, s.peds_walking);
        saw_idling = std::max(saw_idling, s.peds_idling);
        saw_waiting = std::max(saw_waiting, s.peds_waiting);
        // Nothing in this run may put anybody into a panic state: there is no
        // player. If one of these ever moves, something is reading state it
        // was not handed.
        REQUIRE_MSG(s.peds_alarmed == 0 && s.peds_fleeing == 0 &&
                        s.peds_downed == 0,
                    "a pedestrian panicked with no player car in the world",
                    "spontaneous-panic");
        for (const PedAgent& p : crowd.peds()) {
            const auto id = std::make_pair(p.lane_key, p.slot);
            const auto it = last.find(id);
            if (it != last.end()) travelled[id] += glm::length(p.pos - it->second);
            last[id] = p.pos;
            int64_t& run = waiting_run[id];
            run = p.activity == PedActivity::Waiting ? run + 1 : 0;
            worst_wait = std::max(worst_wait, run);
        }
    }

    REQUIRE_MSG(saw_walking > 0, "nobody ever walked", "walking");
    REQUIRE_MSG(saw_idling > 0, "nobody ever stopped to loiter", "idling");
    REQUIRE_MSG(saw_waiting > 0, "nobody ever hesitated at a kerb", "waiting");

    // THE ANTI-VACUITY HALF. A city of statues satisfies every count above.
    // 9000 steps is 75 s; a 1.1 m/s walker covers 82 m of it, so anybody who
    // moved less than 20 m spent most of the run standing still.
    std::size_t stuck = 0;
    double total = 0.0;
    for (const auto& entry : travelled) {
        total += static_cast<double>(entry.second);
        if (entry.second < 20.0f) ++stuck;
    }
    REQUIRE_MSG(stuck == 0, "somebody spent 75 seconds going nowhere",
                "anti-vacuity");
    // And nobody may hold a kerb indefinitely. 9000 steps with no traffic
    // means only the hesitation and the signal can hold anyone, and both are
    // bounded — the signal by its own period.
    REQUIRE_MSG(worst_wait < tune.signal_period_steps,
                "somebody waited longer than a whole signal cycle on an empty "
                "road",
                "kerb-deadlock");

    std::printf("      peak %zu walking / %zu idling / %zu waiting of %zu; "
                "mean %.1f m in 75 s; worst kerb hold %lld steps\n",
                saw_walking, saw_idling, saw_waiting, crowd.peds().size(),
                total / static_cast<double>(travelled.size()),
                static_cast<long long>(worst_wait));
    pass("a quiet street idles and hesitates at kerbs without anybody freezing");
}

// ---------------------------------------------------------------------------
//  2. reacting to the player's car
// ---------------------------------------------------------------------------

// Drive a car at one identified person and report what they did about it.
struct Encounter {
    bool alarmed = false;
    bool fled = false;
    // `downed` is "went to the floor", either way. `killed` says which way.
    // The pair replaced a single flag that meant PedActivity::Downed, which
    // stopped covering the fast half of the speed range the moment being run
    // over at thirty miles an hour started killing people: the tests below
    // went quietly vacuous rather than failing, because a victim who died was
    // simply never recorded as hit.
    bool downed = false;
    bool killed = false;
    // Sampled on the step BEFORE the blow as well as after it. Both are needed
    // because a person can arrive at a knockdown already wounded — the car
    // under test has usually clipped them once on the way in — and a check
    // written against a full hundred points quietly stops meaning anything the
    // first time that happens.
    float health_before = kBodyHealth;
    float health_after = kBodyHealth;
    int64_t first_downed_step = -1;
    // Sampled on the step the knockdown landed, not at the end: by then the
    // car has driven on, which is the whole reason the crowd records them.
    glm::vec2 impact_dir_xz{0.0f};
    float impact_speed_mps = 0.0f;
    glm::vec2 car_travel_xz{0.0f};
    glm::vec2 car_to_victim_xz{0.0f};
};

// `stand_off` is the distance the car refuses to close inside, in metres. At
// zero the car runs the person over; above the knockdown radius it is a car
// bearing down and holding station, which is what separates "did they notice"
// from "were they hit". The car's VELOCITY is its approach speed throughout,
// including while it holds station, because that is what a person reads: a
// vehicle pointed at them and moving.
Encounter drive_at(Crowd& crowd, Scene& scene, uint64_t key, uint32_t slot,
                   float speed, int64_t steps, float stand_off) {
    Encounter e;
    const PedAgent* target = find_ped(crowd, key, slot);
    REQUIRE(target != nullptr);
    // Start well outside the threat range and approach along the ground.
    glm::vec3 car = target->pos - glm::normalize(glm::vec3{0.6f, 0.0f, 0.8f}) * 26.0f;
    car.y = target->pos.y;

    float before_step_health = kBodyHealth;
    {
        const PedAgent* at_start = find_ped(crowd, key, slot);
        if (at_start != nullptr) before_step_health = at_start->health;
    }
    for (int64_t step = 1; step <= steps; ++step) {
        target = find_ped(crowd, key, slot);
        REQUIRE_MSG(target != nullptr, "the person under test was retired",
                    "encounter");
        glm::vec3 aim = target->pos;
        aim.y = car.y;
        VehicleState player = car_aimed_at(car, aim, speed);
        crowd.rebuild_buckets();
        crowd.step_peds(step, &player);
        crowd.publish(scene);

        const PedAgent* now = find_ped(crowd, key, slot);
        REQUIRE(now != nullptr);
        if (now->activity == PedActivity::Alarmed) e.alarmed = true;
        const float health_was = before_step_health;
        before_step_health = now->health;
        if (now->activity == PedActivity::Fleeing) e.fled = true;
        if (ped_holds_impact_pose(now->activity) && !e.downed) {
            e.downed = true;
            e.killed = now->activity == PedActivity::Dead;
            e.health_before = health_was;
            e.health_after = now->health;
            e.first_downed_step = step;
            e.impact_dir_xz = now->impact_dir_xz;
            e.impact_speed_mps = now->impact_speed_mps;
            e.car_travel_xz = glm::vec2{player.velocity.x, player.velocity.z} /
                              std::max(speed, 1e-4f);
            e.car_to_victim_xz = glm::vec2{now->pos.x - player.position.x,
                                           now->pos.z - player.position.z};
        }

        glm::vec3 delta = aim - car;
        delta.y = 0.0f;
        const float d = glm::length(delta);
        if (d < 1e-4f) continue;
        // A negative advance backs the car off, so the stand-off is held
        // exactly even when the person walks into it.
        car += (delta / d) * std::min(speed * kStepSeconds, d - stand_off);
    }
    return e;
}

void a_car_bearing_down_alarms_then_panics_the_street() {
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});
    REQUIRE(crowd.peds().size() > 20);

    // Nine separate encounters, each with a different person, so the result is
    // about the machine and not about one lucky pavement.
    std::size_t alarmed = 0, fled = 0;
    std::size_t attempts = 0;
    std::vector<std::pair<uint64_t, uint32_t>> used;
    for (int i = 0; i < 9; ++i) {
        const PedAgent* victim = pick_walker(
            crowd, used, ped_react::Disposition::Coward, true);
        REQUIRE_MSG(victim != nullptr, "ran out of people on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);
        ++attempts;
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, 16.0f, 700, 3.0f);
        if (e.alarmed) ++alarmed;
        if (e.fled) ++fled;
        REQUIRE_MSG(!e.downed,
                    "the rig ran the person under test over while a 3 m "
                    "stand-off was supposed to hold it clear",
                    "rig");
    }
    REQUIRE_MSG(alarmed == attempts,
                "somebody did not even notice a car coming at 16 m/s",
                "alarm");
    REQUIRE_MSG(fled == attempts,
                "a startle never escalated into a run", "flee");
    std::printf("      %zu/%zu startled and %zu/%zu ran from a 16 m/s approach\n",
                alarmed, attempts, fled, attempts);
    pass("a car bearing down startles a pedestrian and then makes them run");
}

void nerve_decides_who_spooks_and_who_does_not() {
    // The discriminating case, and the reason the disposition is not
    // decoration: at a closing speed BETWEEN a coward's threshold and a
    // die-hard's, the same approach must split the street.
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});

    const PedLifeTuning life{};
    // Between coward (9 * 0.55 = 4.95 m/s) and die-hard (9 * 1.55 = 13.95).
    const float speed = 8.0f;
    REQUIRE(speed > life.panic.trigger_closing * life.coward_closing_mul);
    REQUIRE(speed < life.panic.trigger_closing * life.diehard_closing_mul);

    std::size_t coward_reacted = 0, coward_total = 0;
    std::size_t diehard_reacted = 0, diehard_total = 0;
    std::vector<std::pair<uint64_t, uint32_t>> used;
    for (int i = 0; i < 6; ++i) {
        const PedAgent* victim =
            pick_walker(crowd, used, ped_react::Disposition::Coward, false);
        REQUIRE_MSG(victim != nullptr, "ran out of cowards on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);
        ++coward_total;
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, speed, 700, 3.0f);
        if (e.alarmed || e.fled) ++coward_reacted;
    }
    for (int i = 0; i < 6; ++i) {
        const PedAgent* victim =
            pick_walker(crowd, used, ped_react::Disposition::DieHard, false);
        REQUIRE_MSG(victim != nullptr, "ran out of die-hards on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);
        ++diehard_total;
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, speed, 700, 3.0f);
        if (e.alarmed || e.fled) ++diehard_reacted;
    }
    REQUIRE_MSG(coward_reacted == coward_total,
                "a coward ignored a car closing at 8 m/s", "coward");
    REQUIRE_MSG(diehard_reacted == 0,
                "a die-hard panicked at a closing speed below its own "
                "threshold -- the disposition is not reaching the gate",
                "die-hard");
    std::printf("      at %.0f m/s closing: %zu/%zu cowards reacted, "
                "%zu/%zu die-hards did not\n",
                static_cast<double>(speed), coward_reacted, coward_total,
                diehard_total - diehard_reacted, diehard_total);
    pass("the hidden nerve decides who spooks, and it is the punch reaction's "
         "nerve");
}

void a_knocked_over_pedestrian_always_gets_up() {
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});
    REQUIRE(!crowd.peds().empty());

    const PedLifeTuning life{};
    std::size_t knocked = 0;
    int64_t worst_recovery = 0;
    std::vector<std::pair<uint64_t, uint32_t>> victims;
    {
        std::vector<std::pair<uint64_t, uint32_t>> used;
        for (int i = 0; i < 6; ++i) {
            const PedAgent* v = pick_walker(
                crowd, used, ped_react::Disposition::Coward, true);
            REQUIRE(v != nullptr);
            victims.emplace_back(v->lane_key, v->slot);
            used.push_back(victims.back());
        }
    }

    for (const auto& id : victims) {
        // Run them over at a SURVIVABLE speed, then take the car away and
        // wait. Eight metres a second is a shade under eighteen miles an hour;
        // vehicle_impact_damage() prices it at about a quarter of a life, so
        // there is somebody left to recover. This used to be 14 m/s, and at
        // fourteen there is now nobody to get up — which is a different test,
        // and it is the one directly below this.
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, 8.0f, 900, 0.0f);
        if (!e.downed) continue;
        REQUIRE_MSG(!e.killed, "the survivable speed under test was lethal",
                    "rig");
        REQUIRE_MSG(e.health_after > 0.0f && e.health_after < kBodyHealth,
                    "a knockdown that cost no health proves nothing about "
                    "damage", "rig");
        ++knocked;
        int64_t recovery = 0;
        for (int64_t step = 1; step <= life.downed_max_steps * 3; ++step) {
            crowd.rebuild_buckets();
            crowd.step_peds(step);  // no player: the car has gone
            const PedAgent* p = find_ped(crowd, id.first, id.second);
            REQUIRE(p != nullptr);
            if (!ped_is_floored(p->activity)) break;
            recovery = step;
        }
        const PedAgent* p = find_ped(crowd, id.first, id.second);
        REQUIRE(p != nullptr);
        REQUIRE_MSG(!ped_is_floored(p->activity),
                    "somebody stayed on the floor past three times the "
                    "longest authored recovery",
                    "stuck-down");
        worst_recovery = std::max(worst_recovery, recovery);
    }
    REQUIRE_MSG(knocked > 0, "nobody was knocked over, so nothing was tested",
                "vacuity");
    // Down, and then up: getting up is a second authored span and a body is
    // still on the floor for all of it.
    REQUIRE_MSG(worst_recovery <= life.downed_max_steps + life.rising_steps,
                "a recovery ran past the authored maximum", "bound");
    std::printf("      %zu knocked down, worst recovery %lld steps "
                "(authored max %lld)\n",
                knocked, static_cast<long long>(worst_recovery),
                static_cast<long long>(life.downed_max_steps +
                                       life.rising_steps));
    pass("a knocked-over pedestrian is down for a bounded, authored time and "
         "then gets up");
}

// ---------------------------------------------------------------------------
//  3. none of it depends on how the crowd was assembled
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  the knockdown footprint, and the blow it records
// ---------------------------------------------------------------------------

// A car is not a circle, and the circle it is not is centred most of a car
// length behind its bumper. This drives at a stand-off the NOSE reaches but
// the old 1.35 m centre-circle never could, and requires the person to go
// down; the negative half of the claim is
// a_car_bearing_down_alarms_then_panics_the_street(), which holds 3.0 m —
// outside the footprint by 0.25 m — and requires that nobody is touched.
void the_knockdown_reaches_as_far_as_the_bumper() {
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});
    REQUIRE(!crowd.peds().empty());

    const PedLifeTuning life{};
    // Dead ahead, past the bumper by 0.155 m and past the old circle by 1.25 m.
    const float reach = kPlayerHalfLengthM + life.body_radius_m;
    const float stand_off = kPlayerHalfLengthM + life.body_radius_m * 0.5f;
    REQUIRE_MSG(stand_off < reach,
                "the stand-off under test is outside the footprint, so a "
                "knockdown would prove nothing", "rig");
    REQUIRE_MSG(stand_off > 1.35f,
                "the stand-off under test is inside the centre circle this "
                "test exists to replace", "rig");

    const float approach = 14.0f;
    std::size_t knocked = 0;
    std::size_t attempts = 0;
    std::vector<std::pair<uint64_t, uint32_t>> used;
    for (int i = 0; i < 5; ++i) {
        const PedAgent* victim = pick_walker(
            crowd, used, ped_react::Disposition::Coward, true);
        REQUIRE_MSG(victim != nullptr, "ran out of people on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);
        ++attempts;
        const Encounter e = drive_at(crowd, scene, id.first, id.second,
                                     approach, 700, stand_off);
        if (!e.downed) continue;
        ++knocked;

        // The recorded blow. A ragdoll launches from these, so a direction
        // that is not unit length is a body thrown at the wrong speed, and a
        // direction that points back at the car is a body thrown up the road
        // into the vehicle that just hit it.
        REQUIRE_NEAR(glm::length(e.impact_dir_xz), 1.0f, 1e-3f);
        REQUIRE_NEAR(glm::dot(e.impact_dir_xz, e.car_travel_xz), 1.0f, 1e-3f);
        REQUIRE_MSG(glm::dot(e.impact_dir_xz, e.car_to_victim_xz) > 0.0f,
                    "the blow points back up the road instead of through the "
                    "person it hit", "impact-dir");
        REQUIRE_MSG(e.impact_speed_mps > 0.0f,
                    "a knockdown recorded no closing speed", "impact-speed");
        REQUIRE_MSG(e.impact_speed_mps <= approach + 1e-3f,
                    "the closing speed exceeds the speed the car was doing",
                    "impact-speed");
        // What the blow COST, checked against the blow itself. The damage is
        // priced on the recorded CLOSING speed and not on the speedometer,
        // which is the correct reading and the non-obvious one: a person
        // running away is hit at less than the car is doing, so a 14 m/s
        // approach at a fleeing coward lands somewhere well under 14. Tying
        // the assertion to `impact_speed_mps` is what keeps this honest
        // whichever way the victim happened to be moving.
        const float expected = vehicle_impact_damage(
            e.impact_speed_mps, life.knockdown_speed_mps);
        REQUIRE_MSG(e.killed == (e.health_before - expected <= 0.0f),
                    "the death and the damage curve disagree about this "
                    "impact", "lethal");
        REQUIRE_NEAR(e.health_after,
                     std::max(0.0f, e.health_before - expected), 1e-3);
    }
    REQUIRE_MSG(knocked == attempts,
                "the car's nose passed through somebody without touching "
                "them", "footprint");
    std::printf("      %zu/%zu knocked down at %.2f m stand-off "
                "(bumper reach %.2f m, old centre circle 1.35 m)\n",
                knocked, attempts, static_cast<double>(stand_off),
                static_cast<double>(reach));
    pass("the knockdown footprint reaches the bumper and records the blow");
}

// ---------------------------------------------------------------------------
//  where a struck body ends up
// ---------------------------------------------------------------------------

// A person hit at speed lands down the road, gets up THERE, and walks back.
//
// This exists because the first version did not do the middle part. The
// presentation ragdoll threw the body six metres, the crowd left the person on
// their lane the whole time, and the moment the downed timer expired they
// snapped back to the kerb to stand up. From the driver's seat it reads as the
// body teleporting, and it is the reason PedAgent carries impact_offset at all:
// where somebody lands is sim state, not something a render-clock solver may
// decide.
void a_struck_body_lands_down_the_road_and_walks_back() {
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});
    REQUIRE(!crowd.peds().empty());

    const PedLifeTuning life{};
    std::size_t hit = 0;
    float shortest_throw = 1e9f;
    float worst_leftover = 0.0f;
    std::vector<std::pair<uint64_t, uint32_t>> used;
    for (int i = 0; i < 5; ++i) {
        const PedAgent* victim = pick_walker(
            crowd, used, ped_react::Disposition::Coward, true);
        REQUIRE_MSG(victim != nullptr, "ran out of people on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);

        // Survivable, because the whole claim below is about where somebody
        // is standing when they GET UP. At 15 m/s, which this was, nobody
        // does.
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, 11.0f, 900, 0.0f);
        if (!e.downed) continue;
        REQUIRE_MSG(!e.killed, "the speed under test killed the person whose "
                    "get-up this test is about", "rig");
        ++hit;

        // Airborne or sliding, but definitely not where they were standing.
        const PedAgent* p = find_ped(crowd, id.first, id.second);
        REQUIRE(p != nullptr);
        float thrown = 0.0f;
        int64_t step = 1;
        for (; step <= life.downed_max_steps * 2; ++step) {
            crowd.rebuild_buckets();
            crowd.step_peds(step);  // the car has gone
            p = find_ped(crowd, id.first, id.second);
            REQUIRE(p != nullptr);
            thrown = std::max(thrown, glm::length(p->impact_offset));
            if (p->activity != PedActivity::Downed) break;
        }
        REQUIRE_MSG(p->activity != PedActivity::Downed,
                    "somebody never got up", "stuck-down");

        // THE CLAIM: they are still down the road at the moment they stand up.
        // A body that travelled and then snapped home would pass a
        // peak-distance check and fail this one.
        const float at_getup = glm::length(p->impact_offset);
        REQUIRE_MSG(at_getup > 1.5f,
                    "the body was back at its lane by the time it got up, "
                    "which is the teleport this test exists for",
                    "teleport");
        shortest_throw = std::min(shortest_throw, at_getup);
        REQUIRE_MSG(p->impact_offset.y >= 0.0f,
                    "a body came to rest below the pavement", "sink");

        // THE GET-UP DOES NOT SLIDE. Pushing yourself up off the road is not
        // also travelling across it, and before PedActivity::Rising existed
        // the crowd put people straight back to walking the moment the downed
        // timer expired — so the body slid along the pavement, and back
        // toward its lane, in a pose that is on its hands and knees.
        REQUIRE_MSG(p->activity == PedActivity::Rising,
                    "leaving Downed did not put this person into a get-up",
                    "rising");
        const glm::vec3 rose_at = p->pos;
        for (int64_t j = 1; j <= life.rising_steps; ++j) {
            crowd.rebuild_buckets();
            crowd.step_peds(step + j);
            p = find_ped(crowd, id.first, id.second);
            REQUIRE(p != nullptr);
            if (p->activity != PedActivity::Rising) break;
            REQUIRE_MSG(glm::length(p->pos - rose_at) < 1e-3f,
                        "somebody moved while getting up", "slide");
        }
        step += life.rising_steps;

        // ... and then walks it off, on its feet, in a bounded time.
        const int64_t recover =
            static_cast<int64_t>(at_getup / life.offset_recover_mps /
                                 kStepSeconds) + 240;
        for (int64_t j = 1; j <= recover; ++j) {
            crowd.rebuild_buckets();
            crowd.step_peds(step + j);
            p = find_ped(crowd, id.first, id.second);
            REQUIRE(p != nullptr);
            if (p->activity == PedActivity::Downed) break;  // hit again
        }
        worst_leftover = std::max(worst_leftover, glm::length(p->impact_offset));
    }

    REQUIRE_MSG(hit >= 3, "too few knockdowns to conclude anything",
                "vacuity");
    std::printf("      %zu thrown; shortest still %.2f m from the lane at "
                "get-up, %.3f m left after walking back\n",
                hit, static_cast<double>(shortest_throw),
                static_cast<double>(worst_leftover));
    REQUIRE_MSG(worst_leftover < 0.05f,
                "somebody never walked back to their path", "recover");
    pass("a struck body lands down the road, gets up there, and walks back");
}

// ---------------------------------------------------------------------------
//  and the other half: a body that does not get up
// ---------------------------------------------------------------------------
//
// The sibling of a_knocked_over_pedestrian_always_gets_up(), and the reason
// that one had to be re-pointed at a survivable speed. Before city/body_damage.h
// there was no speed at which running somebody over killed them: the crowd had
// one knockdown, it lasted between three and a half and seven and a half
// seconds, and at the end of it the victim of a forty mile an hour impact stood
// up and carried on walking to the shops.
void a_lethal_impact_leaves_a_body() {
    Net net;
    build(net, 3, 58.0f);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, walkable_tuning());
    Scene scene;
    crowd.refresh(0, {58.0f, 58.0f});
    REQUIRE(!crowd.peds().empty());
    const PedLifeTuning life{};

    std::size_t killed = 0;
    std::vector<std::pair<uint64_t, uint32_t>> used;
    for (int i = 0; i < 5 && killed < 3; ++i) {
        const PedAgent* victim = pick_walker(
            crowd, used, ped_react::Disposition::DieHard, true);
        REQUIRE_MSG(victim != nullptr, "ran out of people on their feet",
                    "vacuity");
        const std::pair<uint64_t, uint32_t> id{victim->lane_key, victim->slot};
        used.push_back(id);
        // A die-hard stands their ground, so the closing speed is the car's
        // speed rather than the car's speed minus a panic sprint. That is what
        // makes this the reliable way to reach the lethal end of the curve.
        const Encounter e =
            drive_at(crowd, scene, id.first, id.second, 26.0f, 900, 0.0f);
        if (!e.downed || !e.killed) continue;
        ++killed;
        REQUIRE(e.health_after == 0.0f);
        REQUIRE_MSG(vehicle_impact_damage(e.impact_speed_mps,
                                          life.knockdown_speed_mps) >=
                        e.health_before,
                    "a death the damage curve does not account for", "lethal");

        // DEAD IS ABSORBING. Three times the longest authored knockdown, plus
        // the get-up, is well past the point at which the old behaviour put
        // this person back on their feet.
        const glm::vec3 fell = find_ped(crowd, id.first, id.second)->pos;
        for (int64_t step = 1; step <= life.downed_max_steps * 3; ++step) {
            crowd.rebuild_buckets();
            crowd.step_peds(step);  // no player: the car has gone
            const PedAgent* p = find_ped(crowd, id.first, id.second);
            REQUIRE(p != nullptr);
            REQUIRE_MSG(p->activity == PedActivity::Dead,
                        "a body got up off the road", "terminal");
            REQUIRE(p->health == 0.0f);
            REQUIRE(p->speed_mps == 0.0f);
        }
        // It came to rest, down the road from where it was standing, and it
        // did NOT walk its impact offset off — that path is for the living.
        const PedAgent* p = find_ped(crowd, id.first, id.second);
        REQUIRE_MSG(glm::length(p->impact_offset) > 1.5f,
                    "a body thrown by a lethal impact crept back to its lane",
                    "teleport");
        REQUIRE(p->impact_offset.y >= 0.0f);
        REQUIRE_MSG(glm::distance(p->pos, fell) < 4.0f,
                    "a settled body kept travelling", "at rest");
    }
    REQUIRE_MSG(killed >= 3, "too few deaths to conclude anything", "vacuity");
    std::printf("      %zu killed outright; all still on the road three "
                "knockdowns later\n", killed);
    pass("a lethal impact leaves a body, and the body stays");
}

void the_activity_machine_is_free_of_scan_order() {
    // The claim the whole module rests on, extended to the new state. Two
    // crowds on ONE identical lane graph instantiate the same people in
    // opposite orders, meet the same car on the same steps, and must stay
    // bit-identical — activity, disposition, timers and all, because
    // population_hash() now folds every one of them in.
    Net net;
    build(net, 5, 62.0f);
    AmbientTuning ambient;
    CrowdTuning fwd = walkable_tuning();
    CrowdTuning rev = fwd;
    rev.reverse_scan_order = true;

    Crowd a, b;
    a.build(net.lanes, kSeed, ambient, fwd);
    b.build(net.lanes, kSeed, ambient, rev);
    Scene sa, sb;

    std::size_t seen_idling = 0, seen_waiting = 0, seen_alarmed = 0;
    std::size_t seen_fleeing = 0, seen_downed = 0;
    glm::vec3 car{40.0f, 0.0f, 62.0f};
    for (int64_t step = 0; step < 3000; ++step) {
        if (step % fwd.refresh_every_steps == 0) {
            a.refresh(step, {car.x, car.z});
            b.refresh(step, {car.x, car.z});
        }
        // A car sweeping the length of one street, pavements included.
        const float t = static_cast<float>(step) * kStepSeconds;
        car = glm::vec3{40.0f + std::fmod(t * 15.0f, 240.0f), 0.0f,
                        62.0f + std::sin(t * 0.9f) * 4.5f};
        const VehicleState player =
            car_aimed_at(car, car + glm::vec3{1.0f, 0.0f, 0.0f}, 15.0f);

        a.rebuild_buckets();
        b.rebuild_buckets();
        a.step_vehicles(step, &player);
        b.step_vehicles(step, &player);
        a.step_peds(step, &player);
        b.step_peds(step, &player);
        a.publish(sa);
        b.publish(sb);
        sa.update();
        sb.update();

        REQUIRE_MSG(a.membership_hash() == b.membership_hash(),
                    "the active SET depended on the scan order", "membership");
        REQUIRE_MSG(a.population_hash() == b.population_hash(),
                    "pedestrian ACTIVITY depended on the scan order", "state");
        const CrowdStats& s = a.stats();
        seen_idling = std::max(seen_idling, s.peds_idling);
        seen_waiting = std::max(seen_waiting, s.peds_waiting);
        seen_alarmed = std::max(seen_alarmed, s.peds_alarmed);
        seen_fleeing = std::max(seen_fleeing, s.peds_fleeing);
        seen_downed = std::max(seen_downed, s.peds_downed);
    }

    // THE ANTI-VACUITY HALF. A population that never left Walking is
    // order-free for reasons that have nothing to do with this machine.
    REQUIRE_MSG(seen_idling > 0 && seen_waiting > 0,
                "the run never exercised loitering or kerb waiting, so the "
                "equality above proves nothing about them",
                "vacuity");
    REQUIRE_MSG(seen_alarmed > 0 && seen_fleeing > 0,
                "the sweeping car never startled anybody, so the equality "
                "above proves nothing about the panic path",
                "vacuity");
    REQUIRE(a.stats().activated > 200);
    std::printf("      3000 steps bit-identical; peak %zu idling, %zu waiting, "
                "%zu alarmed, %zu fleeing, %zu downed\n",
                seen_idling, seen_waiting, seen_alarmed, seen_fleeing,
                seen_downed);
    pass("activity, nerve and every timer behind them are free of scan order");
}

void the_nerve_is_a_property_of_the_person_not_of_the_queue() {
    // The identical argument the driver profile gets, for the identical
    // reason: probablecause pulled its nerve off a stream, so the eighteenth
    // person spawned got the eighteenth draw.
    Net net;
    build(net, 8, 62.0f);
    AmbientTuning ambient;

    CrowdTuning small = walkable_tuning();
    small.ped_activate_m = 90.0f;
    small.ped_retire_m = 130.0f;
    CrowdTuning big = walkable_tuning();
    big.ped_activate_m = 700.0f;
    big.ped_retire_m = 900.0f;

    Crowd cs, cb;
    cs.build(net.lanes, kSeed, ambient, small);
    cb.build(net.lanes, kSeed, ambient, big);
    Scene ss, sb;
    for (int64_t step = 0; step < 900; ++step) {
        const float t = static_cast<float>(step) * kStepSeconds;
        const glm::vec2 focus{60.0f + t * 12.0f, 190.0f};
        if (step % 8 == 0) {
            cs.refresh(step, focus);
            cb.refresh(step, focus);
        }
        cs.rebuild_buckets();
        cb.rebuild_buckets();
        cs.step_peds(step);
        cb.step_peds(step);
        cs.publish(ss);
        cb.publish(sb);
    }

    REQUIRE_MSG(cb.stats().activated > cs.stats().activated + 400,
                "the two crowds spawned nearly the same number of people, so "
                "this proves nothing about stream position",
                "vacuity");

    std::size_t matched = 0;
    for (const PedAgent& a : cs.peds()) {
        const PedAgent* b = find_ped(cb, a.lane_key, a.slot);
        if (!b) continue;
        REQUIRE_MSG(a.disposition == b->disposition,
                    "the same person had a different nerve in a crowd that had "
                    "spawned more people first",
                    "nerve");
        ++matched;
    }
    REQUIRE_MSG(matched > 10, "the two crowds shared nobody", "vacuity");
    std::printf("      %zu shared people, same nerve after %zu vs %zu spawns\n",
                matched, cs.stats().activated, cb.stats().activated);
    pass("the hidden nerve comes off the person's identity, never off a spawn "
         "counter");
}

void the_nerve_splits_the_street_the_way_it_is_authored() {
    // Not a statistics test for its own sake: it is what proves the roll is
    // uniform and keyed rather than, say, always landing in one bucket because
    // the channel collided with something else.
    Net net;
    build(net, 10, 62.0f);
    AmbientTuning ambient;
    CrowdTuning tune = walkable_tuning();
    tune.ped_activate_m = 900.0f;
    tune.ped_retire_m = 1200.0f;
    Crowd crowd;
    crowd.build(net.lanes, kSeed, ambient, tune);
    crowd.refresh(0, {280.0f, 280.0f});
    REQUIRE_MSG(crowd.peds().size() > 2000,
                "too small a sample to say anything about the split",
                "vacuity");

    std::size_t coward = 0, waverer = 0, diehard = 0;
    for (const PedAgent& p : crowd.peds()) {
        switch (p.disposition) {
            case ped_react::Disposition::Coward: ++coward; break;
            case ped_react::Disposition::Waverer: ++waverer; break;
            case ped_react::Disposition::DieHard: ++diehard; break;
        }
    }
    const double n = static_cast<double>(crowd.peds().size());
    const double c = static_cast<double>(coward) / n;
    const double w = static_cast<double>(waverer) / n;
    const double d = static_cast<double>(diehard) / n;
    REQUIRE_NEAR(c, static_cast<double>(ped_react::COWARD_FRACTION), 0.04);
    REQUIRE_NEAR(w, static_cast<double>(ped_react::WAVERER_FRACTION), 0.04);
    REQUIRE_NEAR(d,
                 1.0 - static_cast<double>(ped_react::COWARD_FRACTION) -
                     static_cast<double>(ped_react::WAVERER_FRACTION),
                 0.04);
    std::printf("      %zu people: %.1f%% coward / %.1f%% waverer / "
                "%.1f%% die-hard\n",
                crowd.peds().size(), c * 100.0, w * 100.0, d * 100.0);
    pass("the nerve roll is uniform, so the authored disposition split is the "
         "split the street gets");
}

// ---------------------------------------------------------------------------
//  4. how many people there are, and where
// ---------------------------------------------------------------------------

void the_crowd_clumps_toward_corners_on_the_real_map() {
    // Measured on Pinatty, not on a grid fixture: every interior junction of a
    // regular grid has the same arity, so a grid cannot tell a clumping
    // multiplier apart from a global population knob. The island can.
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler(), LaneBuildParams{});
    REQUIRE(lanes.lane_count() > 1000);

    const AmbientTuning ambient;
    double total_gain = 0.0;
    double weighted_gain = 0.0, weight = 0.0;
    float lo = 1e30f, hi = -1e30f;
    std::size_t quiet_lanes = 0, busy_lanes = 0;
    double baseline_slots = 0.0, clumped_slots = 0.0;
    for (LaneRef lr = 0; lr < lanes.lane_count(); ++lr) {
        const Lane& l = lanes.lane(lr);
        const float gain = ped_hotspot_gain(lanes, lr, ambient);
        total_gain += static_cast<double>(gain);
        weighted_gain += static_cast<double>(gain * l.length_m);
        weight += static_cast<double>(l.length_m);
        lo = std::min(lo, gain);
        hi = std::max(hi, gain);
        if (gain < 1.0f) ++quiet_lanes;
        if (gain > 1.0f) ++busy_lanes;
        baseline_slots +=
            static_cast<double>(ped_schedule(city::kMapSeed, l, ambient, 1.0f).slots);
        clumped_slots +=
            static_cast<double>(ped_schedule(city::kMapSeed, l, ambient, gain).slots);
    }

    const double mean = total_gain / static_cast<double>(lanes.lane_count());
    const double by_length = weighted_gain / weight;
    // IT MUST ACTUALLY SPLIT THE MAP. A multiplier that comes out the same on
    // every lane is a population knob, and the whole point of this one is that
    // some streets are busier than others.
    REQUIRE_MSG(quiet_lanes > lanes.lane_count() / 10,
                "almost no lane came out quieter than baseline, so this is a "
                "global multiplier rather than a clumping term",
                "spread");
    REQUIRE_MSG(busy_lanes > lanes.lane_count() / 10,
                "almost no lane came out busier than baseline", "spread");
    // Stated against the AUTHORED range rather than as a literal, so retuning
    // the two ends cannot silently turn this into a check on nothing.
    REQUIRE_MSG(hi - lo > (ambient.ped_hotspot_busy -
                           ambient.ped_hotspot_quiet) * 0.5f,
                "the island spans less than half the authored clumping range, "
                "so the map's junctions are not actually varied enough for "
                "this to be doing anything",
                "spread");
    // AND IT MUST NOT SMUGGLE IN A POPULATION INCREASE. The two ends are
    // authored either side of 1.0 for exactly this reason; if the island's
    // total moved a lot, the clumping is really a density change wearing a
    // clumping name and the bench numbers stop meaning what they say.
    const double growth = clumped_slots / std::max(1.0, baseline_slots);
    REQUIRE_MSG(growth > 0.75 && growth < 1.35,
                "clumping moved the island's total pedestrian count by more "
                "than a third, so it is a density change and not a clumping "
                "term",
                "neutral");

    std::printf("      gain %.2f..%.2f over %zu lanes (mean %.2f, %.2f by "
                "length); %zu quieter and %zu busier than baseline; island "
                "total x%.2f\n",
                static_cast<double>(lo), static_cast<double>(hi),
                lanes.lane_count(), mean, by_length, quiet_lanes, busy_lanes,
                growth);
    pass("pedestrian density clumps toward junction arity without changing how "
         "many people the island holds");
}

void the_population_cap_is_a_safety_valve_and_never_binds() {
    // tests/traffic_bench.cpp already asserts this for its own configurations.
    // Repeated here against the SHIPPING defaults on the real map, because the
    // radius was raised and the clumping added in the same pass, and a cap
    // that starts binding turns which pedestrians exist into a fact about
    // which lanes were scanned first.
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler(), LaneBuildParams{});

    const CrowdTuning shipping;  // exactly what the game runs with
    Crowd crowd;
    crowd.build(lanes, city::kMapSeed, AmbientTuning{}, shipping);
    Scene scene;
    std::size_t peak_peds = 0, peak_cars = 0, peak_parked = 0;
    for (int64_t step = 0; step < 1200; ++step) {
        const float t = static_cast<float>(step) * kStepSeconds;
        // Across Pinatty Row and out toward the Strand.
        const glm::vec2 focus{-300.0f + t * 22.0f, 60.0f + t * 6.0f};
        if (step % shipping.refresh_every_steps == 0) crowd.refresh(step, focus);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        crowd.step_peds(step);
        crowd.publish(scene);
        const CrowdStats& s = crowd.stats();
        peak_peds = std::max(peak_peds, s.peds);
        peak_cars = std::max(peak_cars, s.vehicles);
        peak_parked = std::max(peak_parked, s.parked);
        REQUIRE_MSG(s.peds < shipping.max_peds,
                    "the pedestrian cap bound, which makes the population "
                    "scan-order dependent",
                    "cap");
        REQUIRE_MSG(s.vehicles < shipping.max_vehicles,
                    "the vehicle cap bound", "cap");
    }
    REQUIRE_MSG(peak_peds > 100 && peak_cars > 10 && peak_parked > 10,
                "the drive did not populate anything, so no cap could have "
                "bound either way",
                "vacuity");
    std::printf("      peak on the shipping defaults: %zu peds, %zu cars, "
                "%zu parked (caps %u / %u)\n",
                peak_peds, peak_cars, peak_parked, shipping.max_peds,
                shipping.max_vehicles);
    pass("the shipping radii populate the island without either cap binding");
}

}  // namespace

int main() {
    std::printf("ped_life_tests\n");
    quiet_street_produces_idling_and_kerb_waiting();
    a_car_bearing_down_alarms_then_panics_the_street();
    nerve_decides_who_spooks_and_who_does_not();
    a_knocked_over_pedestrian_always_gets_up();
    the_knockdown_reaches_as_far_as_the_bumper();
    a_struck_body_lands_down_the_road_and_walks_back();
    a_lethal_impact_leaves_a_body();
    the_activity_machine_is_free_of_scan_order();
    the_nerve_is_a_property_of_the_person_not_of_the_queue();
    the_nerve_splits_the_street_the_way_it_is_authored();
    the_crowd_clumps_toward_corners_on_the_real_map();
    the_population_cap_is_a_safety_valve_and_never_binds();
    return apricot_test::done("ped_life_tests");
}

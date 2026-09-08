#include <algorithm>
#include <limits>
#include <vector>

#include "city/map.h"
#include "city/spines.h"
#include "game/police_offenses.h"
#include "game/police_visibility.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

struct SignalCrossing {
    RoadGraph roads;
    LaneGraph lanes;
    CrowdTuning traffic;
    PoliceTuning police;
    LaneRef incoming = kInvalidLane;
    LaneRef crossing_lane = kInvalidLane;
    uint32_t junction = 0;

    explicit SignalCrossing(const GroundSampler& ground = {}) {
        RoadSpine east_west;
        east_west.id = 1;
        east_west.cls = RoadClass::Arterial;
        east_west.points = {{-220, 0}, {220, 0}};
        RoadSpine north_south;
        north_south.id = 2;
        north_south.cls = RoadClass::Arterial;
        north_south.points = {{0, -220}, {0, 220}};
        roads.build({east_west, north_south}, {}, ground);
        lanes.build(roads, ground);
        for (LaneRef ref = 0; ref < lanes.lane_count(); ++ref) {
            const auto& lane = lanes.lane(ref);
            const auto junction_pos = lanes.junction(lane.junction_to).pos;
            if (glm::length(glm::vec2{junction_pos.x, junction_pos.z}) > 0.1f)
                continue;
            junction = lane.junction_to;
            const auto start = lanes.pose(ref, 0.0f).position;
            if (start.x < -200.0f) incoming = ref;
            if (start.z < -200.0f) crossing_lane = ref;
        }
        REQUIRE(lanes.valid(incoming));
        REQUIRE(lanes.valid(crossing_lane));
        REQUIRE(lanes.approach_control(incoming) == JunctionControl::Signal);
    }

    int64_t phase_start(TrafficSignalPhase desired) const {
        for (int64_t step = 5; step < traffic.signal_period_steps; ++step) {
            if (traffic_signal_phase(lanes, junction, incoming, step, traffic) == desired &&
                traffic_signal_phase(lanes, junction, incoming, step + 4, traffic) == desired)
                return step;
        }
        REQUIRE(false);
        return 0;
    }

    PoliceDrivingSample sample(float front_offset, int64_t step,
                                float half_length = 2.2f) const {
        const auto line = lanes.pose(incoming,
            police_signal_line_station(lanes, incoming, traffic));
        PoliceDrivingSample out;
        out.vehicle_identity = 45;
        out.forward = glm::normalize(glm::vec3{line.tangent.x, 0, line.tangent.z});
        out.position = line.position + out.forward * (front_offset - half_length);
        out.velocity = out.forward * 12.0f;
        out.half_length_m = half_length;
        out.step = step;
        return out;
    }

    std::vector<PoliceOffenseWitness> witnesses(const PoliceDrivingSample& target) const {
        return {{target.position - target.forward * 15.0f, target.forward, true}};
    }

    PoliceOffenseReport cross(PoliceOffenseTracker& tracker, TrafficSignalPhase phase,
                             const std::vector<PoliceOffenseWitness>& watchers,
                             float half_length = 2.2f) const {
        const int64_t step = phase_start(phase);
        REQUIRE(!tracker.observe_driving(lanes, traffic,
            sample(-0.05f, step, half_length), watchers, police));
        return tracker.observe_driving(lanes, traffic,
            sample(0.05f, step + 1, half_length), watchers, police);
    }
};

void witnessed_red_starts_a_response_without_preexisting_heat() {
    SignalCrossing road;
    for (float half_length : {1.6f, 2.2f, 4.2f}) {
        PoliceOffenseTracker tracker;
        const auto watchers = road.witnesses(road.sample(0, 0, half_length));
        const auto report = road.cross(tracker, TrafficSignalPhase::Red,
                                       watchers, half_length);
        REQUIRE(report);
        REQUIRE(report.crime == WantedSystem::Crime::TrafficViolation);
        REQUIRE(report.incoming == road.incoming);
        WantedSystem wanted;
        REQUIRE(wanted.level() == 0);
        wanted.add_heat(report.heat, report.crime);
        REQUIRE(wanted.level() == 1);
    }
    apricot_test::pass("witnessed red crossing starts one-star response for short cars and long vans");
}

void visibility_is_required_when_the_crossing_happens() {
    SignalCrossing road;
    const auto target = road.sample(0, 0);
    auto watchers = road.witnesses(target);
    for (int blocked = 0; blocked < 4; ++blocked) {
        auto list = watchers;
        if (blocked == 0) list.clear();
        if (blocked == 1) list[0].los_clear = false;
        if (blocked == 2) list[0].forward *= -1.0f;
        if (blocked == 3)
            list[0].position = target.position - target.forward * (road.police.witness_range + 5.0f);
        PoliceOffenseTracker tracker;
        REQUIRE(!road.cross(tracker, TrafficSignalPhase::Red, list));
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic,
            road.sample(0.15f, road.phase_start(TrafficSignalPhase::Red) + 2),
            watchers, road.police));
    }
    for (TrafficSignalPhase phase : {TrafficSignalPhase::Green, TrafficSignalPhase::Yellow}) {
        PoliceOffenseTracker tracker;
        REQUIRE(!road.cross(tracker, phase, watchers));
    }
    apricot_test::pass("unseen, occluded, out-of-cone/range, green and yellow crossings are not crimes");
}

void terrain_and_world_props_supply_the_actual_witness_occlusion() {
    TerrainGround ground{city::kMapSeed};
    SignalCrossing road(ground.sampler());
    const int64_t step = road.phase_start(TrafficSignalPhase::Red);
    const auto target = road.sample(0.05f, step + 1);
    const glm::vec3 eye = target.position - target.forward * 12.0f +
        glm::vec3{0, 1.6f, 0};
    const glm::vec3 seen_point = target.position + glm::vec3{0, 1.05f, 0};
    const glm::vec3 ray = seen_point - eye;
    const float distance = glm::length(ray);
    const glm::vec3 midpoint = glm::mix(eye, seen_point, 0.5f);
    for (int obstruction = 0; obstruction < 3; ++obstruction) {
        TerrainCollider collider{city::kMapSeed};
        if (obstruction == 1)
            collider.add_static_box({midpoint - glm::vec3{0.5f, 2, 3},
                                     midpoint + glm::vec3{0.5f, 2, 3}});
        if (obstruction == 2)
            collider.add_kinematic_oriented_box(midpoint, {0.5f, 2, 3}, 0.2f);
        const auto hit = collider.raycast(eye, ray / distance, distance);
        const bool clear = !hit.hit || hit.distance >= distance - 0.08f;
        REQUIRE(clear == (obstruction == 0));
        if (!clear) REQUIRE(hit.prop);
        const std::vector<PoliceOffenseWitness> watchers{{eye, target.forward, clear}};
        PoliceOffenseTracker tracker;
        REQUIRE(static_cast<bool>(road.cross(tracker,
            TrafficSignalPhase::Red, watchers)) == clear);
    }
    apricot_test::pass("real TerrainCollider rays allow a clear view and block crimes behind static or moving world props");
}

void active_traffic_bodies_block_the_officers_view() {
    const VisiblePoliceIdentity witness{17, 4};
    const glm::vec3 eye{0, 1.2f, 0};
    const glm::vec3 target{0, 1.2f, -25};
    PoliceVisibilityBody truck;
    truck.identity = {24, 2};
    truck.local_bounds = {{-1.2f, 0, -4}, {1.2f, 3.2f, 4}};
    truck.world_from_local = glm::translate(glm::mat4{1.0f}, {0, 0, -12});
    REQUIRE(police_traffic_blocks_view(eye, target, {truck}, witness));
    // The side gap is genuinely beside the oriented body. Its enclosing
    // world AABB alone would falsely block this ray for the diagonal truck.
    truck.world_from_local = glm::translate(glm::mat4{1.0f}, {2.5f, 0, -12}) *
        glm::rotate(glm::mat4{1.0f}, 0.78539816f, glm::vec3{0, 1, 0});
    const glm::vec3 short_target{0, 1.2f, -10};
    REQUIRE(truck.local_bounds.transformed(truck.world_from_local)
        .intersect_ray(eye, {0, 0, -1}, 0.0f, 9.92f));
    REQUIRE(!police_traffic_blocks_view(eye, short_target, {truck}, witness));
    // A passing truck on a bridge is not an opaque wall down at street level.
    truck.world_from_local = glm::translate(glm::mat4{1.0f}, {0, 7, -12});
    REQUIRE(!police_traffic_blocks_view(eye, target, {truck}, witness));
    // The cop sees out of their own seated-car proxy.
    truck.identity = witness;
    truck.world_from_local = glm::mat4{1.0f};
    REQUIRE(!police_traffic_blocks_view(eye, target, {truck}, witness));
    // Excluding one witness must not exclude another car with the same lane key.
    truck.identity.slot += 1;
    REQUIRE(police_traffic_blocks_view(eye, target, {truck}, witness));
    apricot_test::pass("active trucks occlude police sight; oriented side gaps, overpasses and the witness's own cruiser stay clear");
}

void scaled_traffic_bounds_keep_the_sight_ray_endpoint_exact() {
    const VisiblePoliceIdentity witness{17, 4};
    const glm::vec3 eye{0, 1.2f, 0};
    const glm::vec3 target{0, 1.2f, -25};
    PoliceVisibilityBody body;
    body.identity = {20, 2};
    body.local_bounds = {{-1, -1, -1}, {1, 1, 1}};
    body.world_from_local = glm::translate(glm::mat4{1.0f}, {0, 1.2f, -10}) *
        glm::scale(glm::mat4{1.0f}, {1.4f, 1.0f, 0.1f});
    REQUIRE(police_traffic_blocks_view(eye, target, {body}, witness));
    // The near face is exactly the target point. Even a long scaled body
    // beyond that surface must not hide the surface we are trying to see.
    body.world_from_local = glm::translate(glm::mat4{1.0f}, {0, 1.2f, -35}) *
        glm::scale(glm::mat4{1.0f}, {1.4f, 1.0f, 10.0f});
    REQUIRE(!police_traffic_blocks_view(eye, target, {body}, witness));
    REQUIRE(!police_traffic_blocks_view(eye, target, {body}, witness, 0.0f));
    body.world_from_local[3].z += 0.2f;
    REQUIRE(police_traffic_blocks_view(eye, target, {body}, witness));
    apricot_test::pass("native model scale preserves world ray distances; a target's own endpoint surface is visible");
}

void authored_signal_approaches_use_the_same_gate_and_phase() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler());
    lanes.build(roads, ground.sampler());
    const CrowdTuning traffic;
    const PoliceTuning police;
    int checked = 0;
    for (glm::vec2 location : {glm::vec2{950, 40}, glm::vec2{950, 200}}) {
        uint32_t junction = UINT32_MAX;
        for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
            const auto pos = lanes.junction(j).pos;
            if (glm::distance(location, glm::vec2{pos.x, pos.z}) < 0.1f)
                junction = j;
        }
        REQUIRE(junction != UINT32_MAX);
        REQUIRE(lanes.junction_control(junction) == JunctionControl::Signal);
        for (LaneRef incoming : lanes.junction(junction).incoming) {
            const auto line = lanes.pose(incoming,
                police_signal_line_station(lanes, incoming, traffic));
            PoliceDrivingSample before;
            before.vehicle_identity = 45;
            before.forward = glm::normalize(glm::vec3{line.tangent.x, 0, line.tangent.z});
            before.position = line.position - before.forward * (before.half_length_m + 0.05f);
            before.velocity = before.forward * 12.0f;
            before.step = lanes.approach_group_a(junction, incoming)
                ? traffic.signal_period_steps / 2 + 5 : 5;
            auto after = before;
            after.position += after.forward * 0.1f;
            after.step += 1;
            const std::vector<PoliceOffenseWitness> watchers{{
                before.position - before.forward * 12.0f + glm::vec3{0, 1.15f, 0},
                before.forward, true}};
            PoliceOffenseTracker tracker;
            REQUIRE(!tracker.observe_driving(lanes, traffic, before, watchers, police));
            const auto report = tracker.observe_driving(lanes, traffic, after, watchers, police);
            if (!report || report.incoming != incoming)
                std::printf("  authored crossing mismatch: expected lane %u at %.0f %.0f, got %u\n",
                    incoming, location.x, location.y, report.incoming);
            REQUIRE(report);
            REQUIRE(report.incoming == incoming);
            ++checked;
        }
    }
    REQUIRE(checked >= 8);
    std::printf("  authored Apron/Sycamore: %d signal lanes crossed, correct gate and red phase\n", checked);
    apricot_test::pass("authored multi-arm Apron and Sycamore approaches identify their actual red-light movement");
}

void stopping_reversing_and_spawn_changes_are_not_crossings() {
    SignalCrossing road;
    const int64_t start = road.phase_start(TrafficSignalPhase::Red);
    const auto watchers = road.witnesses(road.sample(0, start));
    for (int scenario = 0; scenario < 8; ++scenario) {
        PoliceOffenseTracker tracker;
        auto before = road.sample(-0.05f, start);
        auto after = road.sample(0.05f, start + 1);
        if (scenario == 0) {
            before.velocity = after.velocity = glm::vec3{0};
            after.position = before.position;
        }
        if (scenario == 1) {
            std::swap(before.position, after.position);
            before.velocity *= -1.0f;
            after.velocity *= -1.0f;
        }
        if (scenario == 2) after.discontinuous = true;
        if (scenario == 3) after.vehicle_identity += 1;
        if (scenario == 4) {
            before.position -= before.forward * 20.0f;
            after.position += after.forward * 20.0f;
        }
        if (scenario == 5) after.driving = false;
        if (scenario == 6) after.step += 30;
        if (scenario == 7) after.position.x = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, before, watchers, road.police));
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, after, watchers, road.police));
    }
    PoliceOffenseTracker spawned;
    REQUIRE(!spawned.observe_driving(road.lanes, road.traffic,
        road.sample(0.1f, start), watchers, road.police));
    REQUIRE(!spawned.observe_driving(road.lanes, road.traffic,
        road.sample(0.2f, start + 1), watchers, road.police));
    apricot_test::pass("waiting, backing, spawn/vehicle changes and teleports never invent red-light crossings");
}

void only_the_actual_approach_is_checked() {
    SignalCrossing road;
    const int64_t start = road.phase_start(TrafficSignalPhase::Red);
    const auto watchers = road.witnesses(road.sample(0, start));
    for (int scenario = 0; scenario < 3; ++scenario) {
        PoliceOffenseTracker tracker;
        auto before = road.sample(-0.05f, start);
        auto after = road.sample(0.05f, start + 1);
        if (scenario == 0) {
            const glm::vec3 offset{0, 0, 25};
            before.position += offset;
            after.position += offset;
        }
        if (scenario == 1) {
            before.position.y += 12;
            after.position.y += 12;
        }
        if (scenario == 2) {
            before.forward *= -1.0f;
            after.forward *= -1.0f;
        }
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, before, watchers, road.police));
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, after, watchers, road.police));
    }
    REQUIRE(traffic_signal_phase(road.lanes, road.junction,
        road.crossing_lane, start, road.traffic) != TrafficSignalPhase::Red);
    const auto cross_line = road.lanes.pose(road.crossing_lane,
        police_signal_line_station(road.lanes, road.crossing_lane, road.traffic));
    auto before = road.sample(-0.05f, start);
    before.forward = cross_line.tangent;
    before.velocity = cross_line.tangent * 12.0f;
    before.position = cross_line.position - before.forward * (before.half_length_m + 0.05f);
    auto after = before;
    after.position += after.forward * 0.1f;
    after.step += 1;
    PoliceOffenseTracker tracker;
    const auto cross_watchers = road.witnesses(after);
    REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, before, cross_watchers, road.police));
    REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, after, cross_watchers, road.police));
    apricot_test::pass("parallel/off-road/overpass movement and the crossing road's green are not blamed");
}

void crossing_is_latched_and_uses_the_phase_at_the_crossing() {
    SignalCrossing road;
    const auto watchers = road.witnesses(road.sample(0, 0));
    PoliceOffenseTracker tracker;
    REQUIRE(road.cross(tracker, TrafficSignalPhase::Red, watchers));
    const int64_t start = road.phase_start(TrafficSignalPhase::Red);
    for (int i = 2; i < 20; ++i) {
        auto sample = road.sample(i % 2 == 0 ? -0.02f : 0.02f, start + i);
        if (i % 2 == 0) sample.velocity *= -1.0f;
        REQUIRE(!tracker.observe_driving(road.lanes, road.traffic, sample, watchers, road.police));
    }
    int64_t red_begins = 0;
    for (int64_t step = 1; step <= road.traffic.signal_period_steps; ++step) {
        if (traffic_signal_phase(road.lanes, road.junction, road.incoming, step - 1,
                road.traffic) == TrafficSignalPhase::Yellow &&
            traffic_signal_phase(road.lanes, road.junction, road.incoming, step,
                road.traffic) == TrafficSignalPhase::Red)
            red_begins = step;
    }
    REQUIRE(red_begins > 0);
    tracker.reset();
    REQUIRE(!tracker.observe_driving(road.lanes, road.traffic,
        road.sample(-0.02f, red_begins - 1), watchers, road.police));
    REQUIRE(!tracker.observe_driving(road.lanes, road.traffic,
        road.sample(0.08f, red_begins), watchers, road.police));
    apricot_test::pass("one passage reports once; a yellow passage stays legal when the next sample turns red");
}

void player_hits_are_attributed_once_without_an_outside_witness() {
    PoliceCollisionSample hit;
    hit.cruiser = {5, 2};
    hit.normal = {0, 0, 1};
    hit.player_velocity = {0, 0, -1.25f};
    hit.step = 10;
    PoliceOffenseTracker tracker;
    const auto report = tracker.observe_police_contact(hit);
    REQUIRE(report);
    REQUIRE(report.crime == WantedSystem::Crime::PoliceVehicleCollision);
    REQUIRE(report.cruiser == hit.cruiser);
    WantedSystem wanted;
    wanted.add_heat(report.heat, report.crime);
    REQUIRE(wanted.level() > 0);
    for (int64_t step = 11; step < 900; ++step) {
        hit.step = step;
        REQUIRE(!tracker.observe_police_contact(hit));
    }
    hit.step = 1100;
    REQUIRE(tracker.observe_police_contact(hit));
    // A different actual cruiser has its own episode/cooldown.
    hit.cruiser.slot += 1;
    REQUIRE(tracker.observe_police_contact(hit));
    hit.step += PoliceOffenseTracker::contact_release_steps + 1;
    REQUIRE(!tracker.observe_police_contact(hit));
    apricot_test::pass("player bump starts response with no outside witness; sustained contact and quick repeats do not stack heat");
}

void police_initiated_impacts_and_resting_contacts_are_not_player_crimes() {
    for (int scenario = 0; scenario < 6; ++scenario) {
        PoliceOffenseTracker tracker;
        PoliceCollisionSample hit;
        hit.cruiser = {9, 1};
        hit.normal = {0, 0, 1};
        hit.step = 5;
        if (scenario == 0) hit.police_velocity = {0, 0, 8};
        if (scenario == 1) {
            hit.player_velocity = {0, 0, -4};
            hit.police_velocity = {0, 0, 4};
        }
        if (scenario == 2) hit.player_velocity = {0, 0, -0.5f};
        if (scenario == 3) {
            hit.player_velocity = {0, 0, -10};
            hit.normal = {0, 0, 0};
        }
        if (scenario == 4) {
            hit.player_velocity = {5, 0, 0};
            hit.police_velocity = {0, 0, 4};
        }
        if (scenario == 5) hit.player_velocity.x = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(!tracker.observe_police_contact(hit));
        // Changed rebound velocities within the same collision are not a
        // fresh player assault, even if the new projection flips attribution.
        hit.step += 1;
        hit.player_velocity = {0, 0, -10};
        hit.police_velocity = {0, 0, 0};
        hit.normal = {0, 0, 1};
        REQUIRE(!tracker.observe_police_contact(hit));
    }
    apricot_test::pass("cop rams, ties, tangential contact, vibration and rebound do not blame the player");
}

}  // namespace

int main() {
    witnessed_red_starts_a_response_without_preexisting_heat();
    visibility_is_required_when_the_crossing_happens();
    terrain_and_world_props_supply_the_actual_witness_occlusion();
    active_traffic_bodies_block_the_officers_view();
    scaled_traffic_bounds_keep_the_sight_ray_endpoint_exact();
    authored_signal_approaches_use_the_same_gate_and_phase();
    stopping_reversing_and_spawn_changes_are_not_crossings();
    only_the_actual_approach_is_checked();
    crossing_is_latched_and_uses_the_phase_at_the_crossing();
    player_hits_are_attributed_once_without_an_outside_witness();
    police_initiated_impacts_and_resting_contacts_are_not_player_crimes();
    return apricot_test::done("police_offense_tests");
}

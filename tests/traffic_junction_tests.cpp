// Regressions for the authored Nickel / Collector / Spine / Sycamore junctions.
#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>
#include "city/map.h"
#include "city/spines.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "terrain/heightmap.h"
#include "traffic/crowd.h"
#include "test_assert.h"
using namespace apricot;
namespace {
struct Network {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    LaneGraph lanes;
    Network() {
        roads.build(city::map_spines(), {}, ground.sampler());
        lanes.build(roads, ground.sampler());
    }
    uint32_t junction(glm::vec2 position) const {
        for (uint32_t j=0; j<lanes.junction_count(); ++j) {
            const auto p=lanes.junction(j).pos;
            if (glm::distance(glm::vec2{p.x,p.z},position)<0.1f) return j;
        }
        REQUIRE(false);
        return 0;
    }
};
VehicleAgent car_at(const LaneGraph& lanes, LaneRef lane, float distance) {
    VehicleAgent car;
    car.lane_key=lanes.lane(lane).key;
    car.lane=lane;
    car.mode=AgentMode::Integrating;
    car.dist_along_m=distance;
    car.last_dist_m=distance;
    car.cruise_mps=14.0f;
    const auto pose=lanes.pose(lane,distance);
    car.pos=pose.position;
    car.fwd=pose.tangent;
    return car;
}
void choose_exit(VehicleAgent& car, const LaneGraph& lanes, LaneRef exit) {
    for (uint32_t decision=0; decision<1000; ++decision) {
        if (lanes.choose_next(car.lane,city::kMapSeed,decision)==exit) {
            car.decisions=decision;
            return;
        }
    }
    REQUIRE(false);
}
// These fixtures seed exact traffic states; the production API stays const.
void seed(Crowd& crowd, std::vector<VehicleAgent> cars) {
    std::sort(cars.begin(),cars.end(),[](const auto& a,const auto& b) {
        return std::tie(a.lane_key,a.slot)<std::tie(b.lane_key,b.slot);
    });
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles())=std::move(cars);
    crowd.rebuild_buckets();
}
void self_loop_releases_departure_claim(const Network& n) {
    CrowdTuning tuning;
    tuning.max_peds=0;
    const uint32_t junction=n.junction({950,200});
    const float clear=traffic_junction_clearance(n.lanes,junction,tuning);
    int checked=0;
    for (LaneRef approach:n.lanes.junction(junction).incoming) {
        for (const auto& turn:n.lanes.outgoing(approach)) {
            const auto& exit=n.lanes.lane(turn.to);
            if (turn.to==approach || exit.junction_from!=junction ||
                exit.junction_to!=junction) continue;
            Crowd crowd;
            crowd.build(n.lanes,city::kMapSeed,{},tuning);
            auto departed=car_at(n.lanes,turn.to,clear+5.0f);
            departed.committed_junction=junction;
            departed.committed_approach_lane=approach;
            departed.committed_exit_lane=turn.to;
            seed(crowd,{departed});
            crowd.step_vehicles(0);
            REQUIRE(crowd.vehicles().front().committed_junction==0xFFFFFFFFu);
            REQUIRE(crowd.vehicles().front().committed_approach_lane==kInvalidLane);
            ++checked;
        }
    }
    REQUIRE(checked>=2);
    apricot_test::pass("departed self-loop cars release junction ownership before a full lap");
}
void acute_parallel_movements_are_serialized(const Network& n) {
    CrowdTuning tuning;
    tuning.max_peds=0;
    const uint32_t junction=n.junction({950,200});
    const auto& node=n.lanes.junction(junction);
    const float clear=traffic_junction_clearance(n.lanes,junction,tuning);
    REQUIRE(clear>50.0f); // The eleven-degree return overlaps far upstream.
    LaneRef through=kInvalidLane, through_exit=kInvalidLane;
    LaneRef merge=kInvalidLane, merge_exit=kInvalidLane;
    for (LaneRef approach:node.incoming) {
        if (n.lanes.lane(approach).junction_from!=n.junction({950,40})) continue;
        for (const auto& turn:n.lanes.outgoing(approach)) {
            if (n.lanes.lane(turn.to).junction_to!=junction) continue;
            if (turn.kind==TurnKind::Left) {through=approach;through_exit=turn.to;}
        }
    }
    REQUIRE(n.lanes.valid(through));
    // The inside lane continues down the Spine while the outside lane cuts
    // into the loop. Their headings are parallel, but their body paths cross.
    for (const auto& turn:n.lanes.outgoing(through)) {
        if (turn.kind==TurnKind::Straight &&
            n.lanes.lane(turn.to).junction_to!=junction) through_exit=turn.to;
    }
    REQUIRE(n.lanes.lane(through_exit).junction_to!=junction);
    for (LaneRef approach:node.incoming) {
        if (approach==through || n.lanes.lane(approach).junction_from!=
            n.lanes.lane(through).junction_from) continue;
        for (const auto& turn:n.lanes.outgoing(approach)) {
            if (turn.kind==TurnKind::Straight && turn.to!=through_exit &&
                n.lanes.lane(turn.to).junction_to==junction) {
                merge=approach;merge_exit=turn.to;
            }
        }
    }
    REQUIRE(n.lanes.valid(merge));
    REQUIRE(glm::dot(n.lanes.pose(through,n.lanes.length(through)).tangent,
                     n.lanes.pose(merge,n.lanes.length(merge)).tangent)>0.99f);
    auto a=car_at(n.lanes,through,n.lanes.length(through)-clear-0.1f);
    auto b=car_at(n.lanes,merge,n.lanes.length(merge)-clear-0.1f);
    choose_exit(a,n.lanes,through_exit);
    choose_exit(b,n.lanes,merge_exit);
    Crowd crowd;
    crowd.build(n.lanes,city::kMapSeed,{},tuning);
    seed(crowd,{a,b});
    bool a_cleared=false,b_cleared=false;
    for (int64_t step=0; step<tuning.signal_period_steps*4; ++step) {
        crowd.rebuild_buckets();crowd.step_vehicles(step);
        REQUIRE(crowd.stats().ai_collisions==0u);
        unsigned claims=0;
        for (const auto& car:crowd.vehicles()) {
            if (car.committed_junction==junction) ++claims;
            if (car.lane!=through && car.lane_key==a.lane_key &&
                car.committed_junction!=junction) a_cleared=true;
            if (car.lane!=merge && car.lane_key==b.lane_key &&
                car.committed_junction!=junction) b_cleared=true;
        }
        REQUIRE(claims<=1u);
        if (a_cleared && b_cleared) break;
    }
    REQUIRE(a_cleared && b_cleared);
    apricot_test::pass("parallel approaches with crossing swept paths clear the acute merge one at a time");
}
void fresh_cars_avoid_both_ends_of_the_box(const Network& n) {
    CrowdTuning tuning;tuning.max_peds=0;
    std::size_t checked=0;
    for (int64_t step:{0,480,1440,2880}) {
        Crowd crowd;crowd.build(n.lanes,city::kMapSeed,{},tuning);
        crowd.refresh(step,{950,200});
        for (const auto& car:crowd.vehicles()) {
            const auto& lane=n.lanes.lane(car.lane);
            REQUIRE(car.dist_along_m>=traffic_junction_clearance(n.lanes,lane.junction_from,tuning));
            REQUIRE(lane.length_m-car.dist_along_m>=traffic_junction_clearance(n.lanes,lane.junction_to,tuning));
            ++checked;
        }
    }
    REQUIRE(checked>50u);
    apricot_test::pass("fresh traffic cannot materialize inside either junction corridor");
}
void active_green_is_not_vetoed_by_an_aged_red_queue(const Network& n) {
    CrowdTuning tuning;tuning.max_peds=0;
    const uint32_t junction=n.junction({950,200});
    const float clear=traffic_junction_clearance(n.lanes,junction,tuning);
    LaneRef old_lane=kInvalidLane,fresh_lane=kInvalidLane,exit=kInvalidLane;
    for (LaneRef old:n.lanes.junction(junction).incoming) {
        if (n.lanes.approach_group_a(junction,old)) continue;
        for (LaneRef fresh:n.lanes.junction(junction).incoming) {
            if (n.lanes.lane(fresh).junction_from!=n.junction({950,40})) continue;
            for (const auto& a:n.lanes.outgoing(old)) {
                for (const auto& b:n.lanes.outgoing(fresh)) {
                    if (a.to==b.to) {old_lane=old;fresh_lane=fresh;exit=a.to;}
                }
            }
        }
    }
    REQUIRE(n.lanes.valid(old_lane) && n.lanes.valid(fresh_lane));
    auto old=car_at(n.lanes,old_lane,n.lanes.length(old_lane)-clear-0.1f);
    auto fresh=car_at(n.lanes,fresh_lane,n.lanes.length(fresh_lane)-clear-0.1f);
    choose_exit(old,n.lanes,exit);choose_exit(fresh,n.lanes,exit);
    old.stop_junction=junction;old.stop_arrival_step=0;
    const int64_t start=tuning.signal_period_steps;
    REQUIRE(traffic_signal_phase(n.lanes,junction,old_lane,start,tuning)==TrafficSignalPhase::Red);
    REQUIRE(traffic_signal_phase(n.lanes,junction,fresh_lane,start,tuning)==TrafficSignalPhase::Green);
    Crowd crowd;crowd.build(n.lanes,city::kMapSeed,{},tuning);
    seed(crowd,{old,fresh});
    bool fresh_entered=false;
    for (int64_t step=start;step<start+tuning.signal_period_steps/2;++step) {
        crowd.rebuild_buckets();crowd.step_vehicles(step);
        for (const auto& car:crowd.vehicles()) {
            if (car.lane_key==old.lane_key)
                REQUIRE(car.committed_junction==0xFFFFFFFFu);
            if (car.lane_key==fresh.lane_key &&
                car.committed_junction==junction)
                fresh_entered=true;
        }
    }
    REQUIRE(fresh_entered);
    old.mechanical.engine_failed=true;
    seed(crowd,{old,fresh});
    bool fresh_passed_failed_car=false;
    for (int64_t step=start;step<start+tuning.signal_period_steps/2;++step) {
        crowd.rebuild_buckets();crowd.step_vehicles(step);
        for (const auto& car:crowd.vehicles())
            if (car.lane_key==fresh.lane_key &&
                car.committed_junction==junction)
                fresh_passed_failed_car=true;
    }
    REQUIRE(fresh_passed_failed_car);
    for (const auto& car:crowd.vehicles()) {
        if (car.lane_key==old.lane_key) REQUIRE(car.speed_mps<0.01f);
    }
    apricot_test::pass("active green drains before an aged red queue, and a failed red car cannot veto it");
}
void authored_healthy_leads_do_not_starve(const Network& n) {
    using Id=std::pair<uint64_t,uint32_t>;
    struct Trace {int64_t stopped=0; LaneRef last=kInvalidLane;};
    CrowdTuning tuning;tuning.max_peds=0;
    AmbientTuning ambient;ambient.vehicle_spacing_m=48;ambient.max_vehicle_slots=16;
    for (const glm::vec2 focus:{glm::vec2{950,40},glm::vec2{950,200},glm::vec2{700,60}}) {
        const uint32_t junction=n.junction(focus);
        const float clear=traffic_junction_clearance(n.lanes,junction,tuning);
        Crowd crowd;crowd.build(n.lanes,city::kMapSeed,ambient,tuning);
        std::map<Id,Trace> traces;
        int64_t longest=0;
        std::size_t transitions=0,green_waits=0;
        for (int64_t step=0; step<120*120; ++step) {
            if (step%tuning.refresh_every_steps==0) crowd.refresh(step,focus);
            crowd.rebuild_buckets();crowd.step_vehicles(step);
            for (const auto& car:crowd.vehicles()) {
                auto& trace=traces[{car.lane_key,car.slot}];
                if (n.lanes.valid(trace.last) && trace.last!=car.lane &&
                    n.lanes.lane(trace.last).junction_to==junction) ++transitions;
                trace.last=car.lane;
                const auto& lane=n.lanes.lane(car.lane);
                const float slack=lane.length_m-car.dist_along_m-clear;
                bool lead=true;
                for (const auto& other:crowd.vehicles()) {
                    if (other.lane==car.lane && other.dist_along_m>car.dist_along_m) lead=false;
                }
                const bool waiting=lane.junction_to==junction && lead &&
                    !vehicle_engine_failed(car.mechanical) && car.speed_mps<0.2f &&
                    slack>=-0.25f && slack<=2.0f;
                trace.stopped=waiting ? trace.stopped+1 : 0;
                if (trace.stopped>longest) {
                    longest=trace.stopped;
                }
                if (waiting && traffic_signal_phase(n.lanes,junction,car.lane,step,tuning)==TrafficSignalPhase::Green)
                    ++green_waits;
            }
        }
        std::printf("      junction %.0f,%.0f: %zu transitions, healthy lead wait %.2fs, %zu green wait steps\n",
                    focus.x,focus.y,transitions,static_cast<double>(longest)/120.0,green_waits);
        REQUIRE(transitions>=8u);
        // Four cycles include long corridor clearance and competing aged leads.
        // The previous green-only aging allowed a healthy loop lead to wait 95s.
        REQUIRE(longest<tuning.signal_period_steps*4);
        if (n.lanes.junction_control(junction)==JunctionControl::Signal) REQUIRE(green_waits>0u);
    }
    apricot_test::pass("healthy lead cars clear all three authored junctions without indefinite green starvation");
}
}
int main() {
    Network n;
    self_loop_releases_departure_claim(n);
    acute_parallel_movements_are_serialized(n);
    fresh_cars_avoid_both_ends_of_the_box(n);
    active_green_is_not_vetoed_by_an_aged_red_queue(n);
    authored_healthy_leads_do_not_starve(n);
    return apricot_test::done("traffic_junction_tests");
}

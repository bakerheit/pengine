#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "test_assert.h"
#include "traffic/crowd.h"

#include "maneuver_fixture.h"

using namespace apricot;
using apricot_test::pass;
using maneuver_fixture::Fixture;
namespace {

void radius_and_inactive() {
    for (float separation : {45.f, 61.f}) {
        Fixture f;
        f.cars() = {f.car(200, 1), f.car(200-separation, 2, true)};
        f.tick(0);
        REQUIRE((f.cars()[0].emergency_yield != EmergencyYield::None) == (separation < 60.f));
    }
    for (bool upper_level : {false, true}) {
        Fixture f;
        f.cars() = {f.car(200, 1), f.car(160, 2, true)};
        if (upper_level) f.cars()[1].pos.y += 12;
        else f.cars()[1].police_pursuit = false;
        f.tick(0, {400,2}, upper_level ? 1 : 0);
        REQUIRE(f.cars()[0].emergency_yield == EmergencyYield::None);
    }
    pass("60 m active-responder range ignores patrols and traffic on another level");
}

void safe_pull_aside_and_rejoin() {
    Fixture f;
    f.cars() = {f.car(200, 1), f.car(158, 2, true)};
    bool pulled = false, merged = false, passed = false;
    float biggest_step = 0;
    glm::vec3 previous = f.cars()[0].pos;
    for (int64_t step=0; step<6000; ++step) {
        f.tick(step, {420,2});
        auto& civilian = f.cars()[0];
        biggest_step = std::max(biggest_step, glm::distance(previous, civilian.pos));
        previous = civilian.pos;
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        if (civilian.roadside_offset_m > 1.f && civilian.speed_mps < .1f) pulled=true;
        if (f.cars()[1].dist_along_m > civilian.dist_along_m + 18.f) passed=true;
        if (pulled && passed && civilian.emergency_yield == EmergencyYield::None &&
            !civilian.maneuver.active() && std::fabs(civilian.roadside_offset_m) < .01f) {
            merged=true; break;
        }
    }
    std::printf("      pull %d, pass %d, merge %d, largest step %.3fm\n", pulled, passed, merged, biggest_step);
    REQUIRE(pulled && passed && merged);
    REQUIRE(biggest_step < .25f);
    pass("civilian steers aside, stops, lets cruiser pass, then merges back");
}

void blocked_shoulder_and_junction() {
    for (bool committed : {false, true}) {
        Fixture f;
        f.cars() = {f.car(200,1), f.car(155,2,true)};
        if (committed) {
            f.cars()[0].committed_junction=f.lanes.lane(f.lane).junction_from;
            f.cars()[0].committed_approach_lane=f.lane;
            f.cars()[0].committed_exit_lane=f.lane;
        } else {
            std::vector<glm::vec3> obstacles;
            for (float d=202; d<250; d+=3)
                obstacles.push_back(f.lanes.pose(f.lane,d,3).position);
            f.crowd.set_parked_vehicle_poses(obstacles);
        }
        for (int64_t step=0; step<12; ++step) {
            if (committed) f.cars()[0].committed_junction=f.lanes.lane(f.lane).junction_from;
            f.tick(step);
            REQUIRE(!f.cars()[0].maneuver.active());
            REQUIRE(std::fabs(f.cars()[0].roadside_offset_m)<.01f);
            REQUIRE(f.crowd.stats().ai_collisions==0);
        }
    }
    pass("blocked shoulder and committed junction prevent a pull-aside move");
}

void midblock_turn_and_oncoming_gap() {
    for (bool blocked : {false, true}) {
        Fixture f;
        // Outer lane gives a proper turning radius on this four-lane road.
        for (auto ref : f.lanes.lanes_of_edge(f.lanes.lane(f.lane).edge))
            if (f.lanes.lane(ref).forward == f.lanes.lane(f.lane).forward &&
                f.lanes.lane(ref).index > f.lanes.lane(f.lane).index) f.lane=ref;
        f.cars()={f.car(230,1,true)};
        if (blocked) {
            auto other=f.car(0,2,false,f.lanes.opposing(f.lane));
            const auto near=f.lanes.project_onto(other.lane,{244,0});
            other=f.car(near.dist_along_m,2,false,other.lane);
            other.speed_mps=other.cruise_mps=12;
            f.cars().push_back(other);
        }
        bool turned=false;
        float max_jump=0; auto previous=f.cars()[0].pos;
        for(int64_t step=0;step<1800;++step) {
            f.tick(step,{170, -4});
            max_jump=std::max(max_jump,glm::distance(previous,f.cars()[0].pos));
            previous=f.cars()[0].pos;
            REQUIRE(f.crowd.stats().ai_collisions==0);
            if (blocked && step<12) REQUIRE(!f.cars()[0].maneuver.active());
            if(f.cars()[0].fwd.x<-.95f && !f.cars()[0].maneuver.active()) { turned=true; break; }
        }
        REQUIRE(turned);
        REQUIRE(max_jump<.25f);
        REQUIRE(f.cars()[0].decisions==0); // Never reached a junction.
    }
    pass("cruiser reverses mid-block and waits for an oncoming car before turning");
}

void bypass_and_order() {
    Fixture a,b;
    auto cop=a.car(200,1,true), blocker=a.car(221,2);
    blocker.speed_mps=blocker.cruise_mps=0;
    blocker.mechanical.engine_failed=true;
    a.cars()={cop,blocker}; b.cars()={blocker,cop};
    bool bypass=false, completed=false;
    for(int64_t step=0;step<2400;++step) {
        a.tick(step,{290,2}); b.tick(step,{290,2});
        const auto& x=a.cars()[0]; const auto& y=b.cars()[1];
        REQUIRE(x.pos==y.pos && x.fwd==y.fwd && x.speed_mps==y.speed_mps);
        REQUIRE(a.crowd.stats().ai_collisions==0 && b.crowd.stats().ai_collisions==0);
        bypass |= x.maneuver.kind==TrafficManeuverKind::PoliceBypass;
        if(bypass && !x.maneuver.active() && x.dist_along_m>240) { completed=true; break; }
    }
    REQUIRE(bypass && completed);
    pass("cruiser passes a disabled car off its lane and returns, independent of update order");
}
void world_support_props_and_people() {
    for (int scenario = 0; scenario < 4; ++scenario) {
        Fixture f(RoadClass::Arterial, true);
        TerrainCollider world(905);
        if (scenario != 1) world.add_static_ground_rect({300,0},1000,{300,30},0);
        if (scenario == 2) world.add_static_oriented_box({214,1001,4},{16,1,1.3f},.12f);
        f.world = &world;
        f.cars()={f.car(200,1),f.car(158,2,true)};
        if (scenario == 3) {
            auto& people = const_cast<std::vector<PedAgent>&>(f.crowd.peds());
            for (float side=0; side<10; side+=1.0f) {
                PedAgent person; person.pos=f.lanes.pose(f.lane,205,side).position;
                people.push_back(person);
            }
        }
        bool moved=false;
        for(int64_t step=0;step<24;++step) {
            f.tick(step);
            moved |= f.cars()[0].maneuver.active();
        }
        REQUIRE(moved == (scenario == 0));
    }
    pass("body sweep requires ground support and avoids props and pedestrians");
}

void freeway_and_stationary_motion() {
    Fixture f(RoadClass::Freeway);
    f.cars()={f.car(230,1,true)};
    for(int64_t step=0;step<240;++step) {
        f.tick(step,{170,-4});
        REQUIRE(f.cars()[0].maneuver.kind!=TrafficManeuverKind::PoliceTurnaround);
    }
    Fixture stopped;
    stopped.cars()={stopped.car(200,1),stopped.car(158,2,true)};
    auto& v=stopped.cars()[0];
    v.speed_mps=0; v.mechanical.engine_failed=true;
    const auto pos=v.pos;
    for(int64_t step=0;step<24;++step) stopped.tick(step);
    REQUIRE(v.pos==pos);
    REQUIRE(!v.maneuver.active());
    pass("freeways forbid mid-block reversal and a failed stationary car cannot slide aside");
}

void bypass_stops_behind_suspect() {
    Fixture f;
    auto cop=f.car(200,1,true), blocker=f.car(221,2);
    blocker.speed_mps=blocker.cruise_mps=0;
    blocker.mechanical.engine_failed=true;
    f.cars()={cop,blocker};
    const auto target=f.lanes.pose(f.lane,248).position;
    bool passed=false, stopped=false;
    for(int64_t step=0;step<2400;++step) {
        f.crowd.set_police_context(1,target);
        f.crowd.set_police_officer_context(false,false,{0,0});
        f.crowd.rebuild_buckets(); f.crowd.step_vehicles(step);
        const auto& car=f.cars()[0];
        REQUIRE(f.crowd.stats().ai_collisions==0);
        REQUIRE(car.dist_along_m < 242.f);
        passed |= car.maneuver.kind==TrafficManeuverKind::PoliceBypass;
        if(passed && car.officer.phase==PoliceOfficerPhase::Pursuing) {
            REQUIRE(car.speed_mps<.08f);
            REQUIRE(glm::distance(car.pos,target)>6.f);
            REQUIRE(glm::distance(car.pos,target)<18.f);
            stopped=true; break;
        }
    }
    REQUIRE(passed && stopped);
    pass("a bypass ends behind a stopped suspect with room to stop and dismount");
}

void leave_room_to_rejoin_before_junction() {
    Fixture f;
    const auto& lane=f.lanes.lane(f.lane);
    const float gate=lane.length_m-traffic_junction_clearance(f.lanes,lane.junction_to,CrowdTuning{})-4.f;
    f.cars()={f.car(gate-20.f,1),f.car(gate-60.f,2,true)};
    for(int64_t step=0;step<24;++step) {
        f.tick(step,{590,2});
        REQUIRE(f.cars()[0].maneuver.kind!=TrafficManeuverKind::PullAside);
    }
    pass("yielding never parks a civilian with no room to rejoin before the next junction");
}

void displaced_yielding_car_can_rejoin() {
    Fixture f;
    auto car=f.car(200,1);
    car.speed_mps=0;
    car.roadside_offset_m=2.f;
    const auto pose=f.lanes.pose(f.lane,200,2.4f);
    car.pos=pose.position;
    car.collision_offset_xz={pose.right.x*.4f,pose.right.z*.4f};
    f.cars()={car};
    bool merged=false;
    float max_step=0; auto previous=car.pos;
    for(int64_t step=0;step<2400;++step) {
        f.tick(step,{400,2},0);
        const auto& v=f.cars()[0];
        max_step=std::max(max_step,glm::distance(previous,v.pos)); previous=v.pos;
        if(v.roadside_offset_m==0 && !v.maneuver.active()) { merged=true; break; }
    }
    REQUIRE(merged);
    REQUIRE(max_step<.1f);
    pass("a nudged yielding car rejoins from its actual pose without snapping back");
}

// The chase's teeth. Before this a pursuer tracked the player's lane and held
// station behind him forever: every police arc in the tree either pulled the
// cruiser ASIDE or passed something, and the player's own body vetoed any path
// that reached him, so a cruiser could never actually make contact.
void pursuer_rams_the_player_and_rejoins() {
    for (bool pursuing : {true, false}) {
        Fixture f;
        auto cop = f.car(200, 1, pursuing);
        cop.speed_mps = cop.cruise_mps = 14;
        f.cars() = {cop};
        // Player a car-length ahead and offset toward the kerb, rolling. The
        // offset is the point: reaching him means LEAVING the lane centre, so
        // "did it hit him" cannot be satisfied by driving past in the middle
        // of the road, which is what the pursuit already did.
        VehicleState player;
        const auto seat = f.lanes.pose(f.lane, 216, 1.6f);
        player.position = seat.position;
        player.velocity = seat.tangent * 7.0f;
        bool rammed = false, rejoined = false;
        float widest_swerve = 0.0f, closest = 1e9f;
        for (int64_t step = 0; step < 1200; ++step) {
            f.tick_with_player(step, player, 3);
            const auto& v = f.cars()[0];
            if (v.maneuver.kind == TrafficManeuverKind::PoliceRam) rammed = true;
            // Measured ONLY while the ram itself is running. A cruiser that
            // has already given up and swung into the opposing lane for a
            // turnaround reads as an enormous lateral offset, and counting
            // that would let this pass without a ram ever happening.
            const auto here = f.lanes.project_onto(v.lane, {v.pos.x, v.pos.z});
            if (here.valid() && v.maneuver.active() &&
                v.maneuver.kind == TrafficManeuverKind::PoliceRam)
                widest_swerve = std::max(widest_swerve, std::fabs(here.lateral_m));
            if (!pursuing || (v.maneuver.active() &&
                              v.maneuver.kind == TrafficManeuverKind::PoliceRam))
                closest = std::min(closest, glm::distance(
                    glm::vec2{v.pos.x, v.pos.z},
                    glm::vec2{player.position.x, player.position.z}));
            if (rammed && !v.maneuver.active() &&
                std::fabs(v.roadside_offset_m) < 0.01f &&
                v.dist_along_m > 240.0f) { rejoined = true; break; }
        }
        if (!pursuing) {
            // The exemption belongs to the dispatch, not to the arc: ordinary
            // traffic plans nothing and holds the lane centre.
            REQUIRE_MSG(!rammed, "ordinary traffic planned a ram", "ordinary");
            REQUIRE_MSG(widest_swerve < 0.25f,
                        "ordinary traffic left the lane centre", "ordinary");
            continue;
        }
        REQUIRE_MSG(rammed, "no dispatched cruiser ever planned a ram", "ram");
        REQUIRE_MSG(widest_swerve > 0.8f,
                    "the ram never left the lane centre", "ram");
        // Inside the carriageway, not across the median: the clearance test
        // still owns where an arc may go.
        REQUIRE_MSG(widest_swerve < f.lanes.lane(f.lane).width_m,
                    "the ram left the carriageway", "ram");
        REQUIRE_MSG(closest < 3.0f, "the ram never reached the player", "ram");
        // And it ends back in the lane rather than parked on the crown of the
        // road, which is what every other police arc would have left behind.
        REQUIRE_MSG(rejoined, "the cruiser never rejoined after the ram", "ram");
        std::printf("      ram: swerve %.2fm, closest approach %.2fm\n",
            static_cast<double>(widest_swerve), static_cast<double>(closest));
    }
    pass("a dispatched cruiser leaves the lane to make contact with the player, then rejoins");
}

}
int main() {
    std::puts("emergency_traffic_tests");
    radius_and_inactive(); safe_pull_aside_and_rejoin(); blocked_shoulder_and_junction();
    midblock_turn_and_oncoming_gap(); bypass_and_order();
    world_support_props_and_people(); freeway_and_stationary_motion();
    bypass_stops_behind_suspect(); leave_room_to_rejoin_before_junction();
    displaced_yielding_car_can_rejoin(); pursuer_rams_the_player_and_rejoins();
}

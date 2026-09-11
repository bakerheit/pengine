#include "game/weapon_hit.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "traffic/crowd.h"
#include "road_fixture.h"
#include "test_assert.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace apricot;

namespace {
constexpr uint64_t seed=0x50454421ull;
float elevated_ground(const void*,float,float) { return 1000.f; }

struct Fixture {
    RoadGraph roads;
    LaneGraph lanes;
    Crowd crowd;
    CrowdTuning tuning;
    explicit Fixture(bool reversed=false) {
        auto spines=make_grid_spines(4,62.f);
        if (reversed) std::reverse(spines.begin(),spines.end());
        const GroundSampler ground{elevated_ground,nullptr};
        roads.build(spines,RoadGraphParams{},ground);
        lanes.build(roads,ground);
        AmbientTuning ambient;
        ambient.max_vehicle_slots=0;
        tuning.ped_activate_m=400.f;
        tuning.ped_retire_m=600.f;
        crowd.build(lanes,seed,ambient,tuning);
        crowd.refresh(0,{93.f,93.f});
        crowd.rebuild_buckets();
        crowd.step_peds(0);
        REQUIRE(crowd.peds().size()>40);
    }
};

const PedAgent& find_ped(const Crowd& crowd,const PedShotHit& identity) {
    for (const PedAgent& ped:crowd.peds())
        if (ped.lane_key==identity.lane_key && ped.slot==identity.slot) return ped;
    REQUIRE(false);
    return crowd.peds().front();
}

struct ShotLine {
    glm::vec3 origin{},direction{};
    float range=0.f;
    PedShotHit first{};
    uint64_t rear_key=0;
    uint32_t rear_slot=0;
};

ShotLine choose_two_people(const Crowd& crowd) {
    for (const PedAgent& first:crowd.peds()) {
        for (const PedAgent& rear:crowd.peds()) {
            const glm::vec3 delta=rear.pos-first.pos;
            const float distance=glm::length(delta);
            if (distance<3.f || distance>50.f) continue;
            const glm::vec3 direction=delta/distance;
            const glm::vec3 origin=first.pos+glm::vec3{0.f,1.05f,0.f}-direction*2.f;
            const auto query=crowd.raycast_ped(origin,direction,distance+3.f);
            if (query.hit && query.lane_key==first.lane_key && query.slot==first.slot)
                return {origin,direction,distance+3.f,query,rear.lane_key,rear.slot};
        }
    }
    REQUIRE(false);
    return {};
}

void silhouette_math() {
    REQUIRE_NEAR(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},10.f),1.6f,1e-6);
    REQUIRE_NEAR(weapon_ped_hit_distance({.4f,1,-2},{0,0,1},{0,0,0},10.f),1.6f,1e-6);
    REQUIRE(weapon_ped_hit_distance({.401f,1,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1.86f,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,-.01f,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,-1},{0,0,0},10.f)<0.f);
    REQUIRE_NEAR(weapon_ped_hit_distance({0,1,0},{0,0,1},{0,0,0},10.f),0.f,1e-6);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},1.6f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,0},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,2},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},
            std::numeric_limits<float>::quiet_NaN())<0.f);
    apricot_test::pass("standing silhouette, parallel boundaries, range ties and invalid rays");
}

void nearest_hit_and_world_cover() {
    Fixture fixture;
    const ShotLine line=choose_two_people(fixture.crowd);
    const uint64_t before=fixture.crowd.population_hash();
    REQUIRE(fixture.crowd.raycast_ped(line.origin,line.direction,line.range).hit);
    REQUIRE(fixture.crowd.population_hash()==before);
    TerrainCollider world(seed);
    REQUIRE(!world.raycast(line.origin,line.direction,line.range).hit);
    const glm::vec3 centre=line.origin+line.direction*.8f;
    AABB wall;
    wall.expand(centre-glm::vec3{.2f});
    wall.expand(centre+glm::vec3{.2f});
    world.add_static_box(wall);
    const auto obstruction=world.raycast(line.origin,line.direction,line.range);
    REQUIRE(obstruction.hit && obstruction.prop);
    REQUIRE(obstruction.distance<line.first.distance);
    REQUIRE(!fixture.crowd.shoot_ped(line.origin,line.direction,obstruction.distance,1).hit);
    REQUIRE(fixture.crowd.population_hash()==before);
    REQUIRE(!fixture.crowd.shoot_ped(line.origin,line.direction,line.first.distance,1).hit);

    // FIRST ROUND: a wound, not a knockdown. This is the assertion the old
    // behaviour could not make — a single bullet used to put its victim flat on
    // the pavement for seven seconds, which meant the health pool was never
    // spent and the second round had nothing to hit.
    const auto hit=fixture.crowd.shoot_ped(line.origin,line.direction,line.range,1);
    REQUIRE(hit.hit && hit.lane_key==line.first.lane_key && hit.slot==line.first.slot);
    REQUIRE(!hit.killed);
    REQUIRE_NEAR(glm::length(hit.point-(line.origin+line.direction*hit.distance)),0.f,1e-5);
    {
        const PedAgent& wounded=find_ped(fixture.crowd,hit);
        REQUIRE(!ped_is_floored(wounded.activity));
        REQUIRE(wounded.health<kBodyHealth && wounded.health>0.f);
        REQUIRE_MSG(wounded.wounded_steps>0,"a round left no panic behind",
                    "trigger");
    }
    // Second round: still up, and still the nearest body on the line, so the
    // shot keeps finding the same person rather than passing through to the one
    // behind them.
    const auto second=fixture.crowd.shoot_ped(line.origin,line.direction,line.range,2);
    REQUIRE(second.hit && second.lane_key==hit.lane_key && second.slot==hit.slot);
    REQUIRE(!second.killed);
    REQUIRE(!ped_is_floored(find_ped(fixture.crowd,hit).activity));

    // Third round kills, and says so exactly once.
    const auto killing=fixture.crowd.shoot_ped(line.origin,line.direction,line.range,3);
    REQUIRE(killing.hit && killing.lane_key==hit.lane_key && killing.slot==hit.slot);
    REQUIRE(killing.killed && !killing.officer);
    std::size_t dead=0;
    for (const PedAgent& ped:fixture.crowd.peds()) {
        if (ped.activity==PedActivity::Dead) ++dead;
        if (ped.lane_key==line.rear_key && ped.slot==line.rear_slot) {
            REQUIRE(!ped_is_floored(ped.activity));
            REQUIRE(ped.health==kBodyHealth);
        }
    }
    REQUIRE_MSG(dead==1,"three rounds down one line kill exactly one person","one body");
    const PedAgent& victim=find_ped(fixture.crowd,hit);
    REQUIRE(victim.health==0.f);
    REQUIRE(victim.activity_steps==0);  // terminal: nothing is being waited out
    REQUIRE(victim.speed_mps==0.f);
    REQUIRE(victim.impact_speed_mps>0.f);
    REQUIRE(victim.impact_from_bullet);
    REQUIRE_NEAR(glm::length(victim.impact_dir_xz),1.f,1e-6);
    // A body on the pavement is not a target. A fourth round must miss it
    // outright, or every round after a kill re-charges the player for it.
    const auto fourth=fixture.crowd.shoot_ped(line.origin,line.direction,hit.distance+.01f,4);
    REQUIRE(!fourth.hit && !fourth.killed);
    REQUIRE(find_ped(fixture.crowd,hit).activity==PedActivity::Dead);
    apricot_test::pass("real crowd nearest hit, real wall occlusion, three rounds to one kill");
}

// Two crowds built from the same seed with the spine order reversed. Every
// number below has to match between them: the victim, the fall, and where the
// body comes to rest. Spawn order reaching any of that is the bug this pair of
// fixtures exists to catch.
void deterministic_kill_and_recovery() {
    Fixture first,second(true);
    const ShotLine line=choose_two_people(first.crowd);
    PedShotHit a{},b{};
    for (int64_t round=1;round<=3;++round) {
        a=first.crowd.shoot_ped(line.origin,line.direction,line.range,round);
        b=second.crowd.shoot_ped(line.origin,line.direction,line.range,round);
        REQUIRE(a.hit && b.hit && a.lane_key==b.lane_key && a.slot==b.slot);
        REQUIRE(a.killed==b.killed);
        REQUIRE(a.killed==(round==3));
        REQUIRE_NEAR(find_ped(first.crowd,a).health,
                     find_ped(second.crowd,b).health,1e-6);
    }
    const glm::vec3 original=find_ped(first.crowd,a).pos;
    bool saw_displaced=false;
    for (int64_t step=4;step<1500;++step) {
        first.crowd.rebuild_buckets();
        second.crowd.rebuild_buckets();
        first.crowd.step_peds(step);
        second.crowd.step_peds(step);
        const auto& x=find_ped(first.crowd,a);
        const auto& y=find_ped(second.crowd,b);
        REQUIRE(x.activity==y.activity && x.activity_steps==y.activity_steps);
        REQUIRE(x.impact_from_bullet && y.impact_from_bullet);
        REQUIRE_NEAR(glm::length(x.pos-y.pos),0.f,1e-5);
        REQUIRE_NEAR(glm::length(x.impact_velocity-y.impact_velocity),0.f,1e-5);
        // DEAD IS ABSORBING. Fifteen seconds is twice the longest authored
        // knockdown, so a body that was going to get up would have done it by
        // now — which is exactly what it used to do.
        REQUIRE_MSG(x.activity==PedActivity::Dead,"a dead body must not get up",
                    "terminal");
        REQUIRE(x.health==0.f);
        REQUIRE(x.impact_offset.y<=.01f);
        REQUIRE(glm::length(glm::vec2{x.impact_offset.x,x.impact_offset.z})<.5f);
        if (glm::length(x.pos-original)>.01f) saw_displaced=true;
        // And it is not a target, at any point, for as long as it lies there.
        const auto direct=first.crowd.raycast_ped(
            x.pos+glm::vec3{0.f,1.f,-.6f},{0,0,1},.3f);
        REQUIRE(!direct.hit || direct.lane_key!=a.lane_key || direct.slot!=a.slot);
    }
    REQUIRE(saw_displaced);
    // The body SETTLES. The ragdoll integrator keeps running for a corpse —
    // gate it on Downed alone and the body hangs in the air at the height it
    // was shot at — so what has to be true fifteen seconds later is that the
    // ground took everything: no travel across the pavement, and only the
    // residue of the decaying bounce left vertically.
    {
        const glm::vec3 settled=find_ped(first.crowd,a).impact_velocity;
        REQUIRE_NEAR(settled.x,0.f,1e-4);
        REQUIRE_NEAR(settled.z,0.f,1e-4);
        REQUIRE(std::fabs(settled.y)<0.05f);
        const glm::vec3 resting=find_ped(first.crowd,a).pos;
        for (int64_t step=1500;step<1740;++step) {
            first.crowd.rebuild_buckets();
            first.crowd.step_peds(step);
        }
        REQUIRE_MSG(glm::distance(find_ped(first.crowd,a).pos,resting)<1e-3f,
                    "a settled body must not creep","at rest");
    }

    // The survivable path still exists, and still recovers. A car at 3 m/s is
    // a hair over the knockdown floor, which vehicle_impact_damage() prices at
    // almost nothing — so this person goes down, gets up, and walks away.
    const PedAgent& bystander=[&]()->const PedAgent&{
        for (const PedAgent& p:first.crowd.peds())
            if (!ped_is_floored(p.activity) && p.health==kBodyHealth) return p;
        REQUIRE(false);
        return first.crowd.peds().front();
    }();
    const uint64_t key=bystander.lane_key;
    const uint32_t slot=bystander.slot;
    const auto find_bystander=[&]()->const PedAgent&{
        for (const PedAgent& p:first.crowd.peds())
            if (p.lane_key==key && p.slot==slot) return p;
        REQUIRE(false);
        return first.crowd.peds().front();
    };
    VehicleState car;
    car.position=bystander.pos;
    car.velocity={0.f,0.f,3.f};
    first.crowd.rebuild_buckets();
    first.crowd.step_peds(1500,&car);
    REQUIRE(find_bystander().activity==PedActivity::Downed);
    REQUIRE(!find_bystander().impact_from_bullet);
    REQUIRE_MSG(find_bystander().health>0.f,
                "a kerb-speed knockdown must not kill","survivable");
    bool recovered=false;
    for (int64_t step=1501;step<3200 && !recovered;++step) {
        first.crowd.rebuild_buckets();
        first.crowd.step_peds(step);
        if (!ped_is_floored(find_bystander().activity)) recovered=true;
    }
    REQUIRE_MSG(recovered,"a survivable knockdown still ends in a get-up","recovery");
    apricot_test::pass("spawn-order-independent kill, a body that stays down, and a survivable knockdown that does not");
}
// A WOUNDED MAN RUNS. This is a regression guard with a specific bug behind
// it: the first version wrote the wounded panic straight into
// PedAgent::panic_seconds, and because panic_tick() derives its phase from how
// much of PanicTuning::duration has ELAPSED, a timer set ABOVE that duration
// reads as negative elapsed — which is Startle. A shot pedestrian therefore
// stood almost perfectly still for four and a half seconds and only then ran.
//
// It passed every test that asked whether they panicked. Only watching one get
// shot in the actual game showed it, which is why this asserts on the PHASE
// reached and how fast, not on a timer being non-zero.
void a_wounded_pedestrian_flinches_then_runs() {
    Fixture fixture;
    const ShotLine line=choose_two_people(fixture.crowd);
    const auto hit=fixture.crowd.shoot_ped(line.origin,line.direction,line.range,1);
    REQUIRE(hit.hit && !hit.killed);

    int64_t first_flee=-1;
    int64_t last_flee=-1;
    for (int64_t step=2;step<1200;++step) {
        fixture.crowd.rebuild_buckets();
        fixture.crowd.step_peds(step);
        const PedAgent& p=find_ped(fixture.crowd,hit);
        REQUIRE_MSG(!ped_is_floored(p.activity),
                    "one round put somebody on the floor","wound");
        if (p.activity==PedActivity::Fleeing) {
            if (first_flee<0) first_flee=step;
            last_flee=step;
        }
    }
    REQUIRE_MSG(first_flee>=0,"a shot pedestrian never ran","flee");
    // Inside a second. The broken version took 4.4 s to get here.
    REQUIRE_MSG(first_flee<2+120,"a shot pedestrian stood still for over a "
                "second before running","flinch");
    // And it ENDS. The kernel's timer always decays, so nobody sprints forever.
    REQUIRE_MSG(last_flee<1199,"a shot pedestrian never stopped running","decay");
    // Most of the authored window is spent running, not flinching.
    const PedLifeTuning life{};
    const int64_t window=static_cast<int64_t>(life.wounded_panic_seconds*120.f);
    REQUIRE_MSG(last_flee-first_flee>window/2,
                "the wounded panic is mostly flinch rather than flight","shape");
    std::printf("      shot at step 1: ran from %lld to %lld (%.2f s of flight)\n",
                static_cast<long long>(first_flee),static_cast<long long>(last_flee),
                static_cast<double>(last_flee-first_flee)/120.0);
    apricot_test::pass("a wounded pedestrian flinches, runs, and stops");
}

// The fist. It was an animation with a contact window that nothing consumed,
// so the jab swung through people — and the window had existed, unread, since
// the punch landed. Everything below is the pistol's own query at arm's
// length, which is the property worth pinning: one answer to "who is standing
// in front of me", not two that can disagree.
void the_fist_connects_and_four_of_them_kill() {
    Fixture fixture;
    const ShotLine line=choose_two_people(fixture.crowd);
    const PedAgent& start=find_ped(fixture.crowd,line.first);
    const glm::vec3 chest=start.pos+glm::vec3{0.f,1.28f,0.f};
    // Stand where a person throwing a punch stands: just outside the body, not
    // two metres back down the shot line.
    const glm::vec3 fist=chest-line.direction*0.9f;

    // Out of reach. A punch is not a bullet and must not carry down the street.
    REQUIRE(!fixture.crowd.punch_ped(line.origin,line.direction,1.15f,1).hit);
    REQUIRE(find_ped(fixture.crowd,line.first).health==kBodyHealth);

    int connected=0,killed=0;
    for (int64_t swing=1;swing<=4;++swing) {
        const auto blow=fixture.crowd.punch_ped(fist,line.direction,1.15f,swing);
        if (!blow.hit) continue;
        REQUIRE(blow.lane_key==line.first.lane_key && blow.slot==line.first.slot);
        ++connected;
        if (blow.killed) ++killed;
    }
    REQUIRE_MSG(connected==4,"a jab at arm's length missed","reach");
    REQUIRE_MSG(killed==1,"four punches did not kill exactly once","lethal");
    const PedAgent& victim=find_ped(fixture.crowd,line.first);
    REQUIRE(victim.activity==PedActivity::Dead && victim.health==0.f);
    // A FIST IS NOT A GUNSHOT. The authored bullet fall is a specific clip,
    // and playing it for a punch reads as somebody being shot by nothing.
    REQUIRE_MSG(!victim.impact_from_bullet,
                "a punched body plays the bullet fall","fall");
    apricot_test::pass("the fist reaches an arm's length, and four of them kill");
}

// A wounded person goes down to fewer punches, which is the whole reason the
// fist spends from the same hundred points as the pistol instead of counting
// its own knockdowns.
void a_wounded_man_goes_down_to_one_jab() {
    Fixture fixture;
    const ShotLine line=choose_two_people(fixture.crowd);
    // Two rounds first: 68 of 100 spent, so one 25-point jab cannot finish it
    // and two can.
    REQUIRE(fixture.crowd.shoot_ped(line.origin,line.direction,line.range,1).hit);
    REQUIRE(fixture.crowd.shoot_ped(line.origin,line.direction,line.range,2).hit);
    const PedAgent& hurt=find_ped(fixture.crowd,line.first);
    REQUIRE(hurt.health>0.f && hurt.health<kPunchBodyDamage*2.f);
    const glm::vec3 fist=hurt.pos+glm::vec3{0.f,1.28f,0.f}-line.direction*0.9f;
    REQUIRE(!fixture.crowd.punch_ped(fist,line.direction,1.15f,3).killed);
    REQUIRE(fixture.crowd.punch_ped(fist,line.direction,1.15f,4).killed);
    REQUIRE(find_ped(fixture.crowd,line.first).activity==PedActivity::Dead);
    apricot_test::pass("bullets and fists spend from one pool, so a wounded man drops sooner");
}
} // namespace

int main() {
    silhouette_math();
    nearest_hit_and_world_cover();
    deterministic_kill_and_recovery();
    a_wounded_pedestrian_flinches_then_runs();
    the_fist_connects_and_four_of_them_kill();
    a_wounded_man_goes_down_to_one_jab();
    return apricot_test::done("weapon_hit_tests");
}

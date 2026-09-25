// Fire burns the crowd (game/fire_harm.h): a real FireField stepped under a
// real Crowd's pedestrians, bitten on the shared beat the App uses.
#include <cmath>
#include <vector>

#include "core/fixed_step.h"
#include "game/fire_harm.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;
namespace {

float elevated_ground(const void*, float, float) { return 1000.f; }
const float kDt = static_cast<float>(kSimDt);

struct Street {
    RoadGraph roads;
    LaneGraph lanes;
    Crowd crowd;
    CrowdTuning tuning;
    Street() {
        const GroundSampler ground{elevated_ground, nullptr};
        roads.build(make_grid_spines(4, 62.f), RoadGraphParams{}, ground);
        lanes.build(roads, ground);
        AmbientTuning ambient;
        ambient.max_vehicle_slots = 0;
        tuning.ped_activate_m = 400.f;
        tuning.ped_retire_m = 600.f;
        crowd.build(lanes, 0x46495245ull, ambient, tuning);
        crowd.refresh(0, {93.f, 93.f});
        crowd.rebuild_buckets();
        crowd.step_peds(0);
        REQUIRE(crowd.peds().size() > 40);
    }
    const PedAgent* find(uint64_t key, uint32_t slot) const {
        for (const PedAgent& p : crowd.peds())
            if (p.lane_key == key && p.slot == slot) return &p;
        return nullptr;
    }
};

// Light a fire at somebody's feet and let it take hold, with the ground flat
// at their height, as the App's probe would report a pavement.
FireField fire_under(const PedAgent& victim, float seconds, bool still_burning = true) {
    FireField fire;
    REQUIRE(fire.ignite(victim.pos, victim.pos.y, 77));
    const float ground_y = victim.pos.y;
    for (float t = 0.0f; t < seconds; t += kDt)
        fire.step(kDt, [ground_y](float, float, float) {
            FireGround g;
            g.supported = true;
            g.height_m = ground_y;
            return g;
        });
    REQUIRE(fire.burning() == still_burning);
    return fire;
}

void the_clock_beats_only_while_burning() {
    FireBiteClock clock;
    REQUIRE(!clock.due(kDt, false));
    REQUIRE(clock.due(kDt, true));  // the first step of a fire
    int bites = 1;
    for (int i = 0; i < 600; ++i) bites += clock.due(kDt, true) ? 1 : 0;
    // Five seconds at one bite per 0.85 s.
    REQUIRE(bites == 1 + static_cast<int>(600 * kDt / kFireBiteSeconds));
    REQUIRE(!clock.due(kDt, false));
    REQUIRE(clock.due(kDt, true));  // a new fire bites straight away
    apricot_test::pass("one bite per 0.85 s while anything burns, reset when it is out");
}

void people_in_the_flames_are_bitten_and_nobody_else() {
    Street street;
    const PedAgent victim = street.crowd.peds().front();
    const FireField fire = fire_under(victim, 1.0f);
    REQUIRE(fire.heat_at(victim.pos) > kFireBiteMinHeat);

    const auto hits = burn_standing_people(street.crowd, fire, 1);
    bool victim_hit = false;
    for (const PedShotHit& hit : hits) {
        REQUIRE(hit.hit && !hit.officer);
        const PedAgent* p = street.find(hit.lane_key, hit.slot);
        REQUIRE(p != nullptr);
        // Everyone bitten was standing in fire when it bit.
        REQUIRE(fire.heat_at(p->pos) > kFireBiteMinHeat);
        if (hit.lane_key == victim.lane_key && hit.slot == victim.slot) victim_hit = true;
    }
    REQUIRE(victim_hit);
    // Everyone else is untouched.
    for (const PedAgent& p : street.crowd.peds()) {
        if (fire.heat_at(p.pos) > kFireBiteMinHeat) continue;
        REQUIRE(p.health == kBodyHealth);
    }
    const PedAgent* after = street.find(victim.lane_key, victim.slot);
    REQUIRE(after != nullptr);
    REQUIRE(after->health < kBodyHealth && after->health > 0.0f);
    // A bitten survivor runs, as somebody shot at does.
    REQUIRE(after->wounded_steps > 0);
    apricot_test::pass("the person standing in the fire is bitten; the street around is not");
}

void bites_kill_and_the_dead_are_left_alone() {
    Street street;
    const PedAgent victim = street.crowd.peds().front();
    const FireField fire = fire_under(victim, 1.0f);
    // Keep them where they stand: panic would walk them out, which is the
    // point in the game and not what this is measuring.
    int bites = 0;
    bool killed = false;
    for (int step = 1; step < 20 && !killed; ++step) {
        for (const PedShotHit& hit : burn_standing_people(street.crowd, fire, step)) {
            if (hit.lane_key != victim.lane_key || hit.slot != victim.slot) continue;
            ++bites;
            killed = hit.killed;
        }
    }
    REQUIRE(killed);
    // The fire is not stepped between bites here, so the heat under them is
    // fixed and the count is exact: a bite is kFireBiteDamage scaled by it.
    // Four at full heat; more at a patch's edge, where this one stands.
    const float heat = fire.heat_at(victim.pos);
    REQUIRE(heat > kFireBiteMinHeat && heat <= 1.0f);
    REQUIRE(bites == static_cast<int>(std::ceil(kBodyHealth / (kFireBiteDamage * heat))));
    REQUIRE(static_cast<int>(std::ceil(kBodyHealth / kFireBiteDamage)) == 4);
    const PedAgent* body = street.find(victim.lane_key, victim.slot);
    REQUIRE(body != nullptr && body->activity == PedActivity::Dead);
    // Dropped where they stood, not thrown.
    REQUIRE(glm::length(body->impact_velocity) < 1e-4f);
    REQUIRE(!body->impact_from_bullet);
    for (const PedShotHit& hit : burn_standing_people(street.crowd, fire, 30))
        REQUIRE(hit.lane_key != victim.lane_key || hit.slot != victim.slot);
    apricot_test::pass("bites kill on the heat under them (four at full) and skip the dead");
}

void an_out_fire_bites_nobody() {
    Street street;
    FireField fire;
    REQUIRE(burn_standing_people(street.crowd, fire, 1).empty());
    const PedAgent victim = street.crowd.peds().front();
    fire = fire_under(victim, 20.0f, false);  // past the longest cell's lifetime
    REQUIRE(burn_standing_people(street.crowd, fire, 1).empty());
    apricot_test::pass("no fire, or a burnt-out one, hurts nobody");
}

}  // namespace

int main() {
    the_clock_beats_only_while_burning();
    people_in_the_flames_are_bitten_and_nobody_else();
    bites_kill_and_the_dead_are_left_alone();
    an_out_fire_bites_nobody();
    return apricot_test::done("fire_harm_tests");
}

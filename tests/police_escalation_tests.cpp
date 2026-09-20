#include "game/police_escalation.h"
#include "test_assert.h"

using namespace apricot;

namespace {

PoliceEscalationInput one_star(float speed, float dt = 1.0f) {
    return {1, true, false, true, false, speed, dt};
}

void one_star_offers_a_stop_then_fleeing_promotes_a_chase() {
    PoliceEscalationTracker tracker;
    for (int second = 0; second < 3; ++second) {
        const auto out = tracker.step(one_star(10.0f));
        REQUIRE(out.posture == PolicePosture::TrafficStop);
        REQUIRE(out.pull_over_prompt);
        REQUIRE(!out.fled_stop);
    }
    const auto fled = tracker.step(one_star(10.0f));
    REQUIRE(fled.posture == PolicePosture::Chase);
    REQUIRE(fled.fled_stop);
    REQUIRE(fled.minimum_wanted_level == 2);
    apricot_test::pass("one star asks for a stop before sustained flight becomes a two-star chase");
}

void a_compliant_stop_resolves_without_a_chase() {
    PoliceEscalationTracker tracker;
    PoliceEscalationInput stopped = one_star(0.0f);
    stopped.officer_on_foot_near = true;
    REQUIRE(!tracker.step(stopped).stop_resolved);
    REQUIRE(tracker.step(stopped).stop_resolved);
    apricot_test::pass("a stopped driver receives the nonviolent traffic-stop resolution");
}

void tactical_layers_skip_the_traffic_stop() {
    PoliceEscalationTracker armed;
    auto gun = one_star(0.0f);
    gun.armed = true;
    const auto armed_out = armed.step(gun);
    REQUIRE(armed_out.posture == PolicePosture::Tactical);
    REQUIRE(armed_out.minimum_wanted_level == 3);
    REQUIRE(!armed_out.pull_over_prompt);

    PoliceEscalationTracker violent;
    violent.record_civilian_kill();
    violent.record_civilian_kill();
    const auto violent_out = violent.step(one_star(0.0f));
    REQUIRE(violent_out.posture == PolicePosture::Tactical);
    REQUIRE(violent_out.minimum_wanted_level == 3);
    apricot_test::pass("a drawn gun or multiple civilian kills raises a tactical three-star response");
}

}  // namespace

int main() {
    one_star_offers_a_stop_then_fleeing_promotes_a_chase();
    a_compliant_stop_resolves_without_a_chase();
    tactical_layers_skip_the_traffic_stop();
    return apricot_test::done("police_escalation_tests");
}

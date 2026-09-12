#include <limits>
#include "city/halberd_helicopter.h"
#include "game/helicopter.h"
#include "test_assert.h"
using namespace apricot;

namespace {
// Spool a parked machine up to flight idle the way boarding it does, without
// asserting anything about how long that takes -- that is its own check below.
HelicopterState spooled(const HelicopterState& from, const TerrainCollider& g) {
    HelicopterState s = from;
    for (int i = 0; i < 600; ++i) s = step_helicopter(s, {}, g, 1.f/120);
    return s;
}
}  // namespace

int main() {
    TerrainCollider field{42};
    field.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    HelicopterState parked;
    parked.position = {0,200,0};

    // --- Boarding ----------------------------------------------------------
    REQUIRE(helicopter_can_exit(parked));
    REQUIRE(helicopter_in_boarding_range(parked, {-2.3f,200,4.0f},200));
    REQUIRE(!helicopter_in_boarding_range(parked, {2.3f,200,4.0f},200));   // starboard
    REQUIRE(!helicopter_in_boarding_range(parked, {-2.3f,200,7.5f},200));  // nose
    REQUIRE(!helicopter_in_boarding_range(parked, {-2.3f,200,-4.0f},200)); // tail
    REQUIRE(!helicopter_in_boarding_range(parked, {-.5f,200,4.0f},200));   // inside it
    REQUIRE(!helicopter_in_boarding_range(parked, {-2.3f,204,4.0f},200));  // on the roof
    // The door travels with the airframe, so a rotated machine is boarded at
    // the rotated door and not at the world-space one.
    auto rotated = parked; rotated.yaw = 1.3f;
    REQUIRE(helicopter_in_boarding_range(rotated,
        helicopter_point(rotated, {-2.3f,0,4.0f}),200));
    REQUIRE(!helicopter_in_boarding_range(rotated, {-2.3f,200,4.0f},200));

    // --- Parked, rotor running, hands off ----------------------------------
    // The single most important property of the thing: full collective trim is
    // exactly one hover weight, and a float that lands a bit either side of
    // that must not let a parked helicopter wander off its stand.
    auto idle = parked;
    for (int i=0;i<2400;++i) idle = step_helicopter(idle,{},field,1.f/120);
    REQUIRE(glm::length(idle.position-parked.position) < .0001f);
    REQUIRE(idle.grounded && !idle.crashed);
    REQUIRE_NEAR(idle.rotor, 1.0, 1e-5);

    // Spool is a gate, not a formality: collective before the rotor is up must
    // not lift it, and the disc angle must advance while it is turning.
    InputFrame climb{}; climb.held = kBtnShiftUp;
    auto cold = parked;
    cold = step_helicopter(cold, climb, field, 1.f/120);
    REQUIRE(cold.rotor < kHeliLiftoffRotor && cold.grounded);
    for (int i=0;i<60;++i) cold = step_helicopter(cold, climb, field, 1.f/120);
    REQUIRE(cold.grounded);           // still spooling at half a second
    REQUIRE(cold.rotor_angle != parked.rotor_angle);

    // --- Takeoff, and determinism -----------------------------------------
    auto a = spooled(parked, field), b = a;
    REQUIRE(a.grounded);
    for (int i=0;i<600;++i) {
        a = step_helicopter(a, climb, field, 1.f/120);
        b = step_helicopter(b, climb, field, 1.f/120);
        REQUIRE(a.position==b.position && a.velocity==b.velocity);
        REQUIRE(a.rotor_angle==b.rotor_angle);
    }
    REQUIRE(!a.grounded && !a.crashed);
    REQUIRE(a.position.y > 210);                 // climbed, and at a sane rate
    REQUIRE(a.position.y < 260);
    REQUIRE(!helicopter_can_exit(a));
    REQUIRE(!helicopter_in_boarding_range(a,helicopter_point(a,{-2.3f,0,4.f}),a.position.y));

    // --- Hover: hands off, it settles and then stays there -----------------
    // This is the auto-trim, and it is the difference between a machine a
    // player can learn and one that falls out of the sky whenever they think.
    // Letting go does not stop it dead -- a climbing helicopter has momentum,
    // and it floats up over the damping length before it holds. What matters
    // is that the float is SHORT and that what follows it is flat.
    auto hover = a;
    const float released = hover.position.y;
    for (int i=0;i<360;++i) hover = step_helicopter(hover,{},field,1.f/120);
    const float level = hover.position.y;
    REQUIRE(level > released);
    REQUIRE(level - released < 10.f);
    for (int i=0;i<1800;++i) hover = step_helicopter(hover,{},field,1.f/120);
    REQUIRE(std::fabs(hover.position.y-level) < .25f);
    REQUIRE(!hover.grounded && !hover.crashed);

    // --- Forward flight ----------------------------------------------------
    // Cyclic is the only way a helicopter translates: nose down, and the thrust
    // vector that was holding it up now also pushes it along.
    InputFrame ahead{}; ahead.throttle = 1;
    auto cruise = hover;
    for (int i=0;i<900;++i) cruise = step_helicopter(cruise,ahead,field,1.f/120);
    REQUIRE(cruise.pitch < -.4f);                        // nose is down
    REQUIRE(glm::dot(cruise.velocity, helicopter_forward(cruise)) > 20.f);
    REQUIRE(glm::length(glm::vec2{cruise.velocity.x,cruise.velocity.z}) < 70.f);
    REQUIRE(!cruise.crashed);
    // ...and the nose-up cyclic brings it back down to a hover.
    InputFrame back{}; back.brake = 1;
    for (int i=0;i<900;++i) cruise = step_helicopter(cruise,back,field,1.f/120);
    REQUIRE(glm::length(glm::vec2{cruise.velocity.x,cruise.velocity.z}) < 12.f);

    // --- Pedal turn: the manoeuvre a car and a plane both refuse ------------
    auto pedal = hover;
    const float start_yaw = pedal.yaw;
    const glm::vec3 station = pedal.position;
    InputFrame right{}; right.steer = 1;
    for (int i=0;i<240;++i) pedal = step_helicopter(pedal,right,field,1.f/120);
    REQUIRE(pedal.yaw < start_yaw);
    // +Z is forward and right is toward local -X, matching game/aircraft.h.
    REQUIRE(helicopter_forward(pedal).x < 0);
    REQUIRE(pedal.roll < 0);                              // banked into it
    // It turned on the spot rather than flying a circuit.
    REQUIRE(glm::length(pedal.position-station) < 25.f);

    // --- Landing -----------------------------------------------------------
    // Hold the descend button from a hover and you land. Nothing else: no
    // flare, no feathering a digital button. Neutral collective holds altitude
    // by design, so if this did not work there would be no way down at all.
    auto down = hover;
    InputFrame sink{}; sink.held = kBtnShiftDown;
    for (int i=0;i<4000 && !down.grounded;++i)
        down = step_helicopter(down, sink, field, 1.f/120);
    REQUIRE(down.grounded);
    REQUIRE(!down.crashed);
    REQUIRE(helicopter_can_exit(down));

    // Arriving with the speed still on is the landing that does end badly, and
    // it is the one players actually have: fly it at the ground at cruise and
    // the gear does not save you.
    auto dropped = hover;
    for (int i=0;i<2000 && !dropped.grounded;++i)
        dropped = step_helicopter(dropped, ahead, field, 1.f/120);
    for (int i=0;i<4000 && !dropped.grounded;++i)
        dropped = step_helicopter(dropped, sink, field, 1.f/120);
    REQUIRE(dropped.grounded && dropped.crashed);
    // A wreck takes no further input and its rotor winds down rather than
    // freezing mid-blade.
    const auto settled = dropped;
    dropped = step_helicopter(dropped, climb, field, 1.f/120);
    REQUIRE(dropped.position == settled.position && dropped.grounded);
    REQUIRE(dropped.rotor < settled.rotor);

    // --- Rotor rim clearance ----------------------------------------------
    // The 14 m disc is the first thing to touch anything, and it reaches 7 m
    // out where there is no airframe at all -- the hull is 1.4 m half-width
    // and the stub wings stop at 3 m. An obstacle 7.8 m to the side can only
    // be hit by the blades. A mast that clears a gantry while the disc does
    // not is the whole hazard of flying one down a street.
    const glm::vec3 stand{200, 204.6f, 0};
    const float disc = stand.y + city::kHalberdHelicopterRotorHub.y;
    const auto drift_into = [&](float box_y) {
        TerrainCollider world{42};
        world.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
        world.add_static_oriented_box({207.8f,box_y,2.2f},{.3f,.5f,1.f},0);
        auto s2 = hover;
        s2.position = stand;
        s2.velocity = {10,0,0};
        for (int i=0;i<240 && !s2.crashed;++i)
            s2 = step_helicopter(s2,{},world,1.f/120);
        return s2;
    };
    REQUIRE(drift_into(disc).crashed);
    // The same box, the same track, lifted clear over the disc: nothing on the
    // machine reaches it, so it goes by. This is what pins the crash above to
    // the blades rather than to some other probe happening to be in the way.
    const auto cleared = drift_into(disc + 3.f);
    REQUIRE(!cleared.crashed && cleared.position.x > stand.x);

    // --- A wreck FALLS ----------------------------------------------------
    // The bug this pins: flying into a building set crashed and then returned
    // out of the step before any physics ran, so the machine hung in the air
    // at the point of impact with its blades winding down. Everything below
    // was passing at the time -- the old rim test asserted `crashed` and
    // stopped there, which is exactly the half of the behaviour that worked.
    TerrainCollider tower{42};
    tower.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    // The box spans y 200..260; fly at its SIDE at 240, not over its roof.
    tower.add_static_oriented_box({260,230,0},{6,30,6},0);
    auto into_it = hover;
    into_it.position = {200,240,0};
    into_it.velocity = {34,0,0};
    into_it.yaw = -glm::half_pi<float>();          // nose along +X, at the tower
    const unsigned before_impacts = into_it.impacts;
    for (int i=0;i<600 && !into_it.crashed;++i)
        into_it = step_helicopter(into_it,{},tower,1.f/120);
    REQUIRE(into_it.crashed);
    REQUIRE(!into_it.grounded);                    // hit it in the air
    REQUIRE(into_it.impacts > before_impacts);     // the host gets an edge
    REQUIRE(glm::length(into_it.velocity) > 1.f);  // carries off the wall
    REQUIRE(glm::length(into_it.tumble) > .1f);    // and is turning over

    // It comes down. Twelve seconds is far longer than 60 m of fall needs, and
    // the point of the bound is that it must not still be up there.
    const float struck_at = into_it.position.y;
    auto falling = into_it;
    bool turned_over = false;
    for (int i=0;i<1440 && !falling.grounded;++i) {
        const auto before = falling;
        falling = step_helicopter(falling,{},tower,1.f/120);
        if (!falling.grounded) {
            REQUIRE(falling.position.y <= before.position.y + .01f);  // never climbs
            if (std::fabs(falling.pitch-before.pitch) > 1e-5f ||
                std::fabs(falling.roll-before.roll) > 1e-5f) turned_over = true;
        }
    }
    REQUIRE(falling.grounded);
    REQUIRE(turned_over);
    REQUIRE(falling.position.y < struck_at - 20.f);
    REQUIRE(falling.impacts > into_it.impacts);     // second bang, on arrival
    REQUIRE_NEAR(glm::length(falling.velocity), 0.0, 1e-5);
    REQUIRE_NEAR(glm::length(falling.tumble), 0.0, 1e-5);
    // It lies there. Not standing on its nose, and not still rolling.
    REQUIRE(std::fabs(falling.pitch) <= kHeliWreckRestTilt + 1e-5f);
    REQUIRE(std::fabs(falling.roll) <= kHeliWreckRestTilt + 1e-5f);
    // And it stays lying there under full collective: a wreck takes no input.
    const auto lying = falling;
    for (int i=0;i<600;++i) falling = step_helicopter(falling,climb,tower,1.f/120);
    REQUIRE(falling.position == lying.position && falling.grounded);
    REQUIRE(falling.impacts == lying.impacts);
    REQUIRE(!helicopter_in_boarding_range(falling,
        helicopter_point(falling,{-2.3f,0,4.f}), falling.position.y));

    // Flying at the SIDE of a low wall is a crash, not a landing. probe_down()
    // reports the top of whatever is under the origin, so a five-metre wall
    // ahead of the machine reads as a floor ABOVE it -- and the ground-contact
    // check would snap a fifty-knot impact up onto the parapet and call it
    // parked. That is what the first in-game run of this actually did.
    TerrainCollider parapet{42};
    parapet.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    parapet.add_static_oriented_box({250,203,0},{4,3,30},0);   // top at y 206
    auto at_wall = hover;
    at_wall.position = {200,204,0};           // flying BELOW the wall's top
    at_wall.velocity = {30,0,0};
    at_wall.yaw = -glm::half_pi<float>();
    for (int i=0;i<600 && !at_wall.crashed;++i)
        at_wall = step_helicopter(at_wall,{},parapet,1.f/120);
    REQUIRE(at_wall.crashed);
    REQUIRE_MSG(at_wall.position.y < 206.f, "never parked on the parapet", "wall");
    // Descending onto that same wall from above IS a landing.
    auto onto_wall = hover;
    onto_wall.position = {250,214,0};
    onto_wall.velocity = {0,0,0};
    for (int i=0;i<2400 && !onto_wall.grounded;++i)
        onto_wall = step_helicopter(onto_wall,sink,parapet,1.f/120);
    REQUIRE(onto_wall.grounded && !onto_wall.crashed);
    REQUIRE_NEAR(onto_wall.position.y, 206.0, .05);

    // A wreck falling onto a roof stops on the roof rather than dropping
    // through the building it just bounced off.
    TerrainCollider roof{42};
    roof.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    roof.add_static_oriented_box({200,230,0},{40,30,40},0);
    auto onto = hover;
    onto.position = {200,268,0};
    onto.crashed = true; onto.grounded = false;
    onto.velocity = {0,-6,0}; onto.tumble = {1.1f,0,.4f};
    for (int i=0;i<1200 && !onto.grounded;++i)
        onto = step_helicopter(onto,{},roof,1.f/120);
    REQUIRE(onto.grounded && onto.position.y > 250.f);

    // A tidy landing is not an explosion, so it must NOT bump the counter.
    auto quiet = hover;
    const unsigned quiet_before = quiet.impacts;
    for (int i=0;i<4000 && !quiet.grounded;++i)
        quiet = step_helicopter(quiet,sink,field,1.f/120);
    REQUIRE(quiet.grounded && !quiet.crashed);
    REQUIRE(quiet.impacts == quiet_before);

    // --- Water is not a helipad -------------------------------------------
    TerrainCollider sea{42};
    sea.add_static_ground_rect({0,0},0,{8000,8000},0,Surface::Rock);
    auto ditch = parked;
    ditch.position = {0,1.f,0}; ditch.grounded = false; ditch.rotor = 1;
    ditch.velocity = {0,-1.f,0};
    for (int i=0;i<600 && !ditch.grounded;++i)
        ditch = step_helicopter(ditch,{},sea,1.f/120);
    REQUIRE(ditch.grounded && ditch.crashed);

    // --- Degenerate steps do nothing --------------------------------------
    auto guard = hover;
    REQUIRE(step_helicopter(guard,{},field,0).position == guard.position);
    REQUIRE(step_helicopter(guard,{},field,-1.f).position == guard.position);
    REQUIRE(step_helicopter(guard,{},field,
        std::numeric_limits<float>::quiet_NaN()).position == guard.position);

    apricot_test::pass("helicopter boarding, spool gate, parked stability, "
                       "deterministic takeoff, hover trim, cyclic cruise, pedal "
                       "turn, landing, hard landing, rotor rim, a wreck that "
                       "falls, tumbles, grazes and arrives, and ditching");
    return apricot_test::done("helicopter_tests");
}

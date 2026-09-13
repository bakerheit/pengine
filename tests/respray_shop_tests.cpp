// The respray's sim rules (game/respray_shop.h), stepped against real cars
// placed by spawn_vehicle on the real collider at Rook's two bays.
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/fixed_step.h"
#include "game/respray_shop.h"
#include "test_assert.h"

using namespace apricot;
namespace {

const auto& kSite = city::kAutoRepairSite;
constexpr float kBays[] = {-9.0f, -1.0f};

struct Shop {
    const TerrainCollider& ground;
    VehicleTuning tuning;

    // A car at bay-local (x, z), nose toward the back wall (local -z), moving
    // forward at `speed`. Local z grows toward the street.
    VehicleState at(float x, float z, float speed = 0.0f) const {
        auto car = spawn_vehicle(tuning, ground,
            kSite.origin.x + kSite.cos_yaw * x + kSite.sin_yaw * z,
            kSite.origin.z - kSite.sin_yaw * x + kSite.cos_yaw * z,
            std::atan2(kSite.sin_yaw, kSite.cos_yaw));
        car.velocity = vehicle_forward(car) * speed;
        return car;
    }
};

struct Drive {
    Drive(const Shop& s, float b) : shop(s), bay(b) {}
    const Shop& shop;
    float bay;
    float dt = static_cast<float>(kSimDt);
    uint64_t vehicle = 7;
    ResprayVisit visit;

    VehicleState forecourt() const { return shop.at(bay, 12.0f, 3.0f); }
    VehicleState rolling() const { return shop.at(bay, 0.0f, 2.0f); }
    VehicleState stopped() const { return shop.at(bay, -5.0f); }

    ResprayResult step(const VehicleState& car, bool eyes_on,
                       const PaintOrder* order = nullptr, bool driving = true) {
        return step_respray_shop(visit, car, shop.tuning, dt, driving, vehicle,
                                 eyes_on, order);
    }

    // Forecourt, then a step rolling inside the bay, then stopped in it: the
    // three phases a real drive-in passes through.
    void roll_in(bool eyes_forecourt, bool eyes_rolling, bool eyes_stopped) {
        REQUIRE(step(forecourt(), eyes_forecourt).event == ResprayEvent::None);
        REQUIRE(visit.outside_driving && !visit.in_bay && !visit.arrived);
        REQUIRE(step(rolling(), eyes_rolling).event == ResprayEvent::None);
        REQUIRE(visit.in_bay && visit.pulled_in && !visit.arrived);
        REQUIRE(step(stopped(), eyes_stopped).event == ResprayEvent::None);
        REQUIRE(visit.arrived);
    }

    // Runs a started spray to its end with no further order, returning the
    // Completed result and how many steps it took.
    ResprayResult spray_to_end(bool eyes_on, int& steps) {
        steps = 0;
        const auto car = stopped();
        for (int i = 0; i < 100000; ++i) {
            const auto r = step(car, eyes_on);
            ++steps;
            if (r.event == ResprayEvent::Completed) return r;
            REQUIRE(r.event == ResprayEvent::None);
            REQUIRE(visit.spraying());
        }
        REQUIRE_MSG(false, "spray never completed", "spray_to_end");
        return {};
    }
};

const PaintOrder kRed{false, {196, 30, 42}};
const PaintOrder kFactory{true, {}};

// The predicate as it read before repair_bay_fits was extracted, verbatim.
bool legacy_repair_shop_ready(const VehicleState& car, const VehicleTuning& tuning) {
    const auto& site = city::kAutoRepairSite;
    if(glm::length(car.velocity)>.5f || vehicle_up(car).y<.8f ||
        std::fabs(car.position.y-site.ground_m)>2.f) return false;
    const auto local=[&](glm::vec3 p) {
        return glm::vec2{site.cos_yaw*p.x-site.sin_yaw*p.z,
                         site.sin_yaw*p.x+site.cos_yaw*p.z};
    };
    const glm::vec2 center=local(car.position-glm::vec3{site.origin.x,0,site.origin.z});
    const glm::vec2 right=local(vehicle_right(car)),forward=local(vehicle_forward(car));
    const glm::vec2 extent=glm::abs(right)*tuning.car_collision_half_width+
        glm::abs(forward)*tuning.car_collision_half_length;
    if(center.y-extent.y < -14.5f || center.y+extent.y > 3.5f) return false;
    for(float bay:{-9.f,-1.f})
        if(std::fabs(center.x-bay)+extent.x<3.f) return true;
    return false;
}

// 1
void unseen_roll_in_completes_after_one_second(const Shop& shop) {
    for (float bay : kBays) {
        for (float dt : {static_cast<float>(kSimDt), 1.0f}) {
            Drive d{shop, bay};
            d.dt = dt;
            d.roll_in(false, false, false);
            REQUIRE(d.visit.pulled_in && d.visit.arrived && !d.visit.seen);
            const auto started = d.step(d.stopped(), false, &kRed);
            REQUIRE(started.event == ResprayEvent::Started);
            REQUIRE(started.order == kRed);
            REQUIRE(d.visit.spraying() && d.visit.spray_s == 0.0f);
            int steps = 0;
            const auto done = d.spray_to_end(false, steps);
            REQUIRE(done.order == kRed && !done.seen);
            REQUIRE_NEAR(static_cast<double>(static_cast<float>(steps) * dt),
                         static_cast<double>(kRespraySeconds), 0.5 * dt);
            REQUIRE(steps == (dt == 1.0f ? 1 : 120));
            REQUIRE(!d.visit.spraying() && d.visit.arrived);
            // Unlimited resprays per visit, judged against the same latch.
            REQUIRE(d.step(d.stopped(), true, &kFactory).event == ResprayEvent::Started);
            const auto again = d.spray_to_end(true, steps);
            REQUIRE(again.order == kFactory && !again.seen);
        }
    }
    apricot_test::pass("an unseen roll-in arrives and a respray completes after 1.0 s at 1/120 and 1.0 dt");
}

// 2
void a_sighting_while_pulling_in_latches(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        d.roll_in(false, true, false);
        REQUIRE(d.visit.seen);
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
        int steps = 0;
        REQUIRE(d.spray_to_end(false, steps).seen);
        // The arrival step itself is inside the window.
        Drive e{shop, bay};
        e.roll_in(false, false, true);
        REQUIRE(e.visit.seen);
        // A cop on the forecourt, before the car is in a bay, is not "pulling in".
        Drive f{shop, bay};
        f.roll_in(true, false, false);
        REQUIRE(!f.visit.seen);
    }
    apricot_test::pass("a sighting on any pull-in step through arrival latches seen; the forecourt does not");
}

// 3
void a_sighting_after_arrival_does_not_count(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        d.roll_in(false, false, false);
        for (int i = 0; i < 240; ++i) REQUIRE(d.step(d.stopped(), true).event == ResprayEvent::None);
        REQUIRE(!d.visit.seen);
        REQUIRE(d.step(d.stopped(), true, &kRed).event == ResprayEvent::Started);
        int steps = 0;
        const auto done = d.spray_to_end(true, steps);
        REQUIRE(!done.seen);
        REQUIRE(respray_outcome(3, done.seen) == ResprayOutcome::WantedCleared);
    }
    apricot_test::pass("a cop who arrives after the car has stopped does not block the clear");
}

// 4
void leaving_and_returning_unseen_resets(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        d.roll_in(false, true, false);
        REQUIRE(d.visit.seen && d.visit.arrived);
        d.roll_in(false, false, false);  // drives out to the forecourt first
        REQUIRE(d.visit.arrived && !d.visit.seen);
    }
    apricot_test::pass("driving out and pulling back in unseen starts a clean visit");
}

// 5
void a_wobble_cancels_the_spray_but_keeps_the_latch(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        d.roll_in(false, true, false);
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
        for (int i = 0; i < 30; ++i) REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
        const auto wobble = shop.at(bay, -5.0f, 2.0f);
        REQUIRE(repair_bay_fits(wobble, shop.tuning) && !repair_shop_ready(wobble, shop.tuning));
        const auto r = d.step(wobble, false);
        REQUIRE(r.event == ResprayEvent::Cancelled && r.order == kRed);
        REQUIRE(!d.visit.spraying() && d.visit.arrived && d.visit.seen);
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
        int steps = 0;
        REQUIRE(d.spray_to_end(false, steps).seen);
        REQUIRE(steps == 120);
    }
    apricot_test::pass("a speed wobble inside the bay cancels a spray and keeps arrived and seen");
}

// 6
void an_order_that_cannot_start_is_rejected(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        auto r = d.step(d.forecourt(), false, &kRed);
        REQUIRE(r.event == ResprayEvent::Rejected && r.order == kRed && !d.visit.spraying());
        r = d.step(d.rolling(), false, &kRed);
        REQUIRE(r.event == ResprayEvent::Rejected && !d.visit.spraying() && !d.visit.arrived);
        // The order on the arrival step itself starts.
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
        for (int i = 0; i < 12; ++i) REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
        const float before = d.visit.spray_s;
        // A second order while spraying is refused and the spray carries on.
        r = d.step(d.stopped(), false, &kFactory);
        REQUIRE(r.event == ResprayEvent::Rejected && r.order == kFactory);
        REQUIRE(d.visit.order == kRed && d.visit.spray_s > before);
        int steps = 0;
        REQUIRE(d.spray_to_end(false, steps).order == kRed);
        REQUIRE(steps == 120 - 13);  // 12 quiet steps and the rejected one already sprayed
        // Completed wins over a Rejected order on the same step.
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
        for (int i = 0; i < 119; ++i) REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
        r = d.step(d.stopped(), false, &kFactory);
        REQUIRE(r.event == ResprayEvent::Completed && r.order == kRed);
    }
    apricot_test::pass("an order before arrival or during a spray is rejected; Completed wins the step");
}

// 7
void leaving_the_wheel_or_the_car_mid_spray_cancels(const Shop& shop) {
    for (float bay : kBays) {
        for (int mode = 0; mode < 2; ++mode) {
            Drive d{shop, bay};
            d.roll_in(false, false, false);
            REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Started);
            for (int i = 0; i < 60; ++i) d.step(d.stopped(), false);
            ResprayResult r;
            if (mode == 0) {
                r = d.step(d.stopped(), false, nullptr, false);
            } else {
                d.vehicle = 8;
                r = d.step(d.stopped(), false);
            }
            REQUIRE(r.event == ResprayEvent::Cancelled && r.order == kRed);
            REQUIRE(!d.visit.spraying() && !d.visit.arrived && d.visit.vehicle == d.vehicle);
            for (int i = 0; i < 480; ++i) {
                const auto after = d.step(d.stopped(), false);
                REQUIRE(after.event == ResprayEvent::None);
            }
            REQUIRE(!d.visit.arrived);
        }
    }
    apricot_test::pass("getting out or a vehicle change mid-spray cancels and never completes");
}

// 8
void exit_and_reenter_in_the_bay_never_arrives(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        d.roll_in(false, true, false);
        REQUIRE(d.step(d.stopped(), false, nullptr, false).event == ResprayEvent::None);
        REQUIRE(!d.visit.outside_driving && !d.visit.arrived && !d.visit.seen);
        for (int i = 0; i < 240; ++i) REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
        REQUIRE(d.visit.in_bay && !d.visit.pulled_in && !d.visit.arrived);
        REQUIRE(d.step(d.stopped(), false, &kRed).event == ResprayEvent::Rejected);
        ResprayHintInput hint;
        hint.driving = true;
        hint.fits = hint.on_lot = true;
        hint.wanted_level = 2;
        hint.visit = &d.visit;
        REQUIRE(respray_hint(hint) == RespraySuffix::NeedsPullIn);
    }
    apricot_test::pass("getting out and back in while fitted needs a fresh pull-in, even unseen");
}

// 9
void a_visit_that_starts_fitted_never_arrives(const Shop& shop) {
    for (float bay : kBays) {
        for (uint64_t id : {uint64_t{0}, uint64_t{7}}) {
            Drive d{shop, bay};
            d.vehicle = id;  // 0 matches a fresh visit's identity, 7 does not
            for (int i = 0; i < 240; ++i) REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
            REQUIRE(!d.visit.arrived && !d.visit.pulled_in);
            // What teleport, a car swap, load and new game do: a fresh visit.
            d.visit = ResprayVisit{};
            REQUIRE(d.step(d.forecourt(), false).event == ResprayEvent::None);
            REQUIRE(d.visit.outside_driving);
            REQUIRE(d.step(d.stopped(), false).event == ResprayEvent::None);
            REQUIRE(d.visit.pulled_in && d.visit.arrived);
        }
    }
    apricot_test::pass("a visit that starts in the bay never arrives; reset outside then driven in, it does");
}

// 10
void a_crawl_in_arrives_on_the_first_fitted_step(const Shop& shop) {
    for (float bay : kBays) {
        Drive d{shop, bay};
        REQUIRE(d.step(d.forecourt(), false).event == ResprayEvent::None);
        const auto crawl = shop.at(bay, 0.0f, 0.4f);
        REQUIRE(repair_shop_ready(crawl, shop.tuning));
        REQUIRE(d.step(crawl, true).event == ResprayEvent::None);
        REQUIRE(d.visit.pulled_in && d.visit.arrived && d.visit.seen);
    }
    apricot_test::pass("a 0.4 m/s crawl into the bay arrives on its first fitted step");
}

// 11
void outcome_truth_table() {
    static_assert(respray_outcome(0, false) == ResprayOutcome::Painted);
    for (int level = -1; level <= 5; ++level) {
        for (bool seen : {false, true}) {
            const auto expected = level <= 0 ? ResprayOutcome::Painted
                : seen ? ResprayOutcome::WantedKept : ResprayOutcome::WantedCleared;
            REQUIRE(respray_outcome(level, seen) == expected);
        }
    }
    apricot_test::pass("respray_outcome: no stars paints, unseen clears, seen keeps");
}

VehicleAgent pursuing_officer() {
    VehicleAgent car;
    car.lane_key = 10;
    car.slot = 2;
    car.police_unit = car.police_pursuit = true;
    car.officer.phase = PoliceOfficerPhase::Pursuing;
    car.officer.pos = {0, 0, 0};
    return car;
}

// 12
void clearing_heat_drops_the_whole_pursuit() {
    WantedSystem wanted;
    wanted.set_level(3);
    REQUIRE(wanted.level() == 3 && wanted.heat() > 0.0f);

    const glm::vec3 player{2, 0, 0};
    const std::vector<VehicleAgent> cops{pursuing_officer()};
    const std::vector<VisiblePoliceIdentity> visible{{10, 2}};
    PoliceArrestTracker arrest, arrest_control;
    uint64_t step = 0;
    for (; step + 1 < PoliceArrestTracker::kHoldSteps; ++step) {
        REQUIRE(!arrest.observe(step, true, 3, player, cops, visible));
        REQUIRE(!arrest_control.observe(step, true, 3, player, cops, visible));
    }

    const std::vector<PoliceOffenseWitness> witness{
        {{0.0f, 1.5f, 12.0f}, {0.0f, 0.0f, -1.0f}, true}};
    PoliceOffenseTracker offenses, offenses_control;
    REQUIRE(offenses.observe_armed({}, true, witness, {}));
    REQUIRE(offenses_control.observe_armed({}, true, witness, {}));

    clear_heat_after_respray(wanted, offenses, arrest);
    REQUIRE(wanted.level() == 0 && wanted.heat() == 0.0f);
    // The control's hold is one tick from an arrest; the cleared one starts over.
    REQUIRE(arrest_control.observe(step, true, 3, player, cops, visible).has_value());
    REQUIRE(!arrest.observe(step, true, 3, player, cops, visible));
    // The control has already reported this draw; the cleared tracker has not.
    REQUIRE(!offenses_control.observe_armed({}, true, witness, {}));
    REQUIRE(offenses.observe_armed({}, true, witness, {}));
    apricot_test::pass("clear_heat_after_respray: level 3 to 0, offence and arrest trackers reset");
}

// 13
void the_spray_holds_the_car_and_keeps_the_buttons() {
    const InputFrame in{-0.7f, 1.0f, 0.3f, 0.0f, 0.01f, -0.02f,
                        kBtnAccept | kBtnShiftUp, kBtnAccept};
    const InputFrame out = hold_for_respray(in);
    REQUIRE(out.steer == 0.0f && out.throttle == 0.0f && out.brake == 0.0f);
    REQUIRE(out.handbrake == 1.0f);
    REQUIRE(out.look_dx == in.look_dx && out.look_dy == in.look_dy);
    REQUIRE(out.held == in.held && out.pressed == in.pressed);
    REQUIRE(was_pressed(out, kBtnAccept) && is_held(out, kBtnAccept));
    apricot_test::pass("hold_for_respray zeroes the drive axes, sets the handbrake, keeps Accept");
}

// 14
void the_exit_grace_is_a_quarter_second() {
    ResprayVisit v;
    REQUIRE(!respray_blocks_exit(v));
    v.spray_s = 0.0f;
    REQUIRE(respray_blocks_exit(v));
    v.spray_s = 0.1f;
    REQUIRE(respray_blocks_exit(v));
    v.spray_s = kResprayExitGraceSeconds;
    REQUIRE(!respray_blocks_exit(v));
    v.spray_s = 0.3f;
    REQUIRE(!respray_blocks_exit(v));
    apricot_test::pass("Accept is refused for the first 0.25 s of a spray and never otherwise");
}

// 15
void the_lot_and_the_hint_table(const Shop& shop) {
    for (float bay : kBays) {
        const auto stripes = shop.at(bay, 9.0f);
        REQUIRE(on_repair_lot(stripes) && !repair_bay_fits(stripes, shop.tuning));
        REQUIRE(!on_repair_lot(shop.at(bay, 20.0f)));
        REQUIRE(on_repair_lot(shop.at(bay, -5.0f)));
        // Stopped between the stripes: the hint says to pull all the way in,
        // and an order there does nothing.
        Drive d{shop, bay};
        REQUIRE(d.step(stripes, false, &kRed).event == ResprayEvent::Rejected);
        ResprayHintInput in;
        in.driving = true;
        in.on_lot = on_repair_lot(stripes);
        in.fits = repair_bay_fits(stripes, shop.tuning);
        in.visit = &d.visit;
        REQUIRE(respray_hint(in) == RespraySuffix::LotPullIn);
        REQUIRE(std::strcmp(respray_hint_text(respray_hint(in)),
                            "PULL ALL THE WAY INTO A BAY - REPAIR + RESPRAY") == 0);
    }
    REQUIRE(!on_repair_lot(shop.at(17.5f, 0.0f)) && on_repair_lot(shop.at(16.5f, 0.0f)));

    ResprayVisit outside;
    ResprayVisit fitted_not_pulled;
    fitted_not_pulled.in_bay = true;
    ResprayVisit rolling;
    rolling.in_bay = rolling.pulled_in = true;
    ResprayVisit rolling_seen = rolling;
    rolling_seen.seen = true;
    ResprayVisit arrived = rolling;
    arrived.arrived = true;
    ResprayVisit arrived_seen = arrived;
    arrived_seen.seen = true;
    ResprayVisit spraying = arrived;
    spraying.spray_s = 0.5f;

    struct Row {
        const char* name;
        bool driving, on_lot, fits, pending, eyes;
        int wanted;
        const ResprayVisit* visit;
        RespraySuffix expected;
        const char* text;
    };
    const Row rows[] = {
        {"on foot", false, true, true, false, false, 0, &arrived, RespraySuffix::None, ""},
        {"no visit", true, true, true, false, false, 0, nullptr, RespraySuffix::None, ""},
        {"off the lot", true, false, false, false, true, 3, &outside, RespraySuffix::None, ""},
        {"lot", true, true, false, false, false, 0, &outside, RespraySuffix::LotPullIn,
         "PULL ALL THE WAY INTO A BAY - REPAIR + RESPRAY"},
        {"lot wanted seen", true, true, false, false, true, 2, &outside, RespraySuffix::LotWantedSeen,
         "PULL INTO A BAY TO LOSE THE COPS - A COP CAN SEE YOU"},
        {"lot wanted clear", true, true, false, false, false, 2, &outside, RespraySuffix::LotWantedClear,
         "PULL INTO A BAY TO LOSE THE COPS - NO COP SEES YOU"},
        {"rolling in", true, true, true, false, false, 0, &rolling, RespraySuffix::LotPullIn,
         "PULL ALL THE WAY INTO A BAY - REPAIR + RESPRAY"},
        {"rolling in, latched", true, true, true, false, false, 1, &rolling_seen,
         RespraySuffix::LotWantedSeen, "PULL INTO A BAY TO LOSE THE COPS - A COP CAN SEE YOU"},
        {"fitted, not pulled in", true, true, true, false, true, 4, &fitted_not_pulled,
         RespraySuffix::NeedsPullIn, "DRIVE OUT AND PULL IN TO RESPRAY"},
        {"ready", true, true, true, false, true, 0, &arrived, RespraySuffix::Ready, "R / X - RESPRAY"},
        {"ready unseen", true, true, true, false, true, 3, &arrived, RespraySuffix::ReadyUnseen,
         "R / X - RESPRAY TO LOSE THE COPS"},
        {"ready seen", true, true, true, false, false, 3, &arrived_seen, RespraySuffix::ReadySeen,
         "R / X - RESPRAY (STARS STAY - A COP SAW YOU PULL IN)"},
        {"spraying", true, true, true, false, false, 3, &spraying, RespraySuffix::Spraying,
         "RESPRAYING - E / A TO ABORT"},
        {"pending in bay", true, true, true, true, false, 0, &arrived, RespraySuffix::Unavailable,
         "RESPRAY NOT AVAILABLE FOR THIS CAR YET"},
        {"pending on lot", true, true, false, true, false, 2, &outside, RespraySuffix::Unavailable,
         "RESPRAY NOT AVAILABLE FOR THIS CAR YET"},
    };
    bool covered[10] = {};
    for (const Row& row : rows) {
        ResprayHintInput in;
        in.driving = row.driving;
        in.on_lot = row.on_lot;
        in.fits = row.fits;
        in.pending_car = row.pending;
        in.eyes_on = row.eyes;
        in.wanted_level = row.wanted;
        in.visit = row.visit;
        const RespraySuffix got = respray_hint(in);
        REQUIRE_MSG(got == row.expected, "respray_hint", row.name);
        REQUIRE_MSG(std::strcmp(respray_hint_text(got), row.text) == 0, "respray_hint_text", row.name);
        covered[static_cast<size_t>(got)] = true;
    }
    for (bool c : covered) REQUIRE_MSG(c, "a suffix has no row in the table", "coverage");
    static_assert(respray_hint(ResprayHintInput{}) == RespraySuffix::None);
    apricot_test::pass("on_repair_lot bounds, and respray_hint covers every suffix with its exact text");
}

// 16 is repair_shop_tests itself, unmodified. This pins the extraction the
// other way: repair_shop_ready is the old predicate, sampled densely.
void the_extracted_predicate_is_the_old_one(const Shop& shop) {
    const VehicleState base = shop.at(-9.0f, -5.0f);
    int checked = 0, ready = 0;
    for (float x = -20.0f; x <= 20.0f; x += 0.25f) {
        for (float z = -18.0f; z <= 8.0f; z += 0.25f) {
            for (float yaw : {0.0f, 0.2f, 1.5707964f, 3.1415927f}) {
                for (float tilt : {0.0f, 0.7f}) {
                    for (float dy : {0.0f, 2.5f}) {
                        for (float speed : {0.0f, 0.5f, 0.51f}) {
                            VehicleState car = base;
                            car.position = {
                                kSite.origin.x + kSite.cos_yaw * x + kSite.sin_yaw * z,
                                kSite.ground_m + dy,
                                kSite.origin.z - kSite.sin_yaw * x + kSite.cos_yaw * z};
                            car.orientation =
                                base.orientation * glm::angleAxis(yaw, glm::vec3{0, 1, 0}) *
                                glm::angleAxis(tilt, glm::vec3{0, 0, 1});
                            car.velocity = glm::vec3{speed, 0, 0};
                            const bool now = repair_shop_ready(car, shop.tuning);
                            REQUIRE(now == legacy_repair_shop_ready(car, shop.tuning));
                            if (now) REQUIRE(repair_bay_fits(car, shop.tuning));
                            ready += now ? 1 : 0;
                            ++checked;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(ready > 0 && ready < checked);
    apricot_test::pass("repair_shop_ready matches the pre-extraction predicate over the whole lot");
}

}  // namespace

int main() {
    TerrainCollider ground(city::kMapSeed);
    const Shop shop{ground, VehicleTuning{}};
    unseen_roll_in_completes_after_one_second(shop);
    a_sighting_while_pulling_in_latches(shop);
    a_sighting_after_arrival_does_not_count(shop);
    leaving_and_returning_unseen_resets(shop);
    a_wobble_cancels_the_spray_but_keeps_the_latch(shop);
    an_order_that_cannot_start_is_rejected(shop);
    leaving_the_wheel_or_the_car_mid_spray_cancels(shop);
    exit_and_reenter_in_the_bay_never_arrives(shop);
    a_visit_that_starts_fitted_never_arrives(shop);
    a_crawl_in_arrives_on_the_first_fitted_step(shop);
    outcome_truth_table();
    clearing_heat_drops_the_whole_pursuit();
    the_spray_holds_the_car_and_keeps_the_buttons();
    the_exit_grace_is_a_quarter_second();
    the_lot_and_the_hint_table(shop);
    the_extracted_predicate_is_the_old_one(shop);
    return apricot_test::done("respray_shop_tests");
}

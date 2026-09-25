// The car bomb's sim rules (game/car_bomb.h), stepped against real cars placed
// by spawn_vehicle on the real collider at Rook's, a real respray visit for the
// bay gate, a real crowd for the blast, and the real vehicle step for the wreck.
#include <cmath>
#include <vector>

#include "core/fixed_step.h"
#include "game/car_bomb.h"
#include "game/respray_shop.h"
#include "game/vehicle_interaction.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "traffic/crowd.h"
#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;
namespace {

const auto& kSite = city::kAutoRepairSite;
constexpr float kBay = -9.0f;
const float kDt = static_cast<float>(kSimDt);

struct Shop {
    const TerrainCollider& ground;
    VehicleTuning tuning;

    // Bay-local (x, z), nose toward the back wall; z grows toward the street.
    VehicleState at(float x, float z, float speed = 0.0f) const {
        auto car = spawn_vehicle(tuning, ground,
            kSite.origin.x + kSite.cos_yaw * x + kSite.sin_yaw * z,
            kSite.origin.z - kSite.sin_yaw * x + kSite.cos_yaw * z,
            std::atan2(kSite.sin_yaw, kSite.cos_yaw));
        car.velocity = vehicle_forward(car) * speed;
        return car;
    }
    // Well off the lot, out in the street.
    VehicleState away() const { return at(kBay, 60.0f); }
};

// One car's visit to a bay, and the bomb, stepped together as the App steps
// them: the respray visit first, then the bomb reading its gate.
struct Garage {
    explicit Garage(const Shop& s) : shop(s) {}
    const Shop& shop;
    ResprayVisit visit;
    CarBomb bomb;
    uint64_t driven = 7;

    CarBombResult step(const VehicleState& car, bool trigger, bool driving = true,
                       const VehicleState* rigged = nullptr, bool rigged_exists = true) {
        step_respray_shop(visit, car, shop.tuning, kDt, driving, driven, false, nullptr);
        CarBombInput in;
        in.trigger = trigger;
        in.driven_vehicle = driving ? driven : 0u;
        in.bay_ready = driving && visit.arrived && !visit.spraying() &&
                       repair_shop_ready(car, shop.tuning);
        // The rigged car is the driven one unless the test says otherwise.
        const VehicleState& where = rigged ? *rigged : car;
        in.rigged_exists = rigged_exists;
        in.rigged_on_lot = on_repair_lot(where);
        return step_car_bomb(bomb, in);
    }

    void pull_in() {
        REQUIRE(step(shop.at(kBay, 12.0f, 3.0f), false).event == CarBombEvent::None);
        REQUIRE(step(shop.at(kBay, 0.0f, 2.0f), false).event == CarBombEvent::None);
        REQUIRE(step(shop.at(kBay, -5.0f), false).event == CarBombEvent::None);
        REQUIRE(visit.arrived);
    }
};

void fits_only_stopped_in_a_bay_after_pulling_in(const Shop& shop) {
    {
        Garage g(shop);
        // Out in the street: nothing to fit, nothing to set off.
        REQUIRE(g.step(shop.away(), true).event == CarBombEvent::NoBomb);
        REQUIRE(!g.bomb.rigged);
    }
    {
        Garage g(shop);
        // Rolling through the bay has not arrived yet.
        g.step(shop.at(kBay, 12.0f, 3.0f), false);
        REQUIRE(g.step(shop.at(kBay, 0.0f, 2.0f), true).event == CarBombEvent::NoBomb);
        REQUIRE(!g.bomb.rigged);
    }
    {
        Garage g(shop);
        // A car that starts in the bay never arrives: the respray's latch.
        REQUIRE(g.step(shop.at(kBay, -5.0f), true).event == CarBombEvent::NoBomb);
    }
    {
        Garage g(shop);
        g.pull_in();
        // On foot beside it does not fit a bomb.
        REQUIRE(g.step(shop.at(kBay, -5.0f), true, /*driving=*/false).event ==
                CarBombEvent::NoBomb);
    }
    Garage g(shop);
    g.pull_in();
    const CarBombResult fitted = g.step(shop.at(kBay, -5.0f), true);
    REQUIRE(fitted.event == CarBombEvent::Fitted);
    REQUIRE(fitted.vehicle == g.driven && fitted.moved_from == 0u);
    REQUIRE(g.bomb.rigged && g.bomb.vehicle == g.driven);
    apricot_test::pass("a bomb goes on only in a bay the car pulled into and stopped in");
}

void never_on_the_lot_and_once_off_it(const Shop& shop) {
    Garage g(shop);
    g.pull_in();
    REQUIRE(g.step(shop.at(kBay, -5.0f), true).event == CarBombEvent::Fitted);
    // The tap that fitted it, again, a step later: still in the bay, refused,
    // and the rigged car is not refitted either.
    const CarBombResult again = g.step(shop.at(kBay, -5.0f), true);
    REQUIRE(again.event == CarBombEvent::OnLot);
    REQUIRE(g.bomb.rigged);
    // Out on the forecourt is still the lot.
    const VehicleState forecourt = shop.at(kBay, 12.0f);
    REQUIRE(on_repair_lot(forecourt));
    REQUIRE(g.step(forecourt, true).event == CarBombEvent::OnLot);
    // Parked on the lot and walked away from: the car on the lot is what counts.
    REQUIRE(g.step(shop.away(), true, false, &forecourt).event == CarBombEvent::OnLot);
    // Off the lot it goes, and it is spent.
    const VehicleState street = shop.away();
    REQUIRE(!on_repair_lot(street));
    const CarBombResult boom = g.step(street, true, false, &street);
    REQUIRE(boom.event == CarBombEvent::Detonated);
    REQUIRE(boom.vehicle == g.driven);
    REQUIRE(!g.bomb.rigged);
    REQUIRE(g.step(street, true, false, &street).event == CarBombEvent::NoBomb);
    // No press, no event, wherever it is.
    Garage quiet(shop);
    quiet.pull_in();
    quiet.step(shop.at(kBay, -5.0f), true);
    for (int i = 0; i < 120; ++i)
        REQUIRE(quiet.step(street, false, true, &street).event == CarBombEvent::None);
    REQUIRE(quiet.bomb.rigged);
    apricot_test::pass("refused anywhere on Rook's lot; off it one press sets it off, once");
}

void a_second_car_takes_the_bomb(const Shop& shop) {
    Garage g(shop);
    g.pull_in();
    REQUIRE(g.step(shop.at(kBay, -5.0f), true).event == CarBombEvent::Fitted);
    const uint64_t first = g.driven;
    // A different car, a fresh visit.
    g.driven = 99;
    g.visit = {};
    g.pull_in();
    const VehicleState second = shop.at(kBay, -5.0f);
    const CarBombResult moved = g.step(second, true);
    REQUIRE(moved.event == CarBombEvent::Fitted);
    REQUIRE(moved.vehicle == 99u && moved.moved_from == first);
    REQUIRE(g.bomb.vehicle == 99u);
    apricot_test::pass("one bomb at a time: fitting a second car disarms the first");
}

void a_car_that_leaves_the_world_takes_the_bomb(const Shop& shop) {
    Garage g(shop);
    g.pull_in();
    REQUIRE(g.step(shop.at(kBay, -5.0f), true).event == CarBombEvent::Fitted);
    const VehicleState street = shop.away();
    const CarBombResult lost = g.step(street, false, false, &street, /*exists=*/false);
    REQUIRE(lost.event == CarBombEvent::Lost && !g.bomb.rigged);
    REQUIRE(g.step(street, true, false, &street, false).event == CarBombEvent::NoBomb);
    // Lost and a fresh fit on the same step: the fit wins, the bomb is new.
    Garage h(shop);
    h.pull_in();
    REQUIRE(h.step(shop.at(kBay, -5.0f), true).event == CarBombEvent::Fitted);
    h.driven = 42;
    h.visit = {};
    h.pull_in();
    const CarBombResult refit = h.step(shop.at(kBay, -5.0f), true, true, nullptr, false);
    REQUIRE(refit.event == CarBombEvent::Fitted && refit.moved_from == 0u);
    REQUIRE(h.bomb.rigged && h.bomb.vehicle == 42u);
    apricot_test::pass("a rigged car that leaves the world takes its bomb with it");
}

void the_blast_falls_off_and_kills_at_the_car() {
    REQUIRE(car_bomb_damage(0.0f) > kBodyHealth);
    REQUIRE(car_bomb_damage(kCarBombLethalRadiusM) > kBodyHealth);
    // One step past the lethal radius a body survives, hurt.
    const float edge = car_bomb_damage(kCarBombLethalRadiusM + 0.01f);
    REQUIRE(edge < kBodyHealth && edge > kBodyHealth * 0.5f);
    REQUIRE(car_bomb_damage(kCarBombReachM) == 0.0f);
    REQUIRE(car_bomb_damage(50.0f) == 0.0f);
    REQUIRE(car_bomb_damage(-1.0f) == 0.0f);
    REQUIRE(car_bomb_damage(std::nanf("")) == 0.0f);
    float last = car_bomb_damage(kCarBombLethalRadiusM + 0.01f);
    for (float d = kCarBombLethalRadiusM + 0.25f; d < kCarBombReachM; d += 0.25f) {
        const float now = car_bomb_damage(d);
        REQUIRE(now < last);
        last = now;
    }
    REQUIRE(kCarBombHeat > kArsonHeat);
    apricot_test::pass("lethal at the car, survivable past it, nothing past ten metres");
}

void the_wreck_will_not_drive(const Shop& shop) {
    VehicleState car = shop.away();
    for (int i = 0; i < 120; ++i)
        car = step_unoccupied_vehicle(car, shop.tuning, shop.ground, kDt);
    const glm::vec3 parked = car.position;
    wreck_car_bomb_vehicle(car);
    REQUIRE(car.health == 0.0f);
    REQUIRE(vehicle_engine_failed(car.mechanical));
    for (float zone : car.body_damage.zones) REQUIRE(zone >= 0.9f);
    InputFrame floor_it;
    floor_it.throttle = 1.0f;
    for (int i = 0; i < 600; ++i) {
        car = step_vehicle(car, shop.tuning, floor_it, shop.ground, kDt);
        step_vehicle_mechanical(car.mechanical, car.body_damage, kDt, car.mechanical_key);
    }
    const glm::vec3 moved = car.position - parked;
    REQUIRE_MSG(glm::length(glm::vec2{moved.x, moved.z}) < 0.5f,
                "a burnt-out car drove on full throttle", "the_wreck_will_not_drive");
    REQUIRE(vehicle_engine_failed(car.mechanical));
    apricot_test::pass("five seconds of full throttle does not move a burnt-out car");
}

void the_car_goes_up_and_comes_down(const Shop& shop) {
    const auto run = [&](uint64_t identity, float& peak) {
        VehicleState car = shop.away();
        for (int i = 0; i < 120; ++i)
            car = step_unoccupied_vehicle(car, shop.tuning, shop.ground, kDt);
        const float rest = car.position.y;
        wreck_car_bomb_vehicle(car);
        launch_car_bomb_vehicle(car, shop.tuning, identity);
        peak = rest;
        for (int i = 0; i < 600; ++i) {
            car = step_unoccupied_vehicle(car, shop.tuning, shop.ground, kDt);
            peak = std::max(peak, car.position.y);
        }
        REQUIRE(std::isfinite(car.position.y));
        REQUIRE_MSG(peak > rest + 1.0f, "the blast did not lift the car", "launch");
        REQUIRE_MSG(std::fabs(car.position.y - rest) < 0.5f && glm::length(car.velocity) < 0.5f,
                    "the car did not come back down and settle", "launch");
        REQUIRE_MSG(vehicle_up(car).y > 0.8f, "the wreck did not land on its wheels", "launch");
        return car;
    };
    float peak_a = 0.0f, peak_b = 0.0f;
    const VehicleState a = run(5, peak_a);
    const VehicleState b = run(5, peak_b);
    REQUIRE(a.position == b.position && peak_a == peak_b);
    apricot_test::pass("the blast lifts the car over a metre, it lands, and it replays");
}

float elevated_ground(const void*, float, float) { return 1000.f; }

void the_blast_throws_the_crowd() {
    RoadGraph roads;
    LaneGraph lanes;
    Crowd crowd;
    CrowdTuning tuning;
    const GroundSampler ground{elevated_ground, nullptr};
    roads.build(make_grid_spines(4, 62.f), RoadGraphParams{}, ground);
    lanes.build(roads, ground);
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    tuning.ped_activate_m = 400.f;
    tuning.ped_retire_m = 600.f;
    crowd.build(lanes, 0x424F4D42ull, ambient, tuning);
    crowd.refresh(0, {93.f, 93.f});
    crowd.rebuild_buckets();
    crowd.step_peds(0);
    REQUIRE(crowd.peds().size() > 40);

    // Set the bomb off beside somebody, so the query has someone to find.
    const PedAgent& victim = crowd.peds().front();
    const glm::vec3 origin = victim.pos + glm::vec3{1.0f, 0.9f, 0.0f};
    const auto targets = crowd.standing_within(origin, kCarBombReachM);
    REQUIRE(!targets.empty());
    for (const auto& t : targets) {
        REQUIRE(!t.officer);
        REQUIRE(glm::distance(t.body, origin) <= kCarBombReachM + 1e-3f);
    }
    // Nobody standing outside the reach is in the list.
    std::size_t inside = 0;
    for (const PedAgent& p : crowd.peds())
        if (!ped_is_floored(p.activity) &&
            glm::distance(p.pos + glm::vec3{0.f, 1.2f, 0.f}, origin) <= kCarBombReachM) ++inside;
    REQUIRE(inside == targets.size());

    unsigned killed = 0;
    for (const auto& t : targets) {
        const float d = glm::distance(t.body, origin);
        const PedShotHit hit = crowd.blast_ped(t, origin, car_bomb_damage(d), 8.0f, 1);
        REQUIRE(hit.hit);
        REQUIRE(hit.killed == (d <= kCarBombLethalRadiusM));
        if (hit.killed) ++killed;
    }
    REQUIRE(killed >= 1u);
    // The dead are thrown outward and up, and the sim carries them there.
    for (const PedAgent& p : crowd.peds()) {
        if (p.activity != PedActivity::Dead) continue;
        const glm::vec2 out{p.pos.x + 1e-3f - origin.x, p.pos.z - origin.z};
        REQUIRE(p.impact_velocity.y > 0.0f);
        REQUIRE(glm::dot(glm::vec2{p.impact_velocity.x, p.impact_velocity.z}, out) > 0.0f);
        REQUIRE(!p.impact_from_bullet);
    }
    // A second blast finds nobody it already floored, and a dead body is not
    // hit twice.
    for (const auto& t : crowd.standing_within(origin, kCarBombReachM))
        REQUIRE(glm::distance(t.body, origin) > kCarBombLethalRadiusM);
    for (const auto& t : targets) {
        if (glm::distance(t.body, origin) > kCarBombLethalRadiusM) continue;
        const PedShotHit twice = crowd.blast_ped(t, origin, 500.0f, 8.0f, 2);
        REQUIRE(!twice.hit && !twice.killed);
    }
    for (int step = 2; step < 240; ++step) crowd.step_peds(step);
    for (const PedAgent& p : crowd.peds())
        if (p.activity == PedActivity::Dead) REQUIRE(std::isfinite(p.impact_offset.x));
    apricot_test::pass("the blast floors the people at the car and throws them outward");
}

void the_hint_table() {
    CarBombHintInput in;
    REQUIRE(car_bomb_hint(in) == CarBombHint::None);
    in.driving = true;
    in.bay_ready = true;
    REQUIRE(car_bomb_hint(in) == CarBombHint::CanFit);
    in.rigged = true;
    REQUIRE(car_bomb_hint(in) == CarBombHint::CanFit);  // another car: the bomb would move
    in.in_rigged_car = true;
    in.rigged_on_lot = true;
    REQUIRE(car_bomb_hint(in) == CarBombHint::FittedOnLot);
    in.bay_ready = false;
    in.rigged_on_lot = false;
    REQUIRE(car_bomb_hint(in) == CarBombHint::Armed);
    in.driving = false;
    in.in_rigged_car = false;
    REQUIRE(car_bomb_hint(in) == CarBombHint::Armed);  // on foot, the detonator is in hand
    for (auto h : {CarBombHint::CanFit, CarBombHint::FittedOnLot, CarBombHint::Armed})
        REQUIRE(*car_bomb_hint_text(h) != '\0');
    REQUIRE(*car_bomb_hint_text(CarBombHint::None) == '\0');
    apricot_test::pass("the prompt says fit in the bay, arm off the lot, K from anywhere");
}

}  // namespace

int main() {
    TerrainCollider ground(city::kMapSeed);
    const Shop shop{ground, VehicleTuning{}};
    fits_only_stopped_in_a_bay_after_pulling_in(shop);
    never_on_the_lot_and_once_off_it(shop);
    a_second_car_takes_the_bomb(shop);
    a_car_that_leaves_the_world_takes_the_bomb(shop);
    the_blast_falls_off_and_kills_at_the_car();
    the_wreck_will_not_drive(shop);
    the_car_goes_up_and_comes_down(shop);
    the_blast_throws_the_crowd();
    the_hint_table();
    return apricot_test::done("car_bomb_tests");
}

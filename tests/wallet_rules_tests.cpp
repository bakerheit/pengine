#include <cstring>
#include <optional>
#include <vector>

#include "city/authored_staff.h"
#include "game/delivery_mission.h"
#include "game/player_vitals.h"
#include "game/police_arrest.h"
#include "game/wallet_rules.h"
#include "game/wanted_system.h"
#include "test_assert.h"

using namespace apricot;
namespace {

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

void amounts_hang_together() {
    // A new game cannot buy a pistol (~$500) outright; one delivery can.
    REQUIRE(kNewGameCash > 0 && kNewGameCash < 500);
    REQUIRE(kNewGameCash + kDeliveryPayout >= 500 + 50);
    REQUIRE(kHospitalBill > 0 && kHospitalBill < kDeliveryPayout);
    REQUIRE(arrest_fine(0) == 0 && arrest_fine(-3) == 0);
    REQUIRE(arrest_fine(1) == 200 && arrest_fine(2) == 400 && arrest_fine(3) == 800);
    REQUIRE(arrest_fine(4) == 1600 && arrest_fine(5) == 3200);
    REQUIRE(arrest_fine(9) == arrest_fine(5));  // The HUD has five stars.
    for (int stars = 1; stars < 5; ++stars)
        REQUIRE(arrest_fine(stars + 1) == 2 * arrest_fine(stars));
    apricot_test::pass("start, payout, bill and a fine that doubles per star agree with each other");
}

void cash_reads_like_money() {
    char s[32];
    format_cash(s, sizeof(s), 0);             REQUIRE(same(s, "$0"));
    format_cash(s, sizeof(s), 999);           REQUIRE(same(s, "$999"));
    format_cash(s, sizeof(s), 1000);          REQUIRE(same(s, "$1,000"));
    format_cash(s, sizeof(s), 1234567);       REQUIRE(same(s, "$1,234,567"));
    format_cash(s, sizeof(s), kMaxCash);      REQUIRE(same(s, "$999,999,999"));
    format_cash(s, sizeof(s), 750, true);     REQUIRE(same(s, "+$750"));
    format_cash(s, sizeof(s), -3200, true);   REQUIRE(same(s, "-$3,200"));
    format_cash(s, sizeof(s), -250);          REQUIRE(same(s, "-$250"));
    char tiny[4];
    format_cash(tiny, sizeof(tiny), 123456);  // Truncates, never overruns.
    REQUIRE(same(tiny, "$12"));
    apricot_test::pass("the counter groups thousands and signs a delta");
}

void penalties_say_what_was_paid() {
    PlayerEconomy e;
    e.cash = 1000;
    CashPenalty p = charge_penalty(e, 400);
    REQUIRE(p.billed == 400 && p.taken == 400 && e.cash == 600);
    char line[96];
    format_penalty_line(line, sizeof(line), "FINE", p);
    REQUIRE(same(line, "FINE $400"));

    e.cash = 350;
    p = charge_penalty(e, arrest_fine(5));
    REQUIRE(p.billed == 3200 && p.taken == 350 && e.cash == 0);
    format_penalty_line(line, sizeof(line), "FINE", p);
    REQUIRE(same(line, "FINE $3,200  -  PAID $350"));

    p = charge_penalty(e, kHospitalBill);
    REQUIRE(p.taken == 0 && e.cash == 0);
    format_penalty_line(line, sizeof(line), "HOSPITAL BILL", p);
    REQUIRE(same(line, "HOSPITAL BILL $250  -  NO CASH ON YOU"));
    apricot_test::pass("a short wallet pays what it has, and the banner says so");
}

void the_flash_follows_the_real_balance() {
    CashFlash f;
    f.observe(250, 0.016f);  // The first look primes; it is not a payout.
    REQUIRE(!f.showing());
    f.observe(1000, 0.016f);
    REQUIRE(f.showing() && f.delta == 750);
    f.observe(1000, 0.5f);
    REQUIRE(f.showing() && f.delta == 750);
    // Two purchases in a row read as one running total...
    f.resync(1000);
    f.observe(500, 0.016f);
    f.observe(450, 0.2f);
    REQUIRE(f.delta == -550);
    // ...and a change the other way replaces it rather than netting it.
    f.observe(1450, 0.2f);
    REQUIRE(f.delta == 1000);
    // It goes away on its own, fading through the last part.
    f.observe(1450, CashFlash::kShowSeconds - 0.3f);
    REQUIRE(f.showing() && f.alpha() < 1.0f && f.alpha() > 0.0f);
    f.observe(1450, 0.31f);
    REQUIRE(!f.showing() && f.delta == 0);
    // A load swaps the balance without anybody paying anything.
    f.resync(99);
    f.observe(99, 0.016f);
    REQUIRE(!f.showing());
    apricot_test::pass("the delta flash reads the wallet, adds up like changes and expires");
}

// The real producers the app hangs the money on, each of which must fire ONCE:
// a payout, fine or bill on a repeated event is money created or destroyed.
void each_event_charges_once() {
    // The delivery: complete_delivery() flips the stage once.
    PlayerEconomy e;
    MissionStage stage = MissionStage::DeliveryActive;
    const glm::vec3 devon = city::devon_position();
    for (int i = 0; i < 3; ++i)
        if (complete_delivery(stage, devon, true)) earn_cash(e, kDeliveryPayout);
    REQUIRE(stage == MissionStage::DeliveryComplete);
    REQUIRE(e.cash == kNewGameCash + kDeliveryPayout);

    // Death: take_damage() reports the killing blow once.
    PlayerVitals vitals;
    int bills = 0;
    for (int i = 0; i < 4; ++i)
        if (vitals.take_damage(1.0e6f)) { charge_penalty(e, kHospitalBill); ++bills; }
    REQUIRE(bills == 1 && e.cash == kNewGameCash + kDeliveryPayout - kHospitalBill);

    // Arrest: a real tracker and a real wanted level. The fine is priced off
    // the level the player was arrested at, read before the reset.
    WantedSystem wanted;
    wanted.set_level(2);
    PoliceArrestTracker tracker;
    VehicleAgent cop;
    cop.lane_key = 10;
    cop.slot = 2;
    cop.police_unit = cop.police_pursuit = true;
    cop.officer.phase = PoliceOfficerPhase::Pursuing;
    cop.officer.pos = {0, 0, 0};
    const std::vector<VehicleAgent> cars{cop};
    const std::vector<VisiblePoliceIdentity> visible{{10, 2}};
    const int64_t before = e.cash;
    int arrests = 0;
    for (uint64_t step = 0; step < 1200; ++step) {
        if (!tracker.observe(step, true, wanted.level(), {1, 0, 0}, cars, visible)) continue;
        charge_penalty(e, arrest_fine(wanted.level()));
        wanted.reset();
        ++arrests;
    }
    REQUIRE(arrests == 1);
    REQUIRE(e.cash == before - arrest_fine(2));
    apricot_test::pass("the delivery, a death and an arrest each move the wallet exactly once");
}

}  // namespace

int main() {
    amounts_hang_together();
    cash_reads_like_money();
    penalties_say_what_was_paid();
    the_flash_follows_the_real_balance();
    each_event_charges_once();
    return apricot_test::done("wallet_rules_tests");
}

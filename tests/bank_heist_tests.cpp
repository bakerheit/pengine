#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "city/bank_heist_layout.h"
#include "city/bank_vault_layout.h"
#include "game/bank_heist.h"
#include "game/character.h"
#include "game/save_game.h"
#include "game/wanted_system.h"
#include "test_assert.h"

using namespace apricot;
namespace {

constexpr float kDt = static_cast<float>(kSimDt);

glm::vec3 world(glm::vec3 p) {
    const auto& s = city::kBankSite;
    return {s.origin.x + s.cos_yaw * p.x + s.sin_yaw * p.z, s.ground_m + p.y,
            s.origin.z - s.sin_yaw * p.x + s.cos_yaw * p.z};
}
glm::vec3 centre(const city::StartPart& p) {
    return world({p.centre.x, p.bottom_m + p.height_m * 0.5f, p.centre.z});
}
glm::vec3 half(const city::StartPart& p) {
    return glm::vec3{p.width_m, p.height_m, p.depth_m} * 0.5f;
}
float yaw(const city::StartPart& p) {
    return std::atan2(city::kBankSite.sin_yaw, city::kBankSite.cos_yaw) +
           glm::radians(p.yaw_deg);
}

// Takes pile i the way the app does: the heat goes to a real WantedSystem.
BankHeistGrab grab(BankHeistState& s, WantedSystem& wanted, int i, uint64_t step) {
    const BankHeistGrab g = bank_heist_take(s, i, wanted.heat(), step);
    if (g.taken) wanted.add_heat(g.heat, WantedSystem::Crime::Other);
    return g;
}

void loot_is_taken_once() {
    REQUIRE(bank_heist_vault_total() == 43'500);
    BankHeistState s;
    WantedSystem wanted;
    int64_t sum = 0;
    for (std::size_t i = 0; i < kBankHeistPiles.size(); ++i) {
        const BankHeistGrab g = grab(s, wanted, static_cast<int>(i), 1000);
        REQUIRE(g.taken);
        REQUIRE(g.value == kBankHeistPiles[i].value);
        REQUIRE(g.tripped_alarm == (i == 0));
        sum += g.value;
        const BankHeistGrab again = grab(s, wanted, static_cast<int>(i), 1001);
        REQUIRE(!again.taken && again.value == 0 && again.heat == 0.0f);
    }
    REQUIRE(sum == bank_heist_vault_total());
    REQUIRE(s.carried == bank_heist_vault_total());
    REQUIRE(s.taken == kBankHeistAllPiles);
    REQUIRE(!grab(s, wanted, -1, 1).taken);
    REQUIRE(!grab(s, wanted, 5, 1).taken);
    // Nothing left to reach for, anywhere in the room.
    for (const BankHeistPile& p : kBankHeistPiles)
        REQUIRE(bank_heist_target(s, {p.centre.x, 0.2f, p.centre.y - 1.0f}, true) == -1);
    apricot_test::pass("each pile is taken once and the room sums to the vault total");
}

void reach_is_inside_the_vault_on_foot() {
    const BankHeistState s;
    // Beside each table stack, from the north side of the table.
    REQUIRE(bank_heist_target(s, {11.3f, 0.2f, 11.8f}, true) == 0);
    REQUIRE(bank_heist_target(s, {12.5f, 0.2f, 11.8f}, true) == 1);
    REQUIRE(bank_heist_target(s, {13.7f, 0.2f, 11.8f}, true) == 2);
    REQUIRE(bank_heist_target(s, {15.8f, 0.2f, 12.3f}, true) == 3);
    REQUIRE(bank_heist_target(s, {15.8f, 0.2f, 9.6f}, true) == 4);
    REQUIRE(bank_heist_target(s, {11.3f, 0.2f, 11.8f}, false) == -1);   // in a car
    REQUIRE(bank_heist_target(s, {11.3f, 3.4f, 11.8f}, true) == -1);    // on the roof
    REQUIRE(bank_heist_target(s, {11.3f, 0.2f, 6.6f}, true) == -1);     // the lobby, through the wall
    REQUIRE(bank_heist_target(s, {9.0f, 0.2f, 12.5f}, true) == -1);     // out of reach
    // The nearest untaken pile wins; a taken one is skipped.
    BankHeistState part;
    WantedSystem wanted;
    grab(part, wanted, 0, 1);
    REQUIRE(bank_heist_target(part, {11.9f, 0.2f, 11.6f}, true) == 1);
    apricot_test::pass("reach needs the player on foot, in the vault, beside an untaken pile");
}

void alarm_trips_a_real_wanted_level() {
    BankHeistState s;
    WantedSystem wanted;
    REQUIRE(wanted.level() == 0);
    grab(s, wanted, 0, 10);
    REQUIRE(s.live);
    REQUIRE(s.bell_s == kBankHeistBellSeconds);
    REQUIRE(wanted.level() == 3);
    WantedSystem::Crime crime{};
    REQUIRE(wanted.take_crime_report(crime));
    for (int i = 1; i < 5; ++i) grab(s, wanted, i, 10);
    REQUIRE(wanted.level() == 4);  // greed: the whole room is a star more

    // Already wanted for something else: the first grab still lands on three,
    // and never lowers anything.
    BankHeistState s2;
    WantedSystem hot;
    hot.set_level(2);
    grab(s2, hot, 3, 10);
    REQUIRE(hot.level() == 3);
    BankHeistState s3;
    WantedSystem hotter;
    hotter.set_level(4);
    const float before = hotter.heat();
    grab(s3, hotter, 3, 10);
    REQUIRE(hotter.level() == 4 && hotter.heat() > before);
    apricot_test::pass("the first grab puts a real wanted system on three stars, greed on four");
}

void carried_take_is_lost_when_caught() {
    for (int death = 0; death < 2; ++death) {
        BankHeistState s;
        WantedSystem wanted;
        PlayerEconomy wallet;
        const int64_t before = wallet.cash;
        grab(s, wanted, 0, 10);
        grab(s, wanted, 3, 10);
        // Carried while the stars are up: not money yet.
        for (int i = 0; i < 240; ++i) {
            wanted.update(kDt, true, PoliceTuning{});
            REQUIRE(step_bank_heist(s, wallet, wanted.level(), false, 10u + uint64_t(i), kDt)
                        .event == BankHeistEvent::None);
        }
        REQUIRE(wallet.cash == before && s.carried == 18'500);
        // The arrest (or the death) resets the wanted level on the same step;
        // being caught must win over that looking like an escape.
        wanted.reset();
        const BankHeistStep caught = step_bank_heist(s, wallet, wanted.level(), true, 500, kDt);
        REQUIRE(caught.event == BankHeistEvent::Lost);
        REQUIRE(caught.amount == 18'500);
        REQUIRE(wallet.cash == before);
        REQUIRE(s.carried == 0 && !s.live && s.bell_s == 0.0f);
        // The piles stay gone: the loss does not put the money back.
        REQUIRE(bank_heist_pile_taken(s, 0) && bank_heist_pile_taken(s, 3));
        REQUIRE(step_bank_heist(s, wallet, 0, false, 501, kDt).event == BankHeistEvent::None);
        REQUIRE(wallet.cash == before);
    }
    apricot_test::pass("arrest or death before the stars clear loses the whole carried take");
}

void escape_banks_the_take_through_earn_cash() {
    BankHeistState s;
    WantedSystem wanted;
    PlayerEconomy wallet;
    const int64_t before = wallet.cash;
    for (int i = 0; i < 5; ++i) grab(s, wanted, i, 100);
    const PoliceTuning tuning;
    uint64_t step = 100;
    BankHeistStep result;
    // Out of every officer's sight, the real wanted system cools on its own.
    for (int i = 0; i < 120 * 600 && result.event == BankHeistEvent::None; ++i) {
        wanted.update(kDt, false, tuning);
        result = step_bank_heist(s, wallet, wanted.level(), false, ++step, kDt);
        if (result.event == BankHeistEvent::None) REQUIRE(wanted.level() > 0);
    }
    REQUIRE(result.event == BankHeistEvent::Banked);
    REQUIRE(result.amount == bank_heist_vault_total());
    REQUIRE(wallet.cash == before + bank_heist_vault_total());
    REQUIRE(s.carried == 0 && !s.live);
    REQUIRE(step_bank_heist(s, wallet, 0, false, ++step, kDt).event == BankHeistEvent::None);
    REQUIRE(wallet.cash == before + bank_heist_vault_total());

    // A nearly full wallet: earn_cash's cap holds and the step reports what
    // actually went in.
    BankHeistState full;
    WantedSystem w2;
    PlayerEconomy rich;
    rich.cash = kMaxCash - 1'000;
    grab(full, w2, 0, 1);
    const BankHeistStep capped = step_bank_heist(full, rich, 0, false, 2, kDt);
    REQUIRE(capped.event == BankHeistEvent::Banked);
    REQUIRE(capped.amount == 1'000);
    REQUIRE(rich.cash == kMaxCash);
    REQUIRE(valid_player_economy(rich));
    apricot_test::pass("losing the stars banks the take via earn_cash, capped at the wallet's limit");
}

void vault_restocks_after_the_cooldown_once_settled() {
    BankHeistState s;
    WantedSystem wanted;
    PlayerEconomy wallet;
    grab(s, wanted, 1, 1000);
    REQUIRE(s.restock_step == 1000 + kBankHeistRestockSteps);
    // Still live at the restock step: nothing refills under a running heist.
    REQUIRE(step_bank_heist(s, wallet, 3, false, s.restock_step + 5, kDt).event ==
            BankHeistEvent::None);
    REQUIRE(s.live && bank_heist_pile_taken(s, 1));
    step_bank_heist(s, wallet, 0, false, 2000, kDt);  // banked
    REQUIRE(!s.live);

    // Coming back for what was left is a second heist: the alarm trips again,
    // and the restock stays where the first alarm put it.
    const uint64_t restock = s.restock_step;
    WantedSystem again;
    const BankHeistGrab second = grab(s, again, 2, 5000);
    REQUIRE(second.tripped_alarm && again.level() == 3);
    REQUIRE(s.restock_step == restock);
    step_bank_heist(s, wallet, 0, false, 5001, kDt);  // banked again
    REQUIRE(step_bank_heist(s, wallet, 0, false, restock - 1, kDt).event == BankHeistEvent::None);
    REQUIRE(s.taken != 0);
    REQUIRE(step_bank_heist(s, wallet, 0, false, restock, kDt).event ==
            BankHeistEvent::Restocked);
    REQUIRE(s.taken == 0 && s.restock_step == 0 && s.carried == 0);
    REQUIRE(bank_heist_target(s, {11.3f, 0.2f, 11.8f}, true) == 0);
    apricot_test::pass("the vault restocks thirty minutes after the alarm, never mid-heist");
}

void the_vault_state_saves_and_the_take_does_not() {
    GameSave save;
    save.sim_step = 20'000;
    save.character_position = world({12.0f, 0.2f, 11.8f});
    save.car_position = world({0.0f, 0.2f, -14.0f});
    save.bank_loot_taken = 0b01011;
    save.bank_restock_step = 20'000 + kBankHeistRestockSteps - 50;
    std::string bytes, error;
    REQUIRE_MSG(encode_game_save(save, bytes, error), error.c_str(), "encode");
    REQUIRE(bytes.compare(0, 15, "APRICOT_SAVE 7\n") == 0);
    GameSave loaded;
    REQUIRE_MSG(decode_game_save(bytes, loaded, error), error.c_str(), "decode");
    REQUIRE(loaded.bank_loot_taken == save.bank_loot_taken);
    REQUIRE(loaded.bank_restock_step == save.bank_restock_step);
    const BankHeistState s = bank_heist_from_save(loaded.bank_loot_taken, loaded.bank_restock_step);
    REQUIRE(s.taken == 0b01011 && s.carried == 0 && !s.live);
    REQUIRE(bank_heist_target(s, {11.3f, 0.2f, 11.8f}, true) == -1);  // pile 0 gone
    REQUIRE(bank_heist_target(s, {13.7f, 0.2f, 11.8f}, true) == 2);   // pile 2 there

    // Impossible states are refused rather than loaded.
    GameSave bad = save;
    bad.bank_loot_taken = 0x20;
    REQUIRE(!encode_game_save(bad, bytes, error));
    bad = save;
    bad.bank_restock_step = 0;
    REQUIRE(!encode_game_save(bad, bytes, error));
    bad = save;
    bad.bank_restock_step = save.sim_step + kBankHeistRestockSteps + 1;
    REQUIRE(!encode_game_save(bad, bytes, error));
    bad = save;
    bad.bank_loot_taken = 0;
    bad.bank_restock_step = 0;
    REQUIRE(encode_game_save(bad, bytes, error));  // a stocked vault
    apricot_test::pass("save version 6 carries the emptied piles and the restock, and refuses nonsense");
}

void piles_sit_on_what_draws_and_the_carts_are_solid() {
    float table_top = -1.0f;
    for (const auto& p : city::bake_building(city::kBankPlan))
        if (std::strcmp(p.name, "bank vault table") == 0) table_top = p.bottom_m + p.height_m;
    REQUIRE_NEAR(table_top, city::kBankVaultTableTopM, 1e-5);
    for (std::size_t i = 0; i < kBankHeistPiles.size(); ++i) {
        const auto parts = city::bank_heist_pile_parts(i);
        REQUIRE(!parts.empty());
        float low = 1e9f;
        for (const auto& p : parts) {
            REQUIRE(!p.solid);
            REQUIRE(std::strstr(p.name, "bank heist") == p.name);
            low = std::min(low, p.bottom_m);
            REQUIRE(glm::distance(glm::vec2{p.centre.x, p.centre.z}, kBankHeistPiles[i].centre) < 0.7f);
        }
        const float rest = kBankHeistPiles[i].on_cart ? city::kBankHeistCartTopM
                                                      : city::kBankVaultTableTopM;
        REQUIRE_NEAR(low, rest - 0.001f, 1e-4);
    }
    REQUIRE(city::bank_heist_pile_parts(kBankHeistPiles.size()).empty());
    int solid = 0;
    for (const auto& p : city::bank_heist_cart_parts()) solid += p.solid ? 1 : 0;
    REQUIRE(solid == 4);  // body and plinth, two carts
    apricot_test::pass("stacks rest on the drawn table top and bales on the carts; only the carts collide");
}

// The real walking controller, against the real bank, the open door and the
// carts, walks the grab route and finds each pile where the table says.
void real_character_walks_the_grab_route() {
    TerrainCollider collider{city::kMapSeed};
    for (const auto& p : city::bake_building(city::kBankPlan))
        if (p.solid) collider.add_static_oriented_box(centre(p), half(p), yaw(p));
    const auto door = city::bank_vault_leaf(1.0f)[0];
    collider.add_static_oriented_box(centre(door), half(door), yaw(door));
    for (const auto& p : city::bank_heist_cart_parts())
        if (p.solid) collider.add_static_oriented_box(centre(p), half(p), yaw(p));
    const CharacterTuning tuning;
    // The vault's south aisle stays open beside the south cart.
    for (float x = 8.0f; x <= 17.0f; x += 0.1f)
        REQUIRE_MSG(character_position_clear(collider, world({x, 0.2f, 7.65f}), tuning),
                    "a cart blocks the vault's south aisle", "south aisle");
    REQUIRE(!character_position_clear(collider, world({15.8f, 0.2f, 13.3f}), tuning));
    REQUIRE(!character_position_clear(collider, world({15.8f, 0.2f, 8.6f}), tuning));

    PlayerCharacterState player;
    player.position = world({5.4f, 0.2f, 9.0f});  // at the keypad
    struct Stop { glm::vec2 at; int pile; };
    const Stop route[] = {{{5.85f, 11.5f}, -1}, {{8.6f, 11.8f}, -1}, {{11.3f, 11.8f}, 0},
                          {{12.5f, 11.8f}, 1}, {{13.7f, 11.8f}, 2}, {{15.8f, 12.3f}, 3},
                          {{15.8f, 9.6f}, 4}};
    BankHeistState s;
    WantedSystem wanted;
    for (const Stop& stop : route) {
        const glm::vec3 destination = world({stop.at.x, 0.2f, stop.at.y});
        for (int i = 0; i < 1500; ++i) {
            const glm::vec3 delta = destination - player.position;
            const float distance = glm::length(glm::vec2{delta.x, delta.z});
            if (distance < 0.01f) break;
            player.view_yaw = std::atan2(delta.x, -delta.z);
            InputFrame input;
            input.throttle = std::min(1.0f, distance / (tuning.walk_speed_mps * kDt));
            player = step_character(player, tuning, input, collider, kDt);
        }
        REQUIRE_MSG(glm::distance(player.position, destination) < 0.03f,
                    "the walking controller got stuck on the grab route", "grab route");
        const int target = bank_heist_target(s, city::bank_local_position(player.position), true);
        REQUIRE_MSG(target == stop.pile, "wrong pile in reach", "grab route");
        if (target >= 0) REQUIRE(grab(s, wanted, target, 1).taken);
    }
    REQUIRE(s.taken == kBankHeistAllPiles);
    apricot_test::pass("the real character walks in, reaches every pile, and the aisle stays open");
}

}  // namespace

int main() {
    loot_is_taken_once();
    reach_is_inside_the_vault_on_foot();
    alarm_trips_a_real_wanted_level();
    carried_take_is_lost_when_caught();
    escape_banks_the_take_through_earn_cash();
    vault_restocks_after_the_cooldown_once_settled();
    the_vault_state_saves_and_the_take_does_not();
    piles_sit_on_what_draws_and_the_carts_are_solid();
    real_character_walks_the_grab_route();
    std::puts("PASS bank_heist_tests");
}

#include "app/app.h"

#include <cmath>
#include <cstdio>
#include <iterator>

#include <SDL.h>

#include "core/log.h"
#include "game/weapon_ownership.h"

namespace apricot {

// The App's half of Brassline Arms' counter. What is sold, at what price, what
// is refused and where you must stand are game/gun_store_shop.h; this file
// only opens the menu when Accept is pressed in the customer zone, routes the
// frame's input to it while it is open (the sim is paused, like the bank
// keypad and Rook's booth), draws it, and runs --gun-store-check.

namespace {

std::string dollars(int64_t amount) {
    // $1,234,567 — the wallet is nine digits at most.
    std::string digits = std::to_string(amount < 0 ? -amount : amount);
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        out += digits[i];
        const std::size_t left = n - i - 1;
        if (left > 0 && left % 3 == 0) out += ',';
    }
    return (amount < 0 ? "-$" : "$") + out;
}

}  // namespace

bool App::route_gun_store_event(const SDL_Event& e) {
    if (!gun_store_holds_input()) return false;
    gun_store_.input_consumed = true;
    input_.handle_event(e);
    return true;
}

bool App::process_gun_store_input() {
    if (gun_store_.menu.open) {
        const uint32_t pressed = input_.frame().pressed;
        const GunStoreMenuResult result =
            gun_store_.menu.update(pressed, economy_, weapon_use_.reserve);
        const GunStoreItem item = gun_store_.menu.last_item;
        if (result == GunStoreMenuResult::Bought) {
            AP_INFO("gun store: bought %s for %s; cash %s, pistol reserve %d",
                    gun_store_item_name(item), dollars(gun_store_price(item)).c_str(),
                    dollars(economy_.cash).c_str(), weapon_use_.reserve);
        } else if (result == GunStoreMenuResult::Refused) {
            AP_INFO("gun store: %s refused (%s)", gun_store_item_name(item),
                    gun_store_refusal_text(gun_store_.menu.refusal));
        } else if (result == GunStoreMenuResult::Closed) {
            AP_INFO("gun store: counter closed");
        }
        if (!gun_store_.menu.open) {
            input_.set_ui_mode(ui_.modal());
            if (gun_store_.restore_mouse) input_.set_mouse_look(true);
        } else {
            input_.set_ui_mode(true);
        }
        input_.consume_edges();
        clock_.reset();
        return true;
    }
    if (gun_store_.input_consumed) {
        input_.consume_edges();
        clock_.reset();
        return true;
    }
    if (ui_.screen() != UiScreen::Driving || dev_menu_.open() || vehicle_transition_.active() ||
        !player_vitals_.alive() || !at_gun_store_counter(player_character_.position, on_foot_) ||
        !was_pressed(input_.frame(), kBtnAccept))
        return false;
    gun_store_.menu.begin();
    gun_store_.restore_mouse = input_.mouse_look();
    input_.set_ui_mode(true);
    input_.consume_edges();
    clock_.reset();
    AP_INFO("gun store: counter open; cash %s, pistol %s, reserve %d",
            dollars(economy_.cash).c_str(),
            owns_weapon(economy_, WeaponId::Pistol) ? "owned" : "not owned", weapon_use_.reserve);
    return true;
}

void App::draw_gun_store(glm::vec2 vp) {
    const glm::vec4 ivory{0.98f, 0.94f, 0.81f, 1.0f};
    const glm::vec4 brass{0.86f, 0.62f, 0.24f, 1.0f};
    const glm::vec4 dim{0.55f, 0.55f, 0.55f, 1.0f};
    const glm::vec4 red{1.0f, 0.36f, 0.26f, 1.0f};
    const glm::vec4 green{0.55f, 0.95f, 0.55f, 1.0f};
    const GunStoreMenu& menu = gun_store_.menu;
    if (!menu.open) {
        if (ui_.screen() != UiScreen::Driving || weapon_wheel_.open || !player_vitals_.alive() ||
            vehicle_transition_.active() ||
            !at_gun_store_counter(player_character_.position, on_foot_))
            return;
        const char* prompt = "E / A  SHOP - BRASSLINE ARMS";
        // Above the on-foot controls line (vp.y - 90), which it would cover.
        const float half = hud_.measure_text(prompt, 25.0f) * 0.5f + 40.0f;
        hud_.rect({vp.x * 0.5f - half, vp.y - 180.0f}, {vp.x * 0.5f + half, vp.y - 120.0f},
                  {0.015f, 0.025f, 0.04f, 0.92f});
        hud_.text_centered(prompt, vp.x * 0.5f, vp.y - 165.0f, 25.0f, ivory);
        return;
    }

    constexpr float kW = 980.0f, kH = 700.0f;
    const glm::vec2 o = vp * 0.5f - glm::vec2{kW, kH} * 0.5f;
    hud_.rect({0, 0}, vp, {0, 0, 0, 0.55f});
    hud_.rect(o, o + glm::vec2{kW, kH}, {0.035f, 0.04f, 0.045f, 0.97f});
    hud_.rect(o, o + glm::vec2{kW, 8.0f}, {0.55f, 0.12f, 0.10f, 1.0f});
    hud_.title_text_centered("Brassline Arms", vp.x * 0.5f, o.y + 34.0f, 64.0f, brass);
    hud_.text_centered("SIXTH STREET  -  LICENSED DEALER", vp.x * 0.5f, o.y + 112.0f, 20.0f, dim);
    const std::string cash = "YOUR CASH  " + dollars(economy_.cash);
    hud_.text_centered(cash.c_str(), vp.x * 0.5f, o.y + 150.0f, 30.0f, ivory);

    const int reserve = weapon_use_.reserve;
    for (std::size_t row = 0; row < GunStoreMenu::kRows; ++row) {
        const float y = o.y + 210.0f + static_cast<float>(row) * 110.0f;
        const glm::vec2 lo{o.x + 50.0f, y}, hi{o.x + kW - 50.0f, y + 96.0f};
        const bool selected = row == menu.selection;
        hud_.rect(lo, hi, selected ? glm::vec4{0.30f, 0.21f, 0.09f, 1.0f}
                                   : glm::vec4{0.10f, 0.11f, 0.12f, 1.0f});
        if (selected) hud_.outline(lo, hi, 3.0f, brass);
        if (row == GunStoreMenu::kLeaveRow) {
            hud_.text("LEAVE THE COUNTER", lo + glm::vec2{30.0f, 30.0f}, 30.0f, ivory);
            continue;
        }
        const GunStoreItem item = GunStoreMenu::item_at(row);
        const GunStoreRefusal refusal = gun_store_refusal(economy_, reserve, item);
        const bool buyable = refusal == GunStoreRefusal::None;
        hud_.text(gun_store_item_name(item), lo + glm::vec2{30.0f, 12.0f}, 34.0f,
                  buyable ? ivory : dim);
        std::string detail = gun_store_item_detail(item);
        if (item == GunStoreItem::PistolAmmo)
            detail += "   CARRYING " + std::to_string(reserve) + " / " +
                      std::to_string(kGunStoreReserveCap);
        hud_.text(detail.c_str(), lo + glm::vec2{30.0f, 56.0f}, 20.0f, dim);
        const std::string price = dollars(gun_store_price(item));
        const float pw = hud_.measure_text(price.c_str(), 34.0f);
        hud_.text(price.c_str(), {hi.x - 30.0f - pw, lo.y + 12.0f}, 34.0f,
                  buyable ? brass : dim);
        if (!buyable) {
            const char* why = refusal == GunStoreRefusal::AlreadyOwned ? "OWNED"
                                                                       : gun_store_refusal_text(refusal);
            const float ww = hud_.measure_text(why, 20.0f);
            hud_.text(why, {hi.x - 30.0f - ww, lo.y + 58.0f}, 20.0f,
                      refusal == GunStoreRefusal::AlreadyOwned ? green : red);
        }
    }

    // The status line: what Accept will do next, or what the last press did.
    const float status_y = o.y + kH - 128.0f;
    const GunStoreItem last = menu.last_item;
    if (menu.confirming) {
        const std::string ask = "PAY " + dollars(gun_store_price(last)) + " FOR " +
                                gun_store_item_name(last) + "?";
        hud_.text_centered(ask.c_str(), vp.x * 0.5f, status_y, 30.0f, brass);
        hud_.text_centered("E / A  CONFIRM        ESC / B  BACK", vp.x * 0.5f, status_y + 42.0f,
                           20.0f, ivory);
    } else if (menu.bought) {
        const std::string line = std::string("SOLD - ") + gun_store_item_name(last) + " FOR " +
                                 dollars(gun_store_price(last));
        hud_.text_centered(line.c_str(), vp.x * 0.5f, status_y, 30.0f, green);
        const char* hint = last == GunStoreItem::Pistol ? "HOLD TAB / LB TO EQUIP IT"
                                                        : "ROUNDS ADDED TO YOUR RESERVE";
        hud_.text_centered(hint, vp.x * 0.5f, status_y + 42.0f, 20.0f, ivory);
    } else if (menu.refusal != GunStoreRefusal::None) {
        hud_.text_centered(gun_store_refusal_text(menu.refusal), vp.x * 0.5f, status_y, 30.0f, red);
    }
    hud_.text_centered("W / S  SELECT     E / A  BUY     ESC / B  LEAVE", vp.x * 0.5f,
                       o.y + kH - 40.0f, 18.0f, dim);
}

// --gun-store-check. Starts at the counter (main.cpp places the player on the
// customer side), gives itself $1,000, and drives the real keys: the wheel
// refuses the pistol, the prompt shows, E opens the counter, the pistol and a
// box of rounds are bought through the confirm, a second pistol and an
// unaffordable box are refused, ESC leaves, and the wheel then equips the
// bought pistol. Every stage asserts the economy moved exactly as it should
// and leaves a screenshot a human looks at — no suite can tell you the menu is
// readable.
//
// Keys are posted as SDL events, so they take the same route a player's do.
// Timings are rendered frames; the stages wait on state, not on the clock.
void App::tick_gun_store_check() {
    GunStoreCounter& c = gun_store_;
    if (c.check_done || c.check_failed) return;
    const int frame = frames_rendered_;
    const auto key = [&](SDL_Keycode code, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto tap = [&](SDL_Keycode code, int at) {
        if (frame == at) key(code, true);
        if (frame == at + 2) key(code, false);
    };
    const auto fail = [&](const char* reason) {
        c.check_failed = true;
        AP_ERROR("gun store check: %s (frame %d, cash %lld, reserve %d)", reason, frame,
                 static_cast<long long>(economy_.cash), weapon_use_.reserve);
    };
    const auto capture = [&](const char* name) { c.check_capture = name; };

    if (frame == 2) {
        // Face the counter: site-local -Z, in the walk test's yaw convention.
        const glm::vec2 ahead = gun_store_local(player_character_.position) + glm::vec2{0.0f, -1.0f};
        const glm::vec3 to = gun_store_world(ahead) - player_character_.position;
        player_character_.view_yaw = std::atan2(to.x, -to.z);
        player_character_.facing_yaw = player_character_.view_yaw;
        player_character_.view_pitch = -0.12f;
        prev_player_character_ = player_character_;
        if (owns_weapon(economy_, WeaponId::Pistol)) { fail("a new game already owns the pistol"); return; }
        charge_cash(economy_, economy_.cash);
        earn_cash(economy_, 1000);
        AP_INFO("gun store check: $1,000 in hand, pistol not owned");
    }
    // 1. The wheel will not equip a pistol you do not own.
    if (frame == 100) key(SDLK_TAB, true);
    tap(SDLK_2, 105);
    if (frame == 118) {
        if (!weapon_wheel_.open || weapon_wheel_.hovered != WeaponId::Pistol) { fail("wheel did not hover the pistol"); return; }
        capture("wheel-locked");
    }
    if (frame == 122) key(SDLK_TAB, false);
    if (frame == 135) {
        if (weapon_wheel_.equipped != WeaponId::Unarmed) { fail("the wheel equipped an unowned pistol"); return; }
        AP_INFO("gun store check: wheel refused the unowned pistol");
    }
    // 2. The prompt, then the counter.
    if (frame == 150) {
        if (!at_gun_store_counter(player_character_.position, on_foot_)) { fail("the player is not at the counter"); return; }
        capture("prompt");
    }
    tap(SDLK_e, 160);
    if (frame == 175) {
        if (!gun_store_.menu.open) { fail("E at the counter did not open the menu"); return; }
        capture("menu");
    }
    // 3. The pistol, through the confirm.
    tap(SDLK_e, 180);
    if (frame == 195) {
        if (!gun_store_.menu.confirming || economy_.cash != 1000) { fail("no confirm, or charged early"); return; }
        capture("confirm");
    }
    tap(SDLK_e, 200);
    if (frame == 215) {
        if (!owns_weapon(economy_, WeaponId::Pistol) || economy_.cash != 1000 - kGunStorePistolPrice) {
            fail("the pistol purchase did not debit and grant together"); return;
        }
        capture("bought-pistol");
    }
    // 4. A box of rounds.
    const int reserve_before = WeaponUseState::kInitialReserve;
    tap(SDLK_s, 220);
    tap(SDLK_e, 230);
    tap(SDLK_e, 240);
    if (frame == 255) {
        if (weapon_use_.reserve != reserve_before + kGunStoreAmmoBoxRounds ||
            economy_.cash != 1000 - kGunStorePistolPrice - kGunStoreAmmoPrice) {
            fail("the ammunition purchase did not add a box for its price"); return;
        }
        capture("bought-ammo");
    }
    // 5. A second pistol is refused and costs nothing.
    tap(SDLK_w, 260);
    tap(SDLK_e, 270);
    if (frame == 285) {
        if (gun_store_.menu.refusal != GunStoreRefusal::AlreadyOwned ||
            economy_.cash != 1000 - kGunStorePistolPrice - kGunStoreAmmoPrice) {
            fail("a second pistol was not refused"); return;
        }
        capture("already-owned");
    }
    // 6. Not enough cash for a box.
    if (frame == 290) charge_cash(economy_, economy_.cash - 20);
    tap(SDLK_s, 292);
    tap(SDLK_e, 300);
    if (frame == 315) {
        if (gun_store_.menu.refusal != GunStoreRefusal::NotEnoughCash || economy_.cash != 20 ||
            weapon_use_.reserve != reserve_before + kGunStoreAmmoBoxRounds) {
            fail("an unaffordable box was not refused cleanly"); return;
        }
        capture("no-cash");
    }
    // 7. Leave, and draw the pistol that was bought.
    tap(SDLK_ESCAPE, 320);
    if (frame == 335 && gun_store_.menu.open) { fail("ESC did not leave the counter"); return; }
    if (frame == 350) key(SDLK_TAB, true);
    tap(SDLK_2, 355);
    if (frame == 368) {
        if (!weapon_wheel_.open || weapon_wheel_.hovered != WeaponId::Pistol) { fail("wheel did not hover the pistol"); return; }
        capture("wheel-owned");
    }
    if (frame == 372) key(SDLK_TAB, false);
    if (frame == 390 && weapon_wheel_.equipped != WeaponId::Pistol) { fail("the bought pistol would not equip"); return; }
    if (frame == 440) capture("armed");
    if (frame == 445 && c.check_captures == 0x3ffu) {
        c.check_done = true;
        AP_INFO("gun store check: passed; cash %lld, pistol %d/%d",
                static_cast<long long>(economy_.cash), weapon_use_.magazine, weapon_use_.reserve);
    } else if (frame == 445) {
        fail("not every screenshot was captured");
    }
}

void App::capture_gun_store_check() {
    GunStoreCounter& c = gun_store_;
    if (c.check_capture.empty()) return;
    static constexpr const char* kNames[] = {"wheel-locked", "prompt", "menu", "confirm",
        "bought-pistol", "bought-ammo", "already-owned", "no-cash", "wheel-owned", "armed"};
    const std::string name = c.check_capture;
    c.check_capture.clear();
    if (!save_screenshot(screenshot_path_ + "." + name + ".png")) {
        AP_ERROR("gun store check: could not save the %s screenshot", name.c_str());
        return;
    }
    for (unsigned i = 0; i < std::size(kNames); ++i)
        if (name == kNames[i]) c.check_captures |= 1u << i;
}

}  // namespace apricot

#include "app/app.h"

#include <cstdio>

#include "core/log.h"
#include "gfx/hud.h"

namespace apricot {

// The wallet in play: what pays, what costs, and the figure in the corner.
//
// Amounts live in game/wallet_rules.h and every balance change goes through
// game/player_economy.h. This file only decides WHEN: a payout on the step
// the delivery completes, a fine inside arrest_player() (police_gameplay.cpp),
// a bill on the step the player dies. The HUD reads economy_ itself, every
// frame, so a shop or a heist that changes the balance shows up without
// having to know this file exists.

void App::pay_delivery() {
    wallet_.delivery_paid = earn_cash(economy_, kDeliveryPayout);
    AP_INFO("delivery payout: +$%lld, cash $%lld",
            static_cast<long long>(wallet_.delivery_paid),
            static_cast<long long>(economy_.cash));
}

void App::charge_hospital_bill() {
    wallet_.hospital_bill = charge_penalty(economy_, kHospitalBill);
    AP_INFO("hospital bill: billed $%lld, paid $%lld, cash $%lld",
            static_cast<long long>(wallet_.hospital_bill.billed),
            static_cast<long long>(wallet_.hospital_bill.taken),
            static_cast<long long>(economy_.cash));
}

std::string App::arrest_fine_line() const {
    if (wallet_.arrest_fine.billed <= 0) return {};
    char line[96];
    format_penalty_line(line, sizeof(line), "FINE", wallet_.arrest_fine);
    return line;
}

// Under WASTED, for exactly as long as the banner is up.
void App::draw_wasted_bill(glm::vec2 vp) {
    if (wallet_.hospital_bill.billed <= 0) return;
    char line[96];
    format_penalty_line(line, sizeof(line), "HOSPITAL BILL", wallet_.hospital_bill);
    const float alpha = glm::clamp(player_vitals_.dead_seconds / 0.6f, 0.0f, 1.0f);
    hud_.text_centered(line, vp.x * 0.5f + 2.0f, 194.0f, 28.0f,
                       {0.0f, 0.0f, 0.0f, 0.8f * alpha});
    hud_.text_centered(line, vp.x * 0.5f, 192.0f, 28.0f,
                       {0.98f, 0.86f, 0.80f, alpha});
}

// The balance, in a plate beside the clock: the same height, top and panel ink,
// with a green edge where the clock has its amber one, so the pair reads as one
// band across the top-right corner. It sits LEFT of the clock rather than under
// it because under the clock is already the wanted stars, the cooldown meter,
// the recorder badge and the on-foot health bar, and every one of those moves.
//
// The flash sits left of the plate on the same row. Under it is where the
// pistol and molotov readouts start, and a "-$50" drawn over "PISTOL 12 / 48"
// is exactly the purchase you would want to read.
void App::draw_wallet_hud(glm::vec2 vp) {
    if (vp.x <= 0.0f || vp.y <= 0.0f) return;
    // The clock's geometry (GameUi::draw_minimap): right edge vp.x - 28,
    // 142 wide, 54 tall, 28 from the top.
    constexpr float kClockWidth = 142.0f;
    constexpr float kTop = 28.0f;
    constexpr float kHeight = 54.0f;
    constexpr float kGap = 10.0f;
    constexpr float kGlyph = 28.0f;
    constexpr float kPad = 16.0f;
    constexpr float kEdge = 5.0f;
    const float right = vp.x - 28.0f - kClockWidth - kGap;

    char cash[32];
    format_cash(cash, sizeof(cash), economy_.cash);
    // Wide enough for "$9,999" before it grows, so the plate does not twitch
    // as the balance crosses a digit in the common range.
    const float text_w = std::max(hud_.measure_text(cash, kGlyph),
                                  hud_.measure_text("$9,999", kGlyph));
    const float left = right - text_w - 2.0f * kPad - kEdge;
    const glm::vec4 green{0.45f, 0.90f, 0.42f, 1.0f};
    hud_.rect({left + 4.0f, kTop + 4.0f}, {right + 4.0f, kTop + kHeight + 4.0f},
              {0.0f, 0.0f, 0.0f, 0.32f});
    hud_.rect({left, kTop}, {right, kTop + kHeight}, {0.01f, 0.015f, 0.02f, 0.78f});
    hud_.rect({left, kTop}, {left + kEdge, kTop + kHeight}, green);
    const float cash_w = hud_.measure_text(cash, kGlyph);
    const float cash_x = right - kPad - cash_w;
    hud_.text(cash, {cash_x + 1.5f, kTop + 15.5f}, kGlyph, {0.0f, 0.0f, 0.0f, 0.6f});
    hud_.text(cash, {cash_x, kTop + 14.0f}, kGlyph, green);

    const CashFlash& flash = wallet_.flash;
    if (!flash.showing()) return;
    char delta[32];
    format_cash(delta, sizeof(delta), flash.delta, true);
    constexpr float kDeltaGlyph = 26.0f;
    const float alpha = flash.alpha();
    const glm::vec4 ink = flash.delta > 0
        ? glm::vec4{0.52f, 1.0f, 0.46f, alpha}
        : glm::vec4{1.0f, 0.34f, 0.26f, alpha};
    const float delta_w = hud_.measure_text(delta, kDeltaGlyph);
    const float delta_x = left - 18.0f - delta_w;
    const float delta_y = kTop + 15.0f;
    hud_.rect({delta_x - 10.0f, kTop + 8.0f}, {left - 6.0f, kTop + kHeight - 8.0f},
              {0.01f, 0.015f, 0.02f, 0.55f * alpha});
    hud_.text(delta, {delta_x + 1.5f, delta_y + 1.5f}, kDeltaGlyph,
              {0.0f, 0.0f, 0.0f, 0.6f * alpha});
    hud_.text(delta, {delta_x, delta_y}, kDeltaGlyph, ink);
}

}  // namespace apricot

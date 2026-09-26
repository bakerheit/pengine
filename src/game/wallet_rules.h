#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "game/player_economy.h"

namespace apricot {

// What the city pays the player and what it takes back, and the HUD's memory
// of the last change. The balance itself lives in PlayerEconomy and moves only
// through player_economy.h; this file decides the AMOUNTS and how they read.
//
// The scale, so the numbers agree with each other: a new game starts with
// $250, the delivery pays $750, a pistol costs about $500 and a box of rounds
// about $50, a bank job takes tens of thousands. One job buys the gun; one bad
// arrest at three stars costs more than the job paid.

// Lou's delivery, paid once on the step the mission completes.
inline constexpr int64_t kDeliveryPayout = 750;

// Waking up beside your car after WASTED. Flat, because a bill that grew with
// the wanted level would charge twice for a chase the death already ended.
inline constexpr int64_t kHospitalBill = 250;

// The fine doubles with every star: $200 at one, $3,200 at five. Doubling is
// the curve that makes a fifth star a decision rather than a rounding error,
// without letting a one-star jaywalk cost a week's work.
inline constexpr int64_t kArrestFineBase = 100;
inline constexpr int64_t arrest_fine(int wanted_level) {
    if (wanted_level <= 0) return 0;
    const int stars = wanted_level > 5 ? 5 : wanted_level;
    return kArrestFineBase << stars;
}

// A penalty as billed and as actually paid. They differ when the player is
// short, and the banner shows both rather than a figure the wallet never saw.
struct CashPenalty {
    int64_t billed = 0;
    int64_t taken = 0;
};

inline CashPenalty charge_penalty(PlayerEconomy& e, int64_t billed) {
    return {std::max<int64_t>(billed, 0), charge_cash(e, billed)};
}

// "$1,250". `sign` prefixes a + or - for the delta flash; zero prints "$0".
inline void format_cash(char* out, std::size_t size, int64_t amount, bool sign = false) {
    if (!out || size == 0) return;
    const bool negative = amount < 0;
    uint64_t magnitude = negative ? static_cast<uint64_t>(-(amount + 1)) + 1u
                                  : static_cast<uint64_t>(amount);
    char digits[32];
    int n = 0;
    int group = 0;
    do {
        if (group == 3) { digits[n++] = ','; group = 0; }
        digits[n++] = static_cast<char>('0' + static_cast<int>(magnitude % 10u));
        magnitude /= 10u;
        ++group;
    } while (magnitude != 0u && n < 30);
    std::size_t at = 0;
    const auto put = [&](char c) { if (at + 1 < size) out[at++] = c; };
    if (negative) put('-');
    else if (sign) put('+');
    put('$');
    while (n > 0) put(digits[--n]);
    out[at] = '\0';
}

// The line under ARRESTED or WASTED. "FINE $800" when it was paid in full,
// "FINE $800  -  PAID $120" when the player was short, and "NO CASH ON YOU"
// when there was nothing to take.
inline void format_penalty_line(char* out, std::size_t size, const char* what,
                                CashPenalty p) {
    if (!out || size == 0) return;
    char billed[32], taken[32];
    format_cash(billed, sizeof(billed), p.billed);
    format_cash(taken, sizeof(taken), p.taken);
    if (p.taken >= p.billed)
        std::snprintf(out, size, "%s %s", what, billed);
    else if (p.taken > 0)
        std::snprintf(out, size, "%s %s  -  PAID %s", what, billed, taken);
    else
        std::snprintf(out, size, "%s %s  -  NO CASH ON YOU", what, billed);
}

// The "+$750" beside the counter. It watches the real balance and flashes
// whatever changed, rather than being told: a shop, a heist or a fine that
// forgets to announce itself still shows up, because the only thing it reads
// is the wallet. Changes in the same direction while one is showing add up,
// so buying a gun and a box of rounds reads as one "-$550".
//
// A load or a new game replaces the balance without anybody earning or paying
// anything; those call resync() so the swap does not flash.
struct CashFlash {
    static constexpr float kShowSeconds = 2.6f;
    int64_t seen = 0;
    bool primed = false;
    int64_t delta = 0;
    float seconds = 0.0f;

    void resync(int64_t cash) {
        seen = cash;
        primed = true;
        delta = 0;
        seconds = 0.0f;
    }

    void observe(int64_t cash, float dt) {
        if (!primed) { resync(cash); return; }
        seconds = std::max(0.0f, seconds - std::max(0.0f, dt));
        if (seconds <= 0.0f) delta = 0;
        if (cash == seen) return;
        const int64_t change = cash - seen;
        seen = cash;
        if (seconds > 0.0f && delta != 0 && (change > 0) == (delta > 0)) delta += change;
        else delta = change;
        seconds = kShowSeconds;
    }

    bool showing() const { return seconds > 0.0f && delta != 0; }
    // 1 while fresh, easing to 0 over the last 0.6 s.
    float alpha() const { return std::clamp(seconds / 0.6f, 0.0f, 1.0f); }
};

// Everything the HUD needs to say about money beyond the balance itself. One
// member on App, so the wallet's feedback state is one line in its header.
struct WalletNotices {
    CashFlash flash;
    CashPenalty arrest_fine;
    CashPenalty hospital_bill;
    int64_t delivery_paid = 0;
};

}  // namespace apricot

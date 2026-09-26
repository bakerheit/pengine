#pragma once

#include <vector>

#include "city/start_area.h"
#include "game/bank_heist.h"

namespace apricot::city {

// The vault's cash, built from game/bank_heist.h's pile table so the pile the
// grab reaches for is the pile that draws. Parts are bank-local, like
// kBankPlan. Cash is never solid: it sits on the table or on a cart and is
// hidden when taken. The carts are solid and stay.
//
// Names carry the material: "cash" parts are drawn banknote green; the paper
// bands and bale straps are drawn in their finish. The host layer reads the name.

inline void bank_heist_add(std::vector<StartPart>& out, const char* name, float x, float z,
                           float bottom, float w, float h, float d, StartFinish finish,
                           bool solid = false) {
    out.push_back({name, {x, z}, bottom, w, h, d, finish, solid});
}

inline constexpr float kBankVaultTableTopM = 1.12f;
inline constexpr float kBankHeistCartTopM = 0.62f;

// The cash of pile `index`: strapped bricks on the table, or shrink-wrapped
// bales on a cart. Empty for an index out of range.
inline std::vector<StartPart> bank_heist_pile_parts(std::size_t index) {
    std::vector<StartPart> parts;
    if (index >= kBankHeistPiles.size()) return parts;
    const BankHeistPile& pile = kBankHeistPiles[index];
    if (!pile.on_cart) {
        // 3 x 4 bricks, three layers: 0.54 x 0.23 x 0.38 m.
        constexpr float kPitchX = 0.18f, kPitchZ = 0.095f, kLayer = 0.077f;
        for (int layer = 0; layer < 3; ++layer) {
            for (int ix = 0; ix < 3; ++ix) {
                for (int iz = 0; iz < 4; ++iz) {
                    const float x = pile.centre.x + (static_cast<float>(ix) - 1.0f) * kPitchX;
                    const float z = pile.centre.y + (static_cast<float>(iz) - 1.5f) * kPitchZ;
                    const float y = kBankVaultTableTopM + static_cast<float>(layer) * kLayer;
                    bank_heist_add(parts, "bank heist cash brick", x, z, y,
                                   0.17f, 0.075f, 0.085f, StartFinish::White);
                    bank_heist_add(parts, "bank heist paper band", x, z, y - 0.001f,
                                   0.04f, 0.078f, 0.088f, StartFinish::White);
                }
            }
        }
        return parts;
    }
    // 3 x 2 bales, two layers, on the cart deck: 1.18 x 0.52 x 0.78 m.
    constexpr float kPitch = 0.40f, kLayer = 0.26f;
    for (int layer = 0; layer < 2; ++layer) {
        for (int ix = 0; ix < 3; ++ix) {
            for (int iz = 0; iz < 2; ++iz) {
                const float x = pile.centre.x + (static_cast<float>(ix) - 1.0f) * kPitch;
                const float z = pile.centre.y + (static_cast<float>(iz) - 0.5f) * kPitch;
                const float y = kBankHeistCartTopM + static_cast<float>(layer) * kLayer;
                bank_heist_add(parts, "bank heist cash bale", x, z, y,
                               0.38f, 0.255f, 0.38f, StartFinish::White);
                for (float strap : {-0.1f, 0.1f})
                    bank_heist_add(parts, "bank heist bale strap", x + strap, z, y - 0.001f,
                                   0.035f, 0.258f, 0.384f, StartFinish::Yellow);
            }
        }
    }
    return parts;
}

// The two carts the bales ride on: a solid steel body to the deck, and a
// push handle at the west end. They are furniture, not loot.
inline std::vector<StartPart> bank_heist_cart_parts() {
    std::vector<StartPart> parts;
    for (const BankHeistPile& pile : kBankHeistPiles) {
        if (!pile.on_cart) continue;
        const float x = pile.centre.x, z = pile.centre.y;
        bank_heist_add(parts, "bank heist cart body", x, z, 0.10f,
                       1.24f, kBankHeistCartTopM - 0.10f, 0.84f, StartFinish::Steel, true);
        bank_heist_add(parts, "bank heist cart plinth", x, z, 0.0f,
                       1.10f, 0.10f, 0.70f, StartFinish::DarkRoof, true);
        for (float side : {-0.36f, 0.36f})
            bank_heist_add(parts, "bank heist cart handle post", x - 0.66f, z + side,
                           kBankHeistCartTopM, 0.04f, 0.42f, 0.04f, StartFinish::Steel);
        bank_heist_add(parts, "bank heist cart handle bar", x - 0.66f, z,
                       kBankHeistCartTopM + 0.40f, 0.05f, 0.05f, 0.76f, StartFinish::Steel);
    }
    return parts;
}

}  // namespace apricot::city

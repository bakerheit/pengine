#pragma once

#include <string>
#include <SDL.h>
#include "game/bank_vault.h"
#include "gfx/hud.h"

namespace apricot {

class BankInteraction {
public:
    bool modal() const { return panel_ != BankTarget::None; }
    void interact(BankTarget target, BankVaultState& state);
    void event(const SDL_Event& event, glm::vec2 viewport, BankVaultState& state);
    void draw(Hud& hud, glm::vec2 viewport, BankTarget nearby,
              const BankVaultState& state) const;
private:
    void activate(int button, BankVaultState& state);
    BankTarget panel_ = BankTarget::None;
    std::string digits_;
    bool rejected_ = false;
    int selection_ = 0;
};

}  // namespace apricot

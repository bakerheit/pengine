#include "app/bank_interaction.h"

#include <algorithm>
#include "core/log.h"

namespace apricot {
namespace {
constexpr const char* kKeys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "CLEAR", "0", "ENTER"};
float scale(glm::vec2 vp) { return std::min(vp.x / 800.0f, vp.y / 760.0f); }
glm::vec2 origin(glm::vec2 vp, float s) { return vp * 0.5f - glm::vec2{230.0f, 300.0f} * s; }
}

void BankInteraction::interact(BankTarget target, BankVaultState& state) {
    if (target == BankTarget::Note) {
        state.note_read = true;
        panel_ = target;
        AP_INFO("bank: manager note read");
    } else if (target == BankTarget::Vault) {
        if (state.unlocked) {
            bank_vault_toggle(state);
            AP_INFO("bank: vault %s requested", state.target_open ? "open" : "close");
        } else {
            panel_ = target;
            digits_.clear();
            rejected_ = false;
            selection_ = 0;
        }
    }
}

void BankInteraction::activate(int button, BankVaultState& state) {
    if (button == 9) { digits_.clear(); rejected_ = false; }
    else if (button == 11) {
        if (bank_vault_submit(state, digits_)) {
            panel_ = BankTarget::None;
            AP_INFO("bank: vault access granted");
        } else {
            rejected_ = true;
            digits_.clear();
            AP_INFO("bank: vault code rejected");
        }
    } else if (button >= 0 && button < 12 && digits_.size() < 4u) {
        digits_ += kKeys[button];
        rejected_ = false;
    }
}

void BankInteraction::event(const SDL_Event& e, glm::vec2 vp, BankVaultState& state) {
    if (!modal()) return;
    if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
        const SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_ESCAPE) { panel_ = BankTarget::None; return; }
        if (panel_ == BankTarget::Note) {
            if (key == SDLK_RETURN || key == SDLK_e || key == SDLK_BACKSPACE)
                panel_ = BankTarget::None;
            return;
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            activate(key == SDLK_0 ? 10 : static_cast<int>(key - SDLK_1), state);
        } else if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
            activate(static_cast<int>(key - SDLK_KP_1), state);
        } else if (key == SDLK_KP_0) activate(10, state);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) activate(11, state);
        else if (key == SDLK_BACKSPACE) { if (!digits_.empty()) digits_.pop_back(); }
        else if (key == SDLK_UP) selection_ = (selection_ + 9) % 12;
        else if (key == SDLK_DOWN) selection_ = (selection_ + 3) % 12;
        else if (key == SDLK_LEFT) selection_ = (selection_ + 11) % 12;
        else if (key == SDLK_RIGHT) selection_ = (selection_ + 1) % 12;
        else if (key == SDLK_SPACE) activate(selection_, state);
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        const auto b = e.cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_B) { panel_ = BankTarget::None; return; }
        if (panel_ == BankTarget::Note) {
            if (b == SDL_CONTROLLER_BUTTON_A) panel_ = BankTarget::None;
        } else if (b == SDL_CONTROLLER_BUTTON_A) activate(selection_, state);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) selection_ = (selection_ + 9) % 12;
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) selection_ = (selection_ + 3) % 12;
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) selection_ = (selection_ + 11) % 12;
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) selection_ = (selection_ + 1) % 12;
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (panel_ == BankTarget::Note) { panel_ = BankTarget::None; return; }
        const float s = scale(vp);
        const glm::vec2 p = (glm::vec2{e.button.x, e.button.y} - origin(vp, s)) / s;
        if (p.y >= 558.0f && p.y <= 594.0f && p.x >= 20.0f && p.x <= 440.0f) {
            panel_ = BankTarget::None;
        }
        for (int i = 0; i < 12; ++i) {
            const float x = 35.0f + static_cast<float>(i % 3) * 132.0f;
            const float y = 225.0f + static_cast<float>(i / 3) * 73.0f;
            if (p.x >= x && p.x <= x + 124.0f && p.y >= y && p.y <= y + 65.0f) {
                selection_ = i;
                activate(i, state);
                break;
            }
        }
    }
}

void BankInteraction::draw(Hud& hud, glm::vec2 vp, BankTarget nearby,
                           const BankVaultState& state) const {
    const glm::vec4 ivory{0.98f, 0.94f, 0.81f, 1.0f};
    if (!modal()) {
        const char* prompt = nullptr;
        if (nearby == BankTarget::Note) prompt = "E / A  READ MANAGER'S NOTE";
        if (nearby == BankTarget::Vault) prompt = !state.unlocked ?
            "E / A  ENTER VAULT CODE" : (state.target_open ? "E / A  CLOSE VAULT" : "E / A  OPEN VAULT");
        if (state.blocked && nearby == BankTarget::Vault) prompt = "STEP CLEAR OF THE DOOR TO LET IT MOVE";
        if (prompt) {
            hud.rect({vp.x * 0.5f - 290.0f, vp.y - 100.0f},
                     {vp.x * 0.5f + 290.0f, vp.y - 40.0f}, {0.015f, 0.025f, 0.04f, 0.92f});
            hud.text_centered(prompt, vp.x * 0.5f, vp.y - 85.0f, 25.0f, ivory);
        }
        return;
    }
    const float s = scale(vp);
    const glm::vec2 o = origin(vp, s);
    const auto rect = [&](float x, float y, float w, float h, glm::vec4 c) {
        hud.rect(o + glm::vec2{x,y} * s, o + glm::vec2{x+w,y+h} * s, c);
    };
    const auto text = [&](const char* t, float y, float size, glm::vec4 c) {
        hud.text_centered(t, vp.x * 0.5f, o.y + y*s, size*s, c);
    };
    hud.rect({0,0}, vp, {0,0,0,0.65f});
    rect(0,0,460,600, panel_ == BankTarget::Note ? ivory : glm::vec4{0.035f,0.055f,0.08f,1});
    rect(0,0,460,7,{0.76f,0.56f,0.22f,1});
    if (panel_ == BankTarget::Note) {
        const glm::vec4 ink{0.07f,0.16f,0.32f,1};
        text("MANAGER'S NOTE",60,36,ink);
        text("VAULT ACCESS",145,25,ink);
        text(kBankVaultCode,205,100,ink);
        text("CLOSE THE DOOR WHEN FINISHED.",355,23,ink);
        text("- MANAGER",405,25,ink);
        text("ENTER / E / A / ESC  PUT NOTE DOWN",550,18,ink);
        return;
    }
    text("PINATTY VAULT",35,35,ivory);
    text("ENTER FOUR-DIGIT ACCESS CODE",86,19,ivory);
    std::string display = digits_;
    while (display.size() < 4u) display += '_';
    rect(35,120,390,70,{0.005f,0.018f,0.014f,1});
    text(display.c_str(),130,48,{0.54f,0.94f,0.60f,1});
    if (rejected_) text("WRONG CODE - TRY AGAIN",195,19,{1,0.32f,0.23f,1});
    for (int i = 0; i < 12; ++i) {
        const float x = 35.0f + static_cast<float>(i % 3) * 132.0f;
        const float y = 225.0f + static_cast<float>(i / 3) * 73.0f;
        rect(x,y,124,65,i == selection_ ? glm::vec4{0.47f,0.34f,0.14f,1} : glm::vec4{0.17f,0.20f,0.24f,1});
        hud.text_centered(kKeys[i],o.x+(x+62)*s,o.y+(y+18)*s,25*s,ivory);
    }
    text(state.note_read ? "CODE IS ON THE MANAGER'S NOTE" : "LOOK FOR A NOTE IN THE OFFICE",525,18,ivory);
    text("ESC / B  CANCEL",567,19,ivory);
}

}  // namespace apricot

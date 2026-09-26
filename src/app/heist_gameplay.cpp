#include "app/app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "city/bank_heist_layout.h"
#include "city/bank_vault_layout.h"
#include "core/log.h"

namespace apricot {
namespace {

constexpr float kCardSeconds = 5.0f;

// "$43,500". Whole dollars, thousands separated, like the wallet.
std::string dollars(int64_t amount) {
    std::string digits = std::to_string(amount < 0 ? -amount : amount);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<std::size_t>(i), ",");
    return (amount < 0 ? "-$" : "$") + digits;
}

// An alarm bell: a hammer striking a gong fifteen times a second, the partials
// of a real bell rather than one sine. Built once; the rules decide when it
// rings.
PcmClip make_alarm_bell() {
    PcmClip clip;
    clip.sample_rate = kDefaultSampleRate;
    clip.channels = 1;
    constexpr float kSeconds = 1.6f;
    constexpr float kStrikeHz = 15.0f;
    constexpr float kTwoPi = 6.28318530718f;
    const std::size_t frames = static_cast<std::size_t>(kSeconds * static_cast<float>(clip.sample_rate));
    clip.samples.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(clip.sample_rate);
        const float since_strike = std::fmod(t, 1.0f / kStrikeHz);
        const float ring = std::exp(-since_strike * 38.0f);
        const float edge = std::min(1.0f, std::min(t, kSeconds - t) / 0.01f);
        const float tone = 0.55f * std::sin(kTwoPi * 1180.0f * t) +
                           0.28f * std::sin(kTwoPi * 2710.0f * t) +
                           0.12f * std::sin(kTwoPi * 4250.0f * t);
        clip.samples[i] = tone * (0.25f + 0.75f * ring) * edge;
    }
    return clip;
}

glm::vec3 bank_world(glm::vec3 local) {
    const auto& s = city::kBankSite;
    return {s.origin.x + s.cos_yaw * local.x + s.sin_yaw * local.z, s.ground_m + local.y,
            s.origin.z - s.sin_yaw * local.x + s.cos_yaw * local.z};
}

}  // namespace

// The App's half of the heist. The rules are game/bank_heist.h, stepped inside
// the fixed step; what lives here is what needs the world: the key, the heat,
// the nodes, the bell and the words on screen.

void App::reset_bank_heist(const BankHeistState& state) {
    bank_heist_ = state;
    heist_caught_seen_ = police_arrest_reports_ + player_death_reports_;
    heist_card_s_ = 0.0f;
    heist_bell_wait_s_ = 0.0f;
    world_.sync_bank_loot(scene_, bank_heist_.taken);
}

// E / A beside a pile, on foot in the vault. Called from the same place the
// vault keypad's E is, and before it, so a pile in reach wins.
bool App::try_bank_heist_grab() {
    const int pile = bank_heist_target(
        bank_heist_, city::bank_local_position(player_character_.position), on_foot_);
    if (pile < 0) return false;
    const BankHeistGrab grab = bank_heist_take(bank_heist_, pile, wanted_.heat(), step_index_);
    if (!grab.taken) return false;
    wanted_.add_heat(grab.heat, WantedSystem::Crime::Other);
    world_.sync_bank_loot(scene_, bank_heist_.taken);
    if (grab.tripped_alarm) {
        // Anything that already happened is not this heist's to lose.
        heist_caught_seen_ = police_arrest_reports_ + player_death_reports_;
        heist_bell_wait_s_ = 0.0f;
        heist_card_title_ = "ALARM TRIPPED";
        heist_card_line_ = "LOSE THE COPS TO KEEP THE TAKE";
        heist_card_s_ = kCardSeconds;
    }
    AP_INFO("heist: took pile %d (%s); carrying %s; wanted %d%s", pile,
            dollars(grab.value).c_str(), dollars(bank_heist_.carried).c_str(),
            wanted_.level(), grab.tripped_alarm ? "; alarm tripped" : "");
    return true;
}

void App::step_bank_heist_rules() {
    const float dt = static_cast<float>(kSimDt);
    const unsigned caught_count = police_arrest_reports_ + player_death_reports_;
    const bool caught = caught_count != heist_caught_seen_;
    heist_caught_seen_ = caught_count;
    const BankHeistStep result =
        step_bank_heist(bank_heist_, economy_, wanted_.level(), caught, step_index_, dt);
    if (result.event == BankHeistEvent::Banked) {
        heist_card_title_ = "HEIST COMPLETE";
        heist_card_line_ = dollars(result.amount) + " BANKED";
        heist_card_s_ = kCardSeconds + 1.5f;
        VoiceParams ui;
        ui.category = Category::Ui;
        audio_device_.mixer().play_oneshot(&audio_device_.bank().mission_success, ui);
        AP_INFO("heist: lost the cops; banked %s, wallet %s", dollars(result.amount).c_str(),
                dollars(economy_.cash).c_str());
    } else if (result.event == BankHeistEvent::Lost) {
        heist_card_title_ = "TAKE LOST";
        heist_card_line_ = dollars(result.amount) + " WENT BACK TO THE BANK";
        heist_card_s_ = kCardSeconds + 1.5f;
        AP_INFO("heist: caught carrying %s; the take is lost", dollars(result.amount).c_str());
    } else if (result.event == BankHeistEvent::Restocked) {
        AP_INFO("heist: the vault is restocked");
    }
    world_.sync_bank_loot(scene_, bank_heist_.taken);
    heist_card_s_ = std::max(0.0f, heist_card_s_ - dt);

    // The bell rings at the bank, not in the player's ear: drive away and it
    // falls behind you.
    if (bank_heist_.bell_s <= 0.0f) return;
    heist_bell_wait_s_ -= dt;
    if (heist_bell_wait_s_ > 0.0f) return;
    if (heist_bell_clip_.empty()) heist_bell_clip_ = make_alarm_bell();
    VoiceParams bell;
    bell.category = Category::World;
    bell.spatial = true;
    bell.position = bank_world({0.0f, 3.6f, -7.2f});  // over the front door
    bell.gain = 0.9f;
    audio_device_.mixer().play_oneshot(&heist_bell_clip_, bell);
    heist_bell_wait_s_ = heist_bell_clip_.duration_seconds();
}

void App::draw_bank_heist_hud(glm::vec2 vp) {
    if (vp.x <= 0.0f || vp.y <= 0.0f) return;
    const glm::vec4 ivory{0.98f, 0.94f, 0.81f, 1.0f};
    // The grab prompt, where the vault keypad's is.
    const int pile = bank_heist_target(
        bank_heist_, city::bank_local_position(player_character_.position), on_foot_);
    if (pile >= 0 && !bank_interaction_.modal()) {
        const std::string prompt = "E / A  GRAB THE CASH  " +
            dollars(kBankHeistPiles[static_cast<std::size_t>(pile)].value);
        hud_.rect({vp.x * 0.5f - 290.0f, vp.y - 100.0f}, {vp.x * 0.5f + 290.0f, vp.y - 40.0f},
                  {0.015f, 0.025f, 0.04f, 0.92f});
        hud_.text_centered(prompt.c_str(), vp.x * 0.5f, vp.y - 85.0f, 25.0f, ivory);
    }
    // The take in hand, for as long as it is not money yet.
    if (bank_heist_.live) {
        const std::string take = "TAKE  " + dollars(bank_heist_.carried);
        const char* line = "LOSE THE COPS TO BANK IT";
        const float half = std::max(hud_.measure_text(take.c_str(), 40.0f),
                                    hud_.measure_text(line, 18.0f)) * 0.5f + 30.0f;
        const float top = 22.0f;
        hud_.quad({vp.x * 0.5f - half - 10.0f, top}, {vp.x * 0.5f + half, top},
                  {vp.x * 0.5f + half + 10.0f, top + 78.0f}, {vp.x * 0.5f - half, top + 78.0f},
                  {0.012f, 0.03f, 0.018f, 0.82f});
        hud_.text_centered(take.c_str(), vp.x * 0.5f, top + 6.0f, 40.0f,
                           {0.55f, 0.95f, 0.55f, 1.0f});
        hud_.text_centered(line, vp.x * 0.5f, top + 52.0f, 18.0f, ivory);
    }
    if (heist_card_s_ <= 0.0f) return;
    // ALARM and HEIST COMPLETE sit where ARRESTED does; TAKE LOST comes with
    // ARRESTED or WASTED, so it sits below them.
    const bool lost = heist_card_title_[0] == 'T';
    const float alpha = glm::smoothstep(0.0f, 0.6f, heist_card_s_);
    const float centre = vp.x * 0.5f;
    const float top = vp.y * (lost ? 0.52f : 0.29f);
    const float height = std::min(lost ? 56.0f : 80.0f, vp.x * 0.1f);
    const float half = std::max(hud_.measure_title_text(heist_card_title_, height),
                                hud_.measure_text(heist_card_line_.c_str(), 24.0f)) * 0.5f + 54.0f;
    const float bottom = top + height + 64.0f;
    hud_.quad({centre - half - 14.0f, top}, {centre + half, top}, {centre + half + 14.0f, bottom},
              {centre - half, bottom}, {0.012f, 0.025f, 0.02f, 0.84f * alpha});
    const glm::vec4 ink = heist_card_title_[0] == 'A' ? glm::vec4{1.0f, 0.30f, 0.22f, alpha}
                        : lost ? glm::vec4{0.85f, 0.85f, 0.85f, alpha}
                               : glm::vec4{0.55f, 0.95f, 0.55f, alpha};
    hud_.title_text_centered(heist_card_title_, centre + 4.0f, top + 15.0f, height,
                             {0.0f, 0.0f, 0.0f, 0.9f * alpha});
    hud_.title_text_centered(heist_card_title_, centre, top + 11.0f, height, ink);
    hud_.text_centered(heist_card_line_.c_str(), centre, top + height + 26.0f, 24.0f,
                       {1.0f, 0.95f, 0.8f, alpha});
}

}  // namespace apricot

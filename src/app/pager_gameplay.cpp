#include "app/app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "game/delivery_mission.h"
#include "game/lou_page.h"
#include "game/payphones.h"
#include "gfx/glyph_atlas.h"
#include "gfx/hud.h"

namespace apricot {

// The pager in play: Lou's page after the delivery, the payphone the marker
// points at, the call back, and the pager itself on the HUD.
//
// What a page is and how long it stays up is game/pager.h; when Lou pages and
// what counts as calling him is game/lou_page.h. This file decides only where
// those land in the frame: the rules on the fixed step, the beep on the step a
// page starts, and the drawing.

void App::reset_pager() {
    pager_ = {};
    pager_pages_beeped_ = 0;
    lou_page_wait_s_ = 0.0f;
    payphone_target_ = -1;
    // A load straight into the page's objective has a marker on its first
    // frame, before any step has run: the map can be opened from the pause
    // menu without one.
    if (mission_stage_ == MissionStage::CallLou) {
        const glm::vec3 focus = player_focus_position();
        payphone_target_ = nearest_payphone({focus.x, focus.z});
    }
}

void App::step_pager_rules() {
    const float dt = static_cast<float>(kSimDt);
    if (step_lou_page(mission_stage_, lou_page_wait_s_, dt)) {
        pager_.receive(kLouPageText);
        AP_INFO("Lou paged Johnny: \"%s\"", kLouPageText);
    }
    pager_.step(dt);
    // Once per page, on the step it starts showing, however many steps or
    // frames it stays up.
    if (pager_.pages_shown() != pager_pages_beeped_) {
        pager_pages_beeped_ = pager_.pages_shown();
        VoiceParams beep;
        beep.category = Category::Ui;
        beep.gain = 0.55f;
        audio_device_.mixer().play_oneshot(&audio_device_.bank().pager_beep, beep);
    }
    if (mission_stage_ == MissionStage::CallLou) {
        const glm::vec3 focus = player_focus_position();
        payphone_target_ = nearest_payphone({focus.x, focus.z}, payphone_target_);
    } else {
        payphone_target_ = -1;
    }
}

bool App::try_call_lou() {
    // WASTED lies where it fell; it does not get up to use the phone.
    if (!player_vitals_.alive()) return false;
    const int phone = payphone_in_reach(player_character_.position, on_foot_);
    if (!call_lou(mission_stage_, player_character_.position, on_foot_)) return false;
    VoiceParams dial;
    dial.category = Category::Ui;
    dial.gain = 0.6f;
    audio_device_.mixer().play_oneshot(&audio_device_.bank().payphone_dial, dial);
    payphone_target_ = -1;
    vehicle_interaction_notice_ = "CALLED LOU AT THE STORE";
    vehicle_notice_until_ = step_index_ + static_cast<uint64_t>(std::ceil(5.0 / kSimDt));
    AP_INFO("called Lou from %s; the next mission starts from here",
            payphone_site(static_cast<std::size_t>(phone)).name);
    // A checkpoint, like the delivery's. Automated captures never replace the
    // player's real one.
    if (frame_limit_ == 0) save_game();
    return true;
}

// The minimap's and the map's mission marker: the car, then Devon, then the
// payphone Lou's page sends the player to.
std::optional<glm::vec2> App::mission_target() const {
    if (mission_stage_ == MissionStage::DeliveryNeedsCar)
        return glm::vec2{car_.position.x, car_.position.z};
    if (mission_stage_ == MissionStage::DeliveryActive) {
        const glm::vec3 devon = city::devon_position();
        return glm::vec2{devon.x, devon.z};
    }
    if (mission_stage_ == MissionStage::CallLou && payphone_target_ >= 0) {
        const glm::vec3 phone = payphone_position(static_cast<std::size_t>(payphone_target_));
        return glm::vec2{phone.x, phone.z};
    }
    return std::nullopt;
}

namespace {

// Charcoal plastic, a grey-green reflective LCD with dark liquid-crystal dots,
// and a red alert LED: the pager a courier wore on his belt.
constexpr glm::vec4 kShadow{0.0f, 0.0f, 0.0f, 0.36f};
constexpr glm::vec4 kBodyEdge{0.030f, 0.032f, 0.035f, 1.0f};
constexpr glm::vec4 kBody{0.085f, 0.090f, 0.096f, 1.0f};
constexpr glm::vec4 kBodyHighlight{1.0f, 1.0f, 1.0f, 0.10f};
constexpr glm::vec4 kBezel{0.012f, 0.014f, 0.016f, 1.0f};
constexpr glm::vec4 kLcdTop{0.71f, 0.76f, 0.56f, 1.0f};
constexpr glm::vec4 kLcdBottom{0.58f, 0.65f, 0.45f, 1.0f};
constexpr glm::vec4 kLcdGlare{1.0f, 1.0f, 1.0f, 0.06f};
constexpr glm::vec4 kInkLit{0.05f, 0.09f, 0.04f, 0.94f};
// Every dot of the matrix shows faintly even when off, as on a real panel.
constexpr glm::vec4 kInkGhost{0.05f, 0.09f, 0.04f, 0.075f};
constexpr glm::vec4 kButton{0.19f, 0.20f, 0.21f, 1.0f};
constexpr glm::vec4 kButtonShade{0.02f, 0.02f, 0.025f, 1.0f};
constexpr glm::vec4 kLedOn{1.0f, 0.20f, 0.12f, 1.0f};
constexpr glm::vec4 kLedOff{0.26f, 0.06f, 0.05f, 1.0f};

// A rounded rectangle that never overlaps itself: three rects and four quarter
// fans, so a translucent fill such as the shadow blends exactly once.
void rounded_rect(Hud& hud, glm::vec2 lo, glm::vec2 hi, float r, glm::vec4 color) {
    r = std::min(r, 0.5f * std::min(hi.x - lo.x, hi.y - lo.y));
    hud.rect({lo.x + r, lo.y}, {hi.x - r, hi.y}, color);
    hud.rect({lo.x, lo.y + r}, {lo.x + r, hi.y - r}, color);
    hud.rect({hi.x - r, lo.y + r}, {hi.x, hi.y - r}, color);
    constexpr int kSegments = 6;
    const glm::vec2 centres[4] = {{lo.x + r, lo.y + r}, {hi.x - r, lo.y + r},
                                  {hi.x - r, hi.y - r}, {lo.x + r, hi.y - r}};
    for (int corner = 0; corner < 4; ++corner) {
        // Canvas +Y is down, so the top-left corner sweeps 180 to 270 degrees
        // and each corner after it starts a quarter turn on.
        const float start = glm::radians(180.0f + 90.0f * static_cast<float>(corner));
        const glm::vec2 c = centres[corner];
        for (int i = 0; i < kSegments; ++i) {
            const float a = start + glm::radians(90.0f) * static_cast<float>(i) /
                                        static_cast<float>(kSegments);
            const float b = start + glm::radians(90.0f) * static_cast<float>(i + 1) /
                                        static_cast<float>(kSegments);
            hud.triangle(c, c + r * glm::vec2{std::cos(a), std::sin(a)},
                         c + r * glm::vec2{std::cos(b), std::sin(b)}, color);
        }
    }
}

// One line of the 5x7 font (gfx/glyph_atlas.h) as liquid-crystal dots.
// `first_col` is the dot column, counted from the window's left edge, where
// the first glyph starts; only columns inside [0, cols) are drawn, which is
// what clips a crawling message to the LCD.
void dot_text(Hud& hud, const std::string& text, glm::vec2 origin, float pitch,
              float dot, int first_col, int cols, glm::vec4 ink) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int glyph_col = first_col + static_cast<int>(i) * kAdvanceUnits;
        if (glyph_col >= cols) break;
        if (glyph_col + kGlyphUnitsW <= 0) continue;
        int code = static_cast<unsigned char>(text[i]);
        if (code < kFirstChar || code >= kFirstChar + kCharCount) code = '?';
        const auto& glyph = kFont5x7[static_cast<std::size_t>(code - kFirstChar)];
        for (int c = 0; c < kGlyphUnitsW; ++c) {
            const int col = glyph_col + c;
            if (col < 0 || col >= cols) continue;
            const unsigned bits = glyph[static_cast<std::size_t>(c)];
            for (int row = 0; row < kGlyphUnitsH; ++row) {
                if (((bits >> row) & 1u) == 0u) continue;
                const glm::vec2 p = origin + glm::vec2{static_cast<float>(col) * pitch,
                                                       static_cast<float>(row) * pitch};
                hud.rect(p, p + glm::vec2{dot}, ink);
            }
        }
    }
}

float dot_text_width(std::size_t length, float pitch) {
    if (length == 0) return 0.0f;
    return static_cast<float>(length * static_cast<std::size_t>(kAdvanceUnits) - 1u) * pitch;
}

}  // namespace

void App::draw_pager(glm::vec2 vp, float time_of_day, float top) {
    if (!pager_.showing() || vp.x <= 0.0f || vp.y <= 0.0f) return;

    // The message line: the 5x7 font on a 5-unit dot pitch, kPagerLcdCells
    // cells wide. The status line above it is the same font at half the pitch.
    constexpr float kPitch = 5.0f;
    constexpr float kDot = 4.0f;
    constexpr int kCols = kPagerLcdCells * kAdvanceUnits;
    constexpr float kSmallPitch = 2.5f;
    constexpr float kSmallDot = 2.0f;
    constexpr float kTextW = static_cast<float>(kCols) * kPitch;
    constexpr float kTextH = static_cast<float>(kGlyphUnitsH) * kPitch;
    constexpr float kStatusH = static_cast<float>(kGlyphUnitsH) * kSmallPitch;
    constexpr float kGlassPadX = 12.0f;
    constexpr float kGlassPadY = 10.0f;
    constexpr float kStatusGap = 7.0f;
    constexpr float kGlassW = kTextW + 2.0f * kGlassPadX;
    constexpr float kGlassH = kGlassPadY + kStatusH + kStatusGap + kTextH + kGlassPadY;
    constexpr float kBezelW = 6.0f;
    constexpr float kBodyPadX = 22.0f;
    constexpr float kBodyTop = 22.0f;
    constexpr float kBodyBottom = 42.0f;
    constexpr float kBodyW = kGlassW + 2.0f * (kBezelW + kBodyPadX);
    constexpr float kBodyH = kBodyTop + 2.0f * kBezelW + kGlassH + kBodyBottom;
    constexpr float kCorner = 18.0f;

    // In from the left edge and back out, eased; on each beep, a hard rattle.
    const float slide = pager_.slide();
    const float ease = 1.0f - (1.0f - slide) * (1.0f - slide) * (1.0f - slide);
    glm::vec2 lo{28.0f - (1.0f - ease) * (kBodyW + 60.0f), top};
    if (pager_.buzzing()) {
        const float t = pager_.shown_seconds();
        lo += glm::vec2{3.0f * std::sin(t * 311.0f), 1.5f * std::sin(t * 227.0f)};
    }
    const glm::vec2 hi = lo + glm::vec2{kBodyW, kBodyH};

    rounded_rect(hud_, lo + glm::vec2{5.0f, 6.0f}, hi + glm::vec2{5.0f, 6.0f}, kCorner, kShadow);
    rounded_rect(hud_, lo, hi, kCorner, kBodyEdge);
    rounded_rect(hud_, lo + glm::vec2{2.0f}, hi - glm::vec2{2.0f}, kCorner - 2.0f, kBody);
    // Light catching the top edge of the plastic.
    hud_.rect({lo.x + kCorner, lo.y + 4.0f}, {hi.x - kCorner, lo.y + 6.0f}, kBodyHighlight);

    const glm::vec2 bezel_lo = lo + glm::vec2{kBodyPadX, kBodyTop};
    const glm::vec2 bezel_hi = bezel_lo + glm::vec2{kGlassW, kGlassH} + glm::vec2{2.0f * kBezelW};
    rounded_rect(hud_, bezel_lo, bezel_hi, 8.0f, kBezel);
    const glm::vec2 glass_lo = bezel_lo + glm::vec2{kBezelW};
    const glm::vec2 glass_hi = glass_lo + glm::vec2{kGlassW, kGlassH};
    hud_.gradient_rect(glass_lo, glass_hi, kLcdTop, kLcdTop, kLcdBottom, kLcdBottom);

    // Status line: how many pages this pager has had, and the time.
    const glm::vec2 status_lo = glass_lo + glm::vec2{kGlassPadX, kGlassPadY};
    char count[16];
    std::snprintf(count, sizeof(count), "MSG %u", static_cast<unsigned>(pager_.pages_shown()));
    const int small_cols = static_cast<int>(kTextW / kSmallPitch);
    dot_text(hud_, count, status_lo, kSmallPitch, kSmallDot, 0, small_cols, kInkLit);
    int minutes = static_cast<int>(time_of_day * 1440.0f + 0.5f);
    minutes = ((minutes % 1440) + 1440) % 1440;
    const int hour24 = minutes / 60;
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%d:%02d%c", hour24 % 12 == 0 ? 12 : hour24 % 12,
                  minutes % 60, hour24 < 12 ? 'A' : 'P');
    const std::string clock_text = clock;
    const float clock_w = dot_text_width(clock_text.size(), kSmallPitch);
    dot_text(hud_, clock_text, {status_lo.x + kTextW - clock_w, status_lo.y}, kSmallPitch,
             kSmallDot, 0, small_cols, kInkLit);

    // The message, crawling right to left across every dot of the matrix.
    const glm::vec2 text_lo = status_lo + glm::vec2{0.0f, kStatusH + kStatusGap};
    for (int col = 0; col < kCols; ++col) {
        for (int row = 0; row < kGlyphUnitsH; ++row) {
            const glm::vec2 p = text_lo + glm::vec2{static_cast<float>(col) * kPitch,
                                                    static_cast<float>(row) * kPitch};
            hud_.rect(p, p + glm::vec2{kDot}, kInkGhost);
        }
    }
    const int crawled_cols =
        static_cast<int>(pager_.crawl_cells() * static_cast<float>(kAdvanceUnits));
    dot_text(hud_, pager_.text(), text_lo, kPitch, kDot, kCols - crawled_cols, kCols, kInkLit);

    // A faint sheen across the glass.
    hud_.quad({glass_lo.x + kGlassW * 0.14f, glass_lo.y},
              {glass_lo.x + kGlassW * 0.30f, glass_lo.y},
              {glass_lo.x + kGlassW * 0.22f, glass_hi.y},
              {glass_lo.x + kGlassW * 0.06f, glass_hi.y}, kLcdGlare);

    // Three buttons under the glass, and the alert LED that flashes with the beeps.
    const float button_top = bezel_hi.y + 13.0f;
    for (int i = 0; i < 3; ++i) {
        const float right = hi.x - kBodyPadX - static_cast<float>(i) * 58.0f;
        const glm::vec2 b_lo{right - 44.0f, button_top};
        const glm::vec2 b_hi{right, button_top + 15.0f};
        rounded_rect(hud_, b_lo + glm::vec2{0.0f, 2.0f}, b_hi + glm::vec2{0.0f, 2.0f}, 7.5f,
                     kButtonShade);
        rounded_rect(hud_, b_lo, b_hi, 7.5f, kButton);
    }
    const glm::vec2 led{lo.x + kBodyPadX + 9.0f, button_top + 8.0f};
    hud_.circle(led, 6.5f, kButtonShade);
    hud_.circle(led, 4.5f, pager_.buzzing() ? kLedOn : kLedOff);
}

// A floating "Call Lou" tag over the payphone the marker points at, the same
// shape as the one over the mission car. It has to be close enough to matter:
// across the city the minimap's bearing is the guide, and a tag hanging over
// the horizon would only be clutter.
void App::draw_payphone_cue(glm::vec2 vp) {
    if (mission_stage_ != MissionStage::CallLou || payphone_target_ < 0 || pager_.showing())
        return;
    constexpr float kCueRangeM = 150.0f;
    const glm::vec3 phone = payphone_position(static_cast<std::size_t>(payphone_target_));
    const glm::vec3 focus = player_focus_position();
    if (glm::length(glm::vec2{phone.x - focus.x, phone.z - focus.z}) > kCueRangeM) return;
    const float bob =
        std::sin(static_cast<float>(step_index_) * static_cast<float>(kSimDt) * 3.2f) * 0.12f;
    const auto cue = project_mission_cue(phone + glm::vec3{0.0f, 3.0f + bob, 0.0f},
                                         camera_.view_projection(), vp);
    if (!cue.visible) return;
    constexpr const char* kLabel = "Call Lou";
    const float half = hud_.measure_text(kLabel, 24.0f) * 0.5f + 17.0f;
    const glm::vec2 lo{cue.screen.x - half, cue.screen.y - 53.0f};
    const glm::vec2 hi{cue.screen.x + half, cue.screen.y - 17.0f};
    hud_.rect(lo + glm::vec2{3.0f, 4.0f}, hi + glm::vec2{3.0f, 4.0f}, {0.0f, 0.0f, 0.0f, 0.42f});
    hud_.rect(lo, hi, {0.025f, 0.030f, 0.028f, 0.94f});
    hud_.outline(lo, hi, 2.0f, {1.0f, 0.76f, 0.18f, 1.0f});
    hud_.text_centered(kLabel, cue.screen.x, cue.screen.y - 47.0f, 24.0f,
                       {1.0f, 0.94f, 0.73f, 1.0f});
    hud_.triangle({cue.screen.x - 10.0f, cue.screen.y - 17.0f},
                  {cue.screen.x + 10.0f, cue.screen.y - 17.0f},
                  {cue.screen.x, cue.screen.y - 3.0f}, {1.0f, 0.76f, 0.18f, 1.0f});
}

}  // namespace apricot

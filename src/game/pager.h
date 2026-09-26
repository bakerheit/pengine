#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace apricot {

// The pager on Johnny's belt. A page arrives, the pager buzzes, and the message
// crawls right to left across a one-line dot-matrix LCD, twice, before the
// pager goes back on the belt. Pages that arrive while one is showing wait
// their turn, oldest first.
//
// Stepped on the fixed step, so how long a page stays up is a count of sim
// steps and not of rendered frames. It holds presentation state only and none
// of it is saved: which pages the player has had follows from mission
// progress (game/lou_page.h), so a load re-derives the page rather than
// trusting a serialised inbox.

// The LCD's width in character cells, and how fast a page crawls across it.
// The host draws each cell as the 5x7 glyph plus one column of spacing.
inline constexpr int kPagerLcdCells = 16;
inline constexpr float kPagerCrawlCellsPerSecond = 7.0f;
// How many times a page crawls all the way across before the pager goes away.
inline constexpr int kPagerCrawlPasses = 2;
// Sliding on and off the screen.
inline constexpr float kPagerSlideSeconds = 0.3f;

// The alert a page arrives with: two bursts of four short beeps. The HUD
// shakes the pager on each beep, and the synthesised clip (audio/synth.h,
// synth_pager_beep) sounds the same pattern; pager_tests holds the two
// together, so change them as a pair.
inline constexpr int kPagerBeepBursts = 2;
inline constexpr int kPagerBeepsPerBurst = 4;
inline constexpr float kPagerBurstPeriodSeconds = 0.6f;
inline constexpr float kPagerBeepPeriodSeconds = 0.1f;
inline constexpr float kPagerBeepOnSeconds = 0.055f;
inline constexpr float kPagerBuzzSeconds = 1.0f;

// Whether a beep is sounding `t` seconds after a page arrived.
inline bool pager_beep_sounding(float t) {
    if (!(t >= 0.0f)) return false;
    const int burst = static_cast<int>(t / kPagerBurstPeriodSeconds);
    if (burst >= kPagerBeepBursts) return false;
    const float in_burst = t - static_cast<float>(burst) * kPagerBurstPeriodSeconds;
    const int beep = static_cast<int>(in_burst / kPagerBeepPeriodSeconds);
    if (beep >= kPagerBeepsPerBurst) return false;
    return in_burst - static_cast<float>(beep) * kPagerBeepPeriodSeconds <
           kPagerBeepOnSeconds;
}

class Pager {
public:
    // Queue a page. It shows now if the pager is free, after the ones ahead of
    // it otherwise. An empty page is ignored: there is nothing to crawl.
    void receive(std::string text) {
        if (text.empty()) return;
        queue_.push_back(std::move(text));
        if (!showing_) show_next();
    }

    void step(float dt) {
        if (!showing_ || !(dt > 0.0f)) return;
        shown_s_ += dt;
        if (shown_s_ < display_seconds(current_.size())) return;
        showing_ = false;
        shown_s_ = 0.0f;
        current_.clear();
        if (!queue_.empty()) show_next();
    }

    bool showing() const { return showing_; }
    // The page on screen; empty when nothing is.
    const std::string& text() const { return current_; }
    // Seconds since the page on screen arrived.
    float shown_seconds() const { return shown_s_; }
    // True on each beep of the alert, for the HUD's shake.
    bool buzzing() const { return showing_ && pager_beep_sounding(shown_s_); }

    // 0 off screen, 1 fully on: in over the first kPagerSlideSeconds, out over
    // the last.
    float slide() const {
        if (!showing_) return 0.0f;
        const float in = shown_s_ / kPagerSlideSeconds;
        const float out =
            (display_seconds(current_.size()) - shown_s_) / kPagerSlideSeconds;
        const float s = in < out ? in : out;
        return s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    }

    // How far the text has crawled in the current pass, in cells. At 0 its
    // first character is just off the right edge of the LCD; the pass ends at
    // kPagerLcdCells + length, when its last character has left the left edge.
    // Once the passes are spent the LCD is blank and the pager slides away.
    float crawl_cells() const {
        if (!showing_) return 0.0f;
        const float crawled = shown_s_ * kPagerCrawlCellsPerSecond;
        const float pass = static_cast<float>(kPagerLcdCells) +
                           static_cast<float>(current_.size());
        if (crawled >= pass * static_cast<float>(kPagerCrawlPasses)) return pass;
        return crawled - pass * static_cast<float>(static_cast<int>(crawled / pass));
    }

    // Counts every page that has started showing. It only goes up, so the host
    // plays the beep once per step of it, however many frames it looks.
    uint32_t pages_shown() const { return pages_shown_; }
    // The most recent page to show, kept after the pager goes away.
    const std::string& last_page() const { return last_; }
    std::size_t queued() const { return queue_.size(); }

    // How long a page of `length` characters stays up: its crawl passes, then
    // the slide back off the screen.
    static float display_seconds(std::size_t length) {
        const float pass = static_cast<float>(kPagerLcdCells) +
                           static_cast<float>(length);
        return pass * static_cast<float>(kPagerCrawlPasses) /
                   kPagerCrawlCellsPerSecond +
               kPagerSlideSeconds;
    }

private:
    void show_next() {
        current_ = std::move(queue_.front());
        queue_.pop_front();
        last_ = current_;
        showing_ = true;
        shown_s_ = 0.0f;
        ++pages_shown_;
    }

    std::deque<std::string> queue_;
    std::string current_;
    std::string last_;
    bool showing_ = false;
    float shown_s_ = 0.0f;
    uint32_t pages_shown_ = 0;
};

}  // namespace apricot

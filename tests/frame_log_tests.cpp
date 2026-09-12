// The per-frame performance recorder: core/frame_log.h.
//
// What is actually at risk here is not the CSV — it is the claims the trailer
// makes. A recorder that under-reports dips is worse than no recorder, because
// it is evidence: someone reads "0.1% slow frames" and stops looking. So every
// assertion below is about a number that would be QUOTED in a report.
//
// Headless by construction. FrameLog::capture_in_memory() runs the identical
// row-formatting path a real session runs, with the file swapped for a string,
// so this is the producer under test and not a re-implementation of it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/frame_log.h"
#include "test_assert.h"

using namespace apricot;

namespace {

FrameSample frame_at(int index, double t_s, double ms) {
    FrameSample s;
    s.frame = index;
    s.t_s = t_s;
    s.ms = ms;
    s.place = "Downtown";
    s.mode = "drive";
    s.weather = "clear";
    return s;
}

// Count the data rows: everything that is neither the header nor a `#` line.
std::size_t data_rows(const std::string& csv) {
    std::size_t rows = 0;
    std::size_t start = 0;
    bool first = true;
    while (start < csv.size()) {
        const std::size_t end = csv.find('\n', start);
        if (end == std::string::npos) break;
        const std::string line = csv.substr(start, end - start);
        if (!first && !line.empty() && line[0] != '#') ++rows;
        first = false;
        start = end + 1;
    }
    return rows;
}

std::string nth_field(const std::string& line, std::size_t n) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < n; ++i) {
        start = line.find(',', start);
        if (start == std::string::npos) return "";
        ++start;
    }
    const std::size_t end = line.find(',', start);
    return line.substr(start, end == std::string::npos ? end : end - start);
}

// Resolve a column by NAME. Every assertion below goes through this: a test
// that hardcodes an index breaks the moment a column is inserted, and worse,
// a test that hardcodes the WRONG index passes while asserting on a neighbour.
std::size_t column_of(const char* name) {
    const std::string header(FrameLog::header_line());
    for (std::size_t c = 0; c < 80; ++c) {
        if (nth_field(header, c) == name) return c;
    }
    REQUIRE_MSG(false, "the header must name this column", name);
    return 0;
}

std::string nth_data_line(const std::string& csv, std::size_t n) {
    std::size_t start = 0;
    std::size_t seen = 0;
    bool first = true;
    while (start < csv.size()) {
        const std::size_t end = csv.find('\n', start);
        if (end == std::string::npos) break;
        const std::string line = csv.substr(start, end - start);
        if (!first && !line.empty() && line[0] != '#') {
            if (seen == n) return line;
            ++seen;
        }
        first = false;
        start = end + 1;
    }
    return "";
}

// The disabled recorder is the one that runs in every session that did not ask
// for one, so it has to cost nothing and say nothing.
void test_disabled_records_nothing() {
    FrameLog log;
    REQUIRE(!log.enabled());
    for (int i = 0; i < 100; ++i) {
        REQUIRE(!log.record(frame_at(i, i * 0.016, 400.0)));
    }
    log.mark("felt that", frame_at(100, 1.6, 400.0));
    REQUIRE(log.buffer().empty());
    REQUIRE(log.summary().frames == 0);
    REQUIRE(log.marks().empty());
    apricot_test::pass("a recorder with no sink writes nothing and counts nothing");
}

void test_header_and_row_widths_match() {
    FrameLog log;
    log.capture_in_memory();
    log.record(frame_at(0, 0.0, 16.6));

    const std::string header(FrameLog::header_line());
    const std::string row = nth_data_line(log.buffer(), 0);
    REQUIRE(!row.empty());

    const auto commas = [](const std::string& s) {
        std::size_t n = 0;
        for (char c : s) n += (c == ',') ? 1u : 0u;
        return n;
    };
    // A row one field out of step with the header is the classic way a CSV
    // reads fine and means something else entirely.
    REQUIRE_MSG(commas(header) == commas(row),
                "row field count must match the header", "widths");
    REQUIRE(nth_field(row, column_of("frame")) == "0");
    REQUIRE(nth_field(row, column_of("place")) == "Downtown");
    REQUIRE(nth_field(row, column_of("mode")) == "drive");
    apricot_test::pass("every row lines up with the header it was written under");
}

// A comma inside a district name would silently shift every later column.
void test_string_fields_are_scrubbed() {
    FrameLog log;
    log.capture_in_memory();
    FrameSample s = frame_at(0, 0.0, 16.6);
    s.place = "Ostend, East";
    s.mode = "on \"foot\"";
    log.record(s);

    const std::string row = nth_data_line(log.buffer(), 0);
    // Spaces survive — "the Meadows" is a real district name. Only the
    // characters that would break the column structure are replaced.
    REQUIRE(nth_field(row, column_of("place")) == "Ostend_ East");
    REQUIRE(nth_field(row, column_of("mode")) == "on _foot_");
    apricot_test::pass("commas and quotes in a place name cannot shift the columns");
}

void test_absolute_threshold_flags_the_slow_frame() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.spike_ratio = 1000.0;  // isolate the absolute rule
    log.configure(cfg);
    log.capture_in_memory();

    REQUIRE(!log.record(frame_at(0, 0.0, 19.9)));
    REQUIRE(log.record(frame_at(1, 0.02, 20.1)));
    REQUIRE(log.summary().spikes == 1);

    // The flag reaches the row, not just the tally: the CSV is what gets read.
    REQUIRE(nth_field(nth_data_line(log.buffer(), 0), column_of("spike")) == "0");
    REQUIRE(nth_field(nth_data_line(log.buffer(), 1), column_of("spike")) == "1");
    apricot_test::pass("a frame over the absolute threshold is flagged in the row");
}

// The reason the relative rule exists: a session holding 200 fps that drops one
// 12 ms frame has hitched, and no absolute bar set for 60 Hz would see it.
void test_relative_threshold_catches_a_hitch_in_a_fast_session() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.spike_ratio = 2.0;
    cfg.relative_floor_ms = 8.0;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 400; ++i) log.record(frame_at(i, i * 0.005, 5.0));
    REQUIRE_MSG(log.summary().spikes == 0, "a steady fast session has no dips",
                "warmup");
    REQUIRE(log.record(frame_at(400, 2.0, 12.0)));
    REQUIRE(log.summary().spikes == 1);
    apricot_test::pass("a 12 ms frame in a 200 fps session still reads as a dip");
}

// ...and the floor is why that rule does not fire constantly. At 300 fps the
// frame-to-frame jitter alone clears 2x the mean.
void test_relative_floor_suppresses_noise() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.spike_ratio = 2.0;
    cfg.relative_floor_ms = 8.0;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 200; ++i) {
        log.record(frame_at(i, i * 0.003, (i % 2 == 0) ? 2.0 : 7.5));
    }
    REQUIRE_MSG(log.summary().spikes == 0,
                "jitter below the floor is not a dip", "floor");
    apricot_test::pass("frame jitter under the floor never reaches the report");
}

// A spike must not raise the bar it is judged against, or a run of bad frames
// normalises itself and the second half of a stutter goes unrecorded.
void test_a_spike_does_not_hide_the_spike_behind_it() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 100; ++i) log.record(frame_at(i, i * 0.016, 16.0));
    for (int i = 0; i < 10; ++i) log.record(frame_at(100 + i, 1.6 + i * 0.05, 50.0));
    REQUIRE_MSG(log.summary().spikes == 10, "every frame of the run counts",
                "run");
    apricot_test::pass("a run of ten bad frames reports as ten, not as one");
}

void test_percentiles_track_a_known_distribution() {
    FrameLog log;
    log.capture_in_memory();
    // 990 frames at 10 ms, 10 at 40 ms: p50 is 10, p99.9 is in the tail.
    for (int i = 0; i < 990; ++i) log.record(frame_at(i, i * 0.01, 10.0));
    for (int i = 0; i < 10; ++i) log.record(frame_at(990 + i, 9.9 + i * 0.04, 40.0));

    const FrameLog::Summary sum = log.summary();
    REQUIRE(sum.frames == 1000);
    REQUIRE_NEAR(sum.mean_ms, (990 * 10.0 + 10 * 40.0) / 1000.0, 1e-9);
    REQUIRE_NEAR(sum.p50_ms, 10.0, 0.25);
    REQUIRE_NEAR(sum.p90_ms, 10.0, 0.25);
    REQUIRE_NEAR(sum.p99_ms, 10.0, 0.25);
    REQUIRE_MSG(sum.p999_ms >= 39.0, "the 99.9th must land in the tail", "p999");
    REQUIRE_NEAR(sum.worst_ms, 40.0, 1e-9);

    // Rounding a percentile DOWN would read as "we are fine" on the frames
    // that are not, so the bucket's upper edge is the reported value.
    REQUIRE(sum.p50_ms >= 10.0);
    apricot_test::pass("percentiles come out of the histogram at bucket accuracy");
}

// Percentiles must never exceed the worst frame actually seen, or the report
// quotes a number that never happened.
void test_percentiles_never_exceed_the_worst_frame() {
    FrameLog log;
    log.capture_in_memory();
    for (int i = 0; i < 50; ++i) log.record(frame_at(i, i * 0.016, 16.31));
    const FrameLog::Summary sum = log.summary();
    REQUIRE(sum.p50_ms <= sum.worst_ms);
    REQUIRE(sum.p999_ms <= sum.worst_ms);
    apricot_test::pass("no percentile is slower than the slowest frame recorded");
}

// Frames far past the top bucket (a 3 s asset load) must not wrap, saturate
// wrong, or drag the histogram out of range.
void test_enormous_frames_land_in_the_overflow_bucket() {
    FrameLog log;
    log.capture_in_memory();
    for (int i = 0; i < 99; ++i) log.record(frame_at(i, i * 0.016, 16.0));
    REQUIRE(log.record(frame_at(99, 1.6, 3000.0)));
    const FrameLog::Summary sum = log.summary();
    REQUIRE_NEAR(sum.worst_ms, 3000.0, 1e-9);
    REQUIRE_NEAR(sum.p999_ms, 3000.0, 1e-9);
    REQUIRE_NEAR(sum.p50_ms, 16.25, 0.26);
    apricot_test::pass("a three-second frame overflows cleanly and stays in the tail");
}

void test_worst_frames_are_ranked_and_bounded() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();

    // 40 spikes of rising cost; only the top 16 may survive.
    for (int i = 0; i < 40; ++i) {
        log.record(frame_at(i, i * 0.05, 30.0 + i));
    }
    const std::vector<FrameSample> worst = log.worst_frames();
    REQUIRE(worst.size() == 16);
    REQUIRE_NEAR(worst.front().ms, 69.0, 1e-9);
    for (std::size_t i = 1; i < worst.size(); ++i) {
        REQUIRE_MSG(worst[i - 1].ms >= worst[i].ms, "slowest first", "order");
    }
    REQUIRE_MSG(worst.back().ms >= 54.0,
                "the kept set must be the top of the distribution", "cutoff");
    apricot_test::pass("the worst-frame list keeps the top 16 in order, not the last 16");
}

// The whole point of the position columns: naming the block a dip happens on.
void test_hotspots_name_the_block_where_frames_go_slow() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.cell_m = 64.0f;
    log.configure(cfg);
    log.capture_in_memory();

    // A clean stretch of road...
    for (int i = 0; i < 200; ++i) {
        FrameSample s = frame_at(i, i * 0.016, 10.0);
        s.x = 10.0f;
        s.z = 10.0f;
        s.place = "Westmere";
        log.record(s);
    }
    // ...and one junction that hitches half the time.
    for (int i = 0; i < 200; ++i) {
        FrameSample s = frame_at(200 + i, 3.2 + i * 0.016, (i % 2) ? 10.0 : 45.0);
        s.x = 1000.0f;
        s.z = 2000.0f;
        s.place = "Ostend";
        log.record(s);
    }

    const std::vector<FrameLog::Hotspot> hot = log.hotspots();
    REQUIRE(!hot.empty());
    REQUIRE_MSG(std::strcmp(hot.front().place, "Ostend") == 0,
                "the worst cell is the junction, not the clean road", "place");
    REQUIRE(hot.front().frames == 200u);
    REQUIRE(hot.front().slow == 100u);
    // The position reported is where the player WAS, not the middle of the
    // grid cell they were in.
    REQUIRE_NEAR(static_cast<double>(hot.front().x), 1000.0, 0.01);
    REQUIRE_NEAR(static_cast<double>(hot.front().z), 2000.0, 0.01);
    // The clean cell never had a slow frame, so it must not be listed at all.
    for (const FrameLog::Hotspot& h : hot) {
        REQUIRE(std::strcmp(h.place, "Westmere") != 0);
    }
    apricot_test::pass("the hot-spot map points at the junction that hitches");
}

// One slow frame as you clip the corner of a cell is a 100% slow rate and
// means nothing; the sample floor is what keeps it out of the report.
void test_thin_cells_are_not_reported_as_hotspots() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.cell_m = 64.0f;
    log.configure(cfg);
    log.capture_in_memory();

    FrameSample s = frame_at(0, 0.0, 90.0);
    s.x = 5000.0f;
    s.z = 5000.0f;
    s.place = "Fringe";
    log.record(s);
    REQUIRE(log.hotspots().empty());
    REQUIRE_MSG(log.hotspots(1u).size() == 1u,
                "it is still there when the floor is lowered", "floor");
    apricot_test::pass("a cell with one sample is not a hot spot");
}

// Negative world coordinates share a cell with nothing. Truncation toward zero
// would fold x=-10 and x=+10 into the same block and blame the wrong street.
void test_negative_coordinates_get_their_own_cells() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.cell_m = 64.0f;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 100; ++i) {
        FrameSample s = frame_at(i, i * 0.016, 40.0);
        s.x = -10.0f;
        s.z = -10.0f;
        s.place = "West";
        log.record(s);
    }
    for (int i = 0; i < 100; ++i) {
        FrameSample s = frame_at(100 + i, 1.6 + i * 0.016, 40.0);
        s.x = 10.0f;
        s.z = 10.0f;
        s.place = "East";
        log.record(s);
    }
    const std::vector<FrameLog::Hotspot> hot = log.hotspots();
    REQUIRE_MSG(hot.size() == 2, "two sides of the origin are two blocks",
                "sign");
    apricot_test::pass("a position left of the origin is not the same block as one right of it");
}

// The player presses the key AFTER feeling the stutter, so the frame they mean
// is already behind them. A mark that only recorded the frame it landed on
// would point at a healthy one every time.
void test_a_mark_looks_backwards_for_the_frame_the_player_felt() {
    FrameLog log;
    log.capture_in_memory();

    for (int i = 0; i < 60; ++i) log.record(frame_at(i, i * 0.016, 16.0));
    log.record(frame_at(60, 0.96, 120.0));          // the stutter
    for (int i = 0; i < 30; ++i) {                  // half a second of reaction
        log.record(frame_at(61 + i, 0.98 + i * 0.016, 16.0));
    }

    FrameSample at_press = frame_at(91, 1.46, 16.0);
    at_press.x = 400.0f;
    at_press.z = -250.0f;
    at_press.place = "Sycamore";
    log.mark("felt a hitch", at_press);

    REQUIRE(log.marks().size() == 1);
    const FrameLog::Mark& m = log.marks().front();
    REQUIRE_NEAR(m.worst_before_ms, 120.0, 1e-9);
    REQUIRE_NEAR(m.worst_before_t_s, 0.96, 1e-9);
    REQUIRE(m.label == "felt a hitch");
    REQUIRE(std::strcmp(m.place, "Sycamore") == 0);
    REQUIRE_NEAR(static_cast<double>(m.x), 400.0, 1e-6);
    apricot_test::pass("a marker points back at the slow frame, not at the key press");
}

// A mark long after the stutter has nothing to blame, and must say so rather
// than reach further back and invent a culprit.
void test_a_mark_outside_the_lookback_blames_nothing() {
    FrameLog log;
    log.capture_in_memory();
    log.record(frame_at(0, 0.0, 200.0));
    for (int i = 0; i < 600; ++i) log.record(frame_at(1 + i, 0.02 + i * 0.016, 16.0));
    log.mark("late", frame_at(601, 9.6, 16.0));
    REQUIRE_NEAR(log.marks().front().worst_before_ms, 16.0, 1e-9);
    apricot_test::pass("a mark ten seconds late does not blame a ten-second-old frame");
}

void test_a_mark_writes_its_own_row_and_is_not_counted_as_a_frame() {
    FrameLog log;
    log.capture_in_memory();
    for (int i = 0; i < 5; ++i) log.record(frame_at(i, i * 0.016, 16.0));
    log.mark("here", frame_at(5, 0.08, 16.0));

    REQUIRE_MSG(data_rows(log.buffer()) == 6, "the mark is a row", "rows");
    REQUIRE_MSG(log.summary().frames == 5,
                "but it is not a sixth timed frame", "frames");
    REQUIRE(nth_field(nth_data_line(log.buffer(), 5), column_of("mark")) == "here");
    apricot_test::pass("a mark lands in the timeline without inflating the frame count");
}

// The trailer is what gets read first. If it is missing or truncated the file
// is a wall of numbers.
void test_the_trailer_reports_what_the_session_saw() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    cfg.cell_m = 64.0f;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 300; ++i) {
        FrameSample s = frame_at(i, i * 0.016, (i % 50 == 0) ? 60.0 : 16.0);
        s.x = 128.0f;
        s.z = 128.0f;
        s.place = "Pinatty Heights";
        log.record(s);
    }
    log.mark("stutter", frame_at(300, 4.8, 16.0));
    log.close();

    const std::string& out = log.buffer();
    REQUIRE(out.find("---- session summary ----") != std::string::npos);
    REQUIRE(out.find("---- worst frames ----") != std::string::npos);
    REQUIRE(out.find("---- where the dips happen ----") != std::string::npos);
    REQUIRE(out.find("---- moments the player marked ----") != std::string::npos);
    REQUIRE(out.find("Pinatty Heights") != std::string::npos);
    REQUIRE_MSG(out.find("6 of 300 frames") != std::string::npos,
                "the dip count must appear as counted", "spikes");
    apricot_test::pass("the trailer carries the summary, the worst frames and the hot spots");
}

// close() on a recorder that never ran must not emit a trailer full of zeroes
// that reads like a clean session.
void test_closing_an_empty_recorder_says_nothing() {
    FrameLog log;
    log.capture_in_memory();
    log.close();
    REQUIRE(log.buffer().find("session summary") == std::string::npos);
    log.close();  // twice is safe
    apricot_test::pass("an empty session leaves no summary to misread");
}

void test_stutter_time_excludes_the_frame_you_would_have_paid_anyway() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 99; ++i) log.record(frame_at(i, i * 0.016, 16.0));
    log.record(frame_at(99, 1.6, 116.0));

    const FrameLog::Summary sum = log.summary();
    REQUIRE(sum.spikes == 1);
    REQUIRE_NEAR(sum.spike_ms_total, 116.0, 1e-9);
    // 116 ms of frame, of which ~16 was the frame you owed regardless.
    REQUIRE_NEAR(sum.stutter_ms, 116.0 - sum.p50_ms, 1e-9);
    REQUIRE(sum.stutter_ms > 99.0 && sum.stutter_ms < 101.0);
    apricot_test::pass("stutter time is the excess over the median, not the raw total");
}

// A long session must not grow without bound: the histogram, the worst list
// and the mark list are all fixed, and the cell map saturates at the size of
// the world.
void test_a_long_session_stays_bounded() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.cell_m = 64.0f;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 30000; ++i) {
        FrameSample s = frame_at(i, i * 0.008, 8.0);
        s.x = static_cast<float>((i % 64) * 64);
        s.z = static_cast<float>((i % 64) * 64);
        log.record(s);
        if (i % 100 == 0) log.mark("spam", s);
    }
    REQUIRE_MSG(log.marks().size() <= 64, "the mark list is capped", "marks");
    REQUIRE_MSG(log.worst_frames().size() <= 16, "the worst list is capped",
                "worst");
    REQUIRE(log.summary().frames == 30000);
    apricot_test::pass("a three-hour session keeps its bookkeeping the same size as a one-minute one");
}

// The accounting rule is the one thing here that can be wrong in a way that
// LOOKS right: world_ms already contains mesh_ms and render_ms already
// contains cull_ms and light_ms, so a naive sum double-counts and reports more
// than 100% of a frame explained.
void test_accounting_does_not_double_count_nested_costs() {
    FrameSample s = frame_at(0, 0.0, 20.0);
    s.sim_ms = 4.0;
    s.world_ms = 5.0;
    s.mesh_ms = 4.5;    // a SUBSET of world_ms
    s.visual_ms = 3.0;
    s.scene_ms = 1.0;
    s.render_ms = 6.0;
    s.cull_ms = 2.0;    // a SUBSET of render_ms
    s.light_ms = 1.0;   // likewise
    s.swap_ms = 1.0;
    s.gpu_ms = 15.0;    // concurrent, not a slice

    REQUIRE_NEAR(accounted_ms(s), 20.0, 1e-9);
    REQUIRE_MSG(accounted_ms(s) <= s.ms,
                "the phases must never explain more than the frame", "sum");
    apricot_test::pass("nested costs are counted once and the GPU is not counted at all");
}

void test_unaccounted_time_is_reported_not_hidden() {
    FrameLog log;
    log.capture_in_memory();
    FrameSample s = frame_at(0, 0.0, 30.0);
    s.sim_ms = 2.0;
    s.render_ms = 3.0;   // only 5 of 30 ms explained
    log.record(s);

    const FrameLog::Summary sum = log.summary();
    REQUIRE_NEAR(sum.unaccounted_ms, 25.0, 1e-9);

    // ...and it reaches the row, which is where anyone slicing the CSV will
    // look for it.
    const std::string row = nth_data_line(log.buffer(), 0);
    REQUIRE(nth_field(row, column_of("accounted_ms")) == "5.000");
    REQUIRE(nth_field(row, column_of("unaccounted_ms")) == "25.000");
    apricot_test::pass("a frame the phases cannot explain says so, in its own column");
}

// The phase columns exist to separate an average frame from a bad one. If the
// dip means were folded in with the rest, the distinction would be lost.
void test_dip_phases_are_tracked_apart_from_the_average() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();

    for (int i = 0; i < 99; ++i) {
        FrameSample s = frame_at(i, i * 0.008, 8.0);
        s.render_ms = 8.0;
        log.record(s);
    }
    FrameSample bad = frame_at(99, 0.8, 40.0);
    bad.sim_ms = 38.0;       // the dip is the SIM, not the renderer
    bad.render_ms = 2.0;
    REQUIRE(log.record(bad));

    const FrameLog::Summary sum = log.summary();
    REQUIRE_NEAR(sum.dip_sim_ms, 38.0, 1e-9);
    REQUIRE_NEAR(sum.dip_render_ms, 2.0, 1e-9);
    // The all-frames mean is dominated by the 99 healthy frames and would
    // point at the renderer — the exact wrong conclusion.
    REQUIRE(sum.render_ms > sum.sim_ms);
    REQUIRE_MSG(sum.dip_sim_ms > sum.dip_render_ms,
                "the dip view must point at the sim", "dip");
    apricot_test::pass("a dip caused by the sim is not averaged away by healthy render frames");
}

void test_the_trailer_names_where_the_time_went() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();
    for (int i = 0; i < 50; ++i) {
        FrameSample s = frame_at(i, i * 0.03, 30.0);
        s.swap_ms = 26.0;   // blocked on the display
        s.render_ms = 2.0;
        s.sim_ms = 1.0;
        log.record(s);
    }
    log.close();

    const std::string& out = log.buffer();
    REQUIRE(out.find("---- where the time goes") != std::string::npos);
    REQUIRE(out.find("swap") != std::string::npos);
    REQUIRE(out.find("unaccounted") != std::string::npos);
    // A session that spends its dips blocked in swap must SAY so: the CPU
    // numbers above it are all small and would otherwise read as "all fine".
    REQUIRE_MSG(out.find("missed a display deadline") != std::string::npos,
                "a swap-dominated dip is called out by name", "swap");
    apricot_test::pass("the trailer names the phase that ate the frame");
}

// Columns are read by NAME by the report tool. If the header and the row drift
// apart, every number downstream is silently attributed to the wrong phase.
void test_phase_columns_sit_where_the_header_says() {
    FrameLog log;
    log.capture_in_memory();
    FrameSample s = frame_at(0, 0.0, 10.0);
    s.sim_ms = 1.0; s.world_ms = 2.0; s.visual_ms = 3.0;
    s.scene_ms = 4.0; s.render_ms = 5.0; s.swap_ms = 6.0; s.gpu_ms = 7.0;
    log.record(s);

    const std::string header(FrameLog::header_line());
    const std::string row = nth_data_line(log.buffer(), 0);
    const char* names[] = {"sim_ms", "world_ms", "visual_ms", "scene_ms",
                           "render_ms", "swap_ms", "gpu_ms"};
    const double want[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
    (void)header;
    for (std::size_t i = 0; i < 7; ++i) {
        REQUIRE_MSG(std::atof(nth_field(row, column_of(names[i])).c_str()) == want[i],
                    "the value must sit under its own name", names[i]);
    }
    apricot_test::pass("each phase value sits under the header name that claims it");
}

// The sim split has the same double-counting hazard one level down: the three
// named sub-phases live INSIDE sim_ms, and "other" is the residual that keeps
// them honest rather than a fourth measurement.
void test_the_sim_split_is_a_partition_of_the_sim() {
    FrameSample s = frame_at(0, 0.0, 30.0);
    s.sim_ms = 20.0;
    s.sim_traffic_ms = 11.0;
    s.sim_police_ms = 4.0;
    s.sim_character_ms = 3.0;
    REQUIRE_NEAR(sim_other_ms(s), 2.0, 1e-9);
    REQUIRE_NEAR(s.sim_traffic_ms + s.sim_police_ms + s.sim_character_ms +
                     sim_other_ms(s),
                 s.sim_ms, 1e-9);

    // The sub-phases must NOT be added into the frame budget again — sim_ms
    // already carries them.
    REQUIRE_NEAR(accounted_ms(s), 20.0, 1e-9);
    apricot_test::pass("the sim sub-phases partition sim_ms instead of adding to it");
}

// The sub-timers are sampled inside the step loop and sim_ms outside it, so
// rounding can put the named parts a hair above the whole. That must read as
// zero, not as a negative residual that looks like a broken instrument.
void test_a_rounding_overshoot_cannot_produce_negative_other() {
    FrameSample s = frame_at(0, 0.0, 10.0);
    s.sim_ms = 5.0;
    s.sim_traffic_ms = 5.0000001;
    REQUIRE(sim_other_ms(s) == 0.0);
    apricot_test::pass("a sub-phase overshooting its parent reports no residual, never a negative one");
}

void test_the_trailer_breaks_the_sim_down() {
    FrameLog log;
    FrameLog::Config cfg;
    cfg.spike_ms = 20.0;
    log.configure(cfg);
    log.capture_in_memory();
    for (int i = 0; i < 40; ++i) {
        FrameSample s = frame_at(i, i * 0.04, 40.0);
        s.sim_ms = 36.0;
        s.sim_traffic_ms = 30.0;   // traffic is the culprit
        s.sim_police_ms = 3.0;
        s.sim_character_ms = 1.0;
        log.record(s);
    }
    log.close();

    const FrameLog::Summary sum = log.summary();
    REQUIRE_NEAR(sum.dip_sim_traffic_ms, 30.0, 1e-9);
    REQUIRE_NEAR(sum.dip_sim_other_ms, 2.0, 1e-9);
    REQUIRE(log.buffer().find("traffic") != std::string::npos);
    REQUIRE(log.buffer().find("character") != std::string::npos);
    apricot_test::pass("the trailer names which part of the sim ate the step");
}

// The police split is a partition of sim_police_ms, one level below the sim
// split, and carries the same double-counting hazard.
void test_the_police_split_is_a_partition_of_the_police_block() {
    FrameSample s = frame_at(0, 0.0, 40.0);
    s.sim_ms = 30.0;
    s.sim_police_ms = 24.0;
    s.police_vis_ms = 20.0;
    s.police_ctx_ms = 3.0;
    REQUIRE_NEAR(police_other_ms(s), 1.0, 1e-9);
    REQUIRE_NEAR(s.police_vis_ms + s.police_ctx_ms + police_other_ms(s),
                 s.sim_police_ms, 1e-9);
    // ...and it must not inflate the frame budget, which sim_ms already holds.
    REQUIRE_NEAR(accounted_ms(s), 30.0, 1e-9);
    apricot_test::pass("the police sub-phases partition the police block, not the frame");
}

void test_police_overshoot_cannot_produce_negative_other() {
    FrameSample s = frame_at(0, 0.0, 10.0);
    s.sim_police_ms = 4.0;
    s.police_vis_ms = 4.0000001;
    REQUIRE(police_other_ms(s) == 0.0);
    apricot_test::pass("a police sub-phase overshooting its parent reports no residual");
}

// The call count is a column of its own because the same work repeated five
// times a step and the same work made five times slower are indistinguishable
// in a millisecond figure.
void test_the_visibility_call_count_is_recorded() {
    FrameLog log;
    log.capture_in_memory();
    FrameSample s = frame_at(0, 0.0, 30.0);
    s.sim_police_ms = 24.0;
    s.police_vis_ms = 24.0;
    s.police_calls = 60;
    s.police_units = 7;
    log.record(s);

    const std::string row = nth_data_line(log.buffer(), 0);
    REQUIRE(nth_field(row, column_of("police_calls")) == "60");
    REQUIRE(nth_field(row, column_of("police_units")) == "7");
    REQUIRE(nth_field(row, column_of("police_vis_ms")) == "24.000");
    REQUIRE_NEAR(log.summary().police_calls, 60.0, 1e-9);
    apricot_test::pass("how many times the visibility query ran is its own column");
}

}  // namespace

int main() {
    test_disabled_records_nothing();
    test_header_and_row_widths_match();
    test_string_fields_are_scrubbed();
    test_absolute_threshold_flags_the_slow_frame();
    test_relative_threshold_catches_a_hitch_in_a_fast_session();
    test_relative_floor_suppresses_noise();
    test_a_spike_does_not_hide_the_spike_behind_it();
    test_percentiles_track_a_known_distribution();
    test_percentiles_never_exceed_the_worst_frame();
    test_enormous_frames_land_in_the_overflow_bucket();
    test_worst_frames_are_ranked_and_bounded();
    test_hotspots_name_the_block_where_frames_go_slow();
    test_thin_cells_are_not_reported_as_hotspots();
    test_negative_coordinates_get_their_own_cells();
    test_a_mark_looks_backwards_for_the_frame_the_player_felt();
    test_a_mark_outside_the_lookback_blames_nothing();
    test_a_mark_writes_its_own_row_and_is_not_counted_as_a_frame();
    test_the_trailer_reports_what_the_session_saw();
    test_closing_an_empty_recorder_says_nothing();
    test_stutter_time_excludes_the_frame_you_would_have_paid_anyway();
    test_a_long_session_stays_bounded();
    test_accounting_does_not_double_count_nested_costs();
    test_unaccounted_time_is_reported_not_hidden();
    test_dip_phases_are_tracked_apart_from_the_average();
    test_the_trailer_names_where_the_time_went();
    test_phase_columns_sit_where_the_header_says();
    test_the_sim_split_is_a_partition_of_the_sim();
    test_a_rounding_overshoot_cannot_produce_negative_other();
    test_the_trailer_breaks_the_sim_down();
    test_the_police_split_is_a_partition_of_the_police_block();
    test_police_overshoot_cannot_produce_negative_other();
    test_the_visibility_call_count_is_recorded();
    return apricot_test::done("frame_log_tests");
}

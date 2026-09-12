#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace apricot {

// Per-frame performance recorder: one CSV row per rendered frame, plus a
// summary trailer, written for every session so a dip can be read back AFTER
// the play rather than reproduced in front of a profiler.
//
// Why this exists at all, when core/log.h is right there: log.h flushes every
// line. That is correct for events — the tail of a crashed session is the only
// part anyone wants — and it is ruinous per frame. An fflush per frame at
// 120 Hz measurably perturbs the very thing being measured, which turns the
// instrument into the bug. So this buffers whole megabytes and writes them a
// few dozen times a session.
//
// Three properties carry the design:
//
//   1. THE DISABLED PATH DOES NO WORK. record() returns immediately unless a
//      sink or a capture buffer is attached, the same bargain AP_TRACE makes.
//
//   2. MEMORY IS BOUNDED BY THE WORLD, NOT BY THE SESSION LENGTH. Percentiles
//      come from a fixed histogram, not a kept array of every frame; the worst
//      frames are a fixed array; the hot-spot map is keyed by world cell, so it
//      saturates at the size of the city and stops growing. A diagnostic that
//      leaks over a long session is a diagnostic that only works on short ones.
//
//   3. NO CLOCK LIVES HERE. Every timestamp arrives as a parameter. App::run()
//      owns the program's only clock (docs/architecture.md) and this must not
//      become the second one — a sim-side file that reads a clock is how replay
//      quietly stops reproducing.
//
// The file it writes is plain CSV with a `#` comment trailer, so both a human
// and `awk` can read it without a parser. tools/perf_report.sh is that reader.

// One rendered frame, as it looked to the player and as it cost the machine.
// Assembled by App, which is the only place that can see both halves.
struct FrameSample {
    // --- when -------------------------------------------------------------
    int frame = 0;
    double t_s = 0.0;   // session uptime at the START of this frame
    double ms = 0.0;    // what the frame cost, wall, present included

    // --- where ------------------------------------------------------------
    // A borrowed string literal (city::district_name()); never owned, never
    // freed. Keeping it as a pointer is what lets core stay below city.
    const char* place = "";
    const char* mode = "";     // "drive", "foot", "boat", "air", "interior"
    const char* weather = "";
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float speed_mph = 0.0f;
    float time_of_day = 0.0f;

    // --- what it cost -----------------------------------------------------
    int sim_steps = 0;
    bool step_clamped = false;

    // The PHASE breakdown: where the frame actually went. These are the
    // columns that turn "it was slow at that junction" into a cause. They are
    // deliberately coarse and deliberately complete — an earlier version of
    // this recorder logged only the four costs below, which on a 31 ms dip
    // frame summed to 1.35 ms and left 96% of the frame unexplained. Detail
    // that does not add up to the whole is not detail, it is a decoy.
    double sim_ms = 0.0;     // the fixed-step loop: traffic, police, peds, car
    // Inside sim_ms. Not exhaustive on purpose — the three biggest suspects,
    // with sim_other carrying the rest, so the split can never silently fail
    // to add up the way a set of hand-picked costs did before it.
    double sim_traffic_ms = 0.0;
    double sim_police_ms = 0.0;
    double sim_character_ms = 0.0;
    // Inside sim_police_ms: the line-of-sight query, the three context
    // handoffs, and how many times the query ran this frame. The call count
    // is here because the same work repeated five times a step and the same
    // work made five times slower look identical in a millisecond column.
    double police_vis_ms = 0.0;
    double police_ctx_ms = 0.0;
    int police_calls = 0;
    int police_units = 0;
    double world_ms = 0.0;   // terrain streaming and chunk meshing
    double visual_ms = 0.0;  // building scene nodes from sim state
    double scene_ms = 0.0;   // Scene::update()
    double render_ms = 0.0;  // render(), CPU side, swap excluded
    double swap_ms = 0.0;    // blocked in the buffer swap — vsync lives here
    double gpu_ms = 0.0;     // GL_TIME_ELAPSED; lags a frame or two, may be 0

    double cull_ms = 0.0;
    double mesh_ms = 0.0;
    double light_ms = 0.0;
    double fill_ms = 0.0;

    int draw_calls = 0;
    int instances = 0;
    int visible_nodes = 0;
    int scene_nodes = 0;
    int batches = 0;
    unsigned int skipped_binds = 0;

    int chunks_built = 0;
    int chunks_evicted = 0;
    std::size_t resident_chunks = 0;
    double terrain_mb = 0.0;
    bool budget_hit = false;

    int cars = 0;
    int parked = 0;
    int npcs = 0;
    int character_draws = 0;
    std::size_t lights = 0;
    int rain_quads = 0;
    int hud_quads = 0;
    int gl_errors = 0;
};

// What the phases add up to. Deliberately NOT a sum of every ms field in the
// sample: world_ms already contains mesh_ms, and render_ms already contains
// cull_ms and light_ms, so adding those again would double-count the very
// frames under investigation and produce an "accounted" figure over 100%.
// gpu_ms is excluded for a different reason — it runs CONCURRENTLY with CPU
// work, so it is not a slice of the frame, it is a second view of it.
// Whatever in the sim step is not one of the three named sub-phases. Clamped
// at zero: the sub-timers are sampled inside the step loop and sim_ms outside
// it, so a pathological frame can round to a hair below zero, and a negative
// "other" reads as a measurement bug rather than as rounding.
inline double sim_other_ms(const FrameSample& s) {
    const double named =
        s.sim_traffic_ms + s.sim_police_ms + s.sim_character_ms;
    return (s.sim_ms > named) ? s.sim_ms - named : 0.0;
}

// Whatever in the police block is neither the visibility query nor the
// context handoffs. Clamped at zero for the same reason sim_other_ms is: the
// inner timers and the outer bracket are sampled at different points, so
// rounding can put the parts a hair over the whole.
inline double police_other_ms(const FrameSample& s) {
    const double named = s.police_vis_ms + s.police_ctx_ms;
    return (s.sim_police_ms > named) ? s.sim_police_ms - named : 0.0;
}

inline double accounted_ms(const FrameSample& s) {
    return s.sim_ms + s.world_ms + s.visual_ms + s.scene_ms + s.render_ms +
           s.swap_ms;
}

class FrameLog {
public:
    struct Config {
        // Absolute: any frame slower than this is a dip, full stop. 20 ms is
        // "under 50 fps for one frame", which is about where a moving camera
        // stops feeling smooth.
        double spike_ms = 20.0;

        // Relative: a frame this many times the recent smoothed frame time is
        // a dip even when it clears the absolute bar, because a 100 Hz session
        // that drops one 15 ms frame still reads as a hitch.
        double spike_ratio = 2.0;

        // ...but never flag anything faster than this. Without a floor, a
        // session running at 300 fps flags every third frame and the report
        // becomes noise.
        double relative_floor_ms = 8.0;

        // Edge of a hot-spot cell, metres. 64 m is roughly a city block: fine
        // enough to name the junction, coarse enough that a drive-through
        // leaves a usable sample count behind.
        float cell_m = 64.0f;

        // Bytes of formatted rows to hold before writing. One write per ~2000
        // frames.
        std::size_t flush_bytes = 256u * 1024u;
    };

    struct Worst {
        FrameSample sample;
    };

    struct Mark {
        double t_s = 0.0;
        float x = 0.0f, z = 0.0f;
        const char* place = "";
        std::string label;
        // The worst frame in the seconds leading up to the mark. A player
        // presses the key AFTER feeling the stutter, so the frame they mean is
        // always already behind them.
        double worst_before_ms = 0.0;
        double worst_before_t_s = 0.0;
    };

    struct Hotspot {
        // The MEAN position of the frames in this cell, not the cell's centre.
        // A 64 m cell centre can sit 45 m from anywhere the player actually
        // drove — in a city that is the wrong side of the block, and the whole
        // value of this report is naming the right junction.
        float x = 0.0f, z = 0.0f;
        const char* place = "";
        std::uint32_t frames = 0;
        std::uint32_t slow = 0;
        double ms_total = 0.0;
        double worst_ms = 0.0;

        double x_sum = 0.0;
        double z_sum = 0.0;
    };

    struct Summary {
        int frames = 0;
        double seconds = 0.0;
        double mean_ms = 0.0;
        double worst_ms = 0.0;
        double p50_ms = 0.0;
        double p90_ms = 0.0;
        double p99_ms = 0.0;
        double p999_ms = 0.0;
        int spikes = 0;
        double spike_ms_total = 0.0;

        // Mean phase cost across every frame, and across dip frames only. The
        // second is the one worth reading: it says where a BAD frame goes,
        // which is rarely the same shape as where an average one goes.
        double sim_ms = 0.0, world_ms = 0.0, visual_ms = 0.0, scene_ms = 0.0;
        double sim_traffic_ms = 0.0, sim_police_ms = 0.0, sim_character_ms = 0.0;
        double sim_other_ms = 0.0;
        double police_vis_ms = 0.0, police_ctx_ms = 0.0, police_other_ms = 0.0;
        double police_calls = 0.0;
        double dip_police_vis_ms = 0.0, dip_police_ctx_ms = 0.0;
        double dip_police_other_ms = 0.0, dip_police_calls = 0.0;
        double dip_sim_traffic_ms = 0.0, dip_sim_police_ms = 0.0;
        double dip_sim_character_ms = 0.0, dip_sim_other_ms = 0.0;
        double render_ms = 0.0, swap_ms = 0.0, gpu_ms = 0.0, unaccounted_ms = 0.0;
        double dip_sim_ms = 0.0, dip_world_ms = 0.0, dip_visual_ms = 0.0;
        double dip_scene_ms = 0.0, dip_render_ms = 0.0, dip_swap_ms = 0.0;
        double dip_gpu_ms = 0.0, dip_unaccounted_ms = 0.0;
        // Wall time the session spent ABOVE the median, on spike frames only:
        // the honest size of the stutter budget, rather than the raw total,
        // which counts the frame you would have paid for anyway.
        double stutter_ms = 0.0;
    };

    FrameLog() = default;
    FrameLog(const FrameLog&) = delete;
    FrameLog& operator=(const FrameLog&) = delete;
    ~FrameLog() { close(); }

    void configure(const Config& c) { config_ = c; }
    const Config& config() const { return config_; }

    // Attach a file. Writes the header immediately so a session killed with
    // SIGKILL still leaves a readable, if untrailered, file.
    bool open(const char* path) {
        close();
        file_ = std::fopen(path, "w");
        if (!file_) return false;
        path_ = path;
        pending_.reserve(config_.flush_bytes + 1024u);
        pending_ += header_line();
        return true;
    }

    // Record rows without a file, for tests. Same code path as a real session
    // — a recorder tested through a different path is a recorder tested twice
    // and trusted once.
    void capture_in_memory() {
        capture_ = true;
        if (pending_.empty()) pending_ += header_line();
    }

    bool enabled() const { return file_ != nullptr || capture_; }
    const std::string& buffer() const { return pending_; }
    const std::string& path() const { return path_; }

    // Returns true when this frame was a dip. Pass frames in order; the first
    // frame of a session should be dropped by the caller, not here, because
    // only the caller knows which frame paid for shader compilation.
    bool record(const FrameSample& s) {
        if (!enabled()) return false;

        // Judged against the smoothed value BEFORE this frame folds into it,
        // so a spike never gets to raise the bar it is being measured against.
        const bool spike = is_spike(s.ms);

        ++frames_;
        ms_total_ += s.ms;
        if (s.ms > worst_ms_) worst_ms_ = s.ms;
        if (frames_ == 1) first_t_s_ = s.t_s;
        last_t_s_ = s.t_s;
        histogram_[bucket_of(s.ms)]++;

        if (have_ema_) {
            ema_ms_ += (s.ms - ema_ms_) * kEmaWeight;
        } else {
            ema_ms_ = s.ms;
            have_ema_ = true;
        }

        recent_[recent_head_] = Recent{s.t_s, s.ms};
        recent_head_ = (recent_head_ + 1u) % kRecentFrames;
        if (recent_count_ < kRecentFrames) ++recent_count_;

        add_phases(phase_, s);
        if (spike) {
            ++spikes_;
            spike_ms_total_ += s.ms;
            add_phases(dip_phase_, s);
            remember_worst(s);
        }
        tally_cell(s, spike);
        append_row(s, spike, "");
        return spike;
    }

    // A player-pressed "I felt that" marker. Written as its own row so the
    // timeline carries it, and kept in the trailer so the report can lead with
    // the moments a human actually noticed.
    void mark(const char* label, const FrameSample& s) {
        if (!enabled()) return;
        Mark m;
        m.t_s = s.t_s;
        m.x = s.x;
        m.z = s.z;
        m.place = s.place;
        m.label = label ? label : "";
        worst_recent(s.t_s, kMarkLookbackSeconds, m.worst_before_ms,
                     m.worst_before_t_s);
        if (marks_.size() < kMaxMarks) marks_.push_back(m);
        append_row(s, false, m.label.c_str());
    }

    Summary summary() const {
        Summary out;
        out.frames = frames_;
        out.seconds = (frames_ > 0) ? (last_t_s_ - first_t_s_) : 0.0;
        out.mean_ms = (frames_ > 0) ? ms_total_ / frames_ : 0.0;
        out.worst_ms = worst_ms_;
        out.p50_ms = percentile(0.50);
        out.p90_ms = percentile(0.90);
        out.p99_ms = percentile(0.99);
        out.p999_ms = percentile(0.999);
        out.spikes = spikes_;
        out.spike_ms_total = spike_ms_total_;
        out.stutter_ms =
            std::max(0.0, spike_ms_total_ - out.p50_ms * spikes_);

        const double n = (frames_ > 0) ? frames_ : 1.0;
        const double d = (spikes_ > 0) ? spikes_ : 1.0;
        out.sim_ms = phase_.sim / n;
        out.world_ms = phase_.world / n;
        out.visual_ms = phase_.visual / n;
        out.scene_ms = phase_.scene / n;
        out.render_ms = phase_.render / n;
        out.swap_ms = phase_.swap / n;
        out.gpu_ms = phase_.gpu / n;
        out.unaccounted_ms = phase_.unaccounted / n;
        out.sim_traffic_ms = phase_.traffic / n;
        out.sim_police_ms = phase_.police / n;
        out.sim_character_ms = phase_.character / n;
        out.sim_other_ms = phase_.sim_other / n;
        out.dip_sim_traffic_ms = phase_dip(dip_phase_.traffic, d);
        out.dip_sim_police_ms = phase_dip(dip_phase_.police, d);
        out.dip_sim_character_ms = phase_dip(dip_phase_.character, d);
        out.dip_sim_other_ms = phase_dip(dip_phase_.sim_other, d);
        out.police_vis_ms = phase_.police_vis / n;
        out.police_ctx_ms = phase_.police_ctx / n;
        out.police_other_ms = phase_.police_other / n;
        out.police_calls = phase_.police_calls / n;
        out.dip_police_vis_ms = phase_dip(dip_phase_.police_vis, d);
        out.dip_police_ctx_ms = phase_dip(dip_phase_.police_ctx, d);
        out.dip_police_other_ms = phase_dip(dip_phase_.police_other, d);
        out.dip_police_calls = phase_dip(dip_phase_.police_calls, d);
        out.dip_sim_ms = phase_dip(dip_phase_.sim, d);
        out.dip_world_ms = phase_dip(dip_phase_.world, d);
        out.dip_visual_ms = phase_dip(dip_phase_.visual, d);
        out.dip_scene_ms = phase_dip(dip_phase_.scene, d);
        out.dip_render_ms = phase_dip(dip_phase_.render, d);
        out.dip_swap_ms = phase_dip(dip_phase_.swap, d);
        out.dip_gpu_ms = phase_dip(dip_phase_.gpu, d);
        out.dip_unaccounted_ms = phase_dip(dip_phase_.unaccounted, d);
        return out;
    }

    // The worst frames of the session, slowest first.
    std::vector<FrameSample> worst_frames() const {
        std::vector<FrameSample> out(worst_, worst_ + worst_count_);
        std::sort(out.begin(), out.end(),
                  [](const FrameSample& a, const FrameSample& b) {
                      return a.ms > b.ms;
                  });
        return out;
    }

    // Where the dips happened, worst neighbourhood first. Cells with fewer
    // than min_frames samples are dropped: one slow frame as you clip a corner
    // is a 100% slow rate and means nothing.
    std::vector<Hotspot> hotspots(std::uint32_t min_frames = 30u,
                                  std::size_t limit = 8u) const {
        std::vector<Hotspot> out;
        out.reserve(cells_.size());
        for (const auto& kv : cells_) {
            if (kv.second.frames < min_frames || kv.second.slow == 0u) continue;
            out.push_back(kv.second);
            Hotspot& h = out.back();
            h.x = static_cast<float>(h.x_sum / h.frames);
            h.z = static_cast<float>(h.z_sum / h.frames);
        }
        std::sort(out.begin(), out.end(), [](const Hotspot& a, const Hotspot& b) {
            const double ra = static_cast<double>(a.slow) / a.frames;
            const double rb = static_cast<double>(b.slow) / b.frames;
            if (ra != rb) return ra > rb;
            return a.worst_ms > b.worst_ms;
        });
        if (out.size() > limit) out.resize(limit);
        return out;
    }

    const std::vector<Mark>& marks() const { return marks_; }

    // Write the `#` trailer and close. Safe to call twice; safe to call when
    // nothing was ever opened.
    void close() {
        if (!enabled()) return;
        if (frames_ > 0) append_trailer();
        flush(true);
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
        capture_ = false;
    }

    static const char* header_line() {
        // Read this by NAME, never by position — tools/perf_report.sh does,
        // and that is what lets a column be inserted here without silently
        // reinterpreting every older log on disk.
        return "frame,t_s,ms,fps,spike,mark,x,y,z,place,mode,speed_mph,"
               "sim_steps,clamped,"
               "sim_ms,sim_traffic_ms,sim_police_ms,sim_character_ms,"
               "sim_other_ms,"
               "police_vis_ms,police_ctx_ms,police_other_ms,"
               "police_calls,police_units,"
               "world_ms,visual_ms,scene_ms,render_ms,swap_ms,gpu_ms,"
               "accounted_ms,unaccounted_ms,"
               "cull_ms,mesh_ms,light_ms,fill_ms,"
               "draw_calls,instances,visible_nodes,scene_nodes,batches,"
               "skipped_binds,chunks_built,chunks_evicted,resident_chunks,"
               "terrain_mb,budget_hit,cars,parked,npcs,char_draws,lights,"
               "rain_quads,hud_quads,weather,time_of_day,gl_errors\n";
    }

private:
    static constexpr double kEmaWeight = 0.05;
    static constexpr double kMarkLookbackSeconds = 3.0;
    static constexpr std::size_t kMaxWorst = 16;
    static constexpr std::size_t kMaxMarks = 64;
    static constexpr std::size_t kRecentFrames = 512;

    // 0.25 ms buckets to 128 ms, then one overflow bucket. Fine enough to
    // separate a 16.7 ms vsync frame from a 20 ms one, which is the
    // distinction the whole report turns on.
    static constexpr std::size_t kBuckets = 513;
    static constexpr double kBucketMs = 0.25;

    struct Recent {
        double t_s = 0.0;
        double ms = 0.0;
    };

    struct Phases {
        double sim = 0.0, world = 0.0, visual = 0.0, scene = 0.0;
        double render = 0.0, swap = 0.0, gpu = 0.0, unaccounted = 0.0;
        double traffic = 0.0, police = 0.0, character = 0.0, sim_other = 0.0;
        double police_vis = 0.0, police_ctx = 0.0, police_other = 0.0;
        double police_calls = 0.0;
    };

    static void add_phases(Phases& p, const FrameSample& s) {
        p.sim += s.sim_ms;
        p.world += s.world_ms;
        p.visual += s.visual_ms;
        p.scene += s.scene_ms;
        p.render += s.render_ms;
        p.swap += s.swap_ms;
        p.gpu += s.gpu_ms;
        p.unaccounted += s.ms - accounted_ms(s);
        p.traffic += s.sim_traffic_ms;
        p.police += s.sim_police_ms;
        p.character += s.sim_character_ms;
        p.sim_other += sim_other_ms(s);
        p.police_vis += s.police_vis_ms;
        p.police_ctx += s.police_ctx_ms;
        p.police_other += police_other_ms(s);
        p.police_calls += s.police_calls;
    }

    static double phase_dip(double total, double count) { return total / count; }

    static std::size_t bucket_of(double ms) {
        if (!(ms > 0.0)) return 0;
        const double b = ms / kBucketMs;
        if (b >= static_cast<double>(kBuckets - 1)) return kBuckets - 1;
        return static_cast<std::size_t>(b);
    }

    bool is_spike(double ms) const {
        if (ms > config_.spike_ms) return true;
        if (!have_ema_) return false;
        return ms > config_.relative_floor_ms &&
               ms > config_.spike_ratio * ema_ms_;
    }

    double percentile(double q) const {
        if (frames_ <= 0) return 0.0;
        const double want = q * static_cast<double>(frames_);
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            seen += histogram_[i];
            if (static_cast<double>(seen) >= want) {
                if (i == kBuckets - 1) return worst_ms_;
                // The bucket's upper edge: rounding a percentile DOWN reads as
                // "we are fine" on exactly the frames that are not.
                return std::min(worst_ms_, static_cast<double>(i + 1) * kBucketMs);
            }
        }
        return worst_ms_;
    }

    void worst_recent(double now_s, double window_s, double& out_ms,
                      double& out_t) const {
        out_ms = 0.0;
        out_t = now_s;
        for (std::size_t i = 0; i < recent_count_; ++i) {
            const Recent& r = recent_[i];
            if (r.t_s < now_s - window_s || r.t_s > now_s) continue;
            if (r.ms > out_ms) {
                out_ms = r.ms;
                out_t = r.t_s;
            }
        }
    }

    void remember_worst(const FrameSample& s) {
        if (worst_count_ < kMaxWorst) {
            worst_[worst_count_++] = s;
            return;
        }
        std::size_t weakest = 0;
        for (std::size_t i = 1; i < kMaxWorst; ++i) {
            if (worst_[i].ms < worst_[weakest].ms) weakest = i;
        }
        if (s.ms > worst_[weakest].ms) worst_[weakest] = s;
    }

    void tally_cell(const FrameSample& s, bool spike) {
        const float edge = (config_.cell_m > 0.0f) ? config_.cell_m : 64.0f;
        const auto cx = static_cast<std::int32_t>(std::floor(s.x / edge));
        const auto cz = static_cast<std::int32_t>(std::floor(s.z / edge));
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz));
        Hotspot& cell = cells_[key];
        if (cell.frames == 0u) cell.place = s.place;
        cell.x_sum += static_cast<double>(s.x);
        cell.z_sum += static_cast<double>(s.z);
        ++cell.frames;
        cell.ms_total += s.ms;
        if (s.ms > cell.worst_ms) cell.worst_ms = s.ms;
        if (spike) ++cell.slow;
    }

    // CSV has no escape we want to think about at 120 Hz, so the few string
    // fields are scrubbed instead of quoted. They are district names, mode
    // words and marker labels; none of them has any business holding a comma.
    static void scrub(const char* in, char* out, std::size_t n) {
        if (!in) in = "";
        std::size_t i = 0;
        for (; in[i] && i + 1 < n; ++i) {
            const char c = in[i];
            out[i] = (c == ',' || c == '"' || c == '\n' || c == '\r') ? '_' : c;
        }
        out[i] = '\0';
    }

    void append_row(const FrameSample& s, bool spike, const char* mark_label) {
        char place[48], mode[24], weather[24], label[48];
        scrub(s.place, place, sizeof(place));
        scrub(s.mode, mode, sizeof(mode));
        scrub(s.weather, weather, sizeof(weather));
        scrub(mark_label, label, sizeof(label));

        char row[896];
        const int n = std::snprintf(
            row, sizeof(row),
            "%d,%.3f,%.3f,%.1f,%d,%s,%.1f,%.1f,%.1f,%s,%s,%.1f,"
            "%d,%d,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%d,%d,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.1f,"
            "%d,%d,%d,%d,%d,"
            "%u,%d,%d,%zu,"
            "%.2f,%d,%d,%d,%d,%d,%zu,"
            "%d,%d,%s,%.3f,%d\n",
            s.frame, s.t_s, s.ms, (s.ms > 0.0) ? 1000.0 / s.ms : 0.0,
            spike ? 1 : 0, label, static_cast<double>(s.x),
            static_cast<double>(s.y), static_cast<double>(s.z), place, mode,
            static_cast<double>(s.speed_mph), s.sim_steps,
            s.step_clamped ? 1 : 0,
            s.sim_ms, s.sim_traffic_ms, s.sim_police_ms, s.sim_character_ms,
            sim_other_ms(s),
            s.police_vis_ms, s.police_ctx_ms, police_other_ms(s),
            s.police_calls, s.police_units,
            s.world_ms, s.visual_ms, s.scene_ms, s.render_ms,
            s.swap_ms, s.gpu_ms, accounted_ms(s), s.ms - accounted_ms(s),
            s.cull_ms, s.mesh_ms, s.light_ms, s.fill_ms,
            s.draw_calls, s.instances, s.visible_nodes, s.scene_nodes,
            s.batches, s.skipped_binds, s.chunks_built, s.chunks_evicted,
            s.resident_chunks, s.terrain_mb, s.budget_hit ? 1 : 0, s.cars,
            s.parked, s.npcs, s.character_draws, s.lights, s.rain_quads,
            s.hud_quads, weather, static_cast<double>(s.time_of_day),
            s.gl_errors);
        // snprintf returns what it WOULD have written. Appending that count
        // straight from a truncated buffer reads past the end of it.
        if (n > 0) {
            pending_.append(row, std::min(static_cast<std::size_t>(n),
                                          sizeof(row) - 1));
        }
        flush(false);
    }

    void append_trailer() {
        const Summary sum = summary();
        char line[512];
        const auto say = [&](const char* fmt, auto... args) {
            const int n = std::snprintf(line, sizeof(line), fmt, args...);
            if (n > 0) {
                pending_.append(line, std::min(static_cast<std::size_t>(n),
                                               sizeof(line) - 1));
            }
        };

        pending_ += "#\n# ---- session summary ----\n";
        say("# frames %d over %.1f s; mean %.2f ms (%.0f fps)\n", sum.frames,
            sum.seconds, sum.mean_ms,
            sum.mean_ms > 0.0 ? 1000.0 / sum.mean_ms : 0.0);
        say("# p50 %.2f ms (%.0f fps)  p90 %.2f  p99 %.2f  p99.9 %.2f  worst %.2f\n",
            sum.p50_ms, sum.p50_ms > 0.0 ? 1000.0 / sum.p50_ms : 0.0,
            sum.p90_ms, sum.p99_ms, sum.p999_ms, sum.worst_ms);
        say("# dips over %.1f ms (or %.1fx the smoothed frame): %d of %d frames "
            "(%.2f%%), %.0f ms of stutter\n",
            config_.spike_ms, config_.spike_ratio, sum.spikes, sum.frames,
            sum.frames > 0 ? 100.0 * sum.spikes / sum.frames : 0.0,
            sum.stutter_ms);

        // Where the time went. Printed for every frame AND for dip frames
        // alone, because the two answers usually differ and the second is the
        // one that names the bug.
        pending_ += "#\n# ---- where the time goes (mean ms per frame) ----\n";
        say("# %-12s %9s %9s\n", "phase", "all", "dips");
        const auto phase_row = [&](const char* name, double all, double dip) {
            say("# %-12s %9.2f %9.2f\n", name, all, dip);
        };
        phase_row("sim", sum.sim_ms, sum.dip_sim_ms);
        phase_row("  traffic", sum.sim_traffic_ms, sum.dip_sim_traffic_ms);
        phase_row("  police", sum.sim_police_ms, sum.dip_sim_police_ms);
        phase_row("    visible", sum.police_vis_ms, sum.dip_police_vis_ms);
        phase_row("    context", sum.police_ctx_ms, sum.dip_police_ctx_ms);
        phase_row("    other", sum.police_other_ms, sum.dip_police_other_ms);
        say("# %-12s %9.1f %9.1f  (visible_police() calls per frame)\n",
            "    calls", sum.police_calls, sum.dip_police_calls);
        phase_row("  character", sum.sim_character_ms, sum.dip_sim_character_ms);
        phase_row("  other", sum.sim_other_ms, sum.dip_sim_other_ms);
        phase_row("world", sum.world_ms, sum.dip_world_ms);
        phase_row("visual", sum.visual_ms, sum.dip_visual_ms);
        phase_row("scene", sum.scene_ms, sum.dip_scene_ms);
        phase_row("render", sum.render_ms, sum.dip_render_ms);
        phase_row("swap", sum.swap_ms, sum.dip_swap_ms);
        phase_row("unaccounted", sum.unaccounted_ms, sum.dip_unaccounted_ms);
        say("# %-12s %9.2f %9.2f  (concurrent with the CPU, not a slice of it)\n",
            "gpu", sum.gpu_ms, sum.dip_gpu_ms);
        say("# %-12s %9.2f %9.2f\n", "TOTAL", sum.mean_ms,
            sum.spikes > 0 ? sum.spike_ms_total / sum.spikes : 0.0);
        // Time in swap is time blocked on the display, not work. When it
        // dominates, the frame missed a vsync deadline and everything above it
        // is already fast enough — look at gpu, not at the CPU phases.
        if (sum.dip_swap_ms > 0.5 * (sum.spikes > 0
                                         ? sum.spike_ms_total / sum.spikes
                                         : 1.0)) {
            pending_ += "# NOTE: most of a dip is spent blocked in swap — "
                        "these frames missed a display deadline.\n";
        }

        const std::vector<Mark> marks = marks_;
        if (!marks.empty()) {
            pending_ += "#\n# ---- moments the player marked ----\n";
            for (const Mark& m : marks) {
                say("# t=%.1f s  (%.0f, %.0f) %s  worst frame in the previous "
                    "%.0f s: %.1f ms at t=%.1f  %s\n",
                    m.t_s, static_cast<double>(m.x), static_cast<double>(m.z),
                    m.place, kMarkLookbackSeconds, m.worst_before_ms,
                    m.worst_before_t_s, m.label.c_str());
            }
        }

        const std::vector<FrameSample> worst = worst_frames();
        if (!worst.empty()) {
            pending_ += "#\n# ---- worst frames ----\n";
            for (const FrameSample& w : worst) {
                say("# %8.2f ms  t=%8.1f  frame %-7d (%7.0f,%7.0f) %-18s %-9s "
                    "cull %.2f mesh %.2f light %.2f fill %.1f  draws %d  "
                    "built %d  cars %d npcs %d lights %zu\n",
                    w.ms, w.t_s, w.frame, static_cast<double>(w.x),
                    static_cast<double>(w.z), w.place, w.mode, w.cull_ms,
                    w.mesh_ms, w.light_ms, w.fill_ms, w.draw_calls,
                    w.chunks_built, w.cars, w.npcs, w.lights);
            }
        }

        const std::vector<Hotspot> hot = hotspots();
        if (!hot.empty()) {
            pending_ += "#\n# ---- where the dips happen ----\n";
            for (const Hotspot& h : hot) {
                say("# (%7.0f,%7.0f) %-18s %5u frames, %4u slow (%5.1f%%), "
                    "mean %.2f ms, worst %.2f ms\n",
                    static_cast<double>(h.x), static_cast<double>(h.z), h.place,
                    h.frames, h.slow,
                    100.0 * static_cast<double>(h.slow) / h.frames,
                    h.ms_total / h.frames, h.worst_ms);
            }
        }
    }

    void flush(bool force) {
        if (!file_) return;
        if (!force && pending_.size() < config_.flush_bytes) return;
        if (!pending_.empty()) {
            std::fwrite(pending_.data(), 1, pending_.size(), file_);
            pending_.clear();
        }
        if (force) std::fflush(file_);
    }

    Config config_;
    std::FILE* file_ = nullptr;
    bool capture_ = false;
    std::string path_;
    std::string pending_;

    int frames_ = 0;
    double ms_total_ = 0.0;
    double worst_ms_ = 0.0;
    double first_t_s_ = 0.0;
    double last_t_s_ = 0.0;
    double ema_ms_ = 0.0;
    bool have_ema_ = false;
    int spikes_ = 0;
    double spike_ms_total_ = 0.0;
    Phases phase_;
    Phases dip_phase_;

    std::uint64_t histogram_[kBuckets] = {};
    FrameSample worst_[kMaxWorst];
    std::size_t worst_count_ = 0;
    Recent recent_[kRecentFrames];
    std::size_t recent_head_ = 0;
    std::size_t recent_count_ = 0;
    std::vector<Mark> marks_;
    std::unordered_map<std::uint64_t, Hotspot> cells_;
};

}  // namespace apricot

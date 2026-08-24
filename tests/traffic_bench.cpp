// How many agents fit in a 120 Hz step, and where the time goes.
//
// A measurement, printed, with a small number of assertions around it so that a
// run which measured nothing cannot pass green. The assertions are floors on
// what happened, never ceilings on how long it took: a timing assertion turns
// somebody else's slow CI box into a red build, and then the first thing anyone
// does is delete the test.
//
// Timing lives HERE and not inside src/traffic/, because nothing below
// src/app/ may read a clock. That is also why Crowd's step is four public
// phases instead of one call — the phase boundaries are where the clock goes.
//
// Default run is the short ladder, which is what ctest executes. Pass --full
// for the long sweep, the sub-rate ladder and the per-slot-speed comparison.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/fixed_step.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

#include "test_assert.h"
#include "traffic_harness.h"

using namespace apricot;
using apricot_test::pass;
using traffic_harness::Network;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// The 120 Hz step budget, in milliseconds. Everything below is quoted against
// it. It is the WHOLE frame, so a traffic system that eats it has left nothing
// for physics, terrain streaming or drawing.
constexpr double kStepBudgetMs = 1000.0 / 120.0;

struct Profile {
    double refresh = 0.0;
    double buckets = 0.0;
    double vehicles = 0.0;
    double peds = 0.0;
    double publish = 0.0;
    double scene_update = 0.0;

    double total() const {
        return refresh + buckets + vehicles + peds + publish + scene_update;
    }
    void operator+=(const Profile& o) {
        refresh += o.refresh;
        buckets += o.buckets;
        vehicles += o.vehicles;
        peds += o.peds;
        publish += o.publish;
        scene_update += o.scene_update;
    }
    void scale(double s) {
        refresh *= s;
        buckets *= s;
        vehicles *= s;
        peds *= s;
        publish *= s;
        scene_update *= s;
    }
};

struct Run {
    Profile per_step;
    std::size_t vehicles = 0;
    std::size_t peds = 0;
    std::size_t agents = 0;
    std::size_t veh_analytic = 0;
    std::size_t ped_analytic = 0;
    std::size_t stepped = 0;
    std::size_t retired = 0;
    std::size_t lanes_scanned = 0;
    double worst_step_ms = 0.0;
};

Run measure(const LaneGraph& lanes, const CrowdTuning& tune,
            const AmbientTuning& amb, int64_t steps, int64_t warmup) {
    Crowd crowd;
    crowd.build(lanes, traffic_harness::kMapSeed, amb, tune);
    Scene scene;

    Run r;
    Profile acc;
    int64_t counted = 0;

    for (int64_t s = 0; s < warmup + steps; ++s) {
        const bool timed = s >= warmup;
        Profile p;
        Clock::time_point t0;

        if (s % tune.refresh_every_steps == 0) {
            t0 = Clock::now();
            crowd.refresh(s, traffic_harness::player_at(s));
            p.refresh = ms_since(t0);
        }
        t0 = Clock::now();
        crowd.rebuild_buckets();
        p.buckets = ms_since(t0);

        t0 = Clock::now();
        crowd.step_vehicles(s);
        p.vehicles = ms_since(t0);

        t0 = Clock::now();
        crowd.step_peds(s);
        p.peds = ms_since(t0);

        t0 = Clock::now();
        crowd.publish(scene);
        p.publish = ms_since(t0);

        t0 = Clock::now();
        scene.update();
        p.scene_update = ms_since(t0);

        if (timed) {
            acc += p;
            r.worst_step_ms = std::max(r.worst_step_ms, p.total());
            ++counted;
            const CrowdStats& st = crowd.stats();
            r.veh_analytic += st.vehicles_analytic;
            r.ped_analytic += st.peds_analytic;
            r.stepped += st.vehicles_stepped + st.peds_stepped;
        }
    }

    const double inv = counted > 0 ? 1.0 / static_cast<double>(counted) : 0.0;
    acc.scale(inv);
    r.per_step = acc;
    r.vehicles = crowd.vehicles().size();
    r.peds = crowd.peds().size();
    r.agents = r.vehicles + r.peds;
    r.retired = crowd.stats().retired;
    r.lanes_scanned = crowd.stats().lanes_scanned;
    if (counted > 0) {
        r.veh_analytic = static_cast<std::size_t>(
            static_cast<double>(r.veh_analytic) * inv);
        r.ped_analytic = static_cast<std::size_t>(
            static_cast<double>(r.ped_analytic) * inv);
        r.stepped =
            static_cast<std::size_t>(static_cast<double>(r.stepped) * inv);
    }
    return r;
}

void print_header() {
    std::printf(
        "\n   agents   cars   peds |  refresh  bucket   cars    peds  publish"
        "   scene |  total  worst  | %% of 8.33 ms\n");
    std::printf(
        "   ------------------------|--------------------------------------"
        "---------|----------------|-------------\n");
}

void print_row(const Run& r) {
    std::printf("  %7zu %6zu %6zu | %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f |"
                " %6.3f %6.3f | %6.1f%%\n",
                r.agents, r.vehicles, r.peds, r.per_step.refresh,
                r.per_step.buckets, r.per_step.vehicles, r.per_step.peds,
                r.per_step.publish, r.per_step.scene_update, r.per_step.total(),
                r.worst_step_ms, 100.0 * r.per_step.total() / kStepBudgetMs);
}

CrowdTuning tuning_for_radius(float veh_m, float ped_m) {
    CrowdTuning t;
    t.vehicle_activate_m = veh_m;
    t.vehicle_retire_m = veh_m * 1.45f;
    t.ped_activate_m = ped_m;
    t.ped_retire_m = ped_m * 1.45f;
    t.refresh_every_steps = 8;
    return t;
}

// ---------------------------------------------------------------------------
//  the ladder
// ---------------------------------------------------------------------------

void bench_scale_ladder(const LaneGraph& lanes, bool full) {
    std::printf(
        "\n== SCALE: every agent stepped every step (sub_rate = 1) ==\n"
        "   Radius drives the count. Times are per SIM STEP, mean over the\n"
        "   measured window; the 120 Hz budget is %.3f ms for EVERYTHING.\n",
        kStepBudgetMs);
    print_header();

    struct Rung {
        float veh_m;
        float ped_m;
    };
    const std::vector<Rung> shortl = {{110.0f, 55.0f},
                                      {220.0f, 110.0f},
                                      {450.0f, 200.0f},
                                      {900.0f, 340.0f}};
    const std::vector<Rung> longl = {{110.0f, 55.0f},   {220.0f, 110.0f},
                                     {320.0f, 150.0f},  {450.0f, 200.0f},
                                     {650.0f, 260.0f},  {900.0f, 340.0f},
                                     {1300.0f, 460.0f}, {1900.0f, 620.0f},
                                     {2600.0f, 900.0f}};
    const std::vector<Rung>& rungs = full ? longl : shortl;

    Run biggest;
    Run first_over;
    bool found_over = false;
    for (const Rung& g : rungs) {
        const Run r = measure(lanes, tuning_for_radius(g.veh_m, g.ped_m),
                              AmbientTuning{}, full ? 900 : 360, 240);
        print_row(r);
        biggest = r;
        if (!found_over && r.per_step.total() > kStepBudgetMs) {
            found_over = true;
            first_over = r;
        }
    }

    // ANTI-VACUITY. "Everything fit in the budget" is trivially true of a
    // benchmark that never instantiated anything.
    REQUIRE_MSG(biggest.agents > 2000, "the ladder never reached a real scale",
                "vacuity");
    REQUIRE_MSG(biggest.stepped > 0, "no agent was ever stepped", "vacuity");
    if (found_over)
        std::printf("\n   first rung over budget: %zu agents at %.3f ms/step\n",
                    first_over.agents, first_over.per_step.total());
    else
        std::printf("\n   no rung on this ladder exceeded the budget "
                    "(largest: %zu agents at %.3f ms/step)\n",
                    biggest.agents, biggest.per_step.total());
    pass("scale ladder ran and every rung reported a cost");
}

// ---------------------------------------------------------------------------
//  what sub-rate scheduling buys
// ---------------------------------------------------------------------------

void bench_sub_rate(const LaneGraph& lanes) {
    std::printf("\n== SUB-RATE: agent i runs when step %% k == keyed_phase(i) ==\n"
                "   Same population every row. k is the divisor, so k = 4 steps\n"
                "   a quarter of the crowd per step at four times the dt.\n");
    print_header();

    Run base;
    for (int k : {1, 2, 4, 8}) {
        CrowdTuning t = tuning_for_radius(900.0f, 340.0f);
        t.vehicle_sub_rate = k;
        t.ped_sub_rate = k;
        const Run r = measure(lanes, t, AmbientTuning{}, 600, 240);
        std::printf("  k=%d", k);
        print_row(r);
        if (k == 1) base = r;
        else
            REQUIRE_MSG(r.stepped < base.stepped,
                        "sub-rate scheduling did not reduce the stepped count",
                        "sub-rate");
    }
    pass("sub-rate scheduling reduces the per-step agent count as advertised");
}

// ---------------------------------------------------------------------------
//  what the analytic mode is actually worth
// ---------------------------------------------------------------------------

void bench_analytic_share(const LaneGraph& lanes) {
    std::printf("\n== ANALYTIC SHARE: how much of the active set is still on\n"
                "   its closed form, and therefore still radius-invariant ==\n");
    std::printf("\n   mode              cars  analytic   peds  analytic |"
                " ms/step\n");
    std::printf("   ------------------------------------------------------"
                "---------\n");

    for (bool per_slot : {false, true}) {
        AmbientTuning amb;
        amb.per_slot_speed = per_slot;
        const Run r = measure(lanes, tuning_for_radius(450.0f, 200.0f), amb, 900,
                              240);
        std::printf("   %-14s %7zu %8.1f%% %6zu %8.1f%% | %6.3f\n",
                    per_slot ? "per-slot speed" : "one speed/lane", r.vehicles,
                    r.vehicles > 0 ? 100.0 * static_cast<double>(r.veh_analytic) /
                                         static_cast<double>(r.vehicles)
                                   : 0.0,
                    r.peds,
                    r.peds > 0 ? 100.0 * static_cast<double>(r.ped_analytic) /
                                     static_cast<double>(r.peds)
                               : 0.0,
                    r.per_step.total());
        REQUIRE_MSG(r.vehicles > 100, "no cars in the analytic comparison",
                    "vacuity");
    }
    pass("the analytic share is measured under both schedule modes");
}

// ---------------------------------------------------------------------------
//  the cost of the closed form on its own
// ---------------------------------------------------------------------------

void bench_phantom_cost(const LaneGraph& lanes) {
    // What a phantom costs when nobody instantiates it: the number that decides
    // whether "define the whole city" is affordable at all.
    const AmbientTuning amb;
    std::vector<LaneSchedule> sched(lanes.lane_count());
    std::size_t total_slots = 0;
    for (std::size_t i = 0; i < lanes.lane_count(); ++i) {
        sched[i] = vehicle_schedule(traffic_harness::kMapSeed,
                                    lanes.lane(static_cast<LaneRef>(i)), amb);
        total_slots += sched[i].slots;
    }

    const auto t0 = Clock::now();
    double sink = 0.0;
    const int passes = 8;
    for (int p = 0; p < passes; ++p) {
        for (std::size_t i = 0; i < lanes.lane_count(); ++i) {
            const Lane& l = lanes.lane(static_cast<LaneRef>(i));
            for (uint32_t k = 0; k < sched[i].slots; ++k)
                sink += static_cast<double>(
                    phantom_vehicle(traffic_harness::kMapSeed, l, sched[i], k,
                                    1000 + p, amb)
                        .dist_along_m);
        }
    }
    const double ms = ms_since(t0);
    const double evals = static_cast<double>(total_slots) * passes;
    REQUIRE(sink > 0.0);
    REQUIRE(total_slots > 1000);

    std::printf("\n== PHANTOM COST ==\n"
                "   %zu lanes carry %zu vehicle phantoms (whole district, "
                "defined not simulated)\n"
                "   evaluating every one of them: %.3f ms  (%.1f ns/phantom)\n"
                "   %.3f ms is %.1f%% of a 120 Hz step -- and nothing asks for "
                "all of them\n",
                lanes.lane_count(), total_slots, ms / passes,
                ms * 1e6 / evals, ms / passes,
                100.0 * (ms / passes) / kStepBudgetMs);
    pass("the whole district's phantom population has a measured cost");
}

}  // namespace

int main(int argc, char** argv) {
    bool full = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--full") == 0) full = true;

    const auto t0 = Clock::now();
    Network net;
    traffic_harness::build_network(net);
    const double build_ms = ms_since(t0);

    std::printf("\n=== PENG-26: headless traffic bench ===\n");
    std::printf("  district: %d x %d street grid at %.0f m pitch -> %zu edges, "
                "%zu lanes\n",
                traffic_harness::kDistrictN, traffic_harness::kDistrictN,
                static_cast<double>(traffic_harness::kDistrictPitchM),
                net.graph.edge_count(), net.lanes.lane_count());
    std::printf("  built in %.1f ms | sim rate %.0f Hz | step budget %.3f ms\n",
                build_ms, kSimHz, kStepBudgetMs);
    std::printf("  mode: %s\n", full ? "--full (long sweep)" : "short (ctest)");

    REQUIRE(net.lanes.lane_count() > 4000);

    bench_phantom_cost(net.lanes);
    bench_scale_ladder(net.lanes, full);
    if (full) {
        bench_sub_rate(net.lanes);
        bench_analytic_share(net.lanes);
    }

    return apricot_test::done("traffic_bench");
}

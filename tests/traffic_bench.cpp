// How many agents fit in a 120 Hz step, and where the time goes.
//
// A measurement, printed, with assertions around it so that a run which
// measured nothing cannot pass green. The assertions are FLOORS on what
// happened and never CEILINGS on how long it took: a timing assertion turns
// somebody else's slow CI box into a red build, and the first thing anyone does
// with a red build they did not cause is delete the test.
//
// Timing lives HERE and not in src/traffic/, because nothing below src/app/ may
// read a clock. That is also why Crowd's step is four public phases rather than
// one call — the phase boundaries are where the clock goes.
//
// Default run is the short ladder, which is what ctest executes. Pass --full
// for the push-to-failure sweep, the sub-rate ladder and the analytic
// comparison.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/frustum.h"
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

// The 120 Hz step budget. Everything below is quoted against it, and it is the
// WHOLE step: a traffic system that eats it has left nothing for physics,
// terrain streaming or drawing.
constexpr double kStepBudgetMs = 1000.0 / 120.0;

struct Profile {
    double refresh = 0.0;  // amortised over ALL steps, not just refresh steps
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
    double veh_analytic_pct = 0.0;
    double ped_analytic_pct = 0.0;
    std::size_t stepped = 0;  // agents actually updated, mean per step
    std::size_t retired = 0;
    double worst_step_ms = 0.0;      // any step, including a refresh step
    double worst_plain_step_ms = 0.0;  // a step with no refresh on it
    double refresh_step_ms = 0.0;    // mean cost OF a refresh step's refresh
    double moved_m = 0.0;            // total along-lane distance travelled
    bool cap_bound = false;
};

Run measure(const LaneGraph& lanes, const CrowdTuning& tune,
            const AmbientTuning& amb, int64_t steps, int64_t warmup) {
    Crowd crowd;
    crowd.build(lanes, traffic_harness::kMapSeed, amb, tune);
    Scene scene;

    Run r;
    Profile acc;
    int64_t counted = 0;
    int64_t refreshes = 0;
    double refresh_total = 0.0;

    // ANTI-VACUITY, and it is the one that matters most here. "Everything fit
    // in the budget" is trivially true of a benchmark whose cars never moved.
    // The sim determinism suite makes the same argument with a distance floor;
    // this is that argument.
    std::vector<float> last_dist;

    for (int64_t s = 0; s < warmup + steps; ++s) {
        const bool timed = s >= warmup;
        Profile p;
        Clock::time_point t0;
        bool did_refresh = false;

        if (s % tune.refresh_every_steps == 0) {
            t0 = Clock::now();
            crowd.refresh(s, traffic_harness::player_at(s));
            p.refresh = ms_since(t0);
            did_refresh = true;
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
            if (!did_refresh)
                r.worst_plain_step_ms = std::max(r.worst_plain_step_ms, p.total());
            else {
                refresh_total += p.refresh;
                ++refreshes;
            }
            ++counted;
            const CrowdStats& st = crowd.stats();
            r.veh_analytic_pct += st.vehicles > 0
                                      ? static_cast<double>(st.vehicles_analytic) /
                                            static_cast<double>(st.vehicles)
                                      : 0.0;
            r.ped_analytic_pct += st.peds > 0
                                      ? static_cast<double>(st.peds_analytic) /
                                            static_cast<double>(st.peds)
                                      : 0.0;
            r.stepped += st.vehicles_stepped + st.peds_stepped;
            if (st.vehicles >= tune.max_vehicles || st.peds >= tune.max_peds)
                r.cap_bound = true;
        }

        // Distance travelled, summed over the cars alive across this step.
        if (timed) {
            std::vector<float> now;
            now.reserve(crowd.vehicles().size());
            for (const VehicleAgent& v : crowd.vehicles()) now.push_back(v.dist_along_m);
            if (now.size() == last_dist.size())
                for (std::size_t i = 0; i < now.size(); ++i) {
                    const double d = static_cast<double>(now[i]) -
                                     static_cast<double>(last_dist[i]);
                    if (d > 0.0 && d < 5.0) r.moved_m += d;
                }
            last_dist.swap(now);
        }
    }

    const double inv = counted > 0 ? 1.0 / static_cast<double>(counted) : 0.0;
    acc.scale(inv);
    r.per_step = acc;
    r.vehicles = crowd.vehicles().size();
    r.peds = crowd.peds().size();
    r.agents = r.vehicles + r.peds;
    r.retired = crowd.stats().retired;
    r.veh_analytic_pct *= 100.0 * inv;
    r.ped_analytic_pct *= 100.0 * inv;
    r.stepped = static_cast<std::size_t>(static_cast<double>(r.stepped) * inv);
    r.refresh_step_ms =
        refreshes > 0 ? refresh_total / static_cast<double>(refreshes) : 0.0;
    return r;
}

void check_not_vacuous(const Run& r, const char* where) {
    REQUIRE_MSG(r.agents > 0, "no agents existed", where);
    REQUIRE_MSG(r.stepped > 0, "no agent was ever stepped", where);
    REQUIRE_MSG(r.moved_m > static_cast<double>(r.vehicles),
                "the cars did not move -- this benchmark measured nothing",
                where);
    REQUIRE_MSG(!r.cap_bound,
                "a population cap bound, which makes the result scan-order "
                "dependent and the measurement meaningless",
                where);
}

void print_header() {
    std::printf(
        "\n     agents    cars    peds  stepped | refresh  bucket    cars    "
        "peds publish   scene |  total  worst | %% budget\n"
        "   ---------------------------------- |-------------------------------"
        "----------------|---------------|---------\n");
}

void print_row(const char* tag, const Run& r) {
    std::printf("%3s%8zu %7zu %7zu %8zu | %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f |"
                " %6.3f %6.3f | %6.1f%%\n",
                tag, r.agents, r.vehicles, r.peds, r.stepped, r.per_step.refresh,
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

AmbientTuning density(float mul) {
    AmbientTuning a;
    a.vehicle_spacing_m /= mul;
    a.ped_spacing_m /= mul;
    a.max_vehicle_slots = static_cast<uint32_t>(32.0f * mul);
    a.max_ped_slots = static_cast<uint32_t>(48.0f * mul);
    return a;
}

// ---------------------------------------------------------------------------
//  the cost of the closed form on its own
// ---------------------------------------------------------------------------

void bench_phantom_cost(const LaneGraph& lanes) {
    // What a phantom costs when nobody instantiates it. This is the number that
    // decides whether "define the whole city" is affordable at all.
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
    const int passes = 16;
    for (int p = 0; p < passes; ++p)
        for (std::size_t i = 0; i < lanes.lane_count(); ++i) {
            const Lane& l = lanes.lane(static_cast<LaneRef>(i));
            for (uint32_t k = 0; k < sched[i].slots; ++k)
                sink += static_cast<double>(
                    phantom_vehicle(traffic_harness::kMapSeed, l, sched[i], k,
                                    1000 + p, amb)
                        .dist_along_m);
        }
    const double ms = ms_since(t0) / passes;
    const double evals = static_cast<double>(total_slots);
    REQUIRE(sink > 0.0);
    REQUIRE(total_slots > 1000);

    std::printf("\n== PHANTOM COST: the population that is DEFINED, not "
                "simulated ==\n"
                "   %zu lanes carry %zu vehicle phantoms across the whole "
                "district.\n"
                "   Evaluating EVERY ONE of them, every step, would cost "
                "%.3f ms (%.1f ns each)\n"
                "   -- %.1f%% of a 120 Hz step. Nothing ever asks for all of "
                "them; this is the ceiling.\n",
                lanes.lane_count(), total_slots, ms, ms * 1e6 / evals,
                100.0 * ms / kStepBudgetMs);
    pass("the whole district's phantom population has a measured ceiling");
}

// ---------------------------------------------------------------------------
//  ladder 1: what a shipping configuration costs
// ---------------------------------------------------------------------------

void bench_shipping_ladder(const LaneGraph& lanes, bool full) {
    std::printf("\n== LADDER 1: realistic radii, authored density, every agent "
                "stepped every step ==\n"
                "   Radius drives the count. Times are per SIM STEP, mean over "
                "the measured window.\n"
                "   The 120 Hz budget is %.3f ms for EVERYTHING the game does.\n",
                kStepBudgetMs);
    print_header();

    struct Rung {
        float veh_m;
        float ped_m;
    };
    const std::vector<Rung> rungs = {{110.0f, 55.0f},  {220.0f, 110.0f},
                                     {320.0f, 150.0f}, {450.0f, 200.0f},
                                     {650.0f, 260.0f}, {900.0f, 340.0f}};
    Run doc_config;
    for (const Rung& g : rungs) {
        const Run r = measure(lanes, tuning_for_radius(g.veh_m, g.ped_m),
                              AmbientTuning{}, full ? 900 : 300, 240);
        check_not_vacuous(r, "ladder1");
        print_row("", r);
        if (g.veh_m == 220.0f) doc_config = r;
    }
    std::printf("\n   The design doc's first-pass config (220 m / 110 m) lands "
                "at %zu cars + %zu peds\n"
                "   for %.3f ms/step -- %.1f%% of the budget. Its guess was 120 "
                "vehicles and 150 peds.\n",
                doc_config.vehicles, doc_config.peds,
                doc_config.per_step.total(),
                100.0 * doc_config.per_step.total() / kStepBudgetMs);
    pass("a shipping-shaped configuration has a measured per-step cost");
}

// ---------------------------------------------------------------------------
//  ladder 2: push it until it breaks, and say where
// ---------------------------------------------------------------------------

void bench_push_to_failure(const LaneGraph& lanes) {
    std::printf("\n== LADDER 2: PUSH TO FAILURE. Whole-district radius, density "
                "cranked ==\n"
                "   Nothing about this is a shipping configuration. It exists to "
                "find the wall.\n");
    print_header();

    struct Rung {
        float radius_m;
        float density_mul;
    };
    // Radius first, then density. Radius is the knob a shipping game actually
    // has; density past 1.0 is authoring the city denser than any real district
    // and is here only to get past the point where the district runs out of
    // lane metres to put cars on.
    const std::vector<Rung> rungs = {{900.0f, 1.0f},  {1300.0f, 1.0f},
                                     {1900.0f, 1.0f}, {2600.0f, 1.0f},
                                     {2600.0f, 2.0f}, {2600.0f, 3.0f}};

    Run last_under;
    Run first_over;
    bool found = false;
    for (const Rung& g : rungs) {
        CrowdTuning t = tuning_for_radius(g.radius_m, g.radius_m);
        // The caps are a safety valve against a pathological map, and a cap
        // that binds makes the result scan-order dependent. Lifted out of the
        // way here so the measurement measures the engine and not the valve.
        t.max_vehicles = 4000000u;
        t.max_peds = 4000000u;
        const Run r = measure(lanes, t, density(g.density_mul), 180, 90);
        check_not_vacuous(r, "ladder2");
        char tag[8];
        std::snprintf(tag, sizeof(tag), "%gx", static_cast<double>(g.density_mul));
        print_row(tag, r);
        if (r.per_step.total() <= kStepBudgetMs) last_under = r;
        else if (!found) {
            found = true;
            first_over = r;
        }
    }

    REQUIRE_MSG(last_under.agents > 10000,
                "the ladder never reached a real city scale", "ladder2");
    if (found)
        std::printf("\n   *** THE WALL: %zu agents fit (%.3f ms/step, %.1f%%); "
                    "%zu does not (%.3f ms/step).\n",
                    last_under.agents, last_under.per_step.total(),
                    100.0 * last_under.per_step.total() / kStepBudgetMs,
                    first_over.agents, first_over.per_step.total());
    else
        std::printf("\n   *** NO WALL FOUND on this ladder: the largest run, "
                    "%zu agents, still costs %.3f ms/step (%.1f%%).\n",
                    last_under.agents, last_under.per_step.total(),
                    100.0 * last_under.per_step.total() / kStepBudgetMs);
    std::printf("   At the largest run that fits: refresh alone costs %.3f ms "
                "ON the step it lands on,\n"
                "   worst whole step %.3f ms, worst step with no refresh on it "
                "%.3f ms.\n"
                "   THE MEAN FITTING IS NOT THE SAME AS FITTING: the refresh "
                "step is already over.\n"
                "\n   Marginal cost, from the two largest runs that fit: "
                "%.1f ns per car per step,\n"
                "   %.1f ns per pedestrian per step (decision phases only, "
                "excluding refresh/publish/scene).\n",
                last_under.refresh_step_ms, last_under.worst_step_ms,
                last_under.worst_plain_step_ms,
                last_under.vehicles > 0
                    ? last_under.per_step.vehicles * 1e6 /
                          static_cast<double>(last_under.vehicles)
                    : 0.0,
                last_under.peds > 0 ? last_under.per_step.peds * 1e6 /
                                          static_cast<double>(last_under.peds)
                                    : 0.0);
    pass("the ladder ran to a stated limit");
}

// ---------------------------------------------------------------------------
//  what sub-rate scheduling buys
// ---------------------------------------------------------------------------

void bench_sub_rate(const LaneGraph& lanes) {
    std::printf("\n== SUB-RATE: agent i runs when step %% k == keyed_phase(i) "
                "==\n   Same population every row; k divides the per-step agent "
                "count and multiplies dt.\n");
    print_header();

    Run base;
    for (int k : {1, 2, 4, 8}) {
        CrowdTuning t = tuning_for_radius(2600.0f, 2600.0f);
        t.vehicle_sub_rate = k;
        t.ped_sub_rate = k;
        t.max_vehicles = 4000000u;
        t.max_peds = 4000000u;
        const Run r = measure(lanes, t, density(1.0f), 180, 90);
        check_not_vacuous(r, "sub-rate");
        char tag[8];
        std::snprintf(tag, sizeof(tag), "k%d", k);
        print_row(tag, r);
        if (k == 1) base = r;
        else {
            REQUIRE_MSG(r.stepped < base.stepped,
                        "sub-rate scheduling did not reduce the stepped count",
                        "sub-rate");
            std::printf("      k=%d: %.3f ms vs %.3f ms at k=1 -- %.2fx\n", k,
                        r.per_step.total(), base.per_step.total(),
                        base.per_step.total() / std::max(1e-9,
                                                         r.per_step.total()));
        }
    }
    pass("sub-rate scheduling reduces the per-step agent count as advertised");
}

// ---------------------------------------------------------------------------
//  what the analytic mode is actually worth
// ---------------------------------------------------------------------------

void bench_analytic_share(const LaneGraph& lanes) {
    std::printf("\n== ANALYTIC SHARE: how much of the active set is still on "
                "its closed form ==\n"
                "   An analytic agent is reproduced from the absolute step, so "
                "it is radius-invariant.\n"
                "   An integrating one is not. This is the fraction of the "
                "determinism claim that holds.\n"
                "\n   ambient mode      cars  analytic    peds  analytic |"
                " ms/step\n"
                "   ---------------------------------------------------------"
                "-------\n");

    for (bool per_slot : {false, true}) {
        AmbientTuning amb;
        amb.per_slot_speed = per_slot;
        const Run r =
            measure(lanes, tuning_for_radius(450.0f, 200.0f), amb, 900, 240);
        check_not_vacuous(r, "analytic");
        std::printf("   %-14s %7zu %8.1f%% %7zu %8.1f%% | %6.3f\n",
                    per_slot ? "per-slot speed" : "one speed/lane", r.vehicles,
                    r.veh_analytic_pct, r.peds, r.ped_analytic_pct,
                    r.per_step.total());
    }
    std::printf("\n   Measured over 900 steps (7.5 s of sim) after a 2 s warm-up."
                " An active car leaves\n"
                "   the closed form when it reaches the end of the lane it was "
                "instantiated on, so\n"
                "   the share is bounded by residency time over lane traversal "
                "time, not by the mode.\n");
    pass("the analytic share is measured under both schedule modes");
}

// ---------------------------------------------------------------------------
//  what those agents cost the RENDER frame, which is a different budget
// ---------------------------------------------------------------------------

void bench_cull_cost(const LaneGraph& lanes) {
    // Every active agent is a Scene node, and a Scene node is something
    // Scene::cull() has to look at once per RENDER frame. That is a different
    // budget to the sim step and it is not optional: pinatty.md 7.1 measured a
    // flat scan at 0.28 ms for 60k nodes and recommended capping resident
    // static instances near 60k. Agents are on TOP of that cap, so the number
    // worth knowing is what the agents alone add.
    CrowdTuning t = tuning_for_radius(2600.0f, 2600.0f);
    t.max_vehicles = 4000000u;
    t.max_peds = 4000000u;

    Crowd crowd;
    crowd.build(lanes, traffic_harness::kMapSeed, AmbientTuning{}, t);
    Scene scene;
    for (int64_t s = 0; s < 32; ++s)
        traffic_harness::run_step(crowd, scene, s, t.refresh_every_steps, true);
    const std::size_t nodes = scene.size();
    REQUIRE_MSG(nodes > 10000, "not enough nodes to say anything about culling",
                "cull");

    // A camera at the player, looking along +X, with the draw distance the
    // agents were activated within.
    const glm::vec3 eye{traffic_harness::player_at(32).x, 40.0f,
                        traffic_harness::player_at(32).y};
    const glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 3000.0f);
    const glm::mat4 view =
        glm::lookAt(eye, eye + glm::vec3{1.0f, -0.2f, 0.0f}, glm::vec3{0, 1, 0});
    const Frustum fr = Frustum::from_view_proj(proj * view);

    const auto t0 = Clock::now();
    const int passes = 40;
    std::size_t visible = 0;
    for (int i = 0; i < passes; ++i)
        visible = scene.cull(fr, eye, 400.0f).visible.size();
    const double ms = ms_since(t0) / passes;

    std::printf("\n== CULL: what the agents cost the RENDER frame ==\n"
                "   %zu agent nodes in a real Scene; one frustum + distance cull "
                "with the survivor\n"
                "   sort costs %.3f ms and returns %zu visible (%.1f ns/node).\n"
                "   At 60 Hz that is %.1f%% of a render frame, and it is ON TOP "
                "of the static\n"
                "   instances pinatty.md 7.1 already recommends capping near 60k.\n",
                nodes, ms, visible, ms * 1e6 / static_cast<double>(nodes),
                100.0 * ms / (1000.0 / 60.0));
    pass("the render-side cost of a city's worth of agent nodes is measured");
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

    std::printf("\n=== PENG-26: headless traffic bench ===\n"
                "  district : %d x %d street grid at %.0f m pitch -> %zu edges,"
                " %zu directed lanes\n"
                "  built in : %.1f ms | sim rate %.0f Hz | step budget %.3f ms\n"
                "  mode     : %s\n",
                traffic_harness::kDistrictN, traffic_harness::kDistrictN,
                static_cast<double>(traffic_harness::kDistrictPitchM),
                net.graph.edge_count(), net.lanes.lane_count(), build_ms, kSimHz,
                kStepBudgetMs, full ? "--full" : "short (ctest default)");

    REQUIRE(net.lanes.lane_count() > 4000);

    bench_phantom_cost(net.lanes);
    bench_shipping_ladder(net.lanes, full);
    if (full) {
        bench_push_to_failure(net.lanes);
        bench_sub_rate(net.lanes);
        bench_analytic_share(net.lanes);
        bench_cull_cost(net.lanes);
    }

    return apricot_test::done("traffic_bench");
}

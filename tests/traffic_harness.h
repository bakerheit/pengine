#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "scene/scene.h"
#include "traffic/crowd.h"

#include "road_fixture.h"

// Shared rig for the two traffic suites: a district-scale road network, a
// player path that is a pure function of the step, and one call that runs the
// whole per-step sequence in the order Crowd's contract requires.
//
// It exists because the bench and the determinism suite have to drive the
// system IDENTICALLY. Two hand-written loops drift, and the day they drift is
// the day the bench measures one thing and the proof proves another.

namespace traffic_harness {

// The district. 40 x 40 street spines at a 92 m block pitch is 3,120 edges and
// 6,240 directed lanes over a 3.6 km square — the same fixture road_graph_tests
// calls district scale, and comfortably past anything the authored spine table
// for one Pinatty district will hold.
inline constexpr int kDistrictN = 40;
inline constexpr float kDistrictPitchM = 92.0f;
inline constexpr uint64_t kMapSeed = 0xDEADBEEFull;

struct Network {
    apricot::RoadGraph graph;
    apricot::LaneGraph lanes;
};

// `reversed` flips the spine table. It changes every node index, every edge
// index and every LaneRef, and changes NOTHING an agent is keyed on — which is
// what makes it the reordering test the whole determinism claim rests on.
inline void build_network(Network& net, int n = kDistrictN,
                          float pitch = kDistrictPitchM, bool reversed = false) {
    std::vector<apricot::RoadSpine> spines = make_grid_spines(n, pitch);
    if (reversed) std::reverse(spines.begin(), spines.end());
    // A flat sampler: the terrain field is exercised by its own suites, and
    // draping here would make every measurement below partly a measurement of
    // fbm. The lane arc lengths differ by centimetres either way.
    net.graph.build(spines, apricot::RoadGraphParams{}, apricot::GroundSampler{});
    net.lanes.build(net.graph, apricot::GroundSampler{});
}

// Where the player is at step t. A pure function of the step, deliberately: a
// tape is a list of inputs and the position it produces is derived, so a
// harness that stored positions would be storing a second source of truth for
// the same run. A slow diagonal sweep with a cross-track wobble, so the crowd
// keeps meeting lanes it has not met.
inline glm::vec2 player_at(int64_t step, float pitch = kDistrictPitchM,
                           int n = kDistrictN) {
    const float span = pitch * static_cast<float>(n - 1);
    const double t = static_cast<double>(step) / 120.0;  // seconds of sim
    const float u = static_cast<float>(std::fmod(t * 14.0, span));
    const float v = static_cast<float>(
        span * 0.5 + std::sin(t * 0.21) * static_cast<double>(span) * 0.35);
    return glm::vec2{u, v};
}

// The per-step sequence, in the one order Crowd::refresh / rebuild_buckets /
// step_* / publish may legally run in.
inline void run_step(apricot::Crowd& crowd, apricot::Scene& scene, int64_t step,
                     int refresh_every, bool publish) {
    if (step % refresh_every == 0) crowd.refresh(step, player_at(step));
    crowd.rebuild_buckets();
    crowd.step_vehicles(step);
    crowd.step_peds(step);
    if (publish) {
        crowd.publish(scene);
        scene.update();
    }
}

inline void run_steps(apricot::Crowd& crowd, apricot::Scene& scene,
                      int64_t first, int64_t count, int refresh_every,
                      bool publish) {
    for (int64_t s = first; s < first + count; ++s)
        run_step(crowd, scene, s, refresh_every, publish);
}

}  // namespace traffic_harness

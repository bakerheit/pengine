#pragma once

#include <vector>

#include "road/road_graph.h"

// A hand-made spine set, standing in for the map tables.
//
// WHY IT IS STILL HAND-MADE, now that `city::map_spines()` exists. The road
// module takes spines as a parameter precisely so it never has to know a city
// is there, and these suites test the GRAPH, not Pinatty. The layout below
// contains one of each thing the graph has to notice, in eight roads you can
// hold in your head; the real map contains all of them too, buried in ninety,
// which is a far worse place to debug a weld tolerance from.
//
// The real network is exercised end to end in `tests/city_roads_tests.cpp`.
// Everything downstream of the spines in BOTH suites is the REAL producer — the
// real RoadGraph, the real ribbon bake, the real lane graph, the real terrain.
//
// The layout is chosen to contain one of each thing the graph has to notice:
//
//        (-150,180)      (0,200)---(90,200)        street B's end welds to
//             |             |                      alley E's start: degree 2
//         C (street)     B (street)                        (200,130)
//             |             |                             /
//   (-300,0)--+-------------+-------------+--(300,0)     / G (street, bent)
//        A (arterial)     4-WAY        T with dirt    (160,60)
//                                          \           /
//                                       D (dirt)   (100,40)
//                                            (260,120)
//
//   (-320,-250)========================================(320,-250)
//                  F: freeway, on a bridge deck at y = 26
//
// * A x B is an interior CROSSING of two spines -> a 4-way junction that
//   neither spine authored.
// * C and D END on A's interior -> T junctions.
// * B's end and E's start coincide -> one welded node of degree 2, and the
//   class changes across it, which is why edges may not span it.
// * G bends in the middle without meeting anything -> a shape point, NOT a
//   node.
// * F is decked, so it must not drape.
inline std::vector<apricot::RoadSpine> make_test_spines() {
    using namespace apricot;
    std::vector<RoadSpine> s;

    RoadSpine a;
    a.id = 1;
    a.cls = RoadClass::Arterial;
    a.points = {{-300.0f, 0.0f}, {300.0f, 0.0f}};
    a.block_quality = 200;
    a.traffic_density = 1.4f;
    s.push_back(a);

    RoadSpine b;
    b.id = 2;
    b.cls = RoadClass::Street;
    b.points = {{0.0f, -200.0f}, {0.0f, 200.0f}};
    s.push_back(b);

    RoadSpine c;
    c.id = 3;
    c.cls = RoadClass::Street;
    c.points = {{-150.0f, 0.0f}, {-150.0f, 180.0f}};
    s.push_back(c);

    RoadSpine d;
    d.id = 4;
    d.cls = RoadClass::Dirt;
    d.points = {{150.0f, 0.0f}, {260.0f, 120.0f}};
    s.push_back(d);

    RoadSpine e;
    e.id = 5;
    e.cls = RoadClass::Alley;
    e.points = {{0.0f, 200.0f}, {90.0f, 200.0f}};
    s.push_back(e);

    RoadSpine f;
    f.id = 6;
    f.cls = RoadClass::Freeway;
    f.structure = RoadStructure::Bridge;
    f.deck_y_m = 26.0f;
    f.points = {{-320.0f, -250.0f}, {320.0f, -250.0f}};
    f.block_quality = 255;
    s.push_back(f);

    RoadSpine g;
    g.id = 7;
    g.cls = RoadClass::Street;
    g.points = {{100.0f, 40.0f}, {160.0f, 60.0f}, {200.0f, 130.0f}};
    s.push_back(g);

    return s;
}

// Counts the fixture is expected to produce. Spelled out here so every suite
// asserts the same numbers and a change to the layout fails loudly in one
// place instead of quietly in three.
inline constexpr std::size_t kFixtureEdges = 11;
inline constexpr std::size_t kFixtureNodes = 14;
inline constexpr std::size_t kFixtureJunctions = 3;

// A regular grid of streets, for scale. `n` spines each way.
inline std::vector<apricot::RoadSpine> make_grid_spines(int n, float pitch_m) {
    using namespace apricot;
    std::vector<RoadSpine> s;
    const float span = pitch_m * static_cast<float>(n - 1);
    uint32_t id = 1;
    for (int i = 0; i < n; ++i) {
        const float v = pitch_m * static_cast<float>(i);
        RoadSpine ew;
        ew.id = id++;
        ew.cls = RoadClass::Street;
        ew.points = {{0.0f, v}, {span, v}};
        s.push_back(ew);

        RoadSpine ns;
        ns.id = id++;
        ns.cls = RoadClass::Street;
        ns.points = {{v, 0.0f}, {v, span}};
        s.push_back(ns);
    }
    return s;
}

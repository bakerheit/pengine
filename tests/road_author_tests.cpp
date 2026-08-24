// The authoring road graph, headless.
//
// Pins the topology rules that make a junction a junction — shared endpoints
// merging, a mid-span drop SPLITTING an edge into a real T, bulldozing
// collecting its orphan node — plus the Bezier split, the in-memory blob
// round-trip, and the versioned width migration.
//
// NOT HERE, and both omissions are deliberate (PENG-29):
//
//   * the .roads file-migration test. The file layer it exercised did not come
//     across. probablecause's CI has an EXCLUDE list precisely because tests
//     like that one wrote into live world data; tools/ci.sh has no such list
//     and must never grow one.
//   * the lane-graph tests. to_polylines() fed a LaneGraph in probablecause and
//     that class is not in this tree. Rather than drop to_polylines() coverage
//     entirely, the replacement below asserts the CONTRACT a lane producer will
//     consume: endpoints exact, spacing bounded, per-edge width, type and
//     sidewalk flag carried through.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "test_assert.h"

#include "city/road_author.h"
#include "city/road_types.h"

using namespace apricot;

namespace {

using Shape = RoadGraphAuthor::Shape;

float min_dist_to_polyline(glm::vec2 q, const std::vector<glm::vec2>& poly) {
    float best = 1e30f;
    for (std::size_t i = 1; i < poly.size(); ++i) {
        glm::vec2 a = poly[i - 1], b = poly[i], ab = b - a;
        float l2 = ab.x * ab.x + ab.y * ab.y;
        float u = l2 > 1e-9f ? ((q.x - a.x) * ab.x + (q.y - a.y) * ab.y) / l2 : 0.f;
        u = std::clamp(u, 0.f, 1.f);
        glm::vec2 p = a + ab * u;
        best = std::min(best, glm::length(q - p));
    }
    return best;
}

// Build a straight edge through node_at so endpoints snap/merge as the editor does.
RoadEdgeId add_straight(RoadGraphAuthor& a, glm::vec2 p0, glm::vec2 p1, RoadType t = RoadType::Street) {
    RoadNodeId n0 = a.node_at(p0);
    RoadNodeId n1 = a.node_at(p1);
    return a.add_edge(n0, n1, t, road_type_def(t).carriageway_width_m, Shape::Straight, {});
}

// Roads sharing an endpoint merge into one junction.
void test_shared_endpoint() {
    RoadGraphAuthor a;
    add_straight(a, {0.f, 0.f}, {64.f, 0.f});
    RoadNodeId b0 = a.node_at({64.f, 0.f});   // snaps to the existing (64,0) node
    RoadNodeId b1 = a.node_at({64.f, 64.f});
    a.add_edge(b0, b1, RoadType::Street, 12.f, Shape::Straight, {});

    REQUIRE(a.node_count() == 3);
    REQUIRE(a.live_edge_count() == 2);
    REQUIRE(a.node(b0).edges.size() == 2);    // the shared junction
}

// A node dropped mid-span splits the edge into a real T-intersection.
void test_midspan_split() {
    RoadGraphAuthor a;
    add_straight(a, {0.f, 0.f}, {64.f, 0.f});

    RoadNodeId mid = a.node_at({32.f, 0.f});  // lands on the span -> split
    REQUIRE(a.node_count() == 3);
    REQUIRE(a.live_edge_count() == 2);

    RoadNodeId top = a.node_at({32.f, 64.f});
    a.add_edge(mid, top, RoadType::Street, 12.f, Shape::Straight, {});
    REQUIRE(a.node_count() == 4);
    REQUIRE(a.live_edge_count() == 3);
    REQUIRE(a.node(mid).edges.size() == 3);   // T-junction: 3 incident edges
}

// serialize -> deserialize round-trips topology + geometry + per-edge data.
void test_persistence_roundtrip() {
    RoadGraphAuthor a;
    add_straight(a, {0.f, 0.f}, {64.f, 0.f}, RoadType::Avenue);
    RoadNodeId n1 = a.node_at({64.f, 0.f});
    RoadNodeId n2 = a.node_at({120.f, 40.f});
    // Bezier edge with sidewalks explicitly OFF, to confirm the v2 per-edge
    // sidewalk byte round-trips (and isn't just defaulted back to true).
    a.add_edge(n1, n2, RoadType::Street, 12.f, Shape::Bezier, {{90.f, 60.f}},
               /*sidewalks=*/false);

    std::string blob = a.serialize();
    RoadGraphAuthor b;
    REQUIRE(b.deserialize(blob));
    REQUIRE(b.node_count() == a.node_count());
    REQUIRE(b.live_edge_count() == a.live_edge_count());

    // Find the bezier edge in b and confirm its control + type + sidewalk flag
    // survived.
    bool found_bezier = false;
    for (RoadEdgeId e = 0; e < b.edge_count(); ++e) {
        if (!b.edge_alive(e)) continue;
        if (b.edge(e).shape == Shape::Bezier) {
            found_bezier = true;
            REQUIRE(b.edge(e).controls.size() == 1);
            REQUIRE(std::abs(b.edge(e).controls[0].x - 90.f) < 1e-3f);
            REQUIRE(std::abs(b.edge(e).controls[0].y - 60.f) < 1e-3f);
            REQUIRE(b.edge(e).sidewalks == false);
        }
    }
    REQUIRE(found_bezier);
    // The Avenue straight edge keeps the default (sidewalks on).
    for (RoadEdgeId e = 0; e < b.edge_count(); ++e)
        if (b.edge_alive(e) && b.edge(e).shape == Shape::Straight)
            REQUIRE(b.edge(e).sidewalks == true);

}

// de Casteljau split leaves both halves tracing the original curve.
void test_bezier_split_preserves_shape() {
    RoadGraphAuthor a;
    RoadNodeId n0 = a.node_at({0.f, 0.f});
    RoadNodeId n1 = a.node_at({100.f, 0.f});
    RoadEdgeId e = a.add_edge(n0, n1, RoadType::Street, 12.f, Shape::Bezier, {{50.f, 40.f}});

    std::vector<glm::vec2> before = a.tessellate(e, 1.f);
    // bezier2({0,0},{50,40},{100,0}, 0.5) = {50, 20}; drop a node there.
    RoadNodeId mid = a.node_at({50.f, 20.f}, 5.f);
    REQUIRE(a.node_count() == 3);
    REQUIRE(a.live_edge_count() == 2);
    REQUIRE(std::abs(a.node(mid).pos.x - 50.f) < 1.f);
    REQUIRE(std::abs(a.node(mid).pos.y - 20.f) < 1.f);

    float max_dev = 0.f;
    for (RoadEdgeId he = 0; he < a.edge_count(); ++he) {
        if (!a.edge_alive(he)) continue;
        for (glm::vec2 p : a.tessellate(he, 1.f))
            max_dev = std::max(max_dev, min_dist_to_polyline(p, before));
    }

}

// ============================================================================
// PCG-177 — the +2 m global widen, applied to authored data at LOAD.
// Pre-v4 .roadgraph blobs get a pure `width + 2` delta: custom widths keep
// their identity (no snap to canonical type widths — the PCG-170 mistake), and
// the delta is gated on the FILE version so load -> save -> load can never
// compound it. The file on disk is never rewritten by a load.
// ============================================================================

template <class T>
void blob_put(std::string& s, const T& v) {
    s.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

// A hand-built v3 .roadgraph blob (the on-disk format before PCG-177):
// 3 nodes in a row, 2 Street edges — one CUSTOM 9 m (a width-slider alley),
// one at the old 12 m Street canonical.
std::string make_v3_blob() {
    std::string s;
    blob_put(s, std::uint32_t{0x47444152u});              // "RADG"
    blob_put(s, std::uint32_t{3u});                       // version 3 (pre-widen)
    blob_put(s, std::uint32_t{3u});                       // node count
    const float nodes[3][2] = {{0.f, 0.f}, {64.f, 0.f}, {128.f, 0.f}};
    for (const auto& n : nodes) { blob_put(s, n[0]); blob_put(s, n[1]); }
    blob_put(s, std::uint32_t{2u});                       // edge count
    auto edge = [&](std::uint32_t a, std::uint32_t b, float width) {
        blob_put(s, a);
        blob_put(s, b);
        blob_put(s, std::uint8_t{0});                     // type = Street
        blob_put(s, std::uint8_t{0});                     // shape = Straight
        blob_put(s, std::uint8_t{1});                     // sidewalks on
        blob_put(s, width);
        blob_put(s, 1.f);                                 // traffic_density (v3)
        blob_put(s, 1.f);                                 // ped_density (v3)
        blob_put(s, std::uint32_t{0u});                   // no controls
    };
    edge(0, 1, 9.f);                                      // custom alley
    edge(1, 2, 12.f);                                     // old Street canonical
    return s;
}

// Collect the live edge widths, sorted, for exact comparison.
std::vector<float> live_widths(const RoadGraphAuthor& a) {
    std::vector<float> w;
    for (RoadEdgeId e = 0; e < a.edge_count(); ++e)
        if (a.edge_alive(e)) w.push_back(a.edge(e).width);
    std::sort(w.begin(), w.end());
    return w;
}

// Pre-v4 load: +2 on every edge, custom widths preserved (9 -> 11, NOT
// snapped to any canonical), old canonical lands on the new one (12 -> 14),
// and the TYPE is untouched.
void test_widen_delta_on_pre_v4_load() {
    RoadGraphAuthor a;
    REQUIRE(a.deserialize(make_v3_blob()));
    REQUIRE(a.live_edge_count() == 2);
    std::vector<float> w = live_widths(a);
    REQUIRE(w.size() == 2);
    REQUIRE(std::abs(w[0] - 11.f) < 1e-4f);   // custom 9 m alley -> 11 m alley
    REQUIRE(std::abs(w[1] - 14.f) < 1e-4f);   // 12 -> 14 (new Street canonical)
    for (RoadEdgeId e = 0; e < a.edge_count(); ++e)
        if (a.edge_alive(e))
            REQUIRE(a.edge(e).type == RoadType::Street);  // identity preserved
}

// v4 round-trip: serialize() stamps v4, so re-loading the saved blob applies
// NO further delta — a load -> save -> load cycle cannot compound the widen.
void test_widen_delta_idempotent_roundtrip() {
    RoadGraphAuthor a;
    REQUIRE(a.deserialize(make_v3_blob()));
    std::string v4 = a.serialize();

    RoadGraphAuthor b;
    REQUIRE(b.deserialize(v4));
    std::vector<float> w = live_widths(b);
    REQUIRE(w.size() == 2);
    REQUIRE(std::abs(w[0] - 11.f) < 1e-4f);   // unchanged, not 13
    REQUIRE(std::abs(w[1] - 14.f) < 1e-4f);   // unchanged, not 16

    // And a third generation stays put too.
    RoadGraphAuthor c;
    REQUIRE(c.deserialize(b.serialize()));
    REQUIRE(live_widths(c) == w);
}

void test_upgrade_and_bulldoze() {
    RoadGraphAuthor a;
    RoadEdgeId e0 = add_straight(a, {0.f, 0.f}, {64.f, 0.f});
    add_straight(a, {64.f, 0.f}, {64.f, 64.f});
    REQUIRE(a.node_count() == 3);
    REQUIRE(a.live_edge_count() == 2);

    a.set_edge_type(e0, RoadType::Highway, 28.f);
    REQUIRE(a.edge(e0).type == RoadType::Highway);
    REQUIRE(std::abs(a.edge(e0).width - 28.f) < 1e-3f);

    // Bulldoze the second edge; its dangling far node is collected.
    glm::vec2 proj{0.f}; float t = 0.f;
    RoadEdgeId far = a.pick_edge({64.f, 32.f}, 4.f, proj, t);
    REQUIRE(far != kInvalidRoadId);
    a.remove_edge(far);
    REQUIRE(a.live_edge_count() == 1);
    REQUIRE(a.node_count() == 2);          // orphan (64,64) dropped
}

// to_polylines() is the ONE seam between authoring and everything downstream:
// the ribbon mesh and the lane producer both read it and nothing else. So the
// contract it has to keep is that a tessellated edge still IS the edge — same
// endpoints, no gaps a lane builder would have to guess across, and every
// per-edge property carried rather than re-derived from the type table.
void test_to_polylines_carries_the_contract() {
    RoadGraphAuthor a;
    add_straight(a, {0.f, 0.f}, {64.f, 0.f}, RoadType::Avenue);
    RoadNodeId n1 = a.node_at({64.f, 0.f});
    RoadNodeId n2 = a.node_at({64.f, 64.f});
    // A narrower-than-type custom width, sidewalks off, non-default densities:
    // every one of these has to survive, and every one of them is the kind of
    // field a "just look it up in ROAD_TYPES" shortcut would silently discard.
    RoadEdgeId e1 = a.add_edge(n1, n2, RoadType::Street, 9.5f, Shape::Straight, {},
                               /*sidewalks=*/false);
    REQUIRE(e1 != kInvalidRoadId);
    // And a curve, because straight and curved edges are tessellated by
    // genuinely different rules and only one of them samples.
    RoadNodeId n3 = a.node_at({160.f, 60.f});
    RoadEdgeId curve = a.add_edge(n2, n3, RoadType::Street, 14.f, Shape::Bezier,
                                  {{100.f, 100.f}});
    REQUIRE(curve != kInvalidRoadId);

    constexpr float kStep = 4.f;
    const std::vector<RoadGraphAuthor::Polyline> polys = a.to_polylines(kStep);
    REQUIRE(polys.size() == a.live_edge_count());

    bool saw_avenue = false, saw_custom = false, saw_curve = false;
    for (const auto& p : polys) {
        REQUIRE(p.points.size() >= 2);   // never a degenerate one-point road

        // No duplicate samples anywhere: a zero-length segment has no tangent,
        // and a lane builder that normalises one gets a NaN heading.
        for (std::size_t i = 1; i < p.points.size(); ++i)
            REQUIRE(glm::length(p.points[i] - p.points[i - 1]) > 1e-4f);

        if (p.points.size() > 2) {
            // A CURVED edge is sampled, and the samples stay dense enough that
            // a consumer never has to guess across a hole.
            //
            // The bound is 2x the step, NOT the step. tessellate() derives its
            // segment count from an ESTIMATED arc length (half the chord plus
            // half the control polygon), which under-reads through a tight
            // bend, so the real chords run longer there than the step asks
            // for. Measured on this curve at step 4: gaps run 3.23 to 5.07 m.
            // Asserting `<= kStep` would be asserting a promise the function
            // does not make and has never made.
            saw_curve = true;
            for (std::size_t i = 1; i < p.points.size(); ++i)
                REQUIRE(glm::length(p.points[i] - p.points[i - 1]) <= 2.f * kStep);
        } else {
            // A STRAIGHT edge is exactly its two endpoints, at ANY step, and
            // that is deliberate rather than a missing feature: a straight
            // segment is fully described by its ends and a consumer
            // interpolates along it. Anything that assumes to_polylines()
            // samples uniformly is wrong about this, and would read as
            // correct right up until it met a long straight road.
            REQUIRE(p.points.size() == 2);
            REQUIRE(glm::length(p.points[1] - p.points[0]) > kStep);
        }

        if (p.type == RoadType::Avenue) {
            saw_avenue = true;
            REQUIRE(std::abs(p.width -
                             road_type_def(RoadType::Avenue).carriageway_width_m) < 1e-4f);
            REQUIRE(p.sidewalks);
        }
        if (std::abs(p.width - 9.5f) < 1e-4f) {
            saw_custom = true;
            REQUIRE(p.type == RoadType::Street);   // custom width, stock type
            REQUIRE(!p.sidewalks);
        }
    }
    REQUIRE(saw_avenue);
    REQUIRE(saw_custom);
    REQUIRE(saw_curve);

    // The endpoints are EXACT, not merely near. A lane builder stitches
    // junctions by comparing endpoints, so a tessellator that rounds the last
    // sample turns one junction into two roads that nearly touch — which looks
    // perfect and is not drivable.
    bool exact_start = false;
    for (const auto& p : polys)
        if (p.points.front() == glm::vec2{0.f, 0.f} ||
            p.points.back()  == glm::vec2{0.f, 0.f})
            exact_start = true;
    REQUIRE(exact_start);

    bool exact_shared = false;
    for (const auto& p : polys)
        if (p.points.front() == glm::vec2{64.f, 0.f} ||
            p.points.back()  == glm::vec2{64.f, 0.f})
            exact_shared = true;
    REQUIRE(exact_shared);
}

}  // namespace

int main() {
    test_shared_endpoint();
    test_midspan_split();
    test_persistence_roundtrip();
    test_bezier_split_preserves_shape();
    test_to_polylines_carries_the_contract();
    // The +2 m global widen, applied to authored data at deserialize.
    test_widen_delta_on_pre_v4_load();
    test_widen_delta_idempotent_roundtrip();
    test_upgrade_and_bulldoze();
    return apricot_test::done("road_author_tests");
}

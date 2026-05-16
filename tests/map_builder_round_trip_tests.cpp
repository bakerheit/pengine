// Round-trip + collision verification tests for the map builder's persistence
// path (PBD-044, EPIC-002 Phase A verification ticket).
//
// EPIC-002 Phase A added size presets, free scale, yaw rotation, and edit
// persistence via save_ipl-on-evict. PBD-044 is the "exercise along new
// paths" insurance ticket: it pushes an InstanceDef with a non-identity
// scale and rotation through save_ipl + load_ipl and asserts everything
// round-trips within the precision budget the on-disk format affords. It
// also verifies AABB::transform (Arvo's method) grows the world AABB
// correctly for a yaw'd cube — catches catastrophic regressions in the
// AABB transform path that everything physics-side relies on.
//
// The file format (src/world/ipl_loader.cpp:108) writes position at %.4f,
// quaternion at %.6f, and scale at %.4f. Tolerances below reflect those
// printf precisions.
//
// We deliberately match traffic_system_tests.cpp's framework: plain main()
// with assert() per case + a printed pass line. No extra macros, no extra
// fixtures.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/log.h"
#include "scene/aabb.h"
#include "scene/transform.h"
#include "world/instance_def.h"
#include "world/ipl_loader.h"

using namespace pengine;

namespace {

// =============================================================================
// Tolerances
// =============================================================================
//
// IPL writes position at %.4f, scale at %.4f, quaternion at %.6f. The
// reconstructed values can only ever be as precise as the printf format
// allowed. We pick tolerances one decimal looser than the format precision
// to absorb the round-half-to-even ambiguity in the last printed digit.
//
// Position is explicitly "exact (no math)" per the ticket — values like
// 64.0f, 12.5f, -3.25f survive %.4f round-trip bit-perfect when assigned
// directly. But we still use a tiny epsilon to keep the comparator safe
// across compilers / FPUs.

constexpr float kPosTol   = 1e-4f;   // %.4f format budget
constexpr float kScaleTol = 1e-4f;   // %.4f format budget
constexpr float kQuatTol  = 1e-5f;   // %.6f format budget, looser by 10x

// =============================================================================
// Helpers
// =============================================================================

std::filesystem::path tmp_ipl_path(const char* tag) {
    char fname[128];
    std::snprintf(fname, sizeof(fname), "pbd044_%s.ipl", tag);
    return std::filesystem::temp_directory_path() / fname;
}

// FATAL on any condition false. Replaces assert() because this binary builds
// with NDEBUG (RelWithDebInfo) and assert is a no-op — silent test passes
// are worse than no tests. traffic_system_tests.cpp uses assert and only
// gets away with it because its asserts happen to hold; we exercise paths
// that *might* fail (file I/O), so we need a real check.
void fatal_if(bool cond, const char* msg, const char* case_name) {
    if (!cond) {
        std::printf("FAIL [%s] %s\n", case_name, msg);
        std::exit(1);
    }
}

// Save `in` to a temp path, load it back, and stash the result in `out`.
// Aborts the test with a clear FAIL line on any I/O failure rather than
// returning false silently.
void round_trip(const InstanceDef& in, const char* tag, InstanceDef& out) {
    auto path = tmp_ipl_path(tag);
    std::vector<InstanceDef> save_vec{in};
    bool saved = save_ipl(path, save_vec);
    fatal_if(saved, "save_ipl returned false", tag);

    std::vector<InstanceDef> load_vec;
    bool loaded = load_ipl(path, load_vec);
    fatal_if(loaded, "load_ipl returned false", tag);
    fatal_if(load_vec.size() == 1,
             "expected exactly 1 instance loaded", tag);

    out = load_vec.front();

    // Best-effort cleanup. Not fatal if it fails (e.g. CI tmp policy).
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void expect_pos_eq(const glm::vec3& a, const glm::vec3& b, const char* case_name) {
    if (std::abs(a.x - b.x) > kPosTol ||
        std::abs(a.y - b.y) > kPosTol ||
        std::abs(a.z - b.z) > kPosTol) {
        std::printf("FAIL [%s] position: got (%.6f,%.6f,%.6f) want (%.6f,%.6f,%.6f)\n",
                    case_name, a.x, a.y, a.z, b.x, b.y, b.z);
        std::exit(1);
    }
}

void expect_scale_eq(const glm::vec3& a, const glm::vec3& b, const char* case_name) {
    if (std::abs(a.x - b.x) > kScaleTol ||
        std::abs(a.y - b.y) > kScaleTol ||
        std::abs(a.z - b.z) > kScaleTol) {
        std::printf("FAIL [%s] scale: got (%.6f,%.6f,%.6f) want (%.6f,%.6f,%.6f)\n",
                    case_name, a.x, a.y, a.z, b.x, b.y, b.z);
        std::exit(1);
    }
}

void expect_quat_eq(const glm::quat& a, const glm::quat& b, const char* case_name) {
    // Quats q and -q describe the same rotation. The IPL format writes
    // components verbatim and load_ipl reads them verbatim, so a save/load
    // should not flip signs — but if the encoder ever normalises to a
    // canonical hemisphere, this comparator would have to flip. For now we
    // assert raw-component equality, which is what the ticket spec asks for.
    if (std::abs(a.x - b.x) > kQuatTol ||
        std::abs(a.y - b.y) > kQuatTol ||
        std::abs(a.z - b.z) > kQuatTol ||
        std::abs(a.w - b.w) > kQuatTol) {
        std::printf("FAIL [%s] quat: got (%.7f,%.7f,%.7f,%.7f) want (%.7f,%.7f,%.7f,%.7f)\n",
                    case_name, a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
        std::exit(1);
    }
}

void expect_uv_eq(const glm::vec2& a, const glm::vec2& b, const char* case_name) {
    // uv_scale_override is written at %.4f when emitted (and not emitted at
    // all for the (0,0) sentinel — the loader leaves it default-constructed,
    // which is also (0,0), so exact equality holds).
    if (std::abs(a.x - b.x) > kScaleTol ||
        std::abs(a.y - b.y) > kScaleTol) {
        std::printf("FAIL [%s] uv_scale_override: got (%.6f,%.6f) want (%.6f,%.6f)\n",
                    case_name, a.x, a.y, b.x, b.y);
        std::exit(1);
    }
}

void expect_eq_u32(uint32_t a, uint32_t b, const char* case_name, const char* field) {
    if (a != b) {
        std::printf("FAIL [%s] %s: got %u want %u\n", case_name, field, a, b);
        std::exit(1);
    }
}

void expect_eq_full(const InstanceDef& got, const InstanceDef& want, const char* case_name) {
    expect_eq_u32(got.model_id, want.model_id, case_name, "model_id");
    expect_pos_eq  (got.transform.position, want.transform.position, case_name);
    expect_scale_eq(got.transform.scale,    want.transform.scale,    case_name);
    expect_quat_eq (got.transform.rotation, want.transform.rotation, case_name);
    expect_eq_u32  (got.lod_pair, want.lod_pair, case_name, "lod_pair");
    expect_uv_eq   (got.uv_scale_override, want.uv_scale_override, case_name);
}

// Build an InstanceDef from a yaw (degrees), scale, and position. yaw=0 +
// scale=1 + pos=0 is the identity case.
InstanceDef make_inst(float yaw_deg,
                     const glm::vec3& scale,
                     const glm::vec3& pos,
                     uint32_t model_id = 7) {
    InstanceDef d;
    d.model_id           = model_id;
    d.transform.position = pos;
    d.transform.scale    = scale;
    d.transform.set_euler_deg(/*pitch*/ 0.f, yaw_deg, /*roll*/ 0.f);
    return d;
}

// =============================================================================
// Round-trip tests
// =============================================================================

void test_identity_round_trip() {
    const char* name = "identity";
    InstanceDef want = make_inst(0.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_scale_only_round_trip() {
    const char* name = "scale_only";
    // Non-uniform scale stresses the per-axis path through the loader.
    InstanceDef want = make_inst(0.f, {2.f, 3.f, 1.5f}, {0.f, 0.f, 0.f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_yaw_45_round_trip() {
    const char* name = "yaw_45";
    InstanceDef want = make_inst(45.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_yaw_90_round_trip() {
    const char* name = "yaw_90";
    InstanceDef want = make_inst(90.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_yaw_neg135_round_trip() {
    const char* name = "yaw_-135";
    InstanceDef want = make_inst(-135.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_combined_round_trip() {
    const char* name = "combined";
    // The "everything" case: non-uniform scale, mid-rotation yaw, off-origin
    // position. Position chosen so %.4f writes it back exactly.
    InstanceDef want = make_inst(45.f,
                                 {2.5f, 1.25f, 3.0f},
                                 {64.0f, -0.05f, 32.0f});
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_lod_pair_sentinel_round_trip() {
    // sentinel 0xFFFFFFFFu is suppressed on save; loader's default-constructed
    // InstanceDef has 0xFFFFFFFFu, so the absence on disk should round-trip
    // back to the sentinel.
    const char* name = "lod_pair_sentinel";
    InstanceDef want = make_inst(0.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    want.lod_pair = 0xFFFFFFFFu;
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_lod_pair_real_value_round_trip() {
    // A real lod_pair must survive verbatim.
    const char* name = "lod_pair_42";
    InstanceDef want = make_inst(0.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    want.lod_pair = 42u;
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_uv_override_sentinel_round_trip() {
    // (0,0) sentinel is suppressed on save; loader default is also (0,0),
    // so round-trip is bit-perfect.
    const char* name = "uv_sentinel";
    InstanceDef want = make_inst(0.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    want.uv_scale_override = {0.f, 0.f};
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

void test_uv_override_real_value_round_trip() {
    const char* name = "uv_real";
    InstanceDef want = make_inst(0.f, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});
    want.uv_scale_override = {2.0f, 3.0f};
    InstanceDef got{};
    round_trip(want, name, got);
    expect_eq_full(got, want, name);
}

// =============================================================================
// AABB / Arvo collision sanity
// =============================================================================
//
// AABB::transform (src/scene/aabb.h:23-36) implements Arvo's method: project
// the box's center + extents through the matrix's rotation/scale columns.
// Everything physics-side that relies on instance bounds (WorldCollision
// queries, streamer culling) goes through this code. PBD-044's brief flags
// it explicitly as a regression target: if anyone "improves" AABB::transform
// (e.g. swaps abs() for a fast variant, or transposes a column), a 45°-yaw
// cube's bounding box will stop growing correctly and we want to catch that
// loudly.
//
// For a 1×1×1 cube centered at origin yaw'd 45°: each face's footprint on
// the XZ plane projects to a sqrt(2)-wide axis-aligned bounding box. Y is
// unaffected. We assert "grew to roughly sqrt(2)" with a generous tolerance
// — we're catching catastrophic regressions, not numerical drift.

void test_aabb_yaw_45_grows() {
    const char* name = "aabb_yaw_45";

    // 1×1×1 cube centered at origin.
    AABB local{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};

    Transform t;
    t.position = {0.f, 0.f, 0.f};
    t.scale    = {1.f, 1.f, 1.f};
    t.set_euler_deg(0.f, 45.f, 0.f);

    AABB world = local.transform(t.matrix());

    // Expected ~sqrt(2) ≈ 1.4142 on X and Z. Lower bound 1.4f keeps the
    // bar above "no growth at all" (which would be 1.0) and above any
    // plausibly broken Arvo variant (e.g. forgot the abs()).
    float x_width = world.max.x - world.min.x;
    float z_width = world.max.z - world.min.z;
    float y_width = world.max.y - world.min.y;

    if (x_width <= 1.4f || z_width <= 1.4f) {
        std::printf("FAIL [%s] expected X/Z width > 1.4 (sqrt(2)); got X=%.4f Z=%.4f\n",
                    name, x_width, z_width);
        std::exit(1);
    }
    // Sqrt(2) caps at ~1.4143; anything dramatically larger means we've
    // ballooned the box (e.g. summed without an abs cancellation).
    if (x_width > 1.5f || z_width > 1.5f) {
        std::printf("FAIL [%s] X/Z width grew past sqrt(2)+slack; got X=%.4f Z=%.4f\n",
                    name, x_width, z_width);
        std::exit(1);
    }
    // Y axis is the rotation axis: width must stay 1.0 within fp tolerance.
    if (std::abs(y_width - 1.0f) > 1e-4f) {
        std::printf("FAIL [%s] Y width should be unchanged at 1.0; got %.6f\n",
                    name, y_width);
        std::exit(1);
    }
}

void test_aabb_scale_yaw_combined_grows() {
    // A scaled + yaw'd box. 2×1×3 at origin, yaw 45°. The XZ corners after
    // rotation are at (±1, ±1.5) rotated 45° — the projected AABB grows on
    // both X and Z to (|1|+|1.5|) = 2.5 half-extent at 45° (the worst case
    // hits the corner). Y stays at 1.
    const char* name = "aabb_scale_yaw";

    AABB local{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};

    Transform t;
    t.position = {10.f, 0.f, -5.f};   // off-origin position must not affect extents
    t.scale    = {2.f, 1.f, 3.f};
    t.set_euler_deg(0.f, 45.f, 0.f);

    AABB world = local.transform(t.matrix());

    // Half-extent on X and Z at 45° = (2*cos45 + 3*sin45)/2 ≈ 1.7678,
    // so full width ≈ 3.5355. Lower bound 3.0 guards against a bad axis swap.
    float x_width = world.max.x - world.min.x;
    float z_width = world.max.z - world.min.z;
    if (x_width <= 3.0f || z_width <= 3.0f) {
        std::printf("FAIL [%s] expected X/Z width > 3.0; got X=%.4f Z=%.4f\n",
                    name, x_width, z_width);
        std::exit(1);
    }

    // Center should land at the transform's position (Arvo: m[3] column).
    glm::vec3 c = world.center();
    if (std::abs(c.x - 10.f) > 1e-4f ||
        std::abs(c.y -  0.f) > 1e-4f ||
        std::abs(c.z - -5.f) > 1e-4f) {
        std::printf("FAIL [%s] expected center near (10,0,-5); got (%.4f,%.4f,%.4f)\n",
                    name, c.x, c.y, c.z);
        std::exit(1);
    }
}

}  // namespace

int main() {
    // Quiet the loader's INFO/DEBUG chatter; let WARN/ERROR surface.
    pengine::log::min_level() = pengine::log::Level::Warn;

    test_identity_round_trip();
    test_scale_only_round_trip();
    test_yaw_45_round_trip();
    test_yaw_90_round_trip();
    test_yaw_neg135_round_trip();
    test_combined_round_trip();
    test_lod_pair_sentinel_round_trip();
    test_lod_pair_real_value_round_trip();
    test_uv_override_sentinel_round_trip();
    test_uv_override_real_value_round_trip();

    test_aabb_yaw_45_grows();
    test_aabb_scale_yaw_combined_grows();

    std::printf("map_builder_round_trip_tests: OK\n");
    return 0;
}

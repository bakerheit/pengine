// Headless save/load smoke test for the Map Editor persistence path
// (PBD-052). De-risks PBD-014 (CI) by exercising the
// Streamer::save_all_dirty_cells → on-disk IPL → fresh-Streamer-load
// round-trip *without* a GL context.
//
// Why headless matters: traffic_system_tests.cpp pulls in `Window` (and
// therefore SDL_Init + GL context creation) to upload meshes. CI headless
// runners can't easily provide that. PBD-052's job is to verify that the
// Map Builder's persistence pipeline — the value-side path that actually
// persists user edits — runs fine without any of that.
//
// GL-touch surface mapped during the premise check:
//   - Streamer::init                   — does NOT touch GL itself; safe with
//                                         `scene=nullptr`, `terrain_tex=nullptr`,
//                                         `road_graph=nullptr`, `collision=nullptr`.
//                                         Sets `cell_cache_` and starts the
//                                         background loader thread.
//   - Streamer::shutdown               — joins the thread. No GL.
//   - Streamer::pump                   — TOUCHES GL via `Mesh::upload` (terrain)
//                                         and the SceneNode upload path. NOT CALLED.
//   - Streamer::add_instance           — needs a resolved ModelDef with a
//                                         non-null Mesh (GL-bound via
//                                         `ModelRegistry::resolve_assets`).
//                                         NOT CALLED.
//   - Streamer::save_dirty_cell        — pure CPU (`save_ipl` writes text). Safe.
//   - Streamer::save_all_dirty_cells   — pure CPU. Safe.
//   - Background loader thread         — calls `load_or_generate_cell` which is
//                                         pure CPU. Safe. We set
//                                         `world_cells_x=world_cells_z=0` in
//                                         WorldConfig so its `desired` set is
//                                         always empty — the thread spins, sleeps,
//                                         and never enqueues a load.
//
// To exercise the save path without `pump`/`add_instance`, the test calls a
// PBD-052-added test-only seam, `Streamer::inject_loaded_cell_for_test`,
// which installs a `LoadedCell` containing just the InstanceDef vector with
// `dirty=true`. From there, `save_all_dirty_cells` writes through the same
// `cell_cache_` + `ipl_path_for_cell` + `save_ipl` codepath the production
// flow uses on cell evict. A second Streamer instance then `load_ipl`s the
// resulting file and we compare InstanceDef-for-InstanceDef.
//
// Synchronous-save assumption: this test relies on `save_all_dirty_cells`
// being synchronous (the file exists immediately after the call returns).
// That matches today's implementation. If Mira's PBD-048 makes saves async,
// this test will need to grow an explicit join / drain step before the
// `load_ipl`-back step. Documented inline at the call site below.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "test_assert.h"

#include "core/log.h"
#include "scene/transform.h"
#include "world/cell_coord.h"
#include "world/instance_def.h"
#include "world/ipl_loader.h"
#include "world/streamer.h"
#include "world/world_defs.h"

using namespace pengine;

namespace {

// =============================================================================
// Tolerances
// =============================================================================
//
// The on-disk IPL format writes position at %.4f, scale at %.4f, quaternion
// at %.6f. The reconstructed values can only ever be as precise as the
// printf format allowed. Same tolerances PBD-044 (round-trip tests) settled
// on — we mirror them so a regression in either test points at the same
// fix surface.

constexpr float kPosTol   = 1e-4f;
constexpr float kScaleTol = 1e-4f;
constexpr float kQuatTol  = 1e-5f;

// =============================================================================
// Helpers
// =============================================================================

// A WorldConfig safe for headless use: zero-sized world means the background
// loader thread's `desired` set is always empty, so it never enqueues a
// load_or_generate_cell job. The streamer still starts a thread (real
// lifecycle preserved) — it just spins idle until `shutdown` joins it.
WorldConfig headless_cfg() {
    WorldConfig cfg;
    cfg.cell_size     = 256.f;
    cfg.stream_radius = 0;
    cfg.world_cells_x = 0;     // no cell is "in world" → no loads enqueued
    cfg.world_cells_z = 0;
    cfg.max_uploads_per_frame = 1;
    return cfg;
}

// Per-case scratch directory under the OS tmp dir. Recreate-from-scratch each
// case so a leftover file from a previous run can't masquerade as a save.
std::filesystem::path make_scratch_dir(const char* tag) {
    auto dir = std::filesystem::temp_directory_path()
             / (std::string{"pbd052_"} + tag);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);   // best-effort wipe
    std::filesystem::create_directories(dir, ec);
    REQUIRE(!ec);
    return dir;
}

void cleanup_scratch_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    // Not fatal if cleanup fails — CI tmp policy may have moved files.
}

InstanceDef make_inst(uint32_t model_id,
                      const glm::vec3& pos,
                      float yaw_deg = 0.f,
                      const glm::vec3& scale = {1.f, 1.f, 1.f}) {
    InstanceDef d;
    d.model_id           = model_id;
    d.transform.position = pos;
    d.transform.scale    = scale;
    d.transform.set_euler_deg(0.f, yaw_deg, 0.f);
    return d;
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
    if (std::abs(a.x - b.x) > kQuatTol ||
        std::abs(a.y - b.y) > kQuatTol ||
        std::abs(a.z - b.z) > kQuatTol ||
        std::abs(a.w - b.w) > kQuatTol) {
        std::printf("FAIL [%s] quat: got (%.7f,%.7f,%.7f,%.7f) want (%.7f,%.7f,%.7f,%.7f)\n",
                    case_name, a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
        std::exit(1);
    }
}

void expect_instance_eq(const InstanceDef& got, const InstanceDef& want,
                        const char* case_name) {
    if (got.model_id != want.model_id) {
        std::printf("FAIL [%s] model_id: got %u want %u\n",
                    case_name, got.model_id, want.model_id);
        std::exit(1);
    }
    expect_pos_eq  (got.transform.position, want.transform.position, case_name);
    expect_scale_eq(got.transform.scale,    want.transform.scale,    case_name);
    expect_quat_eq (got.transform.rotation, want.transform.rotation, case_name);
}

// Save `instances` for `cell` through a real Streamer's save path, then
// reload them via `load_ipl` from `cache_dir`. The streamer is init'd with
// all-null subsystems and a zero-sized world (see `headless_cfg`) so the
// background loader thread can't enqueue any work.
//
// `expected_file_exists`: whether `cell_<x>_<z>.ipl` should be present on
// disk after the save (true for non-empty saves; the loader will refuse a
// missing file in the empty case and we rely on the streamer to write
// nothing at all there, see test_empty_cell_round_trip).
void save_via_streamer(const std::filesystem::path& cache_dir,
                       CellCoord cell,
                       std::vector<InstanceDef> instances) {
    Streamer s;
    s.init(headless_cfg(), /*scene*/ nullptr, /*collision*/ nullptr,
           /*registry*/ nullptr, /*terrain_tex*/ nullptr,
           /*road_graph*/ nullptr, cache_dir);
    s.inject_loaded_cell_for_test(cell, std::move(instances));
    // PBD-048 watchpoint: `save_all_dirty_cells` is synchronous today. If it
    // becomes async (sync-then-join, or fully async with a barrier), this
    // test needs a barrier here before the subsequent `load_ipl`.
    s.save_all_dirty_cells();
    s.shutdown();
}

bool load_via_ipl(const std::filesystem::path& cache_dir,
                  CellCoord cell,
                  std::vector<InstanceDef>& out) {
    auto path = ipl_path_for_cell(cache_dir, cell);
    return load_ipl(path, out);
}

// =============================================================================
// Test cases
// =============================================================================

// Case 1: an empty cell. The streamer marks an instance-less cell dirty
// (e.g. user deleted the last instance), saves, and on reload we should see
// zero instances. This pins down "empty IPL is valid" and "the loader copes
// with a file containing no records."
void test_empty_cell_round_trip() {
    const char* name = "empty";
    auto dir = make_scratch_dir(name);
    CellCoord cell{0, 0};

    save_via_streamer(dir, cell, /*instances*/ {});

    auto path = ipl_path_for_cell(dir, cell);
    // `save_ipl` writes the header even for an empty vector, so the file
    // should exist. (If a future change suppresses the empty file, the
    // load below would also have to special-case absence; we'd update this
    // assertion together with that change.)
    FATAL_IF(std::filesystem::exists(path),
             "expected IPL file to exist after empty save", name);

    std::vector<InstanceDef> loaded;
    bool ok = load_via_ipl(dir, cell, loaded);
    FATAL_IF(ok, "load_ipl failed on empty-cell file", name);
    if (!loaded.empty()) {
        std::printf("FAIL [%s] expected 0 instances, got %zu\n",
                    name, loaded.size());
        std::exit(1);
    }

    cleanup_scratch_dir(dir);
}

// Case 2: a single building placement. The canonical happy-path the Map
// Builder hits when the user plops one cube and exits.
void test_single_instance_round_trip() {
    const char* name = "single";
    auto dir = make_scratch_dir(name);
    CellCoord cell{1, 2};

    InstanceDef inst = make_inst(/*model_id*/ 11,
                                  {32.0f, 0.5f, 64.0f},
                                  /*yaw_deg*/ 0.f);
    std::vector<InstanceDef> want{inst};

    save_via_streamer(dir, cell, want);

    std::vector<InstanceDef> got;
    bool ok = load_via_ipl(dir, cell, got);
    FATAL_IF(ok, "load_ipl failed on single-instance file", name);
    FATAL_IF(got.size() == 1, "expected exactly 1 loaded instance", name);

    expect_instance_eq(got.front(), inst, name);

    cleanup_scratch_dir(dir);
}

// Case 3: three instances, non-identity transforms, across an "evict"-style
// flow. Mimics the production sequence: user places three buildings, camera
// pans, the cell is about to evict, `save_dirty_cell` runs directly,
// teardown happens, fresh Streamer comes up later and `load_ipl` returns
// the same three records with positions / rotations / scales intact.
void test_multi_instance_evict_round_trip() {
    const char* name = "multi_evict";
    auto dir = make_scratch_dir(name);
    CellCoord cell{3, -1};

    std::vector<InstanceDef> want = {
        make_inst(/*model_id*/ 11, { 12.5f,  0.0f,   8.0f},  45.f,
                  /*scale*/ {2.0f, 1.5f, 2.0f}),
        make_inst(/*model_id*/ 12, {-16.0f,  4.0f,  -2.5f}, -90.f,
                  /*scale*/ {1.0f, 1.0f, 1.0f}),
        make_inst(/*model_id*/ 20, { 64.0f, -0.05f, 32.0f},   0.f,
                  /*scale*/ {8.0f, 0.1f, 256.0f}),  // road slab proportions
    };

    // Drive the "evict" path explicitly via `save_dirty_cell` (the production
    // entry point inside `Streamer::pump`'s evict loop), not just the
    // exit-from-MapBuilder bulk save. Both end up in the same writer, but
    // the per-cell call is the one that fires every camera pan and is
    // therefore the hotter path.
    {
        Streamer s;
        s.init(headless_cfg(), /*scene*/ nullptr, /*collision*/ nullptr,
               /*registry*/ nullptr, /*terrain_tex*/ nullptr,
               /*road_graph*/ nullptr, dir);
        s.inject_loaded_cell_for_test(cell, want);
        s.save_dirty_cell(cell);
        s.shutdown();
    }

    // Construct a fresh Streamer pointed at the same cache_dir. We don't
    // need its state, just want to prove "fresh process, same directory"
    // produces a readable load. The actual read goes through `load_ipl` so
    // the assertion targets the file format, not the streamer's loader
    // thread (which is intentionally idle in headless mode).
    {
        Streamer s2;
        s2.init(headless_cfg(), nullptr, nullptr, nullptr, nullptr, nullptr, dir);
        // No mutation — just lifecycle. The file must have survived.
        s2.shutdown();
    }

    std::vector<InstanceDef> got;
    bool ok = load_via_ipl(dir, cell, got);
    FATAL_IF(ok, "load_ipl failed on multi-instance file", name);
    if (got.size() != want.size()) {
        std::printf("FAIL [%s] expected %zu instances, got %zu\n",
                    name, want.size(), got.size());
        std::exit(1);
    }

    for (std::size_t i = 0; i < want.size(); ++i) {
        expect_instance_eq(got[i], want[i], name);
    }

    cleanup_scratch_dir(dir);
}

}  // namespace

int main() {
    // Quiet the loader's INFO/DEBUG chatter; let WARN/ERROR surface. Same
    // policy as map_builder_round_trip_tests so failure logs stay readable.
    pengine::log::min_level() = pengine::log::Level::Warn;

    test_empty_cell_round_trip();
    test_single_instance_round_trip();
    test_multi_instance_evict_round_trip();

    std::printf("map_editor_persistence_tests: OK\n");
    return 0;
}

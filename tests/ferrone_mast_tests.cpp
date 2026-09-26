// The Ferrone Mast: that it stands on the summit the map says, on a pad that
// meets the real terrain, with its obstruction lights on the sim clock.
//
// Everything here runs against the real producers — the real TerrainGround
// for kMapSeed, the real pad bake, the real scatter density — because the
// failure this exists to catch is a hilltop that pokes up through the gravel,
// or a pad that floats off the slope, and a hand-built flat ground would
// happily pass both. The cooked tower mesh is checked outside ctest by
// tools/validate_ferrone_mast.py; its LOADER is exercised here against a
// synthetic asset tree so the suite needs no cooked files.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "city/ferrone_mast.h"
#include "city/ferrone_mast_asset.h"
#include "city/landmarks.h"
#include "city/map.h"
#include "test_assert.h"

using namespace apricot;

namespace {

namespace fs = std::filesystem;

void mast_is_the_landmark() {
    const auto& lm = city::kFerroneMastLandmark;
    REQUIRE(std::strcmp(lm.name, "Ferrone Mast") == 0);
    REQUIRE(lm.tier == city::LandmarkTier::Island);
    REQUIRE_NEAR(city::kFerroneMastSite.origin.x, lm.pos.x, 1e-4f);
    REQUIRE_NEAR(city::kFerroneMastSite.origin.z, lm.pos.z, 1e-4f);
    REQUIRE_NEAR(city::kFerroneMastHeightM, lm.height_m, 1e-4f);
    // The tower's footprint (7.2 m legs plus 0.55 m piers) must sit inside
    // the pad with room for the fence line.
    const float base = 3.6f + 0.55f;
    REQUIRE(city::kFerroneMastPadMin.x < -base - 1.0f);
    REQUIRE(city::kFerroneMastPadMax.x > base + 1.0f);
    REQUIRE(city::kFerroneMastPadMin.z < -base - 1.0f);
    REQUIRE(city::kFerroneMastPadMax.z > base + 1.0f);
    apricot_test::pass("the mast is kLandmarks' Ferrone Mast row, footprint inside its pad");
}

void pad_meets_the_real_summit() {
    const TerrainGround ground{city::kMapSeed};
    const auto g = ground.sampler();
    const float top = city::ferrone_mast_pad_top(g);
    const auto parts = city::bake_ferrone_mast_pad(g);
    REQUIRE(!parts.empty());

    // No part of the drawn hilltop may come up through the gravel, checked at
    // four times the density the bake sampled at.
    const auto& s = city::kFerroneMastSite;
    float lowest = 1e9f, highest = -1e9f;
    for (float x = city::kFerroneMastPadMin.x; x <= city::kFerroneMastPadMax.x + 1e-3f;
         x += 0.125f)
        for (float z = city::kFerroneMastPadMin.z; z <= city::kFerroneMastPadMax.z + 1e-3f;
             z += 0.125f) {
            const float y = g.at(s.origin.x + x, s.origin.z + z);
            lowest = std::min(lowest, y);
            highest = std::max(highest, y);
            REQUIRE_MSG(y < top - 0.05f, "terrain pokes through the mast pad", "pad top");
        }

    const city::StartPart* wall = nullptr;
    const city::StartPart* gravel = nullptr;
    for (const auto& p : parts) {
        REQUIRE(p.name != nullptr);
        REQUIRE(p.solid);
        REQUIRE(p.width_m > 0 && p.height_m > 0 && p.depth_m > 0);
        REQUIRE_MSG(p.centre.x - p.width_m * .5f >= city::kFerroneMastPadMin.x - 1e-3f &&
                        p.centre.x + p.width_m * .5f <= city::kFerroneMastPadMax.x + 1e-3f &&
                        p.centre.z - p.depth_m * .5f >= city::kFerroneMastPadMin.z - 1e-3f &&
                        p.centre.z + p.depth_m * .5f <= city::kFerroneMastPadMax.z + 1e-3f,
                    "pad piece leaves the lot", p.name);
        if (std::strcmp(p.name, "ferrone mast pad retaining wall") == 0) wall = &p;
        if (std::strcmp(p.name, "ferrone mast pad gravel") == 0) gravel = &p;
    }
    REQUIRE(wall && gravel);
    // The retaining block reaches below the lowest ground (no floating), and
    // it meets the gravel with no gap.
    REQUIRE_MSG(wall->bottom_m < lowest - 0.3f, "mast pad floats off the slope", wall->name);
    REQUIRE_NEAR(wall->bottom_m + wall->height_m, gravel->bottom_m, 1e-3f);
    REQUIRE_NEAR(gravel->bottom_m + gravel->height_m, top, 1e-3f);

    // It is the SUMMIT: nothing within 80 m stands higher than the pad, so
    // the mast is the top of the hill rather than on its flank.
    for (float a = 0.0f; a < 6.2832f; a += 0.1f)
        for (float r = 5.0f; r <= 80.0f; r += 5.0f) {
            const float y = g.at(s.origin.x + r * std::cos(a), s.origin.z + r * std::sin(a));
            REQUIRE_MSG(y < top, "ground near the mast stands higher than its pad",
                        "summit");
        }
    // And it is island-tier by altitude: the design asks for the mast to top
    // everything, and tests/city_map_tests.cpp asserts the 150 m floor on the
    // raw landmark; this is the same check on the ground it is really built on.
    REQUIRE(top + city::kFerroneMastHeightM > 150.0f);
    std::printf("  pad top %.2f m, lowest ground %.2f m, walls up to %.2f m, tip %.2f m ASL\n",
                static_cast<double>(top), static_cast<double>(lowest),
                static_cast<double>(top - lowest),
                static_cast<double>(top + city::kFerroneMastHeightM));
    apricot_test::pass("the pad clears the real summit and reaches the lowest ground");
}

void pad_is_deterministic() {
    const TerrainGround ground{city::kMapSeed};
    const auto a = city::bake_ferrone_mast_pad(ground.sampler());
    const auto b = city::bake_ferrone_mast_pad(ground.sampler());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE(a[i].bottom_m == b[i].bottom_m);
        REQUIRE(a[i].height_m == b[i].height_m);
    }
    apricot_test::pass("the pad bake is a pure function of the terrain");
}

void compound_keeps_scatter_out() {
    const auto& s = city::kFerroneMastSite;
    for (float x = city::kFerroneMastPadMin.x - 5.0f; x <= city::kFerroneMastPadMax.x + 5.0f;
         x += 1.0f)
        for (float z = city::kFerroneMastPadMin.z - 5.0f;
             z <= city::kFerroneMastPadMax.z + 5.0f; z += 1.0f)
            REQUIRE_NEAR(city::wild_scatter_at(s.origin.x + x, s.origin.z + z), 0.0f, 1e-6f);
    // ...and only there: a point well off the pad is back to district density.
    REQUIRE(city::ferrone_mast_lot_contains(s.origin.x, s.origin.z));
    REQUIRE(!city::ferrone_mast_lot_contains(s.origin.x + 40.0f, s.origin.z));
    apricot_test::pass("no tree or boulder is scattered onto the compound");
}

void beacon_flashes_thirty_a_minute() {
    // Count rising edges over a minute of sim time at the fixed step.
    const float dt = 1.0f / 120.0f;
    int flashes = 0;
    float lit = 0.0f;
    bool was_on = false;
    for (int i = 0; i < 120 * 60; ++i) {
        const float level = city::ferrone_mast_beacon_level(static_cast<float>(i) * dt);
        REQUIRE(level >= 0.0f && level <= 1.0f);
        const bool on = level > 0.5f;
        flashes += on && !was_on;
        was_on = on;
        lit += level * dt;
    }
    REQUIRE_MSG(flashes >= 20 && flashes <= 40, "an L-864 flashes 20-40 times a minute",
                "beacon rate");
    REQUIRE(flashes == 30);
    // On for most of 0.8 s in every 2 s, give or take the soft edges.
    REQUIRE(lit > 60.0f * 0.35f && lit < 60.0f * 0.42f);
    // The clock, not the frame: same second, same light; negative time is
    // still a valid phase rather than a dark beacon.
    REQUIRE(city::ferrone_mast_beacon_level(1234.4f) ==
            city::ferrone_mast_beacon_level(1234.4f));
    REQUIRE(city::ferrone_mast_beacon_level(-1.6f) > 0.9f);
    REQUIRE(city::ferrone_mast_beacon_level(0.4f) == 1.0f);
    REQUIRE(city::ferrone_mast_beacon_level(1.2f) == 0.0f);
    apricot_test::pass("the L-864 beacon flashes 30 a minute on the sim clock");
}

void glow_holds_its_size_on_screen() {
    // Never smaller than the lens; beyond that, proportional to distance, so
    // the angular size — and so the pixel size — is constant.
    REQUIRE_NEAR(city::ferrone_mast_glow_diameter(0.0f), city::kFerroneMastGlowMinDiameterM,
                 1e-6f);
    const float a = city::ferrone_mast_glow_diameter(1000.0f) / 1000.0f;
    const float b = city::ferrone_mast_glow_diameter(2400.0f) / 2400.0f;
    REQUIRE_NEAR(a, b, 1e-6f);
    // ~3 px at 1080p across a 60 degree field of view.
    const float px = a / (1.0472f / 1080.0f);
    REQUIRE(px > 2.5f && px < 4.0f);
    // The near set outlives the far one's arrival, so no frame shows neither.
    REQUIRE(city::kFerroneMastNearToM > city::kFerroneMastFarFromM);
    apricot_test::pass("glow spheres hold a few pixels at any range; near/far overlap");
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream(p) << text;
}

void loader_reads_the_cooked_layout() {
    const fs::path assets = fs::temp_directory_path() / "apricot_ferrone_mast_tests";
    const fs::path dir = assets / city::kFerroneMastAssetRoot;
    fs::remove_all(assets);
    fs::create_directories(dir);
    write(dir / "materials.txt",
          "part_00.emesh - 0 0 0 1 0.88 0.33 0.10 1.0\n"
          "part_01.emesh far-lattice.png 0 0 1 2 1 1 1 1\n"
          "part_02.emesh - 0 2 0 0 0.95 0.1 0.06 1\n");
    write(dir / "glow.txt", "part_03.emesh - 0 0 0 0 1 0.16 0.08 1\n");
    write(dir / "collision.txt", "0 1 0 0.5 1 0.5\n3.6 0.2 3.6 0.55 0.3 0.55\n");
    write(dir / "lights.txt", "0 59.2 0 2\n2.5 29 2.5 1\n-1.3 2.8 11.2 3\n");

    city::FerroneMastAsset a;
    REQUIRE(city::load_ferrone_mast_asset(a));
    REQUIRE(a.materials.size() == 3);
    REQUIRE(a.materials[0].lod == city::FerroneMastLod::Near);
    REQUIRE(a.materials[1].alpha && a.materials[1].lod == city::FerroneMastLod::Far);
    REQUIRE(a.materials[1].texture == "far-lattice.png");
    REQUIRE(a.materials[2].glow == city::FerroneMastGlow::FlashingRed);
    REQUIRE(a.glow_sphere.mesh == "part_03.emesh");
    REQUIRE(a.boxes.size() == 2);
    REQUIRE_NEAR(a.boxes[1].half.x, 0.55f, 1e-6f);
    REQUIRE(a.lights.size() == 3);
    REQUIRE(a.lights[2].glow == city::FerroneMastGlow::WarmLamp);

    // A texture that climbs out of the asset directory is refused whole.
    write(dir / "materials.txt", "part_00.emesh ../../../secret.png 0 0 0 1 1 1 1 1\n");
    REQUIRE(!city::load_ferrone_mast_asset(a));
    // So is a draw set nobody handles.
    write(dir / "materials.txt", "part_00.emesh - 0 0 0 7 1 1 1 1\n");
    REQUIRE(!city::load_ferrone_mast_asset(a));
    // And a missing file is a clean false, which World turns into a warning.
    fs::remove(dir / "lights.txt");
    write(dir / "materials.txt", "part_00.emesh - 0 0 0 1 1 1 1 1\n");
    REQUIRE(!city::load_ferrone_mast_asset(a));
    fs::remove_all(assets);
    apricot_test::pass("the loader reads the cooked layout and refuses bad rows");
}

}  // namespace

int main() {
    // Before anything resolves the asset root, which caches for the process.
    const fs::path assets = fs::temp_directory_path() / "apricot_ferrone_mast_tests";
#if defined(_WIN32)
    _putenv_s("APRICOT_ASSETS", assets.string().c_str());
#else
    setenv("APRICOT_ASSETS", assets.string().c_str(), 1);
#endif
    mast_is_the_landmark();
    pad_meets_the_real_summit();
    pad_is_deterministic();
    compound_keeps_scatter_out();
    beacon_flashes_thirty_a_minute();
    glow_holds_its_size_on_screen();
    loader_reads_the_cooked_layout();
    return apricot_test::done("ferrone_mast_tests");
}

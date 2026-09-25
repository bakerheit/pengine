#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "city/landmarks.h"
#include "city/start_area.h"
#include "road/road_graph.h"

namespace apricot::city {

// THE FERRONE MAST — WFRN 97.3's transmitter on the summit of Ferrone Hill,
// and the island-tier landmark docs/design/pinatty.md section 3 has asked for
// since the map was drawn: "from the mast you can see every other landmark".
// For a long time it was a row in kLandmarks and nothing else, so the hill the
// whole north of the island is navigated by had nothing on top of it.
//
// Two halves, and the split is the same one Tidewater Farm and the imported gas
// stations make. The PAD — a concrete hilltop platform with retaining walls —
// is baked here from the sampled terrain, so it always meets the real ground
// and always exists. The TOWER and everything standing on the pad is a cooked
// mesh from tools/ferrone_mast_blender.py (see ferrone_mast_asset.h), built
// procedurally in Blender because a lattice of several hundred angle-iron
// members is not a box table.
//
// WHERE IT STANDS, AND WHY IT IS NOT A FREE CHOICE. The summit is a terraced
// knob: the Bench operators leave a plateau at ~121.5 m about 12 m by 18 m,
// with 8-10 m steps down to a ~112 m bench (the Mast Track has since graded
// the east flank up to meet the gate). The tower centre is
// the middle of that plateau, and it is read from kLandmarks rather than
// repeated here — the landmark row used to say (600, -1780), which is the lip
// of a step, and a second copy of the position is how the two would have come
// apart again.
inline constexpr const Landmark& kFerroneMastLandmark = kLandmarks[0];
static_assert(kLandmarks[0].kind == LandmarkKind::Mast,
              "kLandmarks[0] is no longer the Ferrone Mast; point "
              "kFerroneMastLandmark at the row that is");

// Site-local rectangle of the pad about the tower centre (engine x east, z
// south). The tower's 7.2 m base sits at the north end, the equipment shelter
// at the south end facing the city. tools/ferrone_mast_blender.py mirrors
// these as PAD_X / PAD_Y and lays the fence out inside them.
inline constexpr Vec2 kFerroneMastPadMin{-7.0f, -7.0f};
inline constexpr Vec2 kFerroneMastPadMax{7.0f, 15.0f};

// The pad top clears the highest sampled ground by this much, and its
// retaining walls reach this far below the lowest. Sampled at kFerroneMast-
// SampleStepM, finer than the terrain mesh's own spacing, so no vertex of the
// drawn hilltop can poke up through the gravel between two samples.
inline constexpr float kFerroneMastPadClearanceM = 0.30f;
inline constexpr float kFerroneMastFootingM = 0.60f;
inline constexpr float kFerroneMastSampleStepM = 0.5f;
inline constexpr float kFerroneMastGravelM = 0.18f;
inline constexpr float kFerroneMastCurbM = 0.22f;

// The whole structure, pad top to lightning-rod tip. The cooked manifest
// states the same number, and the landmark row is where it is authored.
inline constexpr float kFerroneMastHeightM = kLandmarks[0].height_m;

// ground_m is zero: the pad pieces carry absolute heights, like the farm's
// measured foundations. The cooked mesh is placed against a copy of this site
// whose ground_m is the pad top (ferrone_mast_mesh_site()).
inline constexpr StartSite kFerroneMastSite{
    "Ferrone Mast",
    {kLandmarks[0].pos.x, kLandmarks[0].pos.z},
    1.0f, 0.0f,
    {(kFerroneMastPadMin.x + kFerroneMastPadMax.x) * 0.5f,
     (kFerroneMastPadMin.z + kFerroneMastPadMax.z) * 0.5f},
    kFerroneMastPadMax.x - kFerroneMastPadMin.x,
    kFerroneMastPadMax.z - kFerroneMastPadMin.z,
    0.0f,
    // A landmark: never cut short by the per-site limit. The global draw
    // distance and the near/far swap in World decide what is submitted.
    2400.0f};

inline bool ferrone_mast_lot_contains(float world_x, float world_z,
                                      float margin_m = 0.0f) {
    const float x = world_x - kFerroneMastSite.origin.x;
    const float z = world_z - kFerroneMastSite.origin.z;
    return x >= kFerroneMastPadMin.x - margin_m &&
           x <= kFerroneMastPadMax.x + margin_m &&
           z >= kFerroneMastPadMin.z - margin_m &&
           z <= kFerroneMastPadMax.z + margin_m;
}

struct FerroneMastGround {
    float lowest = 0.0f;
    float highest = 0.0f;
};

// Every sample over the pad rectangle, edges included.
inline FerroneMastGround ferrone_mast_ground(GroundSampler ground) {
    FerroneMastGround out{1e9f, -1e9f};
    const auto& s = kFerroneMastSite;
    const int nx = static_cast<int>(std::ceil(
        (kFerroneMastPadMax.x - kFerroneMastPadMin.x) / kFerroneMastSampleStepM));
    const int nz = static_cast<int>(std::ceil(
        (kFerroneMastPadMax.z - kFerroneMastPadMin.z) / kFerroneMastSampleStepM));
    for (int i = 0; i <= nx; ++i)
        for (int k = 0; k <= nz; ++k) {
            const float x = std::min(kFerroneMastPadMax.x,
                kFerroneMastPadMin.x + static_cast<float>(i) * kFerroneMastSampleStepM);
            const float z = std::min(kFerroneMastPadMax.z,
                kFerroneMastPadMin.z + static_cast<float>(k) * kFerroneMastSampleStepM);
            const float y = ground.at(s.origin.x + x, s.origin.z + z);
            out.lowest = std::min(out.lowest, y);
            out.highest = std::max(out.highest, y);
        }
    return out;
}

inline float ferrone_mast_pad_top(GroundSampler ground) {
    return ferrone_mast_ground(ground).highest + kFerroneMastPadClearanceM;
}

// The site the cooked mesh is placed against: same origin and yaw, with the
// pad top as its ground so the mesh's local y = 0 lands on the gravel.
inline StartSite ferrone_mast_mesh_site(GroundSampler ground) {
    StartSite s = kFerroneMastSite;
    s.ground_m = ferrone_mast_pad_top(ground);
    return s;
}

// The hilltop platform: one retaining block from below the lowest ground to
// just under the pad top, a gravel wearing course, and a curb round the rim.
// All solid; heights absolute (kFerroneMastSite.ground_m is zero).
inline std::vector<StartPart> bake_ferrone_mast_pad(GroundSampler ground) {
    const FerroneMastGround g = ferrone_mast_ground(ground);
    const float top = g.highest + kFerroneMastPadClearanceM;
    const float bottom = g.lowest - kFerroneMastFootingM;
    const float cx = (kFerroneMastPadMin.x + kFerroneMastPadMax.x) * 0.5f;
    const float cz = (kFerroneMastPadMin.z + kFerroneMastPadMax.z) * 0.5f;
    const float w = kFerroneMastPadMax.x - kFerroneMastPadMin.x;
    const float d = kFerroneMastPadMax.z - kFerroneMastPadMin.z;
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float b, float pw,
                         float ph, float pd, StartFinish finish) {
        out.push_back(StartPart{name, {x, z}, b, pw, ph, pd, finish, true});
    };
    add("ferrone mast pad retaining wall", cx, cz, bottom, w,
        top - kFerroneMastGravelM - bottom, d, StartFinish::Concrete);
    add("ferrone mast pad gravel", cx, cz, top - kFerroneMastGravelM, w,
        kFerroneMastGravelM, d, StartFinish::Asphalt);
    const float c = 0.30f;
    add("ferrone mast pad curb north", cx, kFerroneMastPadMin.z + c * 0.5f, top,
        w, kFerroneMastCurbM, c, StartFinish::Concrete);
    add("ferrone mast pad curb south", cx, kFerroneMastPadMax.z - c * 0.5f, top,
        w, kFerroneMastCurbM, c, StartFinish::Concrete);
    add("ferrone mast pad curb west", kFerroneMastPadMin.x + c * 0.5f, cz, top,
        c, kFerroneMastCurbM, d - 2.0f * c, StartFinish::Concrete);
    add("ferrone mast pad curb east", kFerroneMastPadMax.x - c * 0.5f, cz, top,
        c, kFerroneMastCurbM, d - 2.0f * c, StartFinish::Concrete);
    return out;
}

// ---------------------------------------------------------------------------
//  Obstruction lighting
// ---------------------------------------------------------------------------

// The top beacon is an L-864: red, flashing, 20-40 flashes a minute. This one
// runs at 30 (a 2 s cycle) with the lamp on for 0.8 s, and it is an LED unit,
// so the edges are quick but not a hard pop. Driven by the SIM clock, never
// the wall clock: the same second of a replay shows the same flash.
inline constexpr float kFerroneMastFlashPeriodS = 2.0f;
inline constexpr float kFerroneMastFlashOnS = 0.8f;
inline constexpr float kFerroneMastFlashRampS = 0.06f;

inline float ferrone_mast_beacon_level(float sim_seconds) {
    float t = std::fmod(sim_seconds, kFerroneMastFlashPeriodS);
    if (t < 0.0f) t += kFerroneMastFlashPeriodS;
    if (t >= kFerroneMastFlashOnS) return 0.0f;
    const float up = t / kFerroneMastFlashRampS;
    const float down = (kFerroneMastFlashOnS - t) / kFerroneMastFlashRampS;
    return std::clamp(std::min(up, down), 0.0f, 1.0f);
}

// The renderer has no bloom, so a 30 cm lens is a fraction of a pixel from a
// kilometre away and an obstruction light that cannot be seen is not doing its
// one job — nor the landmark's, which at night is ALL the mast is. Each light
// gets a glow sphere sized to hold roughly three pixels at 1080p and a 60
// degree field of view however far the camera is, and never smaller than the
// lens it sits on.
inline constexpr float kFerroneMastGlowRadiansAcross = 0.0028f;
inline constexpr float kFerroneMastGlowMinDiameterM = 0.45f;

inline float ferrone_mast_glow_diameter(float distance_m) {
    return std::max(kFerroneMastGlowMinDiameterM,
                    distance_m * kFerroneMastGlowRadiansAcross);
}

// Structural members are 6-24 cm across, under a pixel by ~600 m. Beyond this
// the lattice is drawn as the textured far silhouette instead of steel. The
// near set's cut-off sits a little further out, so the two overlap across a
// band rather than leaving a frame with neither.
inline constexpr float kFerroneMastFarFromM = 520.0f;
inline constexpr float kFerroneMastNearToM = 560.0f;

}  // namespace apricot::city

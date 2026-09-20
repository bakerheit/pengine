#pragma once

#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "city/start_area.h"
#include "core/asset_root.h"
#include "physics/terrain_collider.h"

namespace apricot::city {

// Three of the four supplied kyjhi.psx.payphones models: the enclosed booth
// (phonebooth.fbx), the open pedestal payphone (payhpone.fbx) and the small
// wall-mount handset (phone.fbx). Cooked by tools/cook_kyjhi_payphones.py
// into a single mesh/material pair plus a footprint box apiece; see
// docs/assets/kyjhi-payphones.md. All three share the same on-disk layout,
// so one loader and one placement pair serve all of them.
inline constexpr const char* kKyjhiPhoneboothAssetRoot = "models/props/kyjhi_phonebooth/";
inline constexpr const char* kKyjhiPayphoneAssetRoot = "models/props/kyjhi_payphone/";
inline constexpr const char* kKyjhiWallPhoneAssetRoot = "models/props/kyjhi_wall_phone/";

struct KyjhiPropMaterial {
    std::string mesh, texture;
};
struct KyjhiPropBox {
    glm::vec3 centre{}, half{};
};
struct KyjhiPropAsset {
    std::vector<KyjhiPropMaterial> materials;
    KyjhiPropBox box;
};

inline std::string kyjhi_prop_path(const std::string& filename, const char* root) {
    return asset_path(std::string(root) + filename);
}

inline bool load_kyjhi_prop_asset(KyjhiPropAsset& out, const char* root) {
    KyjhiPropAsset asset;
    std::ifstream materials(kyjhi_prop_path("materials.txt", root));
    KyjhiPropMaterial m;
    while (materials >> m.mesh >> m.texture) {
        if (m.mesh.find('/') != std::string::npos || m.mesh.find("..") != std::string::npos ||
            m.texture.find('/') != std::string::npos || m.texture.find("..") != std::string::npos)
            return false;
        asset.materials.push_back(m);
    }
    if (!materials.eof() || asset.materials.empty()) return false;
    std::ifstream collision(kyjhi_prop_path("collision.txt", root));
    KyjhiPropBox b;
    if (!(collision >> b.centre.x >> b.centre.y >> b.centre.z >> b.half.x >> b.half.y >> b.half.z))
        return false;
    if (!(b.half.x > 0 && b.half.y > 0 && b.half.z > 0)) return false;
    asset.box = b;
    out = std::move(asset);
    return true;
}

// One phonebooth per district: a Corner-tier fixture you use to say "meet me
// by the payphone", placed near an already-measured landmark or building site
// so the ground height is real terrain, not a guess. Coordinates and ground_m
// were sampled against kMapSeed with a standalone probe against
// TerrainGround; see docs/assets/kyjhi-payphones.md for how each was chosen.
// Halloway Gas's west wall gets the two uncovered models instead (below), not
// a second enclosed booth on top of the district set.
inline constexpr std::array<StartSite, 10> kKyjhiPhoneboothSites{{
    {"Payphone: Pinatty Row", {40.0f, -95.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 12.000f},
    {"Payphone: Halloway Square", {335.0f, 895.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 13.820f},
    {"Payphone: Saltmarsh", {-980.0f, 40.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 5.500f},
    {"Payphone: Ostend Docks", {-2020.0f, -580.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 3.592f},
    {"Payphone: Kepler Flats", {-245.0f, -1850.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 9.000f},
    {"Payphone: Ferrone Hill", {400.0f, -1700.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 40.692f},
    {"Payphone: Nickel Heights", {1150.0f, 270.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 12.000f},
    {"Payphone: The Strand", {2040.0f, 260.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 11.518f},
    {"Payphone: Camber Point", {165.0f, 2120.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 6.000f},
    {"Payphone: Marrow", {-1080.0f, 1150.0f}, 1.0f, 0.0f, {0, 0}, 1, 1, 35.568f},
}};

// The two models without an enclosure ("without the covers") stand on
// Halloway Gas's west wall (kGasStoreWalls "store west wall", local x=-5.0,
// z -18.7..-7.7, no door or window there to clip) -- these REPLACE what was
// briefly two enclosed booths in the same two spots. Both transformed
// through kGasStationSite's own origin/yaw so they sit flush with the
// building rather than world-axis aligned; ground_m matches the site's
// default (kStartAreaGroundM).
inline constexpr std::array<StartSite, 1> kKyjhiPayphoneSites{{
    // Freestanding pedestal, same 0.9 m clearance and footprint the booth
    // that stood here used, at the wall's south spot (local x=-5.9, z=-10.5).
    {"Payphone pedestal: Halloway Gas west", {13.2299f, -23.0592f}, kGridCos, kGridSin,
     {0, 0}, 1, 1, kStartAreaGroundM},
}};
inline constexpr std::array<StartSite, 1> kKyjhiWallPhoneSites{{
    // Flush-mounted, so it sits closer to the wall face than the pedestal
    // (local x=-5.35, only 0.2 m clear -- its own footprint is much
    // shallower) at the wall's north spot (z=-16.0).
    {"Wall phone: Halloway Gas west", {14.3518f, -28.4716f}, kGridCos, kGridSin,
     {0, 0}, 1, 1, kStartAreaGroundM},
}};

inline glm::vec3 kyjhi_prop_world(glm::vec3 p, const StartSite& s) {
    return {s.origin.x + s.cos_yaw * p.x + s.sin_yaw * p.z, s.ground_m + p.y,
            s.origin.z - s.sin_yaw * p.x + s.cos_yaw * p.z};
}

inline void add_kyjhi_prop_collision(TerrainCollider& collider, const KyjhiPropAsset& asset,
                                      const StartSite& s) {
    const float yaw = std::atan2(s.sin_yaw, s.cos_yaw);
    collider.add_static_oriented_box(kyjhi_prop_world(asset.box.centre, s), asset.box.half, yaw);
}

}  // namespace apricot::city

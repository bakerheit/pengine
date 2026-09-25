#pragma once

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "city/ferrone_mast.h"
#include "core/asset_root.h"
#include "physics/terrain_collider.h"

namespace apricot::city {

// The cooked half of the Ferrone Mast: tower, antennas, compound and fence,
// written by tools/ferrone_mast_blender.py into the ignored
// assets/models/props/ferrone_mast/. Procedural, not a supplied pack, but it
// lands beside every other cooked mesh, so a checkout that has not run the
// script has no tower. World treats that as a warning and leaves the pad bare.
inline constexpr const char* kFerroneMastAssetRoot = "models/props/ferrone_mast/";

// Which draw policy a cooked part follows (the `lod` column).
enum class FerroneMastLod : int {
    Always = 0,  // pole, beacon and side lights: they ARE the landmark at range
    Near = 1,    // the steel lattice and its antennas, up to kFerroneMastNearToM
    Far = 2,     // the alpha-cut silhouette, from kFerroneMastFarFromM out
    Site = 3,    // compound, shelter, fence: ordinary prop distance
};

// What a part does at night (the `emissive` column).
enum class FerroneMastGlow : int {
    None = 0,
    SteadyRed = 1,    // L-810 side lights
    FlashingRed = 2,  // the L-864 beacon
    WarmLamp = 3,     // the shelter's door wall-pack
};

struct FerroneMastMaterial {
    std::string mesh, texture;
    bool glass = false;
    FerroneMastGlow glow = FerroneMastGlow::None;
    bool alpha = false;
    FerroneMastLod lod = FerroneMastLod::Site;
    glm::vec4 tint{1.0f};
};
struct FerroneMastBox {
    glm::vec3 centre{}, half{};
};
struct FerroneMastLight {
    glm::vec3 position{};
    FerroneMastGlow glow = FerroneMastGlow::None;
};
struct FerroneMastAsset {
    std::vector<FerroneMastMaterial> materials;
    FerroneMastMaterial glow_sphere;  // unit-diameter sphere, scaled per light
    std::vector<FerroneMastBox> boxes;
    std::vector<FerroneMastLight> lights;
};

inline std::string ferrone_mast_path(const std::string& filename,
                                     const char* root = kFerroneMastAssetRoot) {
    return asset_path(std::string(root) + filename);
}

namespace detail {
inline bool read_ferrone_mast_materials(std::ifstream& in,
                                        std::vector<FerroneMastMaterial>& out) {
    FerroneMastMaterial m;
    int glass = 0, glow = 0, alpha = 0, lod = 0;
    while (in >> m.mesh >> m.texture >> glass >> glow >> alpha >> lod >> m.tint.r >>
           m.tint.g >> m.tint.b >> m.tint.a) {
        if (m.mesh.find('/') != std::string::npos || m.mesh.find("..") != std::string::npos ||
            m.texture.find('/') != std::string::npos || m.texture.find("..") != std::string::npos)
            return false;
        if (glow < 0 || glow > 3 || lod < 0 || lod > 3) return false;
        m.glass = glass != 0;
        m.glow = static_cast<FerroneMastGlow>(glow);
        m.alpha = alpha != 0;
        m.lod = static_cast<FerroneMastLod>(lod);
        out.push_back(m);
    }
    return in.eof();
}
}  // namespace detail

inline bool load_ferrone_mast_asset(FerroneMastAsset& out,
                                    const char* root = kFerroneMastAssetRoot) {
    FerroneMastAsset asset;
    std::ifstream materials(ferrone_mast_path("materials.txt", root));
    if (!detail::read_ferrone_mast_materials(materials, asset.materials) ||
        asset.materials.empty())
        return false;
    std::ifstream glow(ferrone_mast_path("glow.txt", root));
    std::vector<FerroneMastMaterial> glow_rows;
    if (!detail::read_ferrone_mast_materials(glow, glow_rows) || glow_rows.size() != 1)
        return false;
    asset.glow_sphere = glow_rows.front();
    std::ifstream collision(ferrone_mast_path("collision.txt", root));
    FerroneMastBox b;
    while (collision >> b.centre.x >> b.centre.y >> b.centre.z >> b.half.x >> b.half.y >>
           b.half.z) {
        if (!(b.half.x > 0 && b.half.y > 0 && b.half.z > 0)) return false;
        asset.boxes.push_back(b);
    }
    if (!collision.eof() || asset.boxes.empty()) return false;
    std::ifstream lights(ferrone_mast_path("lights.txt", root));
    FerroneMastLight l;
    int kind = 0;
    while (lights >> l.position.x >> l.position.y >> l.position.z >> kind) {
        if (kind < 1 || kind > 3) return false;
        l.glow = static_cast<FerroneMastGlow>(kind);
        asset.lights.push_back(l);
    }
    if (!lights.eof() || asset.lights.empty()) return false;
    out = std::move(asset);
    return true;
}

inline glm::vec3 ferrone_mast_world(glm::vec3 p, const StartSite& s) {
    return {s.origin.x + s.cos_yaw * p.x + s.sin_yaw * p.z, s.ground_m + p.y,
            s.origin.z - s.sin_yaw * p.x + s.cos_yaw * p.z};
}

// `site` is the mesh site (ferrone_mast_mesh_site()), whose ground is the pad.
inline void add_ferrone_mast_collision(TerrainCollider& collider,
                                       const FerroneMastAsset& asset,
                                       const StartSite& site) {
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& b : asset.boxes)
        collider.add_static_oriented_box(ferrone_mast_world(b.centre, site), b.half, yaw);
}

}  // namespace apricot::city

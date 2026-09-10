#pragma once

#include <fstream>
#include <string>
#include <vector>
#include "city/burgerpiz.h"
#include "core/asset_root.h"
#include "physics/terrain_collider.h"

namespace apricot::city {
struct BurgerPizMaterial {
    std::string mesh,texture;
    bool glass=false,emissive=false;
    glm::vec4 tint{1};
};
struct BurgerPizBox { glm::vec3 centre{},half{}; };
struct BurgerPizGround { glm::vec3 centre{}; glm::vec2 half{}; };
struct BurgerPizAsset {
    std::vector<BurgerPizMaterial> materials;
    std::vector<BurgerPizBox> boxes;
    std::vector<BurgerPizGround> grounds;
    std::vector<glm::vec3> lights;
    std::vector<glm::vec3> parking_lights;
};
inline std::string burgerpiz_path(const std::string& filename,const char* root=kBurgerPizAssetRoot) {
    return asset_path(std::string(root)+filename);
}
inline bool load_burgerpiz_asset(BurgerPizAsset& out,const char* root=kBurgerPizAssetRoot) {
    BurgerPizAsset asset;
    std::ifstream materials(burgerpiz_path("materials.txt",root));
    BurgerPizMaterial m;
    while(materials>>m.mesh>>m.texture>>m.glass>>m.emissive
          >>m.tint.r>>m.tint.g>>m.tint.b>>m.tint.a) {
        if(m.mesh.find('/')!=std::string::npos || m.mesh.find("..")!=std::string::npos ||
           m.texture.find('/')!=std::string::npos || m.texture.find("..")!=std::string::npos)
            return false;
        asset.materials.push_back(m);
    }
    if(!materials.eof() || asset.materials.empty())return false;
    std::ifstream collision(burgerpiz_path("collision.txt",root));
    BurgerPizBox b;
    while(collision>>b.centre.x>>b.centre.y>>b.centre.z>>b.half.x>>b.half.y>>b.half.z) {
        if(!(b.half.x>0 && b.half.y>0 && b.half.z>0))return false;
        asset.boxes.push_back(b);
    }
    if(!collision.eof() || asset.boxes.empty())return false;
    std::ifstream grounds(burgerpiz_path("grounds.txt",root));
    BurgerPizGround g;
    while(grounds>>g.centre.x>>g.centre.y>>g.centre.z>>g.half.x>>g.half.y) {
        if(!(g.half.x>0 && g.half.y>0))return false;
        asset.grounds.push_back(g);
    }
    if(!grounds.eof() || asset.grounds.empty())return false;
    std::ifstream lights(burgerpiz_path("lights.txt",root));
    glm::vec3 p;
    while(lights>>p.x>>p.y>>p.z)asset.lights.push_back(p);
    if(!lights.eof() || asset.lights.empty())return false;
    std::ifstream parking(burgerpiz_path("parking_lights.txt",root));
    while(parking>>p.x>>p.y>>p.z)asset.parking_lights.push_back(p);
    if(!parking.eof() || asset.parking_lights.empty())return false;
    out=std::move(asset);
    return true;
}
inline glm::vec3 burgerpiz_world(glm::vec3 p,const StartSite& s=kBurgerPizSite) {
    return {s.origin.x+s.cos_yaw*p.x+s.sin_yaw*p.z,s.ground_m+p.y,
            s.origin.z-s.sin_yaw*p.x+s.cos_yaw*p.z};
}
inline void add_burgerpiz_collision(TerrainCollider& collider,const BurgerPizAsset& asset,const StartSite& s=kBurgerPizSite) {
    const float yaw=std::atan2(s.sin_yaw,s.cos_yaw);
    for(const auto& b:asset.boxes)
        collider.add_static_oriented_box(burgerpiz_world(b.centre,s),b.half,yaw);
    for(const auto& g:asset.grounds) {
        const auto p=burgerpiz_world(g.centre,s);
        collider.add_static_ground_rect({p.x,p.z},p.y,g.half,yaw,Surface::Rock);
    }
}
} // namespace apricot::city

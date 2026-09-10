#pragma once

#include <fstream>
#include <string>
#include <vector>
#include "city/miandi_gas_station.h"
#include "core/asset_root.h"
#include "physics/terrain_collider.h"

namespace apricot::city {
struct MiandiGasStationMaterial {
    std::string mesh,texture;
    bool glass=false,emissive=false;
    glm::vec4 tint{1};
};
struct MiandiGasStationBox { glm::vec3 centre{},half{}; };
struct MiandiGasStationGround { glm::vec3 centre{}; glm::vec2 half{}; };
struct MiandiGasStationAsset {
    std::vector<MiandiGasStationMaterial> materials;
    std::vector<MiandiGasStationBox> boxes;
    std::vector<MiandiGasStationGround> grounds;
    std::vector<glm::vec3> lights;
    std::vector<MiandiGasStationBox> covers;
};
inline std::string miandi_gas_station_path(const std::string& filename,const char* root=kMiandiGasStationAssetRoot) {
    return asset_path(std::string(root)+filename);
}
inline bool load_miandi_gas_station_asset(MiandiGasStationAsset& out,const char* root=kMiandiGasStationAssetRoot) {
    MiandiGasStationAsset asset;
    std::ifstream materials(miandi_gas_station_path("materials.txt",root));
    MiandiGasStationMaterial m;
    while(materials>>m.mesh>>m.texture>>m.glass>>m.emissive
          >>m.tint.r>>m.tint.g>>m.tint.b>>m.tint.a) {
        if(m.mesh.find('/')!=std::string::npos || m.mesh.find("..")!=std::string::npos ||
           m.texture.find('/')!=std::string::npos || m.texture.find("..")!=std::string::npos)
            return false;
        asset.materials.push_back(m);
    }
    if(!materials.eof() || asset.materials.empty())return false;
    std::ifstream collision(miandi_gas_station_path("collision.txt",root));
    MiandiGasStationBox b;
    while(collision>>b.centre.x>>b.centre.y>>b.centre.z>>b.half.x>>b.half.y>>b.half.z) {
        if(!(b.half.x>0 && b.half.y>0 && b.half.z>0))return false;
        asset.boxes.push_back(b);
    }
    if(!collision.eof() || asset.boxes.empty())return false;
    std::ifstream grounds(miandi_gas_station_path("grounds.txt",root));
    MiandiGasStationGround g;
    while(grounds>>g.centre.x>>g.centre.y>>g.centre.z>>g.half.x>>g.half.y) {
        if(!(g.half.x>0 && g.half.y>0))return false;
        asset.grounds.push_back(g);
    }
    if(!grounds.eof() || asset.grounds.empty())return false;
    std::ifstream lights(miandi_gas_station_path("lights.txt",root));
    glm::vec3 p;
    while(lights>>p.x>>p.y>>p.z)asset.lights.push_back(p);
    if(!lights.eof() || asset.lights.empty())return false;
    std::ifstream covers(miandi_gas_station_path("covers.txt",root));
    while(covers>>b.centre.x>>b.centre.y>>b.centre.z>>b.half.x>>b.half.y>>b.half.z) {
        if(b.half.x<=0 || b.half.y<0 || b.half.z<=0)return false;
        b.half.y=std::max(.05f,b.half.y);
        asset.covers.push_back(b);
    }
    if(!covers.eof() || asset.covers.empty())return false;
    out=std::move(asset);
    return true;
}
inline glm::vec3 miandi_gas_station_world(glm::vec3 p,const StartSite& s=kMiandiGasStationSite) {
    return {s.origin.x+s.cos_yaw*p.x+s.sin_yaw*p.z,s.ground_m+p.y,
            s.origin.z-s.sin_yaw*p.x+s.cos_yaw*p.z};
}
inline void add_miandi_gas_station_collision(TerrainCollider& collider,const MiandiGasStationAsset& asset,const StartSite& s=kMiandiGasStationSite) {
    const float yaw=std::atan2(s.sin_yaw,s.cos_yaw);
    for(const auto& b:asset.boxes)
        collider.add_static_oriented_box(miandi_gas_station_world(b.centre,s),b.half,yaw);
    for(const auto& g:asset.grounds) {
        const auto p=miandi_gas_station_world(g.centre,s);
        collider.add_static_ground_rect({p.x,p.z},p.y,g.half,yaw,Surface::Rock);
    }
}
} // namespace apricot::city

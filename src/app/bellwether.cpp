#include "app/world.h"

#include <cmath>
#include <fstream>
#include <string>
#include "city/bellwether.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/log.h"
#include "gfx/texture.h"

namespace apricot {
bool World::set_bellwether(Renderer& renderer,Scene& scene,TerrainCollider& collider) {
    const std::string root=asset_path("models/world/bellwether/");
    std::ifstream parts(root+"parts.txt");
    if(!parts) {
        AP_ERROR("Bellwether assets missing: run python3 tools/make_bellwether_assets.py");
        return false;
    }
    Transform pose;
    pose.position={city::kBellwetherOrigin.x,city::kBellwetherGroundM,city::kBellwetherOrigin.z};
    std::string filename;float emission=0;
    while(parts>>filename>>emission) {
        if(filename.find('/')!=std::string::npos || filename.find("..")!=std::string::npos ||
           !std::isfinite(emission) || emission<0)return false;
        StaticEmesh mesh;Texture texture;
        if(!read_static_emesh(root+filename,mesh) || !texture.load_file(root+"town-atlas.png"))return false;
        Renderable r;
        r.mesh=renderer.add_mesh(mesh);
        if(r.mesh==kInvalidId)return false;
        bellwether_meshes_.push_back(r.mesh);
        r.material=renderer.add_material(std::move(texture),false,.08f,{},false,true);
        r.tint={1,1,1,emission>0?1.0f+emission:1.0f};
        const auto node=scene.create(r,pose,mesh.bounds);
        start_nodes_.push_back(node);
        if(auto* n=scene.get(node))n->max_draw_distance=1700;
    }
    if(!parts.eof() || bellwether_meshes_.empty())return false;
    std::ifstream collision(root+"collision.txt");
    glm::vec3 centre{},half{};std::size_t box_count=0;
    while(collision>>centre.x>>centre.y>>centre.z>>half.x>>half.y>>half.z) {
        if(!(half.x>0&&half.y>0&&half.z>0))return false;
        collider.add_static_oriented_box(pose.position+centre,half,0);
        ++box_count;
    }
    if(!collision.eof() || box_count==0)return false;
    std::ifstream ground(root+"grounds.txt");glm::vec2 ground_half{};
    while(ground>>centre.x>>centre.y>>centre.z>>ground_half.x>>ground_half.y) {
        if(!(ground_half.x>0&&ground_half.y>0))return false;
        const auto p=pose.position+centre;
        collider.add_static_ground_rect({p.x,p.z},p.y,ground_half,0,Surface::Rock);
    }
    if(!ground.eof())return false;
    std::ifstream lights(root+"lights.txt");BellwetherLight light;
    while(lights>>light.position.x>>light.position.y>>light.position.z>>light.radius>>light.strength) {
        if(!(light.radius>0&&light.strength>0))return false;
        light.position+=pose.position;bellwether_lights_.push_back(light);
    }
    if(!lights.eof() || bellwether_lights_.empty())return false;
    std::ifstream covers(root+"covers.txt");
    while(covers>>centre.x>>centre.y>>centre.z>>half.x>>half.y>>half.z) {
        if(!(half.x>0&&half.y>0&&half.z>0))return false;
        StaticBox box;
        box.centre=pose.position+centre;
        box.bounds={box.centre-half,box.centre+half};
        precipitation_cover_.push_back(box);
    }
    if(!covers.eof())return false;
    interior_streaming_volumes_.push_back({"Bellwether Parish",{438,-743},11.4f,12.4f,22.4f,1,0});
    interior_streaming_volumes_.push_back({"Bellwether Service",{570,-754},11.3f,27.4f,11.4f,1,0});
    AP_INFO("Bellwether: %zu original model groups, %zu solid boxes, %zu warm lights at 480 -700",
        bellwether_meshes_.size(),box_count,bellwether_lights_.size());
    return true;
}
} // namespace apricot

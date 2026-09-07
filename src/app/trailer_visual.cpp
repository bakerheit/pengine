#include "app/trailer_visual.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/log.h"
#include "gfx/renderer.h"
#include "gfx/primitives.h"
#include "app/vehicle_lamp_mesh.h"

namespace apricot {
bool TrailerVisual::init(Renderer& renderer,Scene& scene) {
    const auto load=[&](const char* mesh_path,const char* texture_path,StaticEmesh& mesh,Renderable& out) {
        if(!read_static_emesh(asset_path(mesh_path),mesh))return false;
        Texture texture;if(!texture.load_file(asset_path(texture_path)))return false;
        out.mesh=renderer.add_mesh(mesh);out.material=renderer.add_material(std::move(texture));return true;
    };
    StaticEmesh body,wheel;Renderable b,w;
    if(!load("models/vehicles/harrow_freight_trailer/body.emesh","textures/vehicles/harrow_freight_trailer/body.png",body,b) ||
       !load("models/vehicles/common/wheel.emesh","textures/vehicles/common/wheel.png",wheel,w))return false;
    body_=scene.create(b,{},body.bounds);
    for(std::size_t i=0;i<lamps_.size();++i) {
        auto lamp=b;lamp.uv_scale=vehicle_lamp_surface_uv(i+2,20);
        lamps_[i]=scene.create(lamp,{},body.bounds);
    }
    wheel_scale_=.5f/std::max(wheel.bounds.extents().y,wheel.bounds.extents().z);
    for(auto& id:wheels_)id=scene.create(w,{},wheel.bounds);
    Renderable leg;leg.mesh=renderer.add_mesh(make_box({.10f,.60f,.10f}));leg.material=w.material;
    for(auto& id:legs_)id=scene.create(leg,{},{{-.10f,-.60f,-.10f},{.10f,.60f,.10f}});
    AP_INFO("freight trailer: body and shared tandem wheels loaded");return true;
}
void TrailerVisual::sync(Scene& scene,const TrailerState& previous,const TrailerState& current,float alpha,float lights,float brake) const {
    auto s=current;s.position=glm::mix(previous.position,current.position,alpha);
    s.yaw=previous.yaw+trailer_angle(current.yaw-previous.yaw)*alpha;
    s.pitch=glm::mix(previous.pitch,current.pitch,alpha);
    const float spin=glm::mix(previous.wheel_spin,current.wheel_spin,alpha);
    Transform root;root.position=s.position;root.rotation=trailer_rotation(s);
    Transform body;body.rotation=glm::angleAxis(3.1415926536f,glm::vec3{0,1,0});
    scene.set_transform(body_,root*body);
    const float power=current.attached?std::clamp(lights*.22f+brake,0.f,1.f):0.f;
    for(auto id:lamps_) {
        scene.set_transform(id,root*body);
        if(auto* node=scene.get(id)) {
            node->visible=power>.01f;
            node->renderable.tint=glm::vec4{glm::vec3{1,.025f,.012f}*glm::mix(.72f,1.f,power),1.f+power*2.2f};
        }
    }
    for(std::size_t i=0;i<wheels_.size();++i) {
        Transform wheel;wheel.position={i%2==0?-1.10f:1.10f,.50f,i<2?3.f:4.2f};
        wheel.rotation=glm::angleAxis(-spin,glm::vec3{1,0,0});wheel.scale=glm::vec3{wheel_scale_};
        scene.set_transform(wheels_[i],root*wheel);
    }
    for(std::size_t i=0;i<legs_.size();++i) {
        Transform leg;leg.position={i==0?-.85f:.85f,current.attached?1.16f:.64f,-2.4f};
        if(current.attached)leg.scale.y=.25f;
        scene.set_transform(legs_[i],root*leg);
    }
}
void TrailerVisual::destroy(Scene& scene) {
    for(auto id:lamps_)scene.remove(id);
    scene.remove(body_);for(auto id:wheels_)scene.remove(id);for(auto id:legs_)scene.remove(id);
}
}

#pragma once
#include <memory>
#include "app/vehicle_plate_mesh.h"
#include "core/asset_root.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"
#include "scene/scene.h"

namespace apricot {
// Only the small serial mesh is per registration. Parked clones share it;
// releasing the last clone returns the GPU handle to the renderer's free list.
struct VehiclePlateResource {
    Renderer* renderer=nullptr;
    MeshId mesh=kInvalidId;
    ~VehiclePlateResource() { if (renderer && mesh!=kInvalidId) renderer->remove_mesh(mesh); }
};
inline MaterialId load_vehicle_plate_material(Renderer& renderer) {
    Texture atlas;
    if (!atlas.load_file(asset_path("textures/vehicles/common/license_plates.png"))) return kInvalidId;
    return renderer.add_material(std::move(atlas),false,.25f,Renderer::DepthBias{},false);
}
struct VehiclePlateVisual {
    VehicleRegistration registration{};
    NodeId node=kInvalidId;
    std::shared_ptr<VehiclePlateResource> resource;
    void destroy(Scene& scene) { scene.remove(node); node=kInvalidId; resource.reset(); }
    void set(Renderer& renderer,Scene& scene,MaterialId material,
             const VehiclePlateMounts& mounts,const VehicleRegistration& value) {
        if (!valid_registration(value)) return;
        destroy(scene);
        registration=value;
        const auto data=make_vehicle_plate_mesh(mounts,value);
        if (data.indices.empty()) return;
        resource=std::make_shared<VehiclePlateResource>();
        resource->renderer=&renderer;resource->mesh=renderer.add_mesh(data);
        Renderable r;r.mesh=resource->mesh;r.material=material;
        node=scene.create(r,Transform{},data.bounds);
    }
    void sync(Scene& scene,NodeId body_id) const {
        const auto* body=scene.get(body_id);
        auto* plate=scene.get(node);
        if (!body || !plate) return;
        plate->visible=body->visible;
        plate->renderable.body_damage0=body->renderable.body_damage0;
        plate->renderable.body_damage1=body->renderable.body_damage1;
        plate->renderable.deform_frame=body->renderable.deform_frame;
        scene.set_transform(node,body->local);
    }
};
} // namespace apricot

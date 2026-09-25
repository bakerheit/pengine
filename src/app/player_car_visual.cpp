#include "app/player_car_visual.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "app/vehicle_lamp_mesh.h"
#include "app/vehicle_registration.h"
#include "app/vehicle_snow_mesh.h"
#include "app/emergency_lighting.h"
#include "app/mistral_door.h"
#include "app/mistral_soft_top.h"
#include "app/workman_door.h"
#include "app/vehicle_headlight_profile.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_driver_door.h"
#include "app/vehicle_paint_catalog.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/log.h"
#include "gfx/renderer.h"
#include "gfx/primitives.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

constexpr const char* kWheelMeshPath = "models/vehicles/common/wheel.emesh";
constexpr const char* kWheelTexturePath = "textures/vehicles/common/wheel.png";
constexpr float kPi = 3.14159265358979323846f;

bool front_wheel(int index) {
    return index == kWheelFrontLeft || index == kWheelFrontRight;
}

bool left_wheel(int index) {
    return index == kWheelFrontLeft || index == kWheelRearLeft;
}

Transform chassis_pose(const VehicleState& previous,
                       const VehicleState& current, float alpha) {
    Transform out;
    out.position = glm::mix(previous.position, current.position, alpha);
    out.rotation = glm::slerp(previous.orientation, current.orientation, alpha);
    return out;
}

}  // namespace

bool PlayerCarVisual::load_model(
    Renderer& renderer, const PlayerCarDefinition& definition, Model& out) {
    StaticEmesh body;
    if (!read_static_emesh(asset_path(definition.mesh_path), body)) {
        AP_ERROR("player car: body '%s %s' failed to load",
                 definition.brand, definition.model);
        return false;
    }

    out.plate_mounts=vehicle_plate_mounts(body,definition.mesh_path);
    Texture body_texture;
    // The catalog's atlas, except Workman's semantic body.png. The respray
    // booth looks the paint profile up by this same function, so a respray
    // repaints the atlas this loads.
    const char* texture_path = player_car_body_texture_path(definition.id);
    if (!body_texture.load_file(asset_path(texture_path))) {
        AP_ERROR("player car: paint '%s %s' failed to load",
                 definition.brand, definition.model);
        return false;
    }

    if (has_animated_driver(definition.id) && !is_motorbike(definition.id)) {
        const std::string path=definition.mesh_path;
        const std::string root=path.substr(0,path.find_last_of('/')+1);
        StaticEmesh open_body, door;
        if (!read_static_emesh(asset_path(root + (definition.id==PlayerCarId::Bwc360 ? "body_drive.emesh" : "body_open.emesh")), open_body) ||
            !read_static_emesh(asset_path(root + (definition.id==PlayerCarId::Bwc360 ? "driver_front_door.emesh" : "driver_door.emesh")), door)) {
            AP_ERROR("player car: %s articulated meshes missing; recook the vehicle assets", definition.model);
            return false;
        }
        out.body_mesh = renderer.add_mesh(
            make_vehicle_snow_mesh(open_body, root + (definition.id==PlayerCarId::Bwc360 ? "body_drive.emesh" : "body_open.emesh")));
        out.driver_door_mesh = renderer.add_mesh(door);
        out.driver_door_bounds = door.bounds;
        if (out.driver_door_mesh == kInvalidId) return false;
        if (has_passenger_door(definition.id)) {
            StaticEmesh passenger;
            if (!read_static_emesh(asset_path(root+(definition.id==PlayerCarId::Bwc360 ? "passenger_front_door.emesh" : "passenger_door.emesh")),passenger)) return false;
            out.passenger_door_mesh=renderer.add_mesh(passenger);
            out.passenger_door_bounds=passenger.bounds;
            if (out.passenger_door_mesh==kInvalidId) return false;
        }
    } else out.body_mesh = renderer.add_mesh(
        make_vehicle_snow_mesh(body, definition.mesh_path));
    if (is_convertible(definition.id)) {
        const std::string path=definition.mesh_path;
        const std::string root=path.substr(0,path.find_last_of('/')+1);
        constexpr const char* names[]{"soft_top_rear.emesh","soft_top_front.emesh"};
        for (std::size_t i=0;i<out.soft_top_meshes.size();++i) {
            StaticEmesh bow;
            if (!read_static_emesh(asset_path(root+names[i]),bow)) {
                AP_ERROR("player car: %s folding top missing; recook the vehicle assets",
                         definition.model);
                return false;
            }
            // Uploaded raw, like the driver door: the canvas carries the same
            // solid material weights the snow pass gives ordinary bodywork, and
            // it has no pane to classify.
            out.soft_top_meshes[i]=renderer.add_mesh(bow);
            out.soft_top_bounds[i]=bow.bounds;
            if (out.soft_top_meshes[i]==kInvalidId) return false;
        }
    }
    if (definition.id == PlayerCarId::Bwc360 || definition.id == PlayerCarId::SaddleTango || definition.id == PlayerCarId::HarrowWorkman ||
        definition.id == PlayerCarId::EmberGt || definition.id == PlayerCarId::RodeoGrazer ||
        definition.id == PlayerCarId::AlderPip || definition.id == PlayerCarId::SpagattiShu ||
        definition.id == PlayerCarId::LegacyCar5Next ||
        definition.id == PlayerCarId::LegacyCar5NextPolice ||
        is_municipal_cruiser_91(definition.id) ||
        is_motorbike(definition.id)) {
        constexpr const char* names[]{"windshield", "rear_glass", "passenger_glass", "driver_glass",
                                     "driver_rear_glass", "passenger_rear_glass"};
        const std::string body_path=definition.mesh_path;
        const std::string root=body_path.substr(0,body_path.find_last_of('/')+1);
        const std::size_t pane_count=is_motorbike(definition.id) ? 1u :
            definition.id == PlayerCarId::HarrowWorkman ? 4u : 6u;
        out.glass_material = renderer.add_glass_material();
        for (std::size_t i=0;i<pane_count;++i) {
            StaticEmesh glass;
            if (!read_static_emesh(asset_path(root+(definition.id==PlayerCarId::Bwc360 && i==2 ? "passenger_front_glass" : definition.id==PlayerCarId::Bwc360 && i==3 ? "driver_front_glass" : names[i])+".emesh"),glass)) return false;
            out.glass_meshes[i] = i == 0u && !is_motorbike(definition.id)
                ? renderer.add_mesh(make_windshield_snow_mesh(glass))
                : renderer.add_mesh(glass);
            out.glass_bounds[i]=glass.bounds;
            if (out.glass_meshes[i]==kInvalidId) return false;
        }
        if (out.glass_material==kInvalidId) return false;
    }
    out.body_material = renderer.add_material(std::move(body_texture));
    out.body_bounds = body.bounds;
    if (out.body_mesh == kInvalidId || out.body_material == kInvalidId) {
        AP_ERROR("player car: body '%s %s' failed to upload",
                 definition.brand, definition.model);
        return false;
    }

    if (is_motorbike(definition.id) || definition.id==PlayerCarId::Bwc360 || definition.id==PlayerCarId::SaddleTango || definition.id==PlayerCarId::EmberGt || definition.id==PlayerCarId::RodeoGrazer) {
        const std::string path=definition.mesh_path;
        const std::string root=path.substr(0,path.find_last_of('/')+1);
        constexpr const char* names[]{"front_wheel.emesh","rear_wheel.emesh"};
        for (std::size_t i=0;i<out.custom_wheel_meshes.size();++i) {
            StaticEmesh wheel;
            if (!read_static_emesh(asset_path(root+names[i]),wheel)) {
                // Older retained bike attempts used one shared wheel mesh.
                if (!read_static_emesh(asset_path(root+"wheel.emesh"),wheel)) {
                    AP_ERROR("player bike: separate wheel meshes missing for '%s'",definition.model);
                    return false;
                }
            }
            out.custom_wheel_meshes[i]=renderer.add_mesh(wheel);
            out.custom_wheel_bounds[i]=wheel.bounds;
            out.custom_wheel_radii[i]=
                std::max(wheel.bounds.size().y,wheel.bounds.size().z)*.5f;
            if (out.custom_wheel_meshes[i]==kInvalidId ||
                !(out.custom_wheel_radii[i]>0.f)) return false;
        }
    }

    const auto profile = vehicle_headlight_profile(definition.mesh_path);
    out.headlight_profile = profile.id;
    out.exposed_headlights = profile.exposed();
    for (std::size_t i = 0; i < out.headlight_origins.size(); ++i) {
        if (profile.exposed() && !vehicle_headlight_origin(body, profile, i, out.headlight_origins[i])) {
            AP_ERROR("player car: headlight profile does not fit '%s'", definition.mesh_path);
            return false;
        }
    }
    for (std::size_t i = 0; i < out.lamp_meshes.size(); ++i) {
        // Glow is a masked repeat of the exact body triangles. Independent
        // four-corner lenses cannot follow a nonlinearly deformed low-poly face.
        out.lamp_meshes[i] = out.body_mesh;
        out.lamp_bounds[i] = body.bounds;
    }
    if (std::any_of(out.lamp_meshes.begin(), out.lamp_meshes.end(),
                    [](MeshId id) { return id == kInvalidId; })) {
        AP_ERROR("player car: lamp geometry for '%s %s' failed to upload",
                 definition.brand, definition.model);
        return false;
    }
    return true;
}

bool PlayerCarVisual::init(Renderer& renderer, Scene& scene,
                           const VehicleTuning& tuning,
                           const VehicleState& state,
                           PlayerCarId initial_car) {
    plate_renderer_=&renderer;
    plate_material_=load_vehicle_plate_material(renderer);
    if (plate_material_==kInvalidId) return false;
    for (const auto& car : kPlayerCars) {
        if (!load_model(renderer, car, models_[static_cast<std::size_t>(car.id)])) {
            return false;
        }
    }

    StaticEmesh wheel;
    if (!read_static_emesh(asset_path(kWheelMeshPath), wheel)) {
        AP_ERROR("player car: shared wheel model failed to load");
        return false;
    }
    Texture wheel_texture;
    if (!wheel_texture.load_file(asset_path(kWheelTexturePath))) {
        AP_ERROR("player car: shared wheel paint failed to load");
        return false;
    }
    shared_wheel_mesh_ = renderer.add_mesh(wheel);
    shared_wheel_material_ =
        renderer.add_material(std::move(wheel_texture));
    shared_wheel_bounds_ = wheel.bounds;
    if (shared_wheel_mesh_ == kInvalidId || shared_wheel_material_ == kInvalidId) {
        AP_ERROR("player car: shared wheel failed to upload");
        return false;
    }

    const glm::vec3 wheel_size = wheel.bounds.size();
    const float native_radius = std::max(wheel_size.y, wheel_size.z) * 0.5f;
    if (!(native_radius > 0.0f)) {
        AP_ERROR("player car: wheel mesh has no usable radius");
        return false;
    }
    wheel_scale_ = tuning.wheel_radius / native_radius;
    native_wheel_radius_ = native_radius;
    lamp_material_ = renderer.white_material();
    if (lamp_material_ == kInvalidId) return false;

    initial_car = canonical_player_car_id(initial_car);
    const std::size_t initial_index = static_cast<std::size_t>(initial_car);
    if (initial_index >= models_.size()) return false;
    const Model& initial = models_[initial_index];

    Renderable body_renderable;
    body_renderable.mesh = initial.body_mesh;
    body_renderable.material = initial.body_material;
    body_node_ = scene.create(
        body_renderable, Transform{}, initial.body_bounds);

    Renderable wheel_renderable;
    wheel_renderable.mesh = shared_wheel_mesh_;
    wheel_renderable.material = shared_wheel_material_;
    for (int i = 0; i < kWheelCount; ++i) {
        wheel_nodes_[static_cast<std::size_t>(i)] =
            scene.create(wheel_renderable, Transform{}, wheel.bounds);
    }

    for (std::size_t i = 0; i < lamp_nodes_.size(); ++i) {
        Renderable lamp_renderable;
        lamp_renderable.mesh = initial.lamp_meshes[i];
        lamp_renderable.material = lamp_material_;
        lamp_renderable.uv_scale = vehicle_lamp_surface_uv(i);
        lamp_nodes_[i] = scene.create(
            lamp_renderable, Transform{}, initial.lamp_bounds[i]);
    }

    for (std::size_t i=0;i<emergency_nodes_.size();++i) {
        Renderable lamp=body_renderable;
        lamp.uv_scale=vehicle_lamp_surface_uv(i+4);
        emergency_nodes_[i]=scene.create(lamp,Transform{},initial.body_bounds);
        scene.get(emergency_nodes_[i])->visible=false;
    }
    driver_door_node_ = scene.create(body_renderable, Transform{}, initial.body_bounds);
    scene.get(driver_door_node_)->visible = false;
    passenger_door_node_=scene.create(body_renderable,Transform{},initial.body_bounds);
    scene.get(passenger_door_node_)->visible=false;
    for (auto& id:soft_top_nodes_) {
        id=scene.create(body_renderable,Transform{},initial.body_bounds);
        scene.get(id)->visible=false;
    }
    for (auto& id:glass_nodes_) {
        id=scene.create(body_renderable,Transform{},initial.body_bounds);
        scene.get(id)->visible=false;
    }
    return select(scene, tuning, state, initial_car);
}

bool PlayerCarVisual::select(Scene& scene, const VehicleTuning& tuning,
                             const VehicleState& state, PlayerCarId car) {
    car = canonical_player_car_id(car);
    const std::size_t index = static_cast<std::size_t>(car);
    if (index >= models_.size()) return false;
    const PlayerCarDefinition& definition = player_car_definition(car);
    const Model& model = models_[index];
    SceneNode* body = scene.get(body_node_);
    if (!body) return false;

    body->renderable.mesh = model.body_mesh;
    body->renderable.material = model.body_material;
    factory_material_ = model.body_material;
    paint_base_ = 0;
    respray_.reset();
    body->local_bounds = model.body_bounds;
    if (auto* door = scene.get(driver_door_node_)) {
        door->visible = model.driver_door_mesh != kInvalidId;
        if (door->visible) {
            door->renderable = body->renderable;
            door->renderable.mesh = model.driver_door_mesh;
            door->local_bounds = model.driver_door_bounds;
        }
    }
    for (std::size_t i = 0; i < lamp_nodes_.size(); ++i) {
        SceneNode* lamp = scene.get(lamp_nodes_[i]);
        if (!lamp) return false;
        lamp->renderable.mesh = model.lamp_meshes[i];
        lamp->renderable.material = model.body_material;
        lamp->renderable.uv_scale = vehicle_lamp_surface_uv(i, model.headlight_profile);
        lamp->local_bounds = model.lamp_bounds[i];
        lamp_bounds_[i] = model.lamp_bounds[i];
    }
    const bool bike=is_motorbike(car);
    for (std::size_t i=0;i<wheel_nodes_.size();++i) {
        auto* wheel=scene.get(wheel_nodes_[i]);
        if (!wheel) return false;
        const std::size_t wheel_slot=front_wheel(static_cast<int>(i))?0u:1u;
        const bool custom=model.custom_wheel_meshes[wheel_slot]!=kInvalidId;
        wheel->renderable.mesh=custom?model.custom_wheel_meshes[wheel_slot]:shared_wheel_mesh_;
        wheel->renderable.material=custom?model.body_material:shared_wheel_material_;
        wheel->local_bounds=custom?model.custom_wheel_bounds[wheel_slot]:shared_wheel_bounds_;
        wheel->visible=!bike || i==static_cast<std::size_t>(kWheelFrontLeft) ||
            i==static_cast<std::size_t>(kWheelRearLeft);
    }

    // Every body is independently fitted to the existing player chassis. The
    // visual changes immediately; physics, damage, wheel state and replay do
    // not change identity or dimensions.
    const float scale_x = tuning.half_track / definition.wheel_x;
    const float scale_z = (2.0f * tuning.half_wheelbase) /
        (definition.wheel_front_z + definition.wheel_rear_z);
    body_local_ = Transform{};
    body_local_.scale = {scale_x, scale_z, scale_z};
    body_local_.rotation =
        glm::angleAxis(kPi, glm::vec3{0.0f, 1.0f, 0.0f});
    body_local_.position.y =
        -tuning.com_height_above_mount - static_suspension_length(tuning) -
        definition.arch_centre_y * scale_z;
    body_local_.position.z =
        (definition.wheel_front_z - definition.wheel_rear_z) *
        scale_z * 0.5f;
    placed_body_bounds_ = model.body_bounds.transformed(body_local_.matrix());
    lamp_layout_ = make_vehicle_lamp_layout(placed_body_bounds_);
    for (std::size_t i = 0; i < 2; ++i)
        lamp_layout_.lamps[i].position = body_local_.transform_point(model.headlight_origins[i]);
    const bool issue_plate=!registration_issued_ || registered_car_key_!=state.mechanical_key || active_car_!=car;
    active_car_ = car;
    if (issue_plate) {
        registered_car_key_=state.mechanical_key;
        registration_issued_=true;
        set_registration(scene,player_registration(car,state.mechanical_key,state.position.x,state.position.z));
    }
    wheel_scale_ = tuning.wheel_radius /
        (model.custom_wheel_radii[0]>0.f?model.custom_wheel_radii[0]:native_wheel_radius_);

    sync(scene, tuning, state, state, 0.0f, 1.0f, 0.0f);
    AP_INFO("player %s: %s %s selected, %s moving wheels ready",
            is_motorbike(car)?"bike":"car",definition.brand, definition.model,
            is_motorbike(car)?"two":"four");
    return true;
}

void PlayerCarVisual::sync(Scene& scene, const VehicleTuning& tuning,
                           const VehicleState& previous,
                           const VehicleState& current, float alpha,
                           float headlight_level, float brake_level) const {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    const Transform chassis = chassis_pose(previous, current, a);
    scene.set_transform(body_node_, chassis * body_local_);
    VehicleDamageState visual_damage;
    for (std::size_t i = 0; i < kVehicleDamageZoneCount; ++i) {
        visual_damage.zones[i] = glm::mix(
            previous.body_damage.zones[i], current.body_damage.zones[i], a);
    }
    // Stamp slots can be replaced by a new distant impact. Taking the current
    // bounded records avoids interpolating two unrelated dents across the
    // whole car for one render frame.
    visual_damage.stamps = current.body_damage.stamps;
    const glm::vec4 damage0 = pack_vehicle_damage0(visual_damage);
    const glm::vec2 damage1 = pack_vehicle_damage1(visual_damage);
    SceneNode* body = scene.get(body_node_);
    const glm::vec3 source_centre = body
        ? body->local_bounds.center() : glm::vec3{0.0f};
    const glm::vec3 source_half = body
        ? body->local_bounds.extents() : glm::vec3{1.0f};
    const glm::vec4 packed_damage1{
        damage1.x, damage1.y, source_centre.x, source_centre.z};
    const glm::vec4 deform_frame{
        1.0f / std::max(source_half.x, 0.001f),
        1.0f / std::max(source_half.z, 0.001f), source_centre.y,
        1.0f / std::max(source_half.y, 0.001f)};
    if (body) {
        body->renderable.body_damage0 = damage0;
        body->renderable.body_damage1 = packed_damage1;
        body->renderable.deform_frame = deform_frame;
        // Health must never recolor undamaged paint. Local surface overlays
        // are driven by the regional damage/stamps in the material shader.
        body->renderable.tint = glm::vec4{1.0f};
    }

    plate_.sync(scene,body_node_);
    const float steer = glm::mix(previous.steer_angle, current.steer_angle, a);
    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t wi = static_cast<std::size_t>(i);
        const float suspension = glm::mix(previous.wheels[wi].suspension_length,
                                          current.wheels[wi].suspension_length, a);
        const float spin = glm::mix(previous.wheels[wi].spin,
                                    current.wheels[wi].spin, a);
        const float wheel_damage = glm::mix(
            vehicle_wheel_damage(previous.body_damage, i),
            vehicle_wheel_damage(current.body_damage, i), a);
        const float bent = std::clamp((wheel_damage - 0.28f) / 0.72f,
                                      0.0f, 1.0f);

        Transform local;
        local.position = {
            is_motorbike(active_car_) ? 0.f :
                (left_wheel(i) ? -tuning.half_track : tuning.half_track),
            -tuning.com_height_above_mount - suspension,
            front_wheel(i) ? -tuning.half_wheelbase : tuning.half_wheelbase,
        };
        const glm::quat steer_rotation = front_wheel(i)
            ? glm::angleAxis(-wheel_steer_angle(tuning, steer, i),
                             glm::vec3{0.0f, 1.0f, 0.0f})
            : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        const glm::quat roll_rotation =
            glm::angleAxis(-spin, glm::vec3{1.0f, 0.0f, 0.0f});
        // A bent hub holds a little camber and precesses as the independently
        // animated wheel spins. The phase is sim-owned `spin`, so replay and
        // frame interpolation still reproduce the exact same wobble.
        const float side = left_wheel(i) ? 1.0f : -1.0f;
        const glm::quat bent_rotation =
            glm::angleAxis(side * bent * 0.12f,
                           glm::vec3{0.0f, 0.0f, 1.0f}) *
            glm::angleAxis(std::sin(spin) * bent * 0.065f,
                           glm::vec3{0.0f, 1.0f, 0.0f});
        local.rotation = steer_rotation * bent_rotation * roll_rotation;
        local.scale = glm::vec3{wheel_scale_};
        scene.set_transform(wheel_nodes_[wi], chassis * local);
    }

    const float light = std::clamp(headlight_level, 0.0f, 1.0f);
    const float brake = std::clamp(brake_level, 0.0f, 1.0f);
    for (std::size_t i = 0; i < lamp_nodes_.size(); ++i) {
        const bool front = i < 2u;
        const VehicleLamp lamp = static_cast<VehicleLamp>(i);
        scene.set_transform(lamp_nodes_[i], chassis * body_local_);

        const float health = vehicle_lamp_health(visual_damage, lamp);
        const float power = health * (front ? light
                                             : std::clamp(light * 0.22f + brake,
                                                          0.0f, 1.0f));
        if (SceneNode* node = scene.get(lamp_nodes_[i])) {
            node->renderable.body_damage0 = damage0;
            node->renderable.body_damage1 = packed_damage1;
            node->renderable.deform_frame = deform_frame;
            const glm::vec3 on = front ? glm::vec3{1.0f, 0.88f, 0.65f}
                                       : glm::vec3{1.0f, 0.025f, 0.012f};
            // The body paint already owns the unlit lens. Rendering a dark
            // opaque overlay while power is zero created the black blocks in
            // front of the bumper; this node is glow only.
            node->visible = power > 0.01f && health > 0.04f &&
                (!front || models_[static_cast<std::size_t>(active_car_)].exposed_headlights);
            const glm::vec3 color = on * glm::mix(0.72f, 1.0f, power);
            node->renderable.tint = glm::vec4{color, 1.0f + power * (front ? 1.8f : 2.2f)};
        }
    }
    sync_driver_door(scene, 0.f);
    sync_passenger_door(scene, 0.f);
    // Stowed by default, for the same reason the door closes here: select()
    // and the parked clones go through sync, and the owner re-applies the real
    // fraction in the same frame.
    sync_soft_top(scene, 1.f);
}

void PlayerCarVisual::sync_soft_top(Scene& scene, float stowed) const {
    const auto* body = scene.get(body_node_);
    const auto& model = models_[static_cast<std::size_t>(active_car_)];
    const float fraction = clamp_soft_top(stowed);
    for (std::size_t i=0;i<soft_top_nodes_.size();++i) {
        auto* bow = scene.get(soft_top_nodes_[i]);
        if (!bow) continue;
        bow->visible = body && body->visible && model.soft_top_meshes[i]!=kInvalidId;
        if (!bow->visible) continue;
        // Share the body renderable so paint, dents and scratch stamps stay in
        // the body frame; only the mesh and the hinge pose differ.
        bow->renderable = body->renderable;
        bow->renderable.mesh = model.soft_top_meshes[i];
        bow->local_bounds = model.soft_top_bounds[i];
        scene.set_transform(soft_top_nodes_[i], i==0u
            ? mistral_top_rear_transform(body->local, fraction)
            : mistral_top_front_transform(body->local, fraction));
    }
}

void PlayerCarVisual::sync_driver_door(Scene& scene, float open_fraction) const {
    auto* door = scene.get(driver_door_node_);
    if (!door) return;
    const auto* body = scene.get(body_node_);
    const auto& model = models_[static_cast<std::size_t>(active_car_)];
    door->visible = body && body->visible &&
                    has_animated_driver(active_car_) &&
                    model.driver_door_mesh != kInvalidId;
    for (std::size_t i=0;i<glass_nodes_.size();++i) {
        auto* glass=scene.get(glass_nodes_[i]);
        if (!glass) continue;
        glass->visible=body && body->visible && model.glass_meshes[i]!=kInvalidId;
        if (!glass->visible) continue;
        glass->renderable=body->renderable;
        glass->renderable.mesh=model.glass_meshes[i];
        glass->renderable.material=model.glass_material;
        glass->local_bounds=model.glass_bounds[i];
        scene.set_transform(glass_nodes_[i],i==3 && has_animated_driver(active_car_)
            ? vehicle_driver_door_transform(active_car_,body->local,open_fraction) : body->local);
    }
    if (!door->visible) return;
    // Shared source coordinates keep paint, dents and scratch stamps in the
    // body frame. Lamp overlays use body_open too, never the closed shell.
    door->renderable = body->renderable;
    door->renderable.mesh = model.driver_door_mesh;
    door->local_bounds = model.driver_door_bounds;
    const auto transform = vehicle_driver_door_transform(active_car_,body->local,open_fraction);
    scene.set_transform(driver_door_node_, transform);
}

void PlayerCarVisual::sync_passenger_door(Scene& scene,float open_fraction) const {
    const auto* body=scene.get(body_node_);
    const auto& model=models_[static_cast<std::size_t>(active_car_)];
    auto* door=scene.get(passenger_door_node_);
    if (!door) return;
    door->visible=body && body->visible && model.passenger_door_mesh!=kInvalidId;
    if (!door->visible) return;
    const auto transform=vehicle_passenger_door_transform(active_car_,body->local,open_fraction);
    door->renderable=body->renderable;
    door->renderable.mesh=model.passenger_door_mesh;
    door->local_bounds=model.passenger_door_bounds;
    scene.set_transform(passenger_door_node_,transform);
    if (model.glass_meshes[2]!=kInvalidId) scene.set_transform(glass_nodes_[2],transform);
}

HeadlightRig PlayerCarVisual::headlights(const VehicleState& previous,
                                         const VehicleState& current, float alpha,
                                         float level) const {
    const Transform chassis =
        chassis_pose(previous, current, std::clamp(alpha, 0.0f, 1.0f));

    HeadlightRig rig;
    if (!models_[static_cast<std::size_t>(active_car_)].exposed_headlights) return rig;
    const float base = 4.5f * std::clamp(level, 0.0f, 1.0f);
    VehicleDamageState visual_damage;
    for (std::size_t i = 0; i < kVehicleDamageZoneCount; ++i) {
        visual_damage.zones[i] = glm::mix(previous.body_damage.zones[i],
                                         current.body_damage.zones[i],
                                         std::clamp(alpha, 0.0f, 1.0f));
    }
    rig.intensity = {
        base * vehicle_lamp_health(visual_damage, VehicleLamp::FrontLeft),
        base * vehicle_lamp_health(visual_damage, VehicleLamp::FrontRight)};
    const glm::vec3 direction = glm::normalize(
        chassis.rotation * glm::vec3{0.0f, -0.075f, -1.0f});
    for (int i = 0; i < kHeadlightCount; ++i) {
        const VehicleLamp lamp = i == 0 ? VehicleLamp::FrontLeft
                                        : VehicleLamp::FrontRight;
        const float damage = vehicle_lamp_damage(visual_damage, lamp);
        const Transform lens = deform_vehicle_lamp(
            lamp_layout_.lamps[static_cast<std::size_t>(i)],
            placed_body_bounds_, static_cast<std::size_t>(i), damage);
        rig.position[static_cast<std::size_t>(i)] =
            chassis.position + chassis.rotation * lens.position;
        rig.direction[static_cast<std::size_t>(i)] = direction;
    }
    return rig;
}

void PlayerCarVisual::sync_emergency(Scene& scene, uint64_t step, bool enabled) const {
    const auto powers=police_flash_power(step,enabled && has_police_lightbar(active_car_));
    const auto& model = models_[static_cast<std::size_t>(active_car_)];
    const auto* body=scene.get(body_node_);
    for (std::size_t i=0;i<emergency_nodes_.size();++i) {
        auto* lamp=scene.get(emergency_nodes_[i]);
        if (!lamp) continue;
        lamp->visible=body && body->visible && powers[i]>0;
        if (!lamp->visible) continue;
        lamp->renderable=body->renderable;
        lamp->renderable.uv_scale=
            vehicle_lamp_surface_uv(i + 4u, model.headlight_profile);
        lamp->renderable.tint={1,1,1,4};
        lamp->local_bounds=body->local_bounds;
        scene.set_transform(emergency_nodes_[i],body->local);
    }
}

void PlayerCarVisual::clone_parked(Scene& scene, PlayerCarVisual& out) const {
    out=*this;
    const auto clone=[&](NodeId id) {
        const auto* node=scene.get(id);
        if (!node) return kInvalidId;
        const SceneNode copy=*node; // create can reallocate scene storage
        const auto result=scene.create(copy.renderable,copy.local,copy.local_bounds);
        scene.get(result)->visible=copy.visible;
        return result;
    };
    out.body_node_=clone(body_node_);
    out.plate_.node=clone(plate_.node);
    out.driver_door_node_=clone(driver_door_node_);
    out.passenger_door_node_=clone(passenger_door_node_);
    for (std::size_t i=0;i<soft_top_nodes_.size();++i) out.soft_top_nodes_[i]=clone(soft_top_nodes_[i]);
    for (std::size_t i=0;i<glass_nodes_.size();++i) out.glass_nodes_[i]=clone(glass_nodes_[i]);
    for (std::size_t i=0;i<2;++i) {
        out.emergency_nodes_[i]=clone(emergency_nodes_[i]);
        if (auto* node=scene.get(out.emergency_nodes_[i])) node->visible=false;
    }
    for (std::size_t i=0;i<4;++i) {
        out.wheel_nodes_[i]=clone(wheel_nodes_[i]);
        out.lamp_nodes_[i]=clone(lamp_nodes_[i]);
    }
}

void PlayerCarVisual::point_paint_nodes(Scene& scene, MaterialId paint) {
    if (auto* body=scene.get(body_node_)) body->renderable.material=paint;
    if (auto* door=scene.get(driver_door_node_)) door->renderable.material=paint;
    if (auto* door=scene.get(passenger_door_node_)) door->renderable.material=paint;
    for (const auto id:soft_top_nodes_)
        if (auto* bow=scene.get(id)) bow->renderable.material=paint;
    // All four lamps, as select() points them: the rear lenses sample the same
    // atlas, and a lens is never inside a paint mask (tools/paint_profiles.py
    // lamps), so the brake glow keeps its red.
    for (const auto id:lamp_nodes_)
        if (auto* lamp=scene.get(id)) lamp->renderable.material=paint;
    for (const auto id:wheel_nodes_)
        if (auto* wheel=scene.get(id); wheel && wheel->renderable.mesh!=shared_wheel_mesh_)
            wheel->renderable.material=paint;
}

void PlayerCarVisual::set_factory_paint(Scene& scene, MaterialId material, uint8_t paint_base) {
    factory_material_=material;
    paint_base_=paint_base;
    respray_.reset();
    point_paint_nodes(scene,material);
}

void PlayerCarVisual::apply_respray(Scene& scene, MaterialId material, PaintColor colour) {
    respray_=colour;
    point_paint_nodes(scene,material);
}

void PlayerCarVisual::clear_respray(Scene& scene) {
    respray_.reset();
    point_paint_nodes(scene,factory_material_);
}

void PlayerCarVisual::preview_body_material(Scene& scene, MaterialId material) {
    point_paint_nodes(scene,material);
}

const char* PlayerCarVisual::worn_atlas() const {
    const char* atlas=player_car_paint_base_atlas(active_car_,paint_base_);
    return atlas?atlas:player_car_body_texture_path(active_car_);
}

void PlayerCarVisual::destroy(Scene& scene) {
    plate_.destroy(scene);
    registration_issued_=false;
    for (auto& id:glass_nodes_) { scene.remove(id); id=kInvalidId; }
    for (auto& id:soft_top_nodes_) { scene.remove(id); id=kInvalidId; }
    scene.remove(driver_door_node_);
    driver_door_node_=kInvalidId;
    scene.remove(passenger_door_node_);
    passenger_door_node_=kInvalidId;
    for (auto& id:emergency_nodes_) { scene.remove(id); id=kInvalidId; }
    scene.remove(body_node_);
    body_node_ = kInvalidId;
    for (NodeId& node : wheel_nodes_) {
        scene.remove(node);
        node = kInvalidId;
    }
    for (NodeId& node : lamp_nodes_) {
        scene.remove(node);
        node = kInvalidId;
    }
}

void PlayerCarVisual::set_registration(Scene& scene, const VehicleRegistration& registration) {
    if (!plate_renderer_ || !valid_registration(registration)) return;
    plate_.set(*plate_renderer_,scene,plate_material_,
        models_[static_cast<std::size_t>(active_car_)].plate_mounts,registration);
    plate_.sync(scene,body_node_);
}

}  // namespace apricot

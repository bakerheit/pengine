#include "app/traffic_visual.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "app/vehicle_lamp_mesh.h"
#include "app/vehicle_headlight_profile.h"
#include "app/emergency_lighting.h"
#include "app/road_fixture_layout.h"
#include "app/traffic_signal_mesh.h"
#include "city/interior_streaming.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/fixed_step.h"
#include "core/log.h"
#include "core/rng.h"
#include "gfx/primitives.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"
#include "physics/terrain_collider.h"

namespace apricot {
namespace {

constexpr glm::vec4 kMetal{0.16f, 0.17f, 0.19f, 1.0f};
constexpr glm::vec4 kRedBright{1.0f, 0.08f, 0.05f, 2.5f};
constexpr glm::vec4 kYellowBright{1.0f, 0.78f, 0.05f, 2.5f};
constexpr glm::vec4 kGreenBright{0.05f, 1.0f, 0.18f, 2.5f};
constexpr glm::vec4 kRedDim{0.075f, 0.012f, 0.009f, 1.0f};
constexpr glm::vec4 kYellowDim{0.075f, 0.046f, 0.008f, 1.0f};
constexpr glm::vec4 kGreenDim{0.008f, 0.058f, 0.025f, 1.0f};

constexpr float kPoleThick = 0.20f;

bool identity_less(uint64_t ak, uint32_t as, uint64_t bk, uint32_t bs) {
    return ak != bk ? ak < bk : as < bs;
}

Transform chassis_transform(const VehicleAgent& agent) {
    Transform t;
    t.position = agent.pos;
    const float yaw = std::atan2(-agent.fwd.x, -agent.fwd.z);
    t.rotation = glm::quat(glm::vec3{0.0f, yaw, 0.0f});
    return t;
}

void set_draw_distance(Scene& scene, NodeId id, float distance) {
    if (SceneNode* node = scene.get(id)) node->max_draw_distance = distance;
}

}  // namespace

bool TrafficVisual::load_model(Renderer& renderer, const char* mesh_path,
                               const char* const* paint_paths,
                               std::size_t paint_count,
                               float arch_centre_y_native,
                               float wheel_x_native,
                               float wheel_front_z_native,
                               float wheel_rear_z_native, Model& out) {
    StaticEmesh body;
    if (!read_static_emesh(asset_path(mesh_path), body)) return false;
    out.mesh = renderer.add_mesh(body);
    out.bounds = body.bounds;
    if (out.mesh == kInvalidId) return false;

    if (std::string_view(mesh_path).find("municipal_cruiser_91c/") !=
        std::string_view::npos) {
        constexpr const char* kGlassNames[]{
            "windshield", "rear_glass", "passenger_glass", "driver_glass",
            "driver_rear_glass", "passenger_rear_glass"};
        const std::string path=mesh_path;
        const std::string root=path.substr(0,path.find_last_of('/')+1);
        out.glass_material=renderer.add_glass_material();
        if (out.glass_material==kInvalidId) return false;
        for (std::size_t i=0;i<std::size(kGlassNames);++i) {
            StaticEmesh glass;
            if (!read_static_emesh(asset_path(root+kGlassNames[i]+".emesh"),glass))
                return false;
            out.glass_meshes[i]=renderer.add_mesh(glass);
            out.glass_bounds[i]=glass.bounds;
            if (out.glass_meshes[i]==kInvalidId) return false;
        }
        out.glass_count=std::size(kGlassNames);
    }

    out.paints.clear();
    out.paints.reserve(paint_count);
    for (std::size_t i = 0; i < paint_count; ++i) {
        Texture texture;
        if (!texture.load_file(asset_path(paint_paths[i]))) return false;
        out.paints.push_back(renderer.add_material(std::move(texture)));
    }

    out.layout = make_traffic_visual_layout(
        body.bounds, arch_centre_y_native, wheel_x_native,
        wheel_front_z_native, wheel_rear_z_native);
    const auto profile = vehicle_headlight_profile(mesh_path);
    out.headlight_profile = profile.id;
    out.exposed_headlights = profile.exposed();
    for (std::size_t i = 0; i < out.lamp_meshes.size(); ++i) {
        out.lamp_meshes[i] = out.mesh;
        out.lamp_bounds[i] = body.bounds;
        if (i < 2 && profile.exposed() &&
            !vehicle_headlight_origin(body, profile, i, out.lamp_origins[i])) {
            AP_ERROR("traffic: headlight profile does not fit '%s'", mesh_path);
            return false;
        }
    }
    return !out.paints.empty() &&
           std::none_of(out.lamp_meshes.begin(), out.lamp_meshes.end(),
                        [](MeshId id) { return id == kInvalidId; });
}

bool TrafficVisual::init(Renderer& renderer, Scene& scene,
                         const LaneGraph& lanes, const CrowdTuning& tuning,
                         TerrainCollider& collider) {
    static constexpr const char* kCar5Paints[] = {
        "textures/vehicles/car5/body.png",
        "textures/vehicles/car5/green.png",
        "textures/vehicles/car5/grey.png",
        "textures/vehicles/car5/taxi.png",
    };
    static constexpr const char* kCar8Paints[] = {
        "textures/vehicles/car8/body.png",
        "textures/vehicles/car8/grey.png",
        "textures/vehicles/car8/purple.png",
        "textures/vehicles/car8/mail.png",
    };
    static constexpr const char* kAmbulancePaints[] = {
        "textures/vehicles/ambulance/body.png",
    };
    static constexpr const char* kFiretruckPaints[] = {
        "textures/vehicles/firetruck/body_surface.png",
    };
    static constexpr const char* kHalcyonSixPaints[] = {
        "textures/vehicles/halcyon_six/body_surface.png",
    };
    static constexpr const char* kMontroseRegentEightPaints[] = {
        "textures/vehicles/montrose_regent_eight/body_surface.png",
    };
    static constexpr const char* kVesperVx91Paints[] = {
        "textures/vehicles/vesper_vx91/body_surface.png",
    };
    static constexpr const char* kPolicePaints[] = {
        "textures/vehicles/municipal_cruiser_91c/body.png",
    };

    if (!load_model(renderer, "models/vehicles/car5/body.emesh", kCar5Paints,
                    std::size(kCar5Paints), 0.45f, 1.038f, 2.254f, 1.813f,
                    models_[0]) ||
        !load_model(renderer, "models/vehicles/car8/body.emesh", kCar8Paints,
                    4, 0.525f, 1.10f, 1.65f, 2.25f, models_[1]) ||
        !load_model(renderer, "models/vehicles/ambulance/body.emesh",
                    kAmbulancePaints, 1, .47f, .98f, 1.78f, 1.58f,
                    models_[2]) ||
        !load_model(renderer, "models/vehicles/firetruck/body_surface.emesh",
                    kFiretruckPaints, 1, 0.72f, 1.13f, 2.18f, 2.12f,
                    models_[3]) ||
        !load_model(renderer, "models/vehicles/halcyon_six/body_surface.emesh",
                    kHalcyonSixPaints, 1, 0.68f, 1.06f, 2.18f, 1.86f,
                    models_[4]) ||
        !load_model(renderer,
                    "models/vehicles/montrose_regent_eight/body_surface.emesh",
                    kMontroseRegentEightPaints, 1,
                    0.68f, 1.10f, 2.30f, 2.05f, models_[5]) ||
        !load_model(renderer, "models/vehicles/vesper_vx91/body_surface.emesh",
                    kVesperVx91Paints, 1,
                    0.55f, 1.08f, 2.08f, 1.86f, models_[6]) ||
        !load_model(renderer, "models/vehicles/municipal_cruiser_91c/body.emesh",
                    kPolicePaints, 1,
                    .42f, .94f, 1.63f, 1.53f, models_[7])) {
        AP_ERROR("traffic visual: a traffic body or paint failed to load");
        return false;
    }

    StaticEmesh wheel;
    if (!read_static_emesh(asset_path("models/vehicles/common/wheel.emesh"),
                           wheel)) {
        AP_ERROR("traffic visual: shared wheel model failed to load");
        return false;
    }
    Texture wheel_texture;
    if (!wheel_texture.load_file(
            asset_path("textures/vehicles/common/wheel.png"))) {
        AP_ERROR("traffic visual: shared wheel paint failed to load");
        return false;
    }
    wheel_mesh_ = renderer.add_mesh(wheel);
    wheel_material_ = renderer.add_material(std::move(wheel_texture));
    wheel_bounds_ = wheel.bounds;
    native_wheel_radius_ =
        std::max(wheel.bounds.size().y, wheel.bounds.size().z) * 0.5f;
    if (wheel_mesh_ == kInvalidId || !(native_wheel_radius_ > 0.0f)) return false;

    const MeshData box = make_box(glm::vec3{0.5f});
    box_mesh_ = renderer.add_mesh(box);
    box_bounds_ = box.bounds;
    flat_material_ = renderer.white_material();
    if (box_mesh_ == kInvalidId || flat_material_ == kInvalidId) return false;

    const std::array<MeshData,5> signal_geometry{
        make_traffic_signal_pole(), make_traffic_signal_arm(),
        make_traffic_signal_housing(), make_traffic_signal_lens(),
        make_traffic_signal_border()};
    for(std::size_t i=0;i<signal_geometry.size();++i) {
        signal_meshes_[i]=renderer.add_mesh(signal_geometry[i]);
        signal_bounds_[i]=signal_geometry[i].bounds;
        if(signal_meshes_[i]==kInvalidId)return false;
    }
    Texture signal_white;
    if(!signal_white.make_white())return false;
    signal_material_=renderer.add_material(std::move(signal_white),false,.16f);

    tuning_ = tuning;
    build_signals(scene, lanes, collider);
    build_street_lamps(scene, lanes, collider);
    AP_INFO("traffic visual: 8 bodies, shared moving wheels, %zu signal "
            "heads, %zu street lamps ready",
            signals_.size(), street_lamps_.size());
    return true;
}

TrafficVisual::Rig TrafficVisual::create_rig(
    Scene& scene, const VehicleAgent& agent) const {
    Rig rig;
    rig.lane_key = agent.lane_key;
    rig.slot = agent.slot;

    // Stable secondary buckets split ordinary cars between the original sedan,
    // Halcyon, Montrose, and Vesper. Emergency/truck weights stay unchanged.
    const uint64_t h = traffic_vehicle_identity_hash(
        agent.lane_key, agent.slot);
    rig.model = static_cast<std::size_t>(traffic_vehicle_kind(agent));
    const Model& model = models_[rig.model];

    Renderable body;
    body.mesh = model.mesh;
    body.material = model.paints[static_cast<std::size_t>(
        (h >> 8) % static_cast<uint64_t>(model.paints.size()))];
    rig.body = scene.create(body, Transform{}, model.bounds);
    set_draw_distance(scene, rig.body, 420.0f);

    for (std::size_t i=0;i<model.glass_count;++i) {
        Renderable glass;
        glass.mesh=model.glass_meshes[i];
        glass.material=model.glass_material;
        rig.glass[i]=scene.create(glass,Transform{},model.glass_bounds[i]);
        set_draw_distance(scene,rig.glass[i],420.0f);
    }

    Renderable wheel;
    wheel.mesh = wheel_mesh_;
    wheel.material = wheel_material_;
    for (NodeId& id : rig.wheels) {
        id = scene.create(wheel, Transform{}, wheel_bounds_);
        set_draw_distance(scene, id, 420.0f);
    }
    for (std::size_t i = 0; i < rig.lamps.size(); ++i) {
        Renderable lamp;
        lamp.mesh = model.lamp_meshes[i];
        lamp.material = body.material;
        lamp.uv_scale = vehicle_lamp_surface_uv(i, model.headlight_profile);
        rig.lamps[i] = scene.create(
            lamp, Transform{}, model.lamp_bounds[i]);
        set_draw_distance(scene, rig.lamps[i], 420.0f);
    }
    if (traffic_vehicle_kind(agent) == TrafficVehicleKind::Police) {
        for (std::size_t i = 0; i < rig.emergency.size(); ++i) {
            Renderable emergency = body;
            emergency.uv_scale = vehicle_lamp_surface_uv(
                i + 4u, model.headlight_profile);
            rig.emergency[i] = scene.create(
                emergency, Transform{}, model.bounds);
            if (SceneNode* node = scene.get(rig.emergency[i])) {
                node->visible = false;
            }
            set_draw_distance(scene, rig.emergency[i], 420.0f);
        }
    }
    return rig;
}

void TrafficVisual::destroy_rig(Scene& scene, Rig& rig) const {
    scene.remove(rig.body);
    rig.body = kInvalidId;
    for (NodeId& id : rig.wheels) {
        scene.remove(id);
        id = kInvalidId;
    }
    for (NodeId& id : rig.lamps) {
        scene.remove(id);
        id = kInvalidId;
    }
    for (NodeId& id : rig.emergency) {
        scene.remove(id);
        id = kInvalidId;
    }
    for (NodeId& id : rig.glass) {
        if (id != kInvalidId) scene.remove(id);
        id = kInvalidId;
    }
}

void TrafficVisual::sync_rig(Scene& scene, Rig& rig,
                             const VehicleAgent& agent,
                             const LaneGraph& lanes, int64_t step,
                             float headlight_level) const {
    const Model& model = models_[rig.model];
    const Transform chassis = chassis_transform(agent);

    const Transform body_transform = chassis * model.layout.body;
    scene.set_transform(rig.body, body_transform);
    for (NodeId id : rig.glass)
        if (id != kInvalidId) scene.set_transform(id,body_transform);
    const glm::vec4 damage0 = pack_vehicle_damage0(agent.body_damage);
    const glm::vec2 damage1 = pack_vehicle_damage1(agent.body_damage);
    const glm::vec3 source_centre = model.bounds.center();
    const glm::vec3 source_half = model.bounds.extents();
    const glm::vec4 packed_damage1{
        damage1.x, damage1.y, source_centre.x, source_centre.z};
    const glm::vec4 deform_frame{
        1.0f / std::max(source_half.x, 0.001f),
        1.0f / std::max(source_half.z, 0.001f), source_centre.y,
        1.0f / std::max(source_half.y, 0.001f)};
    if (SceneNode* body = scene.get(rig.body)) {
        body->renderable.body_damage0 = damage0;
        body->renderable.body_damage1 = packed_damage1;
        body->renderable.deform_frame = deform_frame;
    }

    float steer = agent.turn_from_lane != kInvalidLane
        ? agent.turn_steer_rad : 0.0f;
    if (agent.turn_from_lane == kInvalidLane && lanes.valid(agent.lane)) {
        const Lane& lane = lanes.lane(agent.lane);
        const float ahead_d = std::min(agent.dist_along_m + 1.5f, lane.length_m);
        const glm::vec3 ahead = lanes.pose(agent.lane, ahead_d).tangent;
        const float cross_y = agent.fwd.z * ahead.x - agent.fwd.x * ahead.z;
        const float dot = glm::clamp(glm::dot(agent.fwd, ahead), -1.0f, 1.0f);
        steer = std::clamp(std::atan2(cross_y, dot), -0.45f, 0.45f);
    }
    const bool recovering=agent.collision_recovery_seconds>0.f ||
        glm::dot(agent.collision_offset_xz,agent.collision_offset_xz)>.0004f ||
        std::abs(agent.collision_yaw_rad)>.002f;
    if (recovering) {
        // The simulation supplies physical +Y wheel rotation. This visual
        // convention negates steer below; avoid the lane-heading estimate,
        // which would count the recovery yaw a second time.
        steer=(agent.turn_from_lane!=kInvalidLane ? agent.turn_steer_rad:0.f)-agent.collision_steer_rad;
    }
    const float spin = static_cast<float>(step) * static_cast<float>(kSimDt) *
                       agent.speed_mps / model.layout.wheel_radius;
    const float wheel_scale = model.layout.wheel_radius / native_wheel_radius_;

    for (std::size_t i = 0; i < rig.wheels.size(); ++i) {
        const bool front = i < 2u;
        const bool left = i == 0u || i == 2u;
        const float wheel_damage =
            vehicle_wheel_damage(agent.body_damage, static_cast<int>(i));
        const float bent = std::clamp((wheel_damage - 0.28f) / 0.72f,
                                      0.0f, 1.0f);
        Transform local;
        local.position = model.layout.wheel_centres[i];
        const glm::quat steering = front
            ? glm::angleAxis(-steer, glm::vec3{0.0f, 1.0f, 0.0f})
            : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        const glm::quat bent_rotation =
            glm::angleAxis((left ? 1.0f : -1.0f) * bent * 0.12f,
                           glm::vec3{0.0f, 0.0f, 1.0f}) *
            glm::angleAxis(std::sin(spin) * bent * 0.065f,
                           glm::vec3{0.0f, 1.0f, 0.0f});
        local.rotation = steering * bent_rotation *
                         glm::angleAxis(-spin, glm::vec3{1.0f, 0.0f, 0.0f});
        local.scale = glm::vec3{wheel_scale};
        scene.set_transform(rig.wheels[i], chassis * local);
    }

    const float light = std::clamp(headlight_level, 0.0f, 1.0f);
    // AI drivers hold the pedal while queued, and illuminate as soon as their
    // controller asks for meaningfully less than cruise speed.
    const float brake = agent.speed_mps < std::max(0.35f,
                                                   agent.cruise_mps - 0.75f)
                            ? 1.0f
                            : 0.0f;
    for (std::size_t i = 0; i < rig.lamps.size(); ++i) {
        const bool front = i < 2u;
        const VehicleLamp lamp = static_cast<VehicleLamp>(i);
        scene.set_transform(rig.lamps[i], body_transform);

        const float health = vehicle_lamp_health(agent.body_damage, lamp);
        const float power = health * (front ? light
                                             : std::clamp(light * 0.22f + brake,
                                                          0.0f, 1.0f));
        if (SceneNode* node = scene.get(rig.lamps[i])) {
            node->renderable.body_damage0 = damage0;
            node->renderable.body_damage1 = packed_damage1;
            node->renderable.deform_frame = deform_frame;
            const glm::vec3 on = front ? glm::vec3{1.0f, 0.88f, 0.65f}
                                       : glm::vec3{1.0f, 0.025f, 0.012f};
            node->visible = power > 0.01f && health > 0.04f &&
                (!front || model.exposed_headlights);
            const glm::vec3 color = on * glm::mix(0.72f, 1.0f, power);
            node->renderable.tint = glm::vec4{color, 1.0f + power * (front ? 1.8f : 2.2f)};
        }
    }
    const auto emergency_power = police_flash_power(
        static_cast<uint64_t>(step), agent.police_pursuit);
    for (std::size_t i = 0; i < rig.emergency.size(); ++i) {
        SceneNode* node = scene.get(rig.emergency[i]);
        if (!node) continue;
        node->visible = emergency_power[i] > 0.01f;
        if (!node->visible) continue;
        node->renderable.body_damage0 = damage0;
        node->renderable.body_damage1 = packed_damage1;
        node->renderable.deform_frame = deform_frame;
        node->renderable.tint = {1.0f, 1.0f, 1.0f,
                                 1.0f + 3.0f * emergency_power[i]};
        scene.set_transform(rig.emergency[i], body_transform);
    }
}

void TrafficVisual::sync(Scene& scene, const Crowd& crowd,
                         const LaneGraph& lanes, int64_t step,
                         float headlight_level, glm::vec3 focus,
                         float presentation_radius_m) {
    const std::vector<VehicleAgent>& agents = crowd.vehicles();
    headlights_.clear();
    headlights_.reserve(agents.size()*2);
    std::vector<Rig> next;
    next.reserve(presentation_radius_m > 0.0f
                     ? std::min<std::size_t>(agents.size(), 64u)
                     : agents.size());
    std::size_t old = 0;

    for (const VehicleAgent& agent : agents) {
        if (!city::within_presentation_radius(
                agent.pos, focus, presentation_radius_m))
            continue;
        while (old < rigs_.size() &&
               identity_less(rigs_[old].lane_key, rigs_[old].slot,
                             agent.lane_key, agent.slot)) {
            destroy_rig(scene, rigs_[old]);
            ++old;
        }
        Rig rig;
        if (old < rigs_.size() && rigs_[old].lane_key == agent.lane_key &&
            rigs_[old].slot == agent.slot) {
            rig = rigs_[old];
            ++old;
            const std::size_t wanted_model = static_cast<std::size_t>(
                traffic_vehicle_kind(agent));
            if (rig.model != wanted_model) {
                destroy_rig(scene, rig);
                rig = create_rig(scene, agent);
            }
        } else {
            rig = create_rig(scene, agent);
        }
        sync_rig(scene, rig, agent, lanes, step, headlight_level);
        if(headlight_level>0.001f) {
            const Model& model=models_[rig.model];
            const Transform chassis=chassis_transform(agent);
            const glm::vec3 direction=glm::normalize(chassis.rotation*glm::vec3{0,-0.075f,-1});
            for(std::size_t lamp=0;lamp<2;++lamp) {
                if (!model.exposed_headlights) break;
                const auto which=static_cast<VehicleLamp>(lamp);
                const float health=vehicle_lamp_health(agent.body_damage,which);
                if(health<=0.04f) continue;
                const float power=4.5f*std::clamp(headlight_level,0.0f,1.0f)*
                    health;
                if(power<=0.01f) continue;
                Transform lens;
                lens.position=model.layout.body.transform_point(model.lamp_origins[lamp]);
                lens=deform_vehicle_lamp(lens,model.layout.placed_body_bounds,lamp,
                    vehicle_lamp_damage(agent.body_damage,which));
                headlights_.push_back({glm::vec4{chassis.transform_point(lens.position),32.0f},
                                        glm::vec4{direction,power}});
            }
        }
        if (agent.police_pursuit &&
            traffic_vehicle_kind(agent) == TrafficVehicleKind::Police) {
            const Model& model = models_[rig.model];
            append_police_lights(
                headlights_, chassis_transform(agent) * model.layout.body,
                static_cast<uint64_t>(step), true);
        }
        next.push_back(rig);
    }
    while (old < rigs_.size()) {
        destroy_rig(scene, rigs_[old]);
        ++old;
    }
    rigs_ = std::move(next);
    vehicle_headlight_count_ = headlights_.size();

    for (SignalRig& signal : signals_) {
        const TrafficSignalPhase phase = traffic_signal_phase(
            lanes, signal.junction, signal.incoming, step, tuning_);
        if (SceneNode* red = scene.get(signal.red))
            red->renderable.tint = !signal.damage.broken && phase == TrafficSignalPhase::Red
                                       ? kRedBright : kRedDim;
        if (SceneNode* yellow = scene.get(signal.yellow))
            yellow->renderable.tint = !signal.damage.broken && phase == TrafficSignalPhase::Yellow
                                          ? kYellowBright : kYellowDim;
        if (SceneNode* green = scene.get(signal.green))
            green->renderable.tint = !signal.damage.broken && phase == TrafficSignalPhase::Green
                                         ? kGreenBright : kGreenDim;
    }
}

void TrafficVisual::build_signals(Scene& scene, const LaneGraph& lanes,
                                  TerrainCollider& collider) {
    auto make_signal_node = [&](std::size_t mesh, glm::vec3 position,
                                glm::quat rotation, glm::vec3 scale, glm::vec4 tint) {
        Renderable r;
        r.mesh = signal_meshes_[mesh];
        r.material = signal_material_;
        r.tint = tint;
        Transform t;
        t.position = position;
        t.rotation = rotation;
        t.scale = scale;
        const NodeId id = scene.create(r, t, signal_bounds_[mesh]);
        set_draw_distance(scene, id, 900.0f);
        return id;
    };

    for (const TrafficSignalFixtureLayout& fixture :
         build_traffic_signal_layouts(lanes, kSignalPoleHeight)) {
        const uint32_t j = fixture.junction;
        const LaneRef incoming = fixture.incoming;
        const Lane& lane = lanes.lane(incoming);
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const TrafficSignalLayout& layout = fixture.layout;
        const glm::vec3 base{
            layout.pole_ground.x,
            collider.height(layout.pole_ground.x, layout.pole_ground.z),
            layout.pole_ground.z};
        const glm::vec3 top = base + up * kSignalPoleHeight;
        const glm::vec3 arm_end = top + (layout.arm_end - layout.pole_top);
        const glm::vec3 housing = arm_end - up * .87f;
        const glm::vec3 bulb_offset = layout.facing * kSignalLensZ;
        const glm::quat rotation = layout.rotation;

        SignalRig rig;
        rig.damage.lane_key = lane.key;
        rig.base = base;
        rig.junction = j;
        rig.incoming = incoming;
        rig.all[0] = make_signal_node(0,base,rotation,{1,1,1},
            {.28f,.30f,.31f,1.f});
        rig.all[1] = make_signal_node(1,(top+arm_end)*.5f,rotation,
            {layout.arm_length_m,1,1},{.28f,.30f,.31f,1.f});
        rig.all[2] = make_signal_node(2,housing,rotation,{1,1,1},
            {.035f,.039f,.041f,1.f});
        rig.red = rig.all[3] = make_signal_node(3,
            housing+up*kSignalLensSpacing+bulb_offset,rotation,{1,1,1},kRedDim);
        rig.yellow = rig.all[4] = make_signal_node(3,
            housing+bulb_offset,rotation,{1,1,1},kYellowDim);
        rig.green = rig.all[5] = make_signal_node(3,
            housing-up*kSignalLensSpacing+bulb_offset,rotation,{1,1,1},kGreenDim);
        rig.all[6] = make_signal_node(4,housing,rotation,{1,1,1},
            {.86f,.65f,.13f,1.f});
        for (std::size_t n = 0; n < rig.all.size(); ++n)
            rig.standing[n] = scene.get(rig.all[n])->local;

        Transform pole;
        pole.position = base + up * (kSignalPoleHeight * 0.5f);
        pole.rotation = rotation;
        pole.scale = {kPoleThick, kSignalPoleHeight, kPoleThick};
        rig.collider_id = collider.add_kinematic_box(box_bounds_.transformed(pole.matrix()));
        collider.set_kinematic_breakaway(rig.collider_id,
            static_cast<uint32_t>(signals_.size()), kSignalBreakSpeed);
        signals_.push_back(rig);
    }
}

void TrafficVisual::step_signals(Scene& scene, TerrainCollider& collider,
                                 const VehicleState& car, float dt) {
    if (car.breakaway_id < signals_.size()) {
        auto& rig = signals_[car.breakaway_id];
        if (rig.damage.hit(car.breakaway_velocity)) {
            collider.set_kinematic_enabled(rig.collider_id, false);
            // Debris is deliberately non-solid: no invisible upright blocker,
            // moving AABB wall, or fallen arm trapping a junction indefinitely.
            auto resting = rig.damage;
            resting.fall_seconds = kSignalFallSeconds;
            const Transform fall = resting.pose(rig.base);
            float lift = 0.0f;
            for (std::size_t n = 0; n < rig.all.size(); ++n) {
                const auto* node = scene.get(rig.all[n]);
                const AABB bounds = node->local_bounds;
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 p{(corner&1) ? bounds.max.x : bounds.min.x,
                        (corner&2) ? bounds.max.y : bounds.min.y,
                        (corner&4) ? bounds.max.z : bounds.min.z};
                    const glm::vec3 world = (fall * rig.standing[n]).transform_point(p);
                    lift = std::max(lift, collider.height(world.x, world.z) + .025f - world.y);
                }
            }
            rig.damage.resting_lift = lift;
            AP_INFO("signal broken: lane %llu, impact %.1f m/s",
                static_cast<unsigned long long>(rig.damage.lane_key),
                static_cast<double>(glm::length(car.breakaway_velocity)));
        }
    }
    for (auto& rig : signals_) {
        if (!rig.damage.broken || rig.damage.fall_seconds >= kSignalFallSeconds) continue;
        rig.damage.step(dt);
        const Transform fall = rig.damage.pose(rig.base);
        for (std::size_t n = 0; n < rig.all.size(); ++n)
            scene.set_transform(rig.all[n], fall * rig.standing[n]);
    }
}

void TrafficVisual::reset_signals(Scene& scene, TerrainCollider& collider) {
    for (auto& rig : signals_) {
        const uint64_t key = rig.damage.lane_key;
        rig.damage = {};
        rig.damage.lane_key = key;
        collider.set_kinematic_enabled(rig.collider_id, true);
        for (std::size_t n = 0; n < rig.all.size(); ++n)
            scene.set_transform(rig.all[n], rig.standing[n]);
    }
}

void TrafficVisual::build_street_lamps(Scene& scene, const LaneGraph& lanes,
                                      TerrainCollider& collider) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    for (const auto& layout : build_street_lamp_layouts(lanes)) {
        const glm::vec3 base{layout.pole_ground.x,
            collider.height(layout.pole_ground.x, layout.pole_ground.z),
            layout.pole_ground.z};
        // Decked roads are not identified by Lane. Reject a station when the
        // lane and the support terrain differ enough to put a pole in space.
        if (std::fabs(base.y - layout.pole_ground.y) > 1.0f) continue;
        // Sidewalk setback can meet a building facade. Do not plant the pole
        // through an existing solid; authored slabs below knee height are OK.
        bool blocked = false;
        for (const StaticBox& box : collider.static_boxes()) {
            if (!box.enabled) continue;
            const glm::vec3 probe = box.local_point(base + up * 0.75f);
            if (box.collision_bounds().contains(probe)) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        const glm::vec3 top = base + up * kStreetLampHeightM;
        const glm::vec3 end = top + layout.arm_end - layout.pole_top;
        StreetLampRig rig;
        rig.bulb_position = end - up * 0.22f;
        const std::array<glm::vec3, 4> positions{
            base + up * (kStreetLampHeightM * 0.5f), (top + end) * 0.5f,
            end - up * 0.10f, rig.bulb_position};
        const std::array<glm::vec3, 4> scales{
            glm::vec3{0.18f, kStreetLampHeightM, 0.18f},
            glm::vec3{layout.arm_length_m, 0.14f, 0.14f},
            glm::vec3{0.75f, 0.20f, 0.42f}, glm::vec3{0.62f, 0.06f, 0.32f}};
        for (std::size_t i = 0; i < rig.nodes.size(); ++i) {
            Renderable renderable;
            renderable.mesh = box_mesh_;
            renderable.material = flat_material_;
            renderable.tint = i == 3u ? glm::vec4{1.0f, 0.80f, 0.52f, 3.3f} : kMetal;
            Transform transform;
            transform.position = positions[i];
            transform.rotation = layout.rotation;
            transform.scale = scales[i];
            rig.nodes[i] = scene.create(renderable, transform, box_bounds_);
            set_draw_distance(scene, rig.nodes[i], kStreetLampDrawDistanceM);
            if (i == 0u)
                collider.add_static_box(box_bounds_.transformed(transform.matrix()), Surface::Rock);
        }
        street_lamps_.push_back(rig);
    }
}

void TrafficVisual::sync_street_lights(Scene& scene, glm::vec3 camera_position,
                                      float night_level) {
    // Rendering can run more than once between simulation updates.
    headlights_.resize(vehicle_headlight_count_);
    const float power = std::clamp(night_level, 0.0f, 1.0f);
    for (const StreetLampRig& rig : street_lamps_) {
        if (SceneNode* bulb = scene.get(rig.nodes[3]))
            bulb->renderable.tint = {1.0f, 0.80f, 0.52f, 1.0f + 2.3f * power};
        const glm::vec3 delta = rig.bulb_position - camera_position;
        if (power <= 0.001f || glm::dot(delta, delta) >
            kStreetLampLightDistanceM * kStreetLampLightDistanceM) continue;
        headlights_.push_back({glm::vec4{rig.bulb_position, 15.0f},
                               glm::vec4{0.0f, -1.0f, 0.0f, 5.0f * power}});
    }
}

void TrafficVisual::clear_vehicles(Scene& scene) {
    headlights_.clear();
    vehicle_headlight_count_ = 0;
    for (Rig& rig : rigs_) destroy_rig(scene, rig);
    rigs_.clear();
}

void TrafficVisual::destroy(Scene& scene) {
    clear_vehicles(scene);
    for (SignalRig& signal : signals_) {
        for (NodeId id : signal.all) scene.remove(id);
    }
    signals_.clear();
    for (const StreetLampRig& rig : street_lamps_)
        for (NodeId id : rig.nodes) scene.remove(id);
    street_lamps_.clear();
}

}  // namespace apricot

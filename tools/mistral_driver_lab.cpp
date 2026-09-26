// Focused production CharacterVisual driver/cockpit QA, no app-loop changes.
#include <SDL.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "app/character_visual.h"
#include "app/player_car_visual.h"
#include "app/vehicle_model_tuning.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "gfx/renderer.h"
#include "gfx/sky.h"
#include "platform/window.h"

using namespace apricot;

namespace {
bool screenshot(const Window& window, const std::string& path) {
    const int w=window.width(), h=window.height();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4u);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    const std::size_t row=static_cast<std::size_t>(w)*4u;
    for (int y=0;y<h/2;++y)
        for (std::size_t x=0;x<row;++x)
            std::swap(pixels[static_cast<std::size_t>(y)*row+x],pixels[static_cast<std::size_t>(h-y-1)*row+x]);
    const std::filesystem::path parent=std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    return stbi_write_png(path.c_str(),w,h,4,pixels.data(),w*4)!=0;
}
}

int main(int argc,char** argv) {
    std::string output="build/vesper_mistral-driver-front.png", view="front", sequence;
    int frames=120,sequence_step=0; bool occupied=true; bool other=false;
    bool production_car=false;
    int plate_design=-1;
    bool plate_check=false;
    float snow_cover=0.0f;
    float steering=0.0f,suspension=-1.0f;
    float passenger_open=0.0f,driver_open=-1.0f;
    PlayerCarId model=PlayerCarId::VesperMistral;
    VehicleTransitionDirection transition=VehicleTransitionDirection::None;
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if (arg=="--on-foot") occupied=false;
        else if (arg=="--other-car") other=true;
        else if (arg=="--player-car") production_car=true;
        else if (arg=="--plate-check") plate_check=true;
        else if (i+1<argc && arg=="--plate-design") {
            char* end=nullptr;const char* value=argv[++i];
            const long parsed=std::strtol(value,&end,10);
            if (end==value || *end!='\0' || parsed<0 || parsed>=long(kPlateDesigns.size())) return 2;
            plate_design=static_cast<int>(parsed);
        }
        else if (i+1<argc && (arg=="--passenger-door" || arg=="--driver-door")) {
            char* end=nullptr;
            const char* value=argv[++i];
            const float amount=std::strtof(value,&end);
            if (end==value || *end!='\0' || !std::isfinite(amount) || amount<0.f || amount>1.f) return 2;
            (arg=="--passenger-door"?passenger_open:driver_open)=amount;
        }
        else if (i+1<argc && (arg=="--steer" || arg=="--suspension")) {
            char* end=nullptr;const char* value=argv[++i];
            const float parsed=std::strtof(value,&end);
            if (end==value || *end!='\0' || !std::isfinite(parsed) ||
                parsed>(1.f) || parsed<(arg=="--steer"?-1.f:0.f)) return 2;
            (arg=="--steer"?steering:suspension)=parsed;
        }
        else if (i+1<argc && arg=="--snow-cover") {
            char* end=nullptr;
            const char* value=argv[++i];
            snow_cover=std::strtof(value,&end);
            if (end==value || *end!='\0' || !std::isfinite(snow_cover) ||
                snow_cover<0.0f || snow_cover>1.0f) return 2;
        }
        else if (i+1<argc && arg=="--car") {
            const std::string car=argv[++i];
            const std::string key="/"+car+"/";
            bool found=false;
            for (const auto& candidate:kPlayerCars) {
                if (std::string{candidate.mesh_path}.find(key)!=std::string::npos) {
                    model=candidate.id;found=true;break;
                }
            }
            if (!found) return 2;
        }
        else if (i+1<argc && arg=="--screenshot") output=argv[++i];
        else if (i+1<argc && arg=="--sequence") sequence=argv[++i];
        else if (i+1<argc && arg=="--sequence-step") sequence_step=std::max(1,std::atoi(argv[++i]));
        else if (i+1<argc && arg=="--transition") {
            const std::string direction=argv[++i];
            if (direction!="enter" && direction!="exit") return 2;
            transition=direction=="enter" ? VehicleTransitionDirection::Enter : VehicleTransitionDirection::Exit;
        }
        else if (i+1<argc && arg=="--view") view=argv[++i];
        else if (i+1<argc && arg=="--frames") frames=std::max(1,std::atoi(argv[++i]));
        else { std::fprintf(stderr,"--car MODEL_FOLDER --player-car --snow-cover 0..1 --steer -1..1 --suspension 0..1 --driver-door 0..1 --passenger-door 0..1 --plate-design 0..7 --plate-check --view front|windshield|rear|side|passenger|cockpit|inside|controls|plate-front|plate-rear --screenshot PNG --sequence DIR --sequence-step N --transition enter|exit --frames N --on-foot --other-car\n");return 2; }
    }
    if (!production_car && !has_animated_driver(model)) return 2;
    if (view!="front" && view!="windshield" && view!="rear" && view!="side" &&
        view!="passenger" && view!="cockpit" && view!="inside" && view!="controls" && view!="plate-front" && view!="plate-rear") return 2;
    const bool workman=model==PlayerCarId::HarrowWorkman;
    const auto& definition=player_car_definition(model);
    const auto& layout=vehicle_driver_layout(model);
    const bool pip=model==PlayerCarId::AlderPip;
    const std::string model_path=definition.mesh_path;
    const auto slash=model_path.find_last_of('/');
    const auto parent=model_path.find_last_of('/',slash-1);
    const std::string root=model_path.substr(parent+1,slash-parent-1);
    Window window;WindowConfig config;config.title=root+" driver QA";
    config.width=1100;config.height=800;config.vsync=false;config.take_focus=false;
    if (!window.init(config)) return 1;
    Renderer renderer;if (!renderer.init()) return 1;
    Scene scene;Transform body;
    PlayerCarVisual player_car;
    VehicleTuning vehicle_tuning;
    if (production_car || model==PlayerCarId::EmberGt || model==PlayerCarId::RodeoGrazer)
        vehicle_tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,model);
    VehicleState vehicle;
    NodeId door_node=kInvalidId,driver_glass=kInvalidId;
    AABB body_bounds;
    const float length_scale=(2.f*vehicle_tuning.half_wheelbase)/(definition.wheel_front_z+definition.wheel_rear_z);
    body.scale={.78f/definition.wheel_x,length_scale,length_scale};
    if (production_car) {
        // Use the exact game loader, including body glass metadata and glass
        // materials. Orient its chassis so the authored front remains +Z,
        // matching the existing lab views and character staging.
        vehicle.orientation=glm::angleAxis(3.14159265359f,glm::vec3{0,1,0});
        vehicle.position.y=vehicle_tuning.com_height_above_mount+
            static_suspension_length(vehicle_tuning)+definition.arch_centre_y*length_scale;
        vehicle.position.z=(definition.wheel_front_z-definition.wheel_rear_z)*length_scale*.5f;
        for (auto& wheel_state:vehicle.wheels)
            wheel_state.suspension_length=suspension>=0.f
                ? vehicle_tuning.suspension_rest-(1.f-suspension)*vehicle_tuning.suspension_travel
                : static_suspension_length(vehicle_tuning);
        vehicle.steer_angle=steering*vehicle_tuning.max_steer;
        if (!player_car.init(renderer,scene,vehicle_tuning,vehicle,model)) return 1;
        if (plate_check) {
            const auto original=player_car.registration();
            const auto meshes=renderer.mesh_count();
            PlayerCarVisual parked;player_car.clone_parked(scene,parked);
            if (parked.registration()!=original || renderer.mesh_count()!=meshes) return 1;
            auto moved=vehicle;moved.position={7500,5,8400};
            if (!player_car.select(scene,vehicle_tuning,moved,model) || player_car.registration()!=original) return 1;
            for (uint64_t i=1;i<=256;++i) {
                const auto& design=kPlateDesigns[i%kPlateDesigns.size()];
                player_car.set_registration(scene,{design.state,design.series,i});
                if (parked.registration()!=original || renderer.mesh_count()!=meshes+1u) return 1;
            }
            parked.destroy(scene);
            if (renderer.mesh_count()!=meshes) return 1;
            player_car.set_registration(scene,original);
            player_car.sync(scene,vehicle_tuning,vehicle,vehicle,0,0,0);
            if (renderer.mesh_count()!=meshes) return 1;
            std::fprintf(stderr,"plate lifecycle: cross-state retention, parked clone, 256 replacements and GPU release passed\n");
        }
        if (plate_design>=0) {
            const auto& design=kPlateDesigns[static_cast<std::size_t>(plate_design)];
            player_car.set_registration(scene,{design.state,design.series,123456789});
        }
        std::fprintf(stderr,"plate: %s / %s / %s\n",city::state_name(player_car.registration().state),
            kPlateDesigns[plate_design_index(player_car.registration().state,player_car.registration().series)].name,
            plate_serial(player_car.registration()).c_str());
        body=player_car.fitted_body_transform(vehicle);
        Transform chassis;chassis.position=vehicle.position;chassis.rotation=vehicle.orientation;
        body_bounds=player_car.placed_body_bounds().transformed(chassis.matrix());
    } else {
    StaticEmesh car;Texture paint;
    if (!read_static_emesh(asset_path("models/vehicles/"+root+"/body_open.emesh"),car) ||
        !paint.load_file(asset_path("textures/vehicles/"+root+"/body.png"))) return 1;
    Renderable r;r.mesh=renderer.add_mesh(car);r.material=renderer.add_material(std::move(paint));
    scene.create(r,body,car.bounds);
    body_bounds=car.bounds.transformed(body.matrix());
    {
        StaticEmesh door;
        if (!read_static_emesh(asset_path("models/vehicles/"+root+"/driver_door.emesh"),door)) return 1;
        Renderable door_renderable=r;door_renderable.mesh=renderer.add_mesh(door);
        door_node=scene.create(door_renderable,body,door.bounds);
    }
    if (workman || pip || is_municipal_cruiser_91(model)) {
        auto material=renderer.add_glass_material();
        for (const char* name:{"windshield","rear_glass","passenger_glass","driver_glass",
                              "driver_rear_glass","passenger_rear_glass"}) {
            if (workman && (std::string(name)=="driver_rear_glass" ||
                            std::string(name)=="passenger_rear_glass")) continue;
            StaticEmesh glass;
            if (!read_static_emesh(asset_path("models/vehicles/"+root+"/"+name+".emesh"),glass)) return 1;
            Renderable gr;gr.mesh=renderer.add_mesh(glass);gr.material=material;
            auto id=scene.create(gr,body,glass.bounds);
            if (std::string(name)=="driver_glass") driver_glass=id;
        }
    }
    StaticEmesh wheel;Texture tire;
    if (!read_static_emesh(asset_path("models/vehicles/common/wheel.emesh"),wheel) ||
        !tire.load_file(asset_path("textures/vehicles/common/wheel.png"))) return 1;
    Renderable wr;wr.mesh=renderer.add_mesh(wheel);wr.material=renderer.add_material(std::move(tire));
    const float radius=std::max(wheel.bounds.size().y,wheel.bounds.size().z)*.5f;
    for (float x:{-definition.wheel_x,definition.wheel_x})
        for (float z:{-definition.wheel_rear_z,definition.wheel_front_z}) {
        Transform t;t.position=body.transform_point({x,definition.arch_centre_y,z});t.scale=glm::vec3{.32f/radius};
        scene.create(wr,t,wheel.bounds);
    }
    }
    scene.update();
    PlayerCharacterState player;player.position={1.5f,0,0};
    if (transition!=VehicleTransitionDirection::None) {
        player.position=body.transform_point(vehicle_driver_approach_point(model))+glm::vec3{.55f,0,0};
        player.facing_yaw=transition==VehicleTransitionDirection::Exit ? 1.57079632679f : -1.57079632679f;
    }
    Crowd crowd;CharacterVisual characters;
    if (!characters.init(player)) return 1;
    Camera camera;camera.aspect=window.aspect();camera.near_plane=.03f;camera.far_plane=50.f;
    glm::vec3 target{0,.85f,0};camera.position={3.f,2.7f,4.1f};
    if (view=="rear") camera.position={2.8f,2.7f,-4.1f};
    if (view=="plate-front" || view=="plate-rear") {
        StaticEmesh source;
        if (!production_car || !read_static_emesh(asset_path(definition.mesh_path),source)) return 2;
        const auto mounts=vehicle_plate_mounts(source,definition.mesh_path);
        if (mounts.empty()) return 2;
        const auto& mount=view=="plate-front"?mounts.front():mounts.back();
        const Transform fitted=player_car.fitted_body_transform(vehicle);
        target=fitted.transform_point(mount.centre);
        camera.position=target+fitted.rotation*mount.normal*.80f+glm::vec3{.10f,.08f,0};
    }
    if (view=="side") camera.position={4.2f,1.6f,0};
    if (view=="passenger") camera.position={-4.2f,1.6f,0};
    if (view=="inside") {camera.position={.34f,1.40f,-.18f};target={-.8f,1.3f,-.05f};}
    if (view=="cockpit") {camera.position={1.65f,2.5f,-1.95f};target={.35f,.95f,-.08f};}
    if (workman && view=="cockpit") {camera.position={2.4f,1.75f,.3f};target={.35f,1.10f,.1f};}
    if (view=="windshield") {
        const glm::vec3 size=body_bounds.size();
        target=body_bounds.center()+glm::vec3{0,size.y*.12f,size.z*.12f};
        camera.position=target+glm::vec3{size.x*.08f,size.y*.70f,size.z*.82f};
    }
    if (view=="controls") {
        camera.position=body.transform_point(layout.hip+glm::vec3{0,.50f,-.04f});
        target=body.transform_point((layout.wrists[0]+layout.wrists[1]+
                                    layout.ankles[0]+layout.ankles[1])*.25f);
    }
    if (root=="harrow_rearloader") {
        // Frame the full tall cab-over truck and inspect its actual cabin.
        const Transform fitted=player_car.fitted_body_transform(vehicle);
        if (view=="front" || view=="rear" || view=="side" || view=="passenger") {
            target=fitted.transform_point({0,1.75f,0});
            const glm::vec3 offset=view=="front"?glm::vec3{5.3f,2.4f,7.5f}:
                view=="rear"?glm::vec3{5.3f,2.4f,-7.5f}:
                glm::vec3{view=="side"?8.f:-8.f,.2f,0};
            camera.position=target+fitted.rotation*offset;
        } else if (view=="inside" || view=="cockpit") {
            target=fitted.transform_point({.58f,1.95f,2.62f});
            camera.position=fitted.transform_point(view=="inside"?
                glm::vec3{-.15f,2.62f,1.79f}:glm::vec3{3.8f,2.70f,1.30f});
        }
    }
    const glm::vec3 d=glm::normalize(target-camera.position);
    camera.yaw=std::atan2(d.x,-d.z);camera.pitch=std::asin(d.y);
    SkyEnv env=compute_sky_env(.48f);env.ambient=glm::vec3{.42f};env.light_color=glm::vec3{.92f};
    env.fog_density=0;env.fog_start=0;env.fog_end=0;
    env.snow_cover=snow_cover;
    HeadlightRig headlights;CanopyLightRig canopy;canopy.intensity=0;
    Renderer::Options options;int errors=0,draws=0;
    for (int frame=0;frame<frames;++frame) {
        SDL_Event event;while (SDL_PollEvent(&event)) {if (event.type==SDL_QUIT) return 1;}
        // Exercise entry, a frame of exit/other-car suppression, then return.
        // Final image always shows the state selected by command-line flags.
        characters.sync(crowd,player,player,.5f,frame,!occupied,player.position);
        if (production_car)
            player_car.sync(scene,vehicle_tuning,vehicle,vehicle,1.0f,0.0f,0.0f);
        if (transition!=VehicleTransitionDirection::None) {
            const auto tick=static_cast<uint32_t>(static_cast<uint64_t>(frame)*kVehicleTransitionTicks/
                static_cast<uint64_t>(std::max(1,frames-1)));
            characters.sync_transition(model,&body,{transition,tick},1.f);
            const float door_open=vehicle_transition_door_open(sample_vehicle_transition({transition,tick}));
            if (production_car) player_car.sync_driver_door(scene,door_open);
            else scene.set_transform(door_node,vehicle_driver_door_transform(model,body,door_open));
        } else if (frame==frames/2) {
            characters.sync_driver(model,true,&body);
            characters.sync_driver(PlayerCarId::VesperVx91,true,&body);
        } else {
            characters.sync_driver(other?PlayerCarId::VesperVx91:model,occupied,&body);
        }
        if (production_car) {
            if (driver_open>=0.f) player_car.sync_driver_door(scene,driver_open);
            player_car.sync_passenger_door(scene,passenger_open);
        }
        if (driver_glass!=kInvalidId) scene.set_transform(driver_glass,scene.get(door_node)->local);
        window.apply_viewport();glClearColor(.065f,.073f,.085f,1);
        glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));
        scene.update();
        const auto& visible=scene.cull(camera.frustum(),camera.position,50);
        renderer.render(scene,visible.visible,camera,env,headlights,canopy,options);
        characters.render(camera,env,headlights,canopy);draws=characters.last_draw_count();
        renderer.render_glass(scene,visible.visible,camera,env,headlights,canopy);
        const int expected=transition!=VehicleTransitionDirection::None?1:
            (frame==frames/2?0:((!occupied || (!other && has_animated_driver(model)))?1:0));
        if (draws!=expected) {std::fprintf(stderr,"driver visibility mismatch %d != %d\n",draws,expected);return 1;}
        while (glGetError()!=GL_NO_ERROR) ++errors;
        const int capture_step=sequence_step>0?sequence_step:std::max(1,(frames-1)/8);
        if (!sequence.empty() && (frame % capture_step==0 || frame+1==frames)) {
            char name[40];std::snprintf(name,sizeof(name),"/frame-%04d.png",frame);
            if (!screenshot(window,sequence+name)) return 1;
        }
        if (frame+1==frames && !screenshot(window,output)) return 1;
        window.swap();
    }
    std::printf("%s driver lab: %d frames, %d character draws, %d GL errors, snow %.2f, %s loader; %s\n",root.c_str(),frames,draws,errors,static_cast<double>(snow_cover),production_car?"production":"driver",output.c_str());
    if (production_car) player_car.destroy(scene);
    characters.destroy();renderer.destroy();window.shutdown();return errors==0?0:1;
}

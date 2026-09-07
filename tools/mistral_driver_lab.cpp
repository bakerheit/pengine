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
    PlayerCarId model=PlayerCarId::VesperMistral;
    VehicleTransitionDirection transition=VehicleTransitionDirection::None;
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if (arg=="--on-foot") occupied=false;
        else if (arg=="--other-car") other=true;
        else if (i+1<argc && arg=="--car") {
            const std::string car=argv[++i];
            const std::string key="/"+car+"/";
            bool found=false;
            for (const auto& candidate:kPlayerCars) {
                if (std::string{candidate.mesh_path}.find(key)!=std::string::npos &&
                    has_animated_driver(candidate.id)) {
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
        else { std::fprintf(stderr,"--car MODEL_FOLDER --view front|rear|side|passenger|cockpit|inside --screenshot PNG --sequence DIR --sequence-step N --transition enter|exit --frames N --on-foot --other-car\n");return 2; }
    }
    const bool workman=model==PlayerCarId::HarrowWorkman;
    const auto& definition=player_car_definition(model);
    const auto& layout=vehicle_driver_layout(model);
    const bool pip=model==PlayerCarId::AlderPip;
    const std::string model_path=definition.mesh_path;
    const auto slash=model_path.find_last_of('/');
    const auto parent=model_path.find_last_of('/',slash-1);
    const std::string root=model_path.substr(parent+1,slash-parent-1);
    Window window;WindowConfig config;config.title=root+" driver QA";
    config.width=1100;config.height=800;config.vsync=false;
    if (!window.init(config)) return 1;
    Renderer renderer;if (!renderer.init()) return 1;
    Scene scene;Transform body;
    const float length_scale=2.7f/(definition.wheel_front_z+definition.wheel_rear_z);
    body.scale={.78f/definition.wheel_x,length_scale,length_scale};
    StaticEmesh car;Texture paint;
    if (!read_static_emesh(asset_path("models/vehicles/"+root+"/body_open.emesh"),car) ||
        !paint.load_file(asset_path("textures/vehicles/"+root+"/body.png"))) return 1;
    Renderable r;r.mesh=renderer.add_mesh(car);r.material=renderer.add_material(std::move(paint));
    scene.create(r,body,car.bounds);
    NodeId door_node=kInvalidId;
    {
        StaticEmesh door;
        if (!read_static_emesh(asset_path("models/vehicles/"+root+"/driver_door.emesh"),door)) return 1;
        Renderable door_renderable=r;door_renderable.mesh=renderer.add_mesh(door);
        door_node=scene.create(door_renderable,body,door.bounds);
    }
    NodeId driver_glass=kInvalidId;
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
    scene.update();
    PlayerCharacterState player;player.position={1.5f,0,0};
    if (transition!=VehicleTransitionDirection::None) {
        player.position=body.transform_point({layout.approach_x,0,layout.hip.z})+glm::vec3{.55f,0,0};
        player.facing_yaw=transition==VehicleTransitionDirection::Exit ? 1.57079632679f : -1.57079632679f;
    }
    Crowd crowd;CharacterVisual characters;
    if (!characters.init(player)) return 1;
    Camera camera;camera.aspect=window.aspect();camera.near_plane=.03f;camera.far_plane=50.f;
    glm::vec3 target{0,.85f,0};camera.position={3.f,2.7f,4.1f};
    if (view=="rear") camera.position={2.8f,2.7f,-4.1f};
    if (view=="side") camera.position={4.2f,1.6f,0};
    if (view=="passenger") camera.position={-4.2f,1.6f,0};
    if (view=="inside") {camera.position={.34f,1.40f,-.18f};target={-.8f,1.3f,-.05f};}
    if (view=="cockpit") {camera.position={1.65f,2.5f,-1.95f};target={.35f,.95f,-.08f};}
    if (workman && view=="cockpit") {camera.position={2.4f,1.75f,.3f};target={.35f,1.10f,.1f};}
    const glm::vec3 d=glm::normalize(target-camera.position);
    camera.yaw=std::atan2(d.x,-d.z);camera.pitch=std::asin(d.y);
    SkyEnv env=compute_sky_env(.48f);env.ambient=glm::vec3{.42f};env.light_color=glm::vec3{.92f};
    env.fog_density=0;env.fog_start=0;env.fog_end=0;
    HeadlightRig headlights;CanopyLightRig canopy;canopy.intensity=0;
    Renderer::Options options;int errors=0,draws=0;
    for (int frame=0;frame<frames;++frame) {
        SDL_Event event;while (SDL_PollEvent(&event)) {if (event.type==SDL_QUIT) return 1;}
        // Exercise entry, a frame of exit/other-car suppression, then return.
        // Final image always shows the state selected by command-line flags.
        characters.sync(crowd,player,player,.5f,frame,!occupied,player.position);
        if (transition!=VehicleTransitionDirection::None) {
            const auto tick=static_cast<uint32_t>(static_cast<uint64_t>(frame)*kVehicleTransitionTicks/
                static_cast<uint64_t>(std::max(1,frames-1)));
            characters.sync_transition(model,&body,{transition,tick},1.f);
            scene.set_transform(door_node,vehicle_driver_door_transform(model,body,
                vehicle_transition_door_open(sample_vehicle_transition({transition,tick}))));
        } else if (frame==frames/2) {
            characters.sync_driver(model,true,&body);
            characters.sync_driver(PlayerCarId::VesperVx91,true,&body);
        } else {
            characters.sync_driver(other?PlayerCarId::VesperVx91:model,occupied,&body);
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
            (frame==frames/2?0:((!occupied || !other)?1:0));
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
    std::printf("%s driver lab: %d frames, %d character draws, %d GL errors; %s\n",root.c_str(),frames,draws,errors,output.c_str());
    characters.destroy();renderer.destroy();window.shutdown();return errors==0?0:1;
}

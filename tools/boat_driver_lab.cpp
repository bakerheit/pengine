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
#include "gfx/primitives.h"
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
    std::string output="build/marlin-pilot-front.png", view="front", sequence;
    int frames=120,sequence_step=0; bool occupied=true; bool other=false;
    float board_side=1.f;
    VehicleTransitionDirection transition=VehicleTransitionDirection::None;
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if (arg=="--on-foot") occupied=false;
        else if (arg=="--empty") other=true;
        else if (arg=="--port") board_side=-1.f;
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
        else { std::fprintf(stderr,"--view front|rear|side|cockpit --screenshot PNG --sequence DIR --sequence-step N --transition enter|exit --frames N --on-foot --empty --port\n");return 2; }
    }
    Window window;WindowConfig config;config.title="Marlin helm and boarding QA";
    config.width=1100;config.height=800;config.vsync=false;
    if (!window.init(config)) return 1;
    Renderer renderer;if (!renderer.init()) return 1;
    Scene scene;Transform body;
    body.scale={1,1,1};
    StaticEmesh car;Texture paint;
    if (!read_static_emesh(asset_path("models/vehicles/marlin_sprint/body.emesh"),car) ||
        !paint.load_file(asset_path("textures/vehicles/marlin_sprint/body.png"))) return 1;
    Renderable r;r.mesh=renderer.add_mesh(car);r.material=renderer.add_material(std::move(paint));
    scene.create(r,body,car.bounds);
    if(transition!=VehicleTransitionDirection::None) {
        const auto deck=make_box({1.f,.16f,2.6f});Texture timber;
        if(!timber.load_file(asset_path("textures/world/marina/weathered-timber.png"))) return 1;
        Renderable dock;dock.mesh=renderer.add_mesh(deck);dock.material=renderer.add_material(std::move(timber));
        Transform pose;pose.position={board_side*2.8f,.50f,-1.55f};
        scene.create(dock,pose,deck.bounds);
    }
    scene.update();
    PlayerCharacterState player;player.position={board_side*2.6f,.66f,-1.55f};
    player.facing_yaw=-board_side*1.57079632679f;
    Crowd crowd;CharacterVisual characters;
    if (!characters.init(player)) return 1;
    characters.prepare_boat_transition(player);
    Camera camera;camera.aspect=window.aspect();camera.near_plane=.03f;camera.far_plane=50.f;
    glm::vec3 target{0,.85f,-.5f};camera.position={4.f,3.5f,6.f};
    if (view=="rear") camera.position={4.f,3.5f,-6.f};
    if (view=="side") camera.position={6.f,2.6f,-.3f};
    if (view=="cockpit") {camera.position={2.2f,2.9f,-2.8f};target={.51f,.85f,-.65f};}
    camera.position.x*=board_side;
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
            const auto tick=static_cast<uint32_t>(static_cast<uint64_t>(frame)*kBoatTransitionTicks/
                static_cast<uint64_t>(std::max(1,frames-1)));
            characters.sync_boat_transition(&body,{transition,tick},1.f);
        } else if (frame==frames/2) {
            characters.sync_boat_driver(&body,true,0,0);
            characters.sync_boat_driver(&body,false,0,0);
        } else {
            characters.sync_boat_driver(&body,occupied && !other,0,0);
        }
        window.apply_viewport();glClearColor(.065f,.073f,.085f,1);
        glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));
        scene.update();
        const auto& visible=scene.cull(camera.frustum(),camera.position,50);
        renderer.render(scene,visible.visible,camera,env,headlights,canopy,options);
        characters.render(camera,env,headlights,canopy);draws=characters.last_draw_count();
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
    std::printf("Marlin driver lab: %d frames, %d character draws, %d GL errors; %s\n",frames,draws,errors,output.c_str());
    characters.destroy();renderer.destroy();window.shutdown();return errors==0?0:1;
}

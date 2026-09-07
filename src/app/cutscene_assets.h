#pragma once
#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "app/cutscene_pose.h"
#include "core/asset_root.h"
#include "gfx/renderer.h"
#include "gfx/sky.h"
#include "gfx/skinned_mesh.h"
#include "gfx/texture.h"
#include "scene/scene.h"

namespace apricot::cutscene_runtime {
namespace cs=apricot::cutscene;
namespace fs=std::filesystem;
inline std::string resolve(const std::string& path) {
    return fs::path(path).is_absolute()?path:asset_path(path);
}
struct ActorVisual {
    Scene* scene=nullptr;Renderer* renderer=nullptr;
    NodeId node=kInvalidId;MeshId static_mesh=kInvalidId;
    SkinnedMesh skin;Texture texture;Skeleton skeleton;Animation animation,walk;
    bool has_walk=false;
    AABB bounds;float scale=1,plant=0;
    std::vector<glm::mat4> local,matrices;
    std::vector<glm::vec4> real,dual;
    std::string error;
    ~ActorVisual() {
        if (node!=kInvalidId) scene->remove(node);
        if (static_mesh!=kInvalidId) renderer->remove_mesh(static_mesh);
    }
    bool init(const cs::Actor& actor,Scene& world,Renderer& render) {
        scene=&world;renderer=&render;
        fs::path skeleton_path=resolve(actor.mesh);skeleton_path.replace_extension(".eskel");
        if (fs::is_regular_file(skeleton_path)) {
            SkinnedEmesh source;
            if (!read_skinned_emesh(resolve(actor.mesh),source)||!skeleton.load(skeleton_path.string())||
                !skeleton.accepts(source)||actor.animation.empty()||
                !animation.load(resolve(actor.animation),skeleton)||animation.unresolved_channels()!=0||
                !skin.upload(source)||!texture.load_file(resolve(actor.texture))) {
                error="Could not load character mesh, skin or animation.";return false;
            }
            bounds=source.bounds;scale=actor.height/std::max(bounds.size().y,.01f);
            plant=locomotion_plant_offset(source,skeleton,animation,scale);
            if (!actor.walk_animation.empty()) {
                has_walk=walk.load(resolve(actor.walk_animation),skeleton)&&walk.unresolved_channels()==0;
                if (!has_walk) {error="Could not load walking animation.";return false;}
            }
        } else {
            StaticEmesh source;
            if (!read_static_emesh(resolve(actor.mesh),source)) {error="Could not load static mesh.";return false;}
            bounds=source.bounds;static_mesh=render.add_mesh(source);
            Renderable item;item.mesh=static_mesh;item.material=render.white_material();
            if (!actor.texture.empty()) {
                Texture paint;
                if (!paint.load_file(resolve(actor.texture))) {error="Texture not found.";return false;}
                item.material=render.add_material(std::move(paint));
            }
            node=world.create(item,{},bounds);
        }
        return true;
    }
    Transform transform(const cs::Actor& actor,float time) const {
        const float lift=skin.valid()?plant*(actor.height/std::max(bounds.size().y,.01f)/scale):0;
        return cs::actor_transform(actor,bounds,lift,time);
    }
    void sync(const cs::Actor& actor,float time) {
        if (node!=kInvalidId) scene->set_transform(node,transform(actor,time));
    }
    void draw(const cs::Actor& actor,float time,Shader& shader,const Camera& camera,
              const SkyEnv& env,const HeadlightRig& headlights,const CanopyLightRig& canopy) {
        if (!skin.valid()||!error.empty()) return;
        if (!cs::sample_actor_pose(actor,time,skeleton,animation,has_walk?&walk:nullptr,
                transform(actor,time),local)) {error="Hand pose needs a compatible arm rig.";return;}
        skeleton.compute_skin_matrices(local,matrices);
        skin_matrices_to_dual_quaternions(matrices,real,dual);
        shader.bind();shader.set_mat4("u_view_proj",camera.view_projection());
        shader.set_mat4("u_model",transform(actor,time).matrix());shader.set_int("u_diffuse",0);
        shader.set_vec4("u_tint",glm::vec4{1});
        shader.set_vec4_array("u_dq_real",real.data(),static_cast<int>(real.size()));
        shader.set_vec4_array("u_dq_dual",dual.data(),static_cast<int>(dual.size()));
        apply_lighting(shader,env,camera.position,headlights,canopy);
        shader.set_float("u_specular_strength",0);texture.bind(0);skin.draw();
    }
};
struct AudioPreview {
    SDL_AudioDeviceID device=0;
    std::unordered_map<std::string,std::vector<Uint8>> cache;
    std::vector<int> active;
    bool was_playing=false;float previous=0;
    ~AudioPreview() {if (device) SDL_CloseAudioDevice(device);}
    void init() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) return;
        SDL_AudioSpec want{};want.freq=24000;want.format=AUDIO_S16SYS;want.channels=1;want.samples=512;
        device=SDL_OpenAudioDevice(nullptr,0,&want,nullptr,0);
        if (device) SDL_PauseAudioDevice(device,0);
    }
    void stop() {if (device) SDL_ClearQueuedAudio(device);active.clear();was_playing=false;}
    const std::vector<Uint8>& load(const std::string& path) {
        if (cache.count(path)) return cache.at(path);
        auto& data=cache[path];SDL_AudioSpec spec{};Uint8* bytes=nullptr;Uint32 length=0;
        if (!SDL_LoadWAV(resolve(path).c_str(),&spec,&bytes,&length)) return data;
        SDL_AudioCVT cvt{};
        if (length>16u*1024u*1024u) {SDL_FreeWAV(bytes);return data;}
        if (SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,1,24000)<0) {
            SDL_FreeWAV(bytes);return data;
        }
        cvt.len=static_cast<int>(length);
        data.resize(static_cast<std::size_t>(cvt.len)*static_cast<std::size_t>(cvt.len_mult));
        std::copy(bytes,bytes+length,data.data());SDL_FreeWAV(bytes);cvt.buf=data.data();
        if (SDL_ConvertAudio(&cvt)<0) data.clear();else data.resize(static_cast<std::size_t>(cvt.len_cvt));
        return data;
    }
    void update(const cs::Document& doc,float time,bool playing,bool seek,std::string& status) {
        if (!device) return;
        if (!playing) {stop();previous=time;return;}
        if (seek||!was_playing||time<previous) {SDL_ClearQueuedAudio(device);active.clear();}
        // Dialogue is a single spoken track. The newest active cue wins on overlaps.
        int chosen=-1;
        for (std::size_t i=0;i<doc.cues.size();++i) {
            const auto& cue=doc.cues[i];
            if (!cue.audio.empty()&&time>=cue.start&&time<cue.start+cue.duration) chosen=static_cast<int>(i);
        }
        if (chosen<0) {if (!active.empty()) SDL_ClearQueuedAudio(device);active.clear();}
        else if (active.empty()||active[0]!=chosen) {
            SDL_ClearQueuedAudio(device);active={chosen};
            const auto& cue=doc.cues[static_cast<std::size_t>(chosen)];const auto& data=load(cue.audio);
            if (data.empty()) status="Could not play dialogue WAV: "+cue.audio;
            const auto offset=static_cast<std::size_t>(std::max(0.f,time-cue.start)*24000)*2u;
            if (offset<data.size()) SDL_QueueAudio(device,data.data()+offset,static_cast<Uint32>(data.size()-offset));
        }
        previous=time;was_playing=true;
    }
};

} // namespace apricot::cutscene_runtime

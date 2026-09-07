// Standalone native cutscene editor; shares production world and asset loaders.
#include <SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "cutscene_document.h"
#include "cutscene_pose.h"
#include "app/cutscene_assets.h"
#include "app/world.h"
#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "city/start_area.h"
#include "core/asset_root.h"
#include "core/skeletal_animation.h"
#include "gfx/gl_state.h"
#include "gfx/skinned_mesh.h"
#include "gfx/sky.h"
#include "gfx/ocean.h"
#include "gfx/tiled_lighting.h"
#include "platform/window.h"
#include "terrain/heightmap.h"

namespace {
using namespace apricot;
namespace cs=apricot::cutscene;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr float kSidebar=350, kTop=62, kBottom=190;
std::string resolve(const std::string& path) {
    return fs::path(path).is_absolute()?path:asset_path(path);
}
glm::vec3 site(float x,float y,float z) {
    const auto& s=city::kGasStationSite;
    return {s.origin.x+s.cos_yaw*x+s.sin_yaw*z,s.ground_m+y,
            s.origin.z-s.sin_yaw*x+s.cos_yaw*z};
}
cs::Document opening() {
    cs::Document doc;doc.title="Johnny Mercer - The lotto";
    // All three camera setups stay on the east side of the conversation axis.
    const cs::View together{site(3.0f,1.85f,-11.7f),site(-.4f,1.35f,-9.95f),58};
    const cs::View johnny{site(1.2f,1.72f,-10.5f),site(-.4f,1.57f,-8.75f),49};
    const cs::View lou{site(1.3f,1.75f,-9.2f),site(-.4f,1.58f,-11.2f),49};
    const auto add=[&](const char* name,float duration,const cs::View& view) {
        cs::Shot shot;shot.name=name;shot.duration=duration;shot.from=shot.to=view;
        doc.shots.push_back(shot);
    };
    add("01 - At the counter",9,together);
    add("02 - Lou hit it once",10,lou);
    add("03 - Picking numbers",5,johnny);
    add("04 - The bank's truck",10,lou);
    add("05 - Changing the subject",7,together);
    add("06 - Let me do it",11,johnny);
    add("07 - They...",10,lou);
    cs::Actor a;a.name="Johnny Mercer";
    a.mesh="models/characters/psx_pack/player_male_01/skin.emesh";
    a.texture="models/characters/psx_pack/player_male_01/body.png";
    a.animation="models/characters/psx_pack/animations/idle.eanim";
    a.from=a.to=site(-.4f,.15f,-8.75f);a.yaw_from=a.yaw_to=174;a.end=62;
    cs::Actor b=a;b.name="Lou - Manager";
    b.mesh="models/characters/psx_pack/civilian_male_05/skin.emesh";
    b.texture="models/characters/psx_pack/civilian_male_05/body.png";
    b.from=b.to=site(-.4f,.15f,-11.2f);b.height=1.8f;b.yaw_from=b.yaw_to=-6;
    doc.actors={a,b};
    doc.cues={{"Johnny","Lotto's up to twelve million.","",1,3},
              {"Lou","Don't waste your money.","",4.3f,2.2f},
              {"Johnny","Somebody's gotta win.","",6.8f,2.2f},
              {"Lou","I hit it once. Never played again.","",9.6f,4},
              {"Johnny","You? How much?","",14,2},
              {"Lou","Enough to buy this place.","",16.5f,2.5f},
              {"Johnny","No shit. What'd you do, pick birthdays?","",19.5f,4},
              {"Lou","Didn't pick numbers. Picked a truck.","",24,3.5f},
              {"Johnny","A truck?","",28,1.5f},
              {"Lou","Had the bank's name on the side.","",30,3.3f},
              {"Johnny","Right.","",34.5f,1},
              {"Lou","Anyway. I gotta go make a delivery.","",37,4},
              {"Johnny","Let me do it.","",41.5f,1.7f},
              {"Lou","You?","",43.6f,1},
              {"Johnny","Yeah. I've been in here all day. Let me get out of the store for a bit.","",45.3f,5.8f},
              {"Lou","Ya, actually, they... that might not be a bad idea.","",53,6.3f}};
    return doc;
}

Camera camera_from(const cs::View& v,float aspect) {
    Camera camera;camera.position=v.eye;camera.aspect=aspect;camera.near_plane=.05f;
    const glm::vec3 diff=v.target-v.eye;
    const glm::vec3 d=glm::length(diff)>.001f?glm::normalize(diff):glm::vec3{0,0,-1};
    camera.yaw=std::atan2(d.x,-d.z);camera.pitch=std::asin(std::clamp(d.y,-.999f,.999f));
    camera.fov_y=glm::radians(v.fov);camera.far_plane=2000;
    return camera;
}
cs::View capture(const Camera& camera,float focus) {
    return {camera.position,camera.position+camera.forward()*focus,glm::degrees(camera.fov_y)};
}
bool edit_text(const char* label,std::string& value,bool multiline=false) {
    char buffer[4096];std::snprintf(buffer,sizeof(buffer),"%s",value.c_str());
    const bool changed=multiline?ImGui::InputTextMultiline(label,buffer,sizeof(buffer),{0,75})
                                :ImGui::InputText(label,buffer,sizeof(buffer));
    if (changed) value=buffer;
    return changed;
}
bool screenshot(const Window& window,const fs::path& path) {
    std::vector<unsigned char> data(static_cast<std::size_t>(window.width())*
                                    static_cast<std::size_t>(window.height())*4u);
    glReadPixels(0,0,window.width(),window.height(),GL_RGBA,GL_UNSIGNED_BYTE,data.data());
    std::error_code ec;if (!path.parent_path().empty()) fs::create_directories(path.parent_path(),ec);
    stbi_flip_vertically_on_write(1);
    return !ec&&stbi_write_png(path.string().c_str(),window.width(),window.height(),4,
                              data.data(),window.width()*4)!=0;
}
using cutscene_runtime::ActorVisual;
using cutscene_runtime::AudioPreview;

struct Editor {
    Window window;Renderer renderer;Scene scene;World world;TerrainCollider collider{city::kMapSeed};
    Sky sky;Ocean ocean;Shader skin_shader;TiledLighting lighting;
    cs::Document doc=opening();std::vector<std::unique_ptr<ActorVisual>> actors;
    AudioPreview audio;
    Camera camera;float focus=8,time=0,move_speed=8;
    bool playing=false,loop=false,free_camera=false,viewer=false,running=true,dirty=false,seek=false;
    bool mouse_look=false,quit_requested=false;int shot=0,actor=0,cue=0,frames=0,frame_limit=0,errors=0;
    std::string project="",status="Space plays the scene. Tab opens the viewer.";
    std::string capture_path,export_dir,filter;
    int export_count=0;
    std::vector<std::string> meshes,textures,animations,wavs;
    bool init() {
        WindowConfig cfg;cfg.title="Probable Cause - Cutscene Studio";cfg.width=1440;cfg.height=900;
        cfg.vsync=frame_limit==0;
        if (!window.init(cfg)||!renderer.init()||!sky.init()||!ocean.init()||
            !skin_shader.build_from_files("shaders/skinned_character.vert","shaders/skinned_character.frag")) return false;
        IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
        const char* font="/System/Library/Fonts/Supplemental/Arial.ttf";
        if (fs::is_regular_file(font)) ImGui::GetIO().Fonts->AddFontFromFileTTF(font,16);
        ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=0;style.FrameRounding=4;
        style.Colors[ImGuiCol_WindowBg]={.09f,.105f,.12f,1};
        style.Colors[ImGuiCol_Button]={.22f,.27f,.3f,1};style.Colors[ImGuiCol_ButtonHovered]={.36f,.42f,.43f,1};
        style.Colors[ImGuiCol_CheckMark]={.95f,.66f,.27f,1};style.Colors[ImGuiCol_SliderGrab]={.95f,.66f,.27f,1};
        if (!ImGui_ImplSDL2_InitForOpenGL(window.sdl(),window.gl_context())||!ImGui_ImplOpenGL3_Init("#version 330 core")) return false;
        StreamerConfig streaming;streaming.load_radius=20;streaming.evict_radius=24;
        streaming.lod_ring[0]=4;streaming.lod_ring[1]=8;streaming.lod_ring[2]=14;
        if (!world.init(renderer,city::kMapSeed,streaming)||
            !world.set_roads(renderer,scene,collider,city::map_spines())||
            !world.set_starting_area(renderer,scene,collider)) return false;
        camera=camera_from(cs::sample(doc,time),window.aspect());
        world.fill(scene,renderer,camera.position);reload_actors();scan_assets();audio.init();
        std::size_t voice_count=0,voice_ready=0;
        for (const auto& voice_cue:doc.cues) {
            if (voice_cue.audio.empty()) continue;
            ++voice_count;
            if (!audio.load(voice_cue.audio).empty()) ++voice_ready;
            else status="Could not load dialogue WAV: "+voice_cue.audio;
        }
        if (voice_count&&!audio.device) status="Could not open the audio output device.";
        std::printf("Cutscene audio: %zu/%zu voice files loaded; output device %s.\n",
                    voice_ready,voice_count,audio.device?"ready":"unavailable");
        std::printf("Cutscene Studio ready: %zu world nodes, %zu staged actors, %zu model assets.\n",
                    scene.size(),actors.size(),meshes.size());std::fflush(stdout);return true;
    }
    void scan_assets() {
        const fs::path root=asset_root();
        for (const auto& entry:fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            const auto ext=entry.path().extension().string();
            const auto rel=fs::relative(entry.path(),root).generic_string();
            if (ext==".emesh") meshes.push_back(rel);
            else if (ext==".png") textures.push_back(rel);
            else if (ext==".eanim") animations.push_back(rel);
            else if (ext==".wav") wavs.push_back(rel);
        }
        for (auto* list:{&meshes,&textures,&animations,&wavs}) std::sort(list->begin(),list->end());
    }
    void reload_actors() {
        actors.clear();
        for (const auto& a:doc.actors) {
            auto visual=std::make_unique<ActorVisual>();
            if (!visual->init(a,scene,renderer)) status=a.name+": "+visual->error;
            actors.push_back(std::move(visual));
        }
    }
    void shutdown() {
        audio.stop();actors.clear();world.shutdown(scene,renderer);skin_shader.destroy();ocean.destroy();sky.destroy();
        lighting.destroy();renderer.destroy();ImGui_ImplOpenGL3_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();
    }
    bool picker(const char* label,std::string& value,const std::vector<std::string>& list) {
        bool changed=false;
        if (ImGui::BeginCombo(label,fs::path(value).filename().string().c_str())) {
            ImGui::PushID(label);edit_text("Filter",filter);
            for (const auto& path:list) {
                if (!filter.empty()&&path.find(filter)==std::string::npos) continue;
                if (ImGui::Selectable(path.c_str(),path==value)) {value=path;changed=true;filter.clear();}
            }
            ImGui::PopID();ImGui::EndCombo();
        }
        return changed;
    }
    void choose_mesh(cs::Actor& a) {
        fs::path path(a.mesh);auto body=path.parent_path()/"body.png";
        if (fs::exists(resolve(body.generic_string()))) a.texture=body.generic_string();
        else {
            auto relative=path.parent_path().generic_string();
            if (relative.rfind("models/",0)==0) relative.replace(0,7,"textures/");
            const auto proposed=relative+"/body.png";
            a.texture=fs::exists(resolve(proposed))?proposed:"";
        }
        path.replace_extension(".eskel");
        a.animation=fs::exists(resolve(path.generic_string()))?
            "models/characters/psx_pack/animations/idle.eanim":"";
    }
    void pane(const char* name,ImVec2 position,ImVec2 size) {
        ImGui::SetNextWindowPos(position);ImGui::SetNextWindowSize(size);
        ImGui::Begin(name,nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|
                     ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
    }
    void save_project() {
        std::string message;
        if (cs::save(project,doc,message)) {dirty=false;status="Saved "+fs::path(project).filename().string();}
        else status=message;
    }
    void toolbar(float width) {
        pane("Toolbar",{0,0},{width,kTop});
        ImGui::TextColored({.95f,.72f,.38f,1},"CUTSCENE STUDIO");ImGui::SameLine();
        ImGui::TextUnformatted("/ Probable Cause");ImGui::SameLine();
        if (ImGui::Button(playing?"Pause [Space]":"Play [Space]")) {
            if (time>=doc.duration()) time=0;
            playing=!playing;free_camera=false;seek=true;
        }
        ImGui::SameLine();if (ImGui::Button("Restart")) {time=0;seek=true;free_camera=false;}
        ImGui::SameLine();if (ImGui::Button("Viewer [Tab]")) {viewer=true;free_camera=false;}
        ImGui::SameLine();if (ImGui::Button("Save")) save_project();
        ImGui::SameLine();if (ImGui::Button("Screenshot")) {
            capture_path=fs::path(project).parent_path().string()+"/cutscene-preview.png";
        }
        ImGui::Text("%s%s",doc.title.c_str(),dirty?"  *":"");ImGui::End();
    }
    void sidebar(float height) {
        pane("Inspector",{0,kTop},{kSidebar,height-kTop-kBottom});
        ImGui::PushItemWidth(-132);
        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Shots")) {
                for (std::size_t i=0;i<doc.shots.size();++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(doc.shots[i].name.c_str(),shot==static_cast<int>(i))) {
                        shot=static_cast<int>(i);time=doc.shot_start(i);playing=false;free_camera=false;seek=true;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add shot")&&doc.shots.size()<256) {
                    cs::Shot s;s.from=s.to=capture(camera,focus);s.name="Shot "+std::to_string(doc.shots.size()+1);
                    doc.shots.push_back(s);shot=static_cast<int>(doc.shots.size()-1);dirty=true;
                }
                ImGui::SameLine();if (ImGui::Button("Delete")&&doc.shots.size()>1) {
                    doc.shots.erase(doc.shots.begin()+shot);shot=std::max(0,shot-1);dirty=true;
                    time=std::min(time,doc.duration());seek=true;
                }
                auto& s=doc.shots[static_cast<std::size_t>(shot)];ImGui::Separator();
                dirty|=edit_text("Name",s.name);
                dirty|=ImGui::SliderFloat("Seconds",&s.duration,.1f,60,"%.1f s");
                dirty|=ImGui::Checkbox("Ease camera movement",&s.smooth);
                ImGui::Checkbox("Free camera",&free_camera);
                ImGui::TextWrapped("Hold right mouse in the view. WASD move; Q/E down/up; Shift faster.");
                ImGui::SliderFloat("Fly speed",&move_speed,1,100,"%.0f m/s");
                ImGui::SliderFloat("Focus distance",&focus,.5f,100,"%.1f m");
                float lens=glm::degrees(camera.fov_y);
                if (ImGui::SliderFloat("Camera lens",&lens,15,100,"%.0f degrees")) {
                    camera.fov_y=glm::radians(lens);free_camera=true;
                }
                if (ImGui::Button("Capture start")) {s.from=capture(camera,focus);dirty=true;}
                ImGui::SameLine();if (ImGui::Button("Capture end")) {s.to=capture(camera,focus);dirty=true;}
                if (ImGui::Button("View start")) {camera=camera_from(s.from,camera.aspect);free_camera=true;}
                ImGui::SameLine();if (ImGui::Button("View end")) {camera=camera_from(s.to,camera.aspect);free_camera=true;}
                if (ImGui::Button("Hold still")) {s.to=s.from;dirty=true;}
                if (ImGui::TreeNode("Exact camera positions")) {
                    dirty|=ImGui::DragFloat3("Start eye",&s.from.eye.x,.1f);
                    dirty|=ImGui::DragFloat3("Start look",&s.from.target.x,.1f);
                    dirty|=ImGui::DragFloat3("End eye",&s.to.eye.x,.1f);
                    dirty|=ImGui::DragFloat3("End look",&s.to.target.x,.1f);ImGui::TreePop();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Cast")) {
                for (std::size_t i=0;i<doc.actors.size();++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(doc.actors[i].name.c_str(),actor==static_cast<int>(i))) actor=static_cast<int>(i);
                    ImGui::PopID();
                }
                if (ImGui::Button("Add actor / prop")&&doc.actors.size()<64) {
                    cs::Actor a=opening().actors[0];a.name="New actor";
                    a.from=a.to=camera.position+camera.forward()*focus;
                    a.from.y=a.to.y=collider.height(a.from.x,a.from.z);
                    doc.actors.push_back(a);actor=static_cast<int>(doc.actors.size()-1);reload_actors();dirty=true;
                }
                ImGui::SameLine();if (ImGui::Button("Remove")&&!doc.actors.empty()) {
                    doc.actors.erase(doc.actors.begin()+actor);actor=std::max(0,actor-1);reload_actors();dirty=true;
                }
                if (!doc.actors.empty()) {
                    auto& a=doc.actors[static_cast<std::size_t>(actor)];ImGui::Separator();
                    dirty|=edit_text("Actor name",a.name);bool reload=false;
                    if (picker("Model",a.mesh,meshes)) {choose_mesh(a);reload=true;}
                    reload|=picker("Texture",a.texture,textures);
                    reload|=picker("Animation",a.animation,animations);
                    reload|=picker("Walking clip",a.walk_animation,animations);
                    if (reload) {reload_actors();dirty=true;}
                    ImGui::TextWrapped("%s",a.mesh.c_str());
                    if (!actors[static_cast<std::size_t>(actor)]->error.empty())
                        ImGui::TextWrapped("%s",actors[static_cast<std::size_t>(actor)]->error.c_str());
                    dirty|=ImGui::DragFloat("Height (m)",&a.height,.01f,.05f,50,"%.2f");
                    dirty|=ImGui::DragFloat3("Start position",&a.from.x,.05f);
                    dirty|=ImGui::DragFloat("Start facing",&a.yaw_from,1,-360,360,"%.0f degrees");
                    if (ImGui::Button("Snap to ground")) {
                        a.from.y=collider.height(a.from.x,a.from.z);a.to.y=collider.height(a.to.x,a.to.z);dirty=true;
                    }
                    ImGui::SameLine();if (ImGui::Button("Frame actor")) {
                        const glm::vec3 target=cs::actor_position(a,time)+glm::vec3{0,a.height*.6f,0};
                        camera=camera_from({target+glm::vec3{0,.3f,4},target,45},camera.aspect);
                        focus=4;free_camera=true;playing=false;
                    }
                    if (ImGui::Button("Keep actor here")) {
                        if (!a.keys.empty()) {a.from=cs::actor_position(a,time);a.yaw_from=cs::actor_yaw(a,time);a.keys.clear();}
                        a.to=a.from;a.yaw_to=a.yaw_from;dirty=true;
                    }
                    if (ImGui::TreeNode("Movement")) {
                        if (!a.keys.empty()) ImGui::TextWrapped("The action keys below control this actor's route and hand poses.");
                        dirty|=ImGui::DragFloat3("End position",&a.to.x,.05f);
                        dirty|=ImGui::DragFloat("End facing",&a.yaw_to,1,-360,360,"%.0f degrees");
                        dirty|=ImGui::SliderFloat("Move starts",&a.start,0,doc.duration());
                        a.end=std::max(a.start,a.end);
                        dirty|=ImGui::SliderFloat("Move ends",&a.end,a.start,std::max(a.start,doc.duration()));
                        ImGui::TreePop();
                    }
                    if (ImGui::TreeNode("Action keys")) {
                        ImGui::TextWrapped("Positions and hand targets use world coordinates. Scrub to preview a key.");
                        if (ImGui::Button("Add key at playhead")&&a.keys.size()<2048) {
                            cs::ActorKey key=a.keys.empty()?cs::ActorKey{}:cs::actor_key(a,time);
                            key.position=cs::actor_position(a,time);key.yaw=cs::actor_yaw(a,time);key.time=time;
                            const auto existing=std::find_if(a.keys.begin(),a.keys.end(),[&](const auto& k){return std::abs(k.time-time)<.001f;});
                            if (existing==a.keys.end()) a.keys.push_back(key);else *existing=key;
                            std::sort(a.keys.begin(),a.keys.end(),[](const auto& x,const auto& y){return x.time<y.time;});dirty=true;
                        }
                        int remove_key=-1;
                        for (std::size_t k=0;k<a.keys.size();++k) {
                            ImGui::PushID(static_cast<int>(k));auto& key=a.keys[k];
                            if (ImGui::TreeNode("Key","%.2f seconds",static_cast<double>(key.time))) {
                                if (ImGui::Button("Preview")) {time=key.time;playing=false;seek=true;}
                                ImGui::SameLine();if (ImGui::Button("Delete")) remove_key=static_cast<int>(k);
                                const float earliest=k?a.keys[k-1].time+.001f:0;
                                const float latest=k+1<a.keys.size()?a.keys[k+1].time-.001f:std::max(doc.duration(),key.time);
                                dirty|=ImGui::DragFloat("Time",&key.time,.02f,earliest,latest,"%.2f seconds",ImGuiSliderFlags_AlwaysClamp);
                                dirty|=ImGui::DragFloat3("Position",&key.position.x,.025f);
                                dirty|=ImGui::DragFloat("Facing",&key.yaw,1);
                                dirty|=ImGui::SliderFloat("Hold / reach",&key.reach,0,1);
                                dirty|=ImGui::DragFloat3("Left hand",&key.left_hand.x,.01f);
                                dirty|=ImGui::DragFloat3("Right hand",&key.right_hand.x,.01f);
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        if (remove_key>=0) {a.keys.erase(a.keys.begin()+remove_key);dirty=true;}
                        ImGui::TreePop();
                    }
                    if (ImGui::TreeNode("Conversation gestures")) {
                        ImGui::TextWrapped("Timed gestures blend into the idle pose. Walking and box contact take priority.");
                        if (ImGui::Button("Add gesture here")&&a.gestures.size()<512) {
                            a.gestures.push_back({time,2,.7f,cs::GestureKind::Explain});dirty=true;
                        }
                        int remove_gesture=-1;
                        for (std::size_t g=0;g<a.gestures.size();++g) {
                            ImGui::PushID(static_cast<int>(g));auto& gesture=a.gestures[g];
                            if (ImGui::TreeNode("Gesture","Gesture %zu - %.2f s",g+1,static_cast<double>(gesture.start))) {
                                int kind=static_cast<int>(gesture.kind);
                                if (ImGui::Combo("Style",&kind,"Explain\0Dismiss\0Shrug\0Me / volunteer\0Nod\0Glance\0")) {
                                    gesture.kind=static_cast<cs::GestureKind>(kind);dirty=true;
                                }
                                dirty|=ImGui::DragFloat("Starts",&gesture.start,.05f,0,doc.duration(),"%.2f s",ImGuiSliderFlags_AlwaysClamp);
                                dirty|=ImGui::DragFloat("Lasts",&gesture.duration,.05f,.2f,60,"%.2f s",ImGuiSliderFlags_AlwaysClamp);
                                dirty|=ImGui::SliderFloat("Amount",&gesture.strength,0,1);
                                if (ImGui::Button("Preview gesture")) {time=gesture.start+gesture.duration*.4f;playing=false;seek=true;}
                                ImGui::SameLine();if (ImGui::Button("Remove gesture")) remove_gesture=static_cast<int>(g);
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        if (remove_gesture>=0) {a.gestures.erase(a.gestures.begin()+remove_gesture);dirty=true;}
                        ImGui::TreePop();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Dialogue")) {
                for (std::size_t i=0;i<doc.cues.size();++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const std::string label=doc.cues[i].speaker+": "+doc.cues[i].text;
                    if (ImGui::Selectable(label.c_str(),cue==static_cast<int>(i))) {
                        cue=static_cast<int>(i);time=doc.cues[i].start;seek=true;playing=false;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add line")&&doc.cues.size()<512) {
                    doc.cues.push_back({"Johnny","New line","",time,3});
                    cue=static_cast<int>(doc.cues.size()-1);dirty=true;
                }
                ImGui::SameLine();if (ImGui::Button("Delete line")&&!doc.cues.empty()) {
                    doc.cues.erase(doc.cues.begin()+cue);cue=std::max(0,cue-1);dirty=true;audio.stop();
                }
                if (!doc.cues.empty()) {
                    auto& c=doc.cues[static_cast<std::size_t>(cue)];ImGui::Separator();
                    dirty|=edit_text("Speaker",c.speaker);dirty|=edit_text("Subtitle",c.text,true);
                    dirty|=ImGui::SliderFloat("At",&c.start,0,doc.duration(),"%.2f s");
                    dirty|=ImGui::SliderFloat("Length",&c.duration,.1f,60,"%.2f s");
                    if (picker("Voice WAV",c.audio,wavs)) {dirty=true;seek=true;audio.cache.clear();}
                    dirty|=edit_text("Audio path",c.audio);
                    if (ImGui::Button("Clear voice")) {c.audio.clear();dirty=true;audio.stop();}
                    ImGui::SameLine();if (ImGui::Button("Play line")) {time=c.start;playing=true;free_camera=false;seek=true;}
                    ImGui::TextWrapped("Use a recorded WAV when ready. Empty voice tracks show subtitles only.");
                    if (!audio.device) ImGui::TextWrapped("Audio device unavailable; subtitle preview still works.");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Project")) {
                dirty|=edit_text("Title",doc.title);
                dirty|=ImGui::SliderFloat("Time of day",&doc.daylight,0,1);
                edit_text("Project file",project);
                if (ImGui::Button("Save project")) save_project();
                if (ImGui::Button("Reload saved project")) {
                    if (dirty) ImGui::OpenPopup("Discard edits?");else load_project();
                }
                if (ImGui::BeginPopupModal("Discard edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Reload replaces your unsaved scene edits.");
                    if (ImGui::Button("Reload")) {load_project();ImGui::CloseCurrentPopup();}
                    ImGui::SameLine();if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
                ImGui::Separator();ImGui::TextWrapped("World and assets come directly from this game's asset folder. Scene saves contain references, not copies.");
                ImGui::TextWrapped("Asset root: %s",asset_root().c_str());
                ImGui::TextWrapped("This is a blocking editor. Character clips loop; hand props, lip sync and final acting are not authored yet.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();ImGui::End();
    }
    void load_project() {
        std::string message;
        if (!cs::load(project,doc,message)) {status=message;return;}
        shot=actor=cue=0;time=0;playing=false;free_camera=false;dirty=false;seek=true;
        audio.stop();audio.cache.clear();reload_actors();status="Project reloaded.";
    }
    void timeline(float width,float height) {
        pane("Timeline",{0,height-kBottom},{width,kBottom});
        ImGui::Text("TIMELINE    %.2f / %.2f s",static_cast<double>(time),static_cast<double>(doc.duration()));
        ImGui::SameLine();ImGui::Checkbox("Loop",&loop);
        ImGui::SameLine();ImGui::TextUnformatted("Left/Right: one frame   Home: start   Tab: viewer");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##playhead",&time,0,doc.duration(),"%.2f s")) {
            playing=false;free_camera=false;seek=true;
        }
        const float available=ImGui::GetContentRegionAvail().x;
        for (std::size_t i=0;i<doc.shots.size();++i) {
            if (i) ImGui::SameLine(0,3);
            ImGui::PushID(static_cast<int>(i));
            const bool current=cs::shot_at(doc,time)==i;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button,{.43f,.30f,.15f,1});
            if (ImGui::Button(doc.shots[i].name.c_str(),{std::max(20.f,(available-3*static_cast<float>(doc.shots.size()))*doc.shots[i].duration/doc.duration()),30})) {
                shot=static_cast<int>(i);time=doc.shot_start(i);playing=false;free_camera=false;seek=true;
            }
            if (current) ImGui::PopStyleColor();ImGui::PopID();
        }
        ImGui::Separator();ImGui::TextWrapped("%s",status.c_str());ImGui::End();
    }
    void events(float dt,float width,float height) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type==SDL_QUIT) {
                if (dirty&&!frame_limit) quit_requested=true;else running=false;
            }
            if (e.type==SDL_WINDOWEVENT) {
                int w=0,h=0;SDL_GL_GetDrawableSize(window.sdl(),&w,&h);window.on_resize(w,h);
                if (e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {mouse_look=false;SDL_SetRelativeMouseMode(SDL_FALSE);}
            }
            if (e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_RIGHT&&
                (viewer||(e.button.x>=kSidebar&&e.button.y>=kTop&&e.button.y<height-kBottom))) {
                mouse_look=true;free_camera=true;playing=false;SDL_SetRelativeMouseMode(SDL_TRUE);
            }
            if (e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_RIGHT) {
                mouse_look=false;SDL_SetRelativeMouseMode(SDL_FALSE);
            }
            if (e.type==SDL_MOUSEMOTION&&mouse_look) camera.add_look(static_cast<float>(e.motion.xrel)*.003f,static_cast<float>(e.motion.yrel)*.003f);
            if (e.type==SDL_KEYDOWN&&!e.key.repeat&&!ImGui::GetIO().WantTextInput) {
                switch(e.key.keysym.sym) {
                    case SDLK_SPACE: if(time>=doc.duration())time=0;playing=!playing;free_camera=false;seek=true;break;
                    case SDLK_TAB:viewer=!viewer;free_camera=false;break;
                    case SDLK_ESCAPE:viewer=false;mouse_look=false;SDL_SetRelativeMouseMode(SDL_FALSE);break;
                    case SDLK_HOME:time=0;playing=false;free_camera=false;seek=true;break;
                    case SDLK_LEFT:time=std::max(0.f,time-1.f/30);playing=false;free_camera=false;seek=true;break;
                    case SDLK_RIGHT:time=std::min(doc.duration(),time+1.f/30);playing=false;free_camera=false;seek=true;break;
                    case SDLK_s:if (e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI)) save_project();break;
                    default:break;
                }
            }
        }
        if (mouse_look) {
            const auto* k=SDL_GetKeyboardState(nullptr);const float speed=move_speed*dt*(k[SDL_SCANCODE_LSHIFT]?4.f:1.f);
            camera.position+=camera.forward()*speed*static_cast<float>(k[SDL_SCANCODE_W]-k[SDL_SCANCODE_S]);
            camera.position+=camera.right()*speed*static_cast<float>(k[SDL_SCANCODE_D]-k[SDL_SCANCODE_A]);
            camera.position.y+=speed*static_cast<float>(k[SDL_SCANCODE_E]-k[SDL_SCANCODE_Q]);
        }
        (void)width;
    }
    int run() {
        auto previous=Clock::now();
        while (running) {
            const auto now=Clock::now();float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),0.f,.1f);previous=now;
            if (frame_limit) dt=1.f/30;
            int lw=0,lh=0;SDL_GetWindowSize(window.sdl(),&lw,&lh);
            const float width=static_cast<float>(lw),height=static_cast<float>(lh);
            events(dt,width,height);if (!running)break;
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
            if (quit_requested) {ImGui::OpenPopup("Save before closing?");quit_requested=false;}
            if (ImGui::BeginPopupModal("Save before closing?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("This cutscene has unsaved edits.");
                if (ImGui::Button("Save and close")) {save_project();if(!dirty)running=false;}
                ImGui::SameLine();if (ImGui::Button("Discard and close"))running=false;
                ImGui::SameLine();if (ImGui::Button("Keep editing"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
            }
            if (playing) {
                time+=dt;
                if (time>=doc.duration()) {if(loop){time=std::fmod(time,doc.duration());seek=true;}else{time=doc.duration();playing=false;}}
            }
            if (!export_dir.empty()) {time=static_cast<float>(frames)/30;viewer=true;free_camera=false;}
            if (!viewer) {toolbar(width);sidebar(height);timeline(width,height);}
            const float left=viewer?0:kSidebar,top=viewer?0:kTop;
            const float vw=std::max(1.f,width-left),vh=std::max(1.f,height-top-(viewer?0:kBottom));
            if (!free_camera) camera=camera_from(cs::sample(doc,time),vw/vh);else camera.aspect=vw/vh;
            audio.update(doc,time,playing,seek,status);seek=false;
            world.update(scene,renderer,camera.position);
            for (std::size_t i=0;i<actors.size();++i) actors[i]->sync(doc.actors[i],time);
            scene.update();
            if (!window.minimised()) {
                const float sx=static_cast<float>(window.width())/std::max(1.f,width);
                const float sy=static_cast<float>(window.height())/std::max(1.f,height);
                glViewport(0,0,window.width(),window.height());glClearColor(.04f,.045f,.05f,1);
                glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));
                glViewport(static_cast<int>(left*sx),static_cast<int>((viewer?0:kBottom)*sy),
                           static_cast<int>(vw*sx),static_cast<int>(vh*sy));
                const SkyEnv env=compute_sky_env(doc.daylight);HeadlightRig headlights;
                std::vector<TrafficSpotLight> interior;
                for (const auto& part:city::kGasStationParts) {
                    if (std::string_view(part.name)!="store interior ceiling light lens") continue;
                    const auto at=site(part.centre.x,part.bottom_m-.04f,part.centre.z);
                    if (glm::distance(at,camera.position)<65.f)
                        interior.push_back({glm::vec4{at,6.6f},{0,-1,0,2.3f},{1,.88f,.70f,.58f}});
                }
                // The production light shader uses framebuffer coordinates.
                // Map its tile projection into the editor's offset viewport.
                glm::mat4 screen(1);
                screen[0][0]=vw/std::max(1.f,width);screen[1][1]=vh/std::max(1.f,height);
                screen[3][0]=(2*left+vw)/std::max(1.f,width)-1;
                screen[3][1]=(2*(viewer?0:kBottom)+vh)/std::max(1.f,height)-1;
                if (!lighting.upload(interior,screen*camera.view_projection(),camera.view(),
                                     window.width(),window.height())) {
                    ++errors;running=false;
                }
                headlights.traffic=lighting.view();
                const auto& canopy=world.canopy_lights();sky.render(camera,env,time);
                const auto& visible=scene.cull(camera.frustum(),camera.position,1800);
                renderer.render(scene,visible.visible,camera,env,headlights,canopy,{});
                for (std::size_t i=0;i<actors.size();++i) actors[i]->draw(doc.actors[i],time,skin_shader,camera,env,headlights,canopy);
                ocean.render(camera,env,headlights,canopy,time,
                             kSeaLevelMetres);
                renderer.render_glass(scene,visible.visible,camera,env,headlights,canopy);
                auto* draw=ImGui::GetForegroundDrawList();
                for (const auto& c:doc.cues) if (time>=c.start&&time<c.start+c.duration) {
                    const std::string text=c.speaker+": "+c.text;
                    const auto size=ImGui::CalcTextSize(text.c_str(),nullptr,false,vw-80);
                    const ImVec2 pos{left+(vw-size.x)*.5f,top+vh-size.y-34};
                    draw->AddRectFilled({pos.x-12,pos.y-8},{pos.x+size.x+12,pos.y+size.y+8},IM_COL32(0,0,0,210),5);
                    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),pos,IM_COL32(255,245,220,255),text.c_str(),nullptr,vw-80);
                }
                if (!viewer) draw->AddText({left+16,top+14},IM_COL32(255,233,190,255),free_camera?"FREE CAMERA":"SHOT PREVIEW");
                ImGui::Render();glViewport(0,0,window.width(),window.height());
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());gl_state::invalidate_all();
                if (!capture_path.empty()&&(!frame_limit||frames+1>=frame_limit)) {
                    if (!screenshot(window,capture_path)) {status="Screenshot failed.";++errors;}
                    else status="Screenshot saved.";capture_path.clear();
                }
                if (!export_dir.empty()) {
                    char filename[40];std::snprintf(filename,sizeof(filename),"frame-%05d.png",frames);
                    if (!screenshot(window,fs::path(export_dir)/filename)) {++errors;running=false;}
                }
                for(int n=0;n<32;++n) {const auto err=glGetError();if(err==GL_NO_ERROR)break;++errors;std::fprintf(stderr,"GL error: %u\n",err);}
                window.swap();++frames;
            } else SDL_Delay(20);
            if (frame_limit&&frames>=frame_limit)running=false;
            if (export_count&&frames>=export_count)running=false;
        }
        std::printf("Cutscene Studio: %d frames, %d GL errors, %zu actors, %.2f seconds.\n",frames,errors,actors.size(),static_cast<double>(doc.duration()));
        return errors?1:0;
    }
};
} // namespace
int main(int argc,char** argv) {
    Editor editor;editor.project=(fs::path(asset_root())/"cutscenes/johnny-opening.cutscene").string();
    std::string scene_file;bool create_default=false;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--viewer")editor.viewer=true;
        else if(arg=="--play")editor.playing=true;
        else if(arg=="--create-default")create_default=true;
        else if(arg=="--help") {
            std::puts("apricot_cutscene_lab [--scene FILE] [--viewer] [--play] [--time SECONDS] [--frames N] [--screenshot PNG] [--export-frames DIR] [--create-default]\nStandalone shared-world cutscene viewer/editor. Tab: viewer, Space: play, right mouse + WASD/QE: fly.");return 0;
        } else if(i+1<argc&&arg=="--scene")scene_file=argv[++i];
        else if(i+1<argc&&arg=="--screenshot")editor.capture_path=argv[++i];
        else if(i+1<argc&&arg=="--export-frames")editor.export_dir=argv[++i];
        else if(i+1<argc&&arg=="--frames")editor.frame_limit=std::max(1,std::stoi(argv[++i]));
        else if(i+1<argc&&arg=="--time")editor.time=std::max(0.f,std::stof(argv[++i]));
        else {std::fprintf(stderr,"Unknown or incomplete option: %s\n",arg.c_str());return 2;}
    }
    if(!scene_file.empty())editor.project=scene_file;
    if(fs::exists(editor.project)) {
        std::string load_error;
        if(!cs::load(editor.project,editor.doc,load_error)) {std::fprintf(stderr,"%s\n",load_error.c_str());return 1;}
    } else if(!scene_file.empty()&&!create_default) {std::fprintf(stderr,"Scene does not exist.\n");return 1;}
    if(create_default) {
        if(fs::exists(editor.project)) {std::fprintf(stderr,"Refusing to overwrite an existing scene.\n");return 1;}
        if(!cs::save(editor.project,editor.doc,editor.status))return 1;
    }
    if(!editor.export_dir.empty()) {
        editor.viewer=true;editor.playing=false;
        editor.export_count=static_cast<int>(std::ceil(editor.doc.duration()*30));
    }
    if(!editor.init())return 1;
    const int result=editor.run();editor.shutdown();return result;
}

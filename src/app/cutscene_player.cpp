#include "app/cutscene_player.h"
#include "app/cutscene_assets.h"
#include "game/cutscene_playback.h"
#include "gfx/hud.h"
#include <sstream>

namespace apricot {
struct CutscenePlayer::Impl {
    cutscene::Document doc;
    cutscene::Playback playback;
    std::vector<std::unique_ptr<cutscene_runtime::ActorVisual>> actors;
    cutscene_runtime::AudioPreview audio;
    Shader shader;
    std::string status,project;
    void visible(bool show) {
        for (auto& actor:actors) if (auto* node=actor->scene->get(actor->node)) node->visible=show;
    }
};
CutscenePlayer::CutscenePlayer()=default;
CutscenePlayer::~CutscenePlayer()=default;
bool CutscenePlayer::start(Scene& scene,Renderer& renderer,std::string& error,const std::string& project) {
    if (!impl_ || impl_->project!=project) {
        auto next=std::make_unique<Impl>();
        if (!cutscene::load(asset_path(project),next->doc,error)) return false;
        if (next->doc.actors.size()<2) {error="Cutscene has no cast.";return false;}
        if (!next->shader.build_from_files("shaders/skinned_character.vert","shaders/skinned_character.frag")) {
            error="Cutscene character shader could not load.";return false;
        }
        for (const auto& actor:next->doc.actors) {
            auto visual=std::make_unique<cutscene_runtime::ActorVisual>();
            if (!visual->init(actor,scene,renderer)) {error=actor.name+": "+visual->error;return false;}
            next->actors.push_back(std::move(visual));
        }
        for (const auto& cue:next->doc.cues) if (!cue.audio.empty()&&next->audio.load(cue.audio).empty()) {
            error="Cutscene dialogue could not load: "+cue.audio;return false;
        }
        next->audio.init();
        next->project=project;
        impl_=std::move(next);
    }
    impl_->playback.start(impl_->doc.duration());
    impl_->visible(true);sync();return true;
}
void CutscenePlayer::destroy() {impl_.reset();}
bool CutscenePlayer::active() const {return impl_&&impl_->playback.active;}
bool CutscenePlayer::paused() const {return impl_&&impl_->playback.paused;}
void CutscenePlayer::toggle_pause() {
    if (active()) {impl_->playback.paused=!paused();impl_->audio.stop();}
}
bool CutscenePlayer::advance(float dt) {
    if (!active()) return false;
    const bool finished=impl_->playback.advance(dt);
    impl_->audio.update(impl_->doc,time(),active()&&!paused(),false,impl_->status);
    if (finished) stop();return finished;
}
bool CutscenePlayer::skip() {
    if (!active()) return false;
    impl_->playback.skip();stop();return true;
}
void CutscenePlayer::stop() {
    if (!impl_) return;
    impl_->audio.stop();impl_->playback.active=false;impl_->visible(false);
}
float CutscenePlayer::time() const {return impl_?impl_->playback.time:0;}
float CutscenePlayer::daylight() const {return impl_?impl_->doc.daylight:.46f;}
Camera CutscenePlayer::camera(float aspect) const {
    const auto view=cutscene::sample(impl_->doc,time());
    Camera result;result.position=view.eye;result.aspect=aspect;result.near_plane=.05f;result.far_plane=2000;
    const auto direction=glm::normalize(view.target-view.eye);
    result.yaw=std::atan2(direction.x,-direction.z);
    result.pitch=std::asin(std::clamp(direction.y,-.999f,.999f));result.fov_y=glm::radians(view.fov);
    return result;
}
glm::vec3 CutscenePlayer::handoff_position() const {
    return cutscene::actor_position(impl_->doc.actors.front(),impl_->doc.duration());
}
float CutscenePlayer::handoff_yaw() const {
    // Scene models face +Z at zero; the player controller faces -Z.
    return glm::radians(180.f-cutscene::actor_yaw(impl_->doc.actors.front(),impl_->doc.duration()));
}
void CutscenePlayer::sync() {
    if (active()) for (std::size_t i=0;i<impl_->actors.size();++i)
        impl_->actors[i]->sync(impl_->doc.actors[i],time());
}
void CutscenePlayer::draw(const Camera& camera,const SkyEnv& env,const HeadlightRig& lights,const CanopyLightRig& canopy) {
    if (active()) for (std::size_t i=0;i<impl_->actors.size();++i)
        impl_->actors[i]->draw(impl_->doc.actors[i],time(),impl_->shader,camera,env,lights,canopy);
}
void CutscenePlayer::draw_hud(Hud& hud,glm::vec2 vp) const {
    hud.rect({0,0},{vp.x,60},{0,0,0,1});hud.rect({0,vp.y-136},{vp.x,vp.y},{0,0,0,.86f});
    const auto label=paused()?"PAUSED  |  P / START: RESUME  |  ESC / B: SKIP":"P / START: PAUSE  |  ESC / B: SKIP";
    hud.text_centered(label,vp.x*.5f,17,24,{.8f,.8f,.8f,1});
    for (const auto& cue:impl_->doc.cues) if(time()>=cue.start&&time()<cue.start+cue.duration) {
        std::istringstream words(cue.speaker+": "+cue.text);std::string word,line;
        std::vector<std::string> lines;
        while (words>>word) {
            auto next=line.empty()?word:line+" "+word;
            if(!line.empty()&&hud.measure_text(next.c_str(),40)>vp.x-90) {lines.push_back(line);line=word;}
            else line=std::move(next);
        }
        if(!line.empty()) lines.push_back(line);
        float y=vp.y-112;
        for(const auto& text:lines) {hud.text_centered(text.c_str(),vp.x*.5f,y,40,{1,.97f,.89f,1});y+=44;}
    }
}
}

#pragma once
#include <memory>
#include <string>
#include "game/cutscene_document.h"
#include "gfx/camera.h"
#include "gfx/lighting.h"
#include "gfx/sky_env.h"

namespace apricot {
class Renderer;class Scene;class Hud;
class CutscenePlayer {
public:
    CutscenePlayer();
    ~CutscenePlayer();
    bool start(Scene&,Renderer&,std::string& error,
               const std::string& project="cutscenes/johnny-opening.cutscene");
    void destroy();
    bool active() const;
    bool paused() const;
    void toggle_pause();
    bool advance(float dt);
    bool skip();
    void stop();
    float time() const;
    float daylight() const;
    Camera camera(float aspect) const;
    glm::vec3 handoff_position() const;
    float handoff_yaw() const;
    void sync();
    void draw(const Camera&,const SkyEnv&,const HeadlightRig&,const CanopyLightRig&);
    void draw_hud(Hud&,glm::vec2 viewport) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

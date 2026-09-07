#pragma once
#include "game/weapon.h"
#include "gfx/hud.h"

namespace apricot {
inline void draw_weapon_wheel(Hud& hud,const WeaponWheel& wheel,glm::vec2 viewport) {
    if(!wheel.open) return;
    const glm::vec2 center=viewport*.5f;
    const float radius=std::min(190.f,viewport.y*.28f);
    hud.rect({0,0},viewport,{0,0,0,.45f});
    for(int half=0;half<2;++half) {
        const bool selected=(wheel.hovered==WeaponId::Pistol)==(half==0);
        const glm::vec4 color=selected ? glm::vec4{.79f,.52f,.15f,.95f}:glm::vec4{.08f,.09f,.10f,.96f};
        for(int i=0;i<48;++i) {
            const float a=-1.5707963f+static_cast<float>(half)*3.1415927f+static_cast<float>(i)*3.1415927f/48.f;
            const float b=a+3.1415927f/48.f;
            const glm::vec2 va{std::cos(a),std::sin(a)},vb{std::cos(b),std::sin(b)};
            hud.quad(center+va*55.f,center+va*radius,center+vb*radius,center+vb*55.f,color);
        }
    }
    hud.circle(center,51,{.02f,.025f,.03f,.98f});
    hud.line(center+glm::vec2{0,-radius},center+glm::vec2{0,-56},3,{0,0,0,.8f});
    hud.line(center+glm::vec2{0,56},center+glm::vec2{0,radius},3,{0,0,0,.8f});
    const float offset=radius*.63f;
    const glm::vec4 ink{1,.97f,.86f,1};
    const glm::vec2 gun=center+glm::vec2{offset-28,-26};
    hud.rect(gun,gun+glm::vec2{57,13},ink);
    hud.quad(gun+glm::vec2{7,13},gun+glm::vec2{26,13},gun+glm::vec2{20,40},gun+glm::vec2{4,40},ink);
    hud.outline(gun+glm::vec2{24,13},gun+glm::vec2{42,27},3,ink);
    const glm::vec2 fist=center+glm::vec2{-offset-23,-24};
    hud.rect(fist+glm::vec2{0,12},fist+glm::vec2{46,39},ink);
    for(int i=0;i<4;++i) hud.rect(fist+glm::vec2{static_cast<float>(i)*12,0},
        fist+glm::vec2{static_cast<float>(i)*12+10,21},ink);
    hud.text_centered("UNARMED",center.x-offset,center.y+33,20,ink);
    hud.text_centered("PISTOL",center.x+offset,center.y+33,20,ink);
    hud.text_centered("WEAPONS",center.x,center.y-radius-43,29,ink);
    hud.text_centered("RELEASE TAB / LB TO EQUIP",center.x,center.y+radius+22,20,ink);
    hud.text_centered("MOVE MOUSE OR STICK  |  ESC / B TO CANCEL",center.x,center.y+radius+49,15,{.8f,.82f,.83f,1});
}
} // namespace apricot

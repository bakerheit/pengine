#pragma once
#include "game/weapon.h"
#include "gfx/hud.h"

namespace apricot {

// THE SECTORS DRAWN HERE AND THE SECTORS WeaponWheel::point() TESTS ARE THE
// SAME SECTORS. Both come from kWeaponWheelOrder and kWeaponSlotCount, and the
// angle convention below — clockwise from straight up, screen y down — is the
// one point() documents. The previous two-slot wheel wrote its halves out by
// hand in both places, which was survivable at two and is exactly how a wheel
// ends up highlighting one weapon and equipping another.
inline glm::vec2 weapon_wheel_direction(std::size_t slot) {
    const float turns=static_cast<float>(slot)*kWeaponWheelSectorTurns;
    const float radians=turns*6.28318531f;
    return {std::sin(radians),-std::cos(radians)};
}

inline void draw_weapon_wheel(Hud& hud,const WeaponWheel& wheel,glm::vec2 viewport) {
    if(!wheel.open) return;
    const glm::vec2 center=viewport*.5f;
    const float radius=std::min(190.f,viewport.y*.28f);
    constexpr float kHub=55.f;
    hud.rect({0,0},viewport,{0,0,0,.45f});

    constexpr int kArcSteps=32;
    for(std::size_t slot=0;slot<kWeaponSlotCount;++slot) {
        const bool selected=wheel.hovered==kWeaponWheelOrder[slot];
        const glm::vec4 color=selected ? glm::vec4{.79f,.52f,.15f,.95f}:glm::vec4{.08f,.09f,.10f,.96f};
        const float begin=(static_cast<float>(slot)-.5f)*kWeaponWheelSectorTurns*6.28318531f;
        const float sweep=kWeaponWheelSectorTurns*6.28318531f/static_cast<float>(kArcSteps);
        for(int i=0;i<kArcSteps;++i) {
            const float a=begin+static_cast<float>(i)*sweep;
            const float b=a+sweep;
            const glm::vec2 va{std::sin(a),-std::cos(a)},vb{std::sin(b),-std::cos(b)};
            hud.quad(center+va*kHub,center+va*radius,center+vb*radius,center+vb*kHub,color);
        }
        // The divider sits on the sector BOUNDARY, not beside it, so the seam
        // the eye follows is the seam the pointer crosses.
        const glm::vec2 edge{std::sin(begin),-std::cos(begin)};
        hud.line(center+edge*(kHub+1.f),center+edge*radius,3,{0,0,0,.8f});
    }
    hud.circle(center,51,{.02f,.025f,.03f,.98f});

    const float offset=radius*.66f;
    const glm::vec4 ink{1,.97f,.86f,1};
    const glm::vec4 flame{1,.62f,.16f,1};
    const auto anchor=[&](std::size_t slot) {
        return center+weapon_wheel_direction(slot)*offset;
    };

    // Unarmed: a fist, knuckles up.
    const glm::vec2 fist=anchor(0)+glm::vec2{-23,-26};
    hud.rect(fist+glm::vec2{0,12},fist+glm::vec2{46,39},ink);
    for(int i=0;i<4;++i) hud.rect(fist+glm::vec2{static_cast<float>(i)*12,0},
        fist+glm::vec2{static_cast<float>(i)*12+10,21},ink);

    // Pistol: slide, grip, trigger guard.
    const glm::vec2 gun=anchor(1)+glm::vec2{-28,-26};
    hud.rect(gun,gun+glm::vec2{57,13},ink);
    hud.quad(gun+glm::vec2{7,13},gun+glm::vec2{26,13},gun+glm::vec2{20,40},gun+glm::vec2{4,40},ink);
    hud.outline(gun+glm::vec2{24,13},gun+glm::vec2{42,27},3,ink);

    // Molotov: a contoured bottle with a rag alight in its neck. The flame is
    // the only coloured glyph on the wheel on purpose — it is the one item
    // here that keeps doing something after it leaves your hand.
    const glm::vec2 bottle=anchor(2)+glm::vec2{-9,-30};
    hud.quad(bottle+glm::vec2{8,0},bottle+glm::vec2{10,0},
             bottle+glm::vec2{14,10},bottle+glm::vec2{4,10},flame);
    hud.rect(bottle+glm::vec2{7,9},bottle+glm::vec2{11,20},ink);
    hud.quad(bottle+glm::vec2{7,20},bottle+glm::vec2{11,20},
             bottle+glm::vec2{18,31},bottle+glm::vec2{0,31},ink);
    hud.rect(bottle+glm::vec2{0,31},bottle+glm::vec2{18,56},ink);

    hud.text_centered(weapon_name(kWeaponWheelOrder[0]),
        anchor(0).x,anchor(0).y+27,20,ink);
    hud.text_centered(weapon_name(kWeaponWheelOrder[1]),
        anchor(1).x,anchor(1).y+27,20,ink);
    hud.text_centered(weapon_name(kWeaponWheelOrder[2]),
        anchor(2).x,anchor(2).y+33,20,ink);
    hud.text_centered("WEAPONS",center.x,center.y-radius-43,29,ink);
    hud.text_centered("RELEASE TAB / LB TO EQUIP",center.x,center.y+radius+22,20,ink);
    hud.text_centered("MOVE MOUSE OR STICK  |  ESC / B TO CANCEL",center.x,center.y+radius+49,15,{.8f,.82f,.83f,1});
}
} // namespace apricot

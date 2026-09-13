#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace apricot {
// APPEND ONLY. The order is the wheel's sector order and the save game's
// stored value; renumbering an existing entry silently re-equips every
// existing save with the weapon that took its number.
enum class WeaponId : uint8_t { Unarmed, Pistol, Molotov };
inline constexpr std::size_t kWeaponSlotCount=3;
inline const char* weapon_name(WeaponId id) {
    switch (id) {
        case WeaponId::Pistol: return "PISTOL";
        case WeaponId::Molotov: return "MOLOTOV";
        case WeaponId::Unarmed: break;
    }
    return "UNARMED";
}

// Which sector of the wheel an item sits in, measured CLOCKWISE FROM STRAIGHT
// UP in screen space, where y runs down the screen. Three equal thirds:
// unarmed at the top, pistol to the lower right, molotov to the lower left.
//
// Exported rather than buried in point() because the renderer has to draw the
// sectors in exactly the same places the pointer tests them. When the two were
// separately written constants, the wheel highlighted one weapon and equipped
// another — and that reads as the click being dropped, not as a layout bug.
inline constexpr float kWeaponWheelSectorTurns=1.f/static_cast<float>(kWeaponSlotCount);
inline constexpr WeaponId kWeaponWheelOrder[kWeaponSlotCount]={
    WeaponId::Unarmed,WeaponId::Pistol,WeaponId::Molotov};

// The dead zone keeps a quick tap from changing the equipped item.
struct WeaponWheel {
    bool open=false;
    WeaponId equipped=WeaponId::Unarmed;
    WeaponId hovered=WeaponId::Unarmed;
    void begin() { open=true;hovered=equipped; }
    void point(float x,float y) {
        if (!open || !std::isfinite(x) || !std::isfinite(y) || x*x+y*y<.04f) return;
        // atan2(x, -y) is the angle clockwise from straight up with y down the
        // screen, in (-pi, pi]. Shifted into [0, 1) turns and offset by half a
        // sector so the FIRST entry straddles up rather than starting there.
        const float turns=std::atan2(x,-y)/6.28318531f+.5f/static_cast<float>(kWeaponSlotCount);
        const float wrapped=turns-std::floor(turns);
        const auto sector=static_cast<std::size_t>(wrapped*static_cast<float>(kWeaponSlotCount));
        hovered=kWeaponWheelOrder[std::min(sector,kWeaponSlotCount-1)];
    }
    void close(bool confirm) { if(open && confirm) equipped=hovered;open=false; }
};

struct WeaponUseInput {
    bool available=false;
    bool aim=false;
    bool fire_pressed=false;
    bool reload_pressed=false;
};

// All timing comes from the caller's simulation step. Input presses are
// consumed even while holstered or blocked, so closing a menu cannot shoot.
struct WeaponUseState {
    static constexpr int kMagazineCapacity=12;
    static constexpr int kInitialReserve=48;
    static constexpr float kDrawSeconds=.30f;
    static constexpr float kFireInterval=.22f;
    static constexpr float kReloadSeconds=1.35f;

    WeaponId equipped=WeaponId::Unarmed;
    int magazine=kMagazineCapacity;
    int reserve=kInitialReserve;
    float aim_blend=0.f;
    float equip_blend=0.f;
    float recoil=0.f;
    float shot_age=60.f;
    float cooldown=0.f;
    bool reloading=false;
    float reload_elapsed=0.f;

    float reload_progress() const {
        return reloading ? std::clamp(reload_elapsed/kReloadSeconds,0.f,1.f):0.f;
    }

    bool step(WeaponId selected,const WeaponUseInput& input,float dt) {
        const bool fire_edge=input.fire_pressed && !fire_was_pressed_;
        const bool reload_edge=input.reload_pressed && !reload_was_pressed_;
        fire_was_pressed_=input.fire_pressed;
        reload_was_pressed_=input.reload_pressed;
        const bool active=input.available && selected==WeaponId::Pistol;
        // Paused menus owe no simulation time, but still revoke weapon use.
        // Cancel immediately without advancing cooldown or transferring ammo.
        if (!active) {
            equipped=selected;
            available_last_=false;
            reloading=false;
            reload_elapsed=0.f;
            equip_blend=aim_blend=recoil=0.f;
            shot_age=60.f;
        }
        if (!std::isfinite(dt) || dt<=0.f) return false;

        const bool changed=selected!=equipped;
        const bool ready_before=active && available_last_ && !changed && equip_blend>=1.f;
        if (changed || !active || !available_last_) {
            reloading=false;
            reload_elapsed=0.f;
            recoil=0.f;
            shot_age=60.f;
            if (changed || (active && !available_last_)) equip_blend=0.f;
        }
        equipped=selected;
        available_last_=active;
        cooldown=std::max(0.f,cooldown-dt);
        recoil=std::max(0.f,recoil-dt/kFireInterval);
        shot_age=std::min(60.f,shot_age+dt);
        equip_blend=approach(equip_blend,active ? 1.f:0.f,dt/kDrawSeconds);

        // A press made during the reload is spent, even on its last tick.
        const bool was_reloading=reloading;
        if (reloading) {
            reload_elapsed=std::min(kReloadSeconds,reload_elapsed+dt);
            if (reload_elapsed>=kReloadSeconds) {
                const int transfer=std::min(kMagazineCapacity-magazine,reserve);
                magazine+=transfer;
                reserve-=transfer;
                reloading=false;
                reload_elapsed=0.f;
            }
        }
        if (active && ready_before && reload_edge && !was_reloading &&
            magazine<kMagazineCapacity && reserve>0) {
            reloading=true;
            reload_elapsed=0.f;
        }
        const bool fired=ready_before && !was_reloading && !reloading &&
            fire_edge && cooldown<=0.f && magazine>0;
        if (fired) {
            --magazine;
            cooldown=kFireInterval;
            recoil=1.f;
            shot_age=0.f;
        }
        const bool aiming=active && input.aim && !reloading;
        aim_blend=approach(aim_blend,aiming ? equip_blend:0.f,
                           dt/(aiming ? .16f:.12f));
        return fired;
    }

private:
    bool available_last_=false;
    bool fire_was_pressed_=false;
    bool reload_was_pressed_=false;

    static float approach(float value,float target,float amount) {
        return value<target ? std::min(target,value+amount):std::max(target,value-amount);
    }
};
} // namespace apricot

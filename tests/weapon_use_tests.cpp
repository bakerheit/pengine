#include "game/weapon.h"
#include "test_assert.h"
#include <limits>

using namespace apricot;

namespace {
constexpr float tick=1.f/120.f;
constexpr WeaponUseInput idle{true,false,false,false};
constexpr WeaponUseInput trigger{true,false,true,false};
constexpr WeaponUseInput reload{true,false,false,true};

WeaponUseState drawn_pistol() {
    WeaponUseState state;
    REQUIRE(!state.step(WeaponId::Pistol,idle,WeaponUseState::kDrawSeconds));
    REQUIRE_NEAR(state.equip_blend,1.f,1e-6);
    return state;
}

void draw_and_trigger_edges() {
    WeaponUseState state;
    REQUIRE(!state.step(WeaponId::Unarmed,trigger,1.f));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,WeaponUseState::kDrawSeconds));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,1.f));
    REQUIRE(state.magazine==12);
    REQUIRE(!state.step(WeaponId::Pistol,idle,tick));
    REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE(state.magazine==11);
    REQUIRE_NEAR(state.recoil,1.f,1e-6);
    REQUIRE_NEAR(state.shot_age,0.f,1e-6);
    REQUIRE(!state.step(WeaponId::Pistol,trigger,1.f));
    REQUIRE(state.magazine==11);
    REQUIRE_NEAR(state.recoil,0.f,1e-6);
    REQUIRE_NEAR(state.shot_age,1.f,1e-6);

    REQUIRE(!state.step(WeaponId::Pistol,idle,tick));
    REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE(!state.step(WeaponId::Pistol,idle,tick));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,1.f));
    REQUIRE(state.magazine==10); // A too-early click is never queued.
    apricot_test::pass("draw delay, semi-auto edge, cooldown and recoil");
}

void magazine_and_reload() {
    auto state=drawn_pistol();
    for (int i=0;i<WeaponUseState::kMagazineCapacity;++i) {
        REQUIRE(!state.step(WeaponId::Pistol,idle,.3f));
        REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    }
    REQUIRE(state.magazine==0);
    REQUIRE(state.reserve==48);
    REQUIRE(!state.step(WeaponId::Pistol,idle,1.f));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE_NEAR(state.recoil,0.f,1e-6);
    REQUIRE(state.shot_age>1.f);

    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(state.reloading);
    REQUIRE_NEAR(state.reload_progress(),0.f,1e-6);
    REQUIRE(!state.step(WeaponId::Pistol,idle,WeaponUseState::kReloadSeconds*.5f));
    REQUIRE_NEAR(state.reload_progress(),.5f,1e-6);
    REQUIRE(state.magazine==0 && state.reserve==48);
    REQUIRE(!state.step(WeaponId::Pistol,trigger,WeaponUseState::kReloadSeconds*.5f));
    REQUIRE(!state.reloading);
    REQUIRE(state.magazine==12 && state.reserve==36);
    REQUIRE(!state.step(WeaponId::Pistol,trigger,.3f));
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(!state.reloading); // Full magazines cannot spend reserves.
    apricot_test::pass("empty magazine, no dry-fire recoil, timed ammunition transfer");
}

void reload_conservation_and_cancel() {
    auto state=drawn_pistol();
    REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(!state.step(WeaponId::Pistol,idle,.5f));
    REQUIRE(!state.step(WeaponId::Unarmed,idle,2.f));
    REQUIRE(!state.reloading);
    REQUIRE(state.magazine==11 && state.reserve==48);
    REQUIRE(!state.step(WeaponId::Pistol,idle,.3f));
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(!state.step(WeaponId::Pistol,idle,2.f));
    REQUIRE(state.magazine==12 && state.reserve==47);

    state.magazine=2;
    state.reserve=3;
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(!state.step(WeaponId::Pistol,idle,2.f));
    REQUIRE(state.magazine==5 && state.reserve==0);
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(!state.reloading);
    apricot_test::pass("holster cancels reload and partial reload conserves ammo");
}

void modal_and_aim() {
    auto state=drawn_pistol();
    REQUIRE(!state.step(WeaponId::Pistol,{true,true,false,false},.08f));
    REQUIRE_NEAR(state.aim_blend,.5f,1e-6);
    REQUIRE(state.step(WeaponId::Pistol,{true,true,true,false},.08f));
    REQUIRE_NEAR(state.aim_blend,1.f,1e-6);
    REQUIRE(!state.step(WeaponId::Pistol,reload,.12f));
    REQUIRE(state.reloading);
    REQUIRE_NEAR(state.aim_blend,0.f,1e-6);
    REQUIRE(!state.step(WeaponId::Pistol,{false,true,true,true},2.f));
    REQUIRE(!state.reloading);
    REQUIRE(state.magazine==11 && state.reserve==48);
    REQUIRE_NEAR(state.equip_blend,0.f,1e-6);
    REQUIRE_NEAR(state.aim_blend,0.f,1e-6);
    REQUIRE_NEAR(state.recoil,0.f,1e-6);
    REQUIRE(!state.step(WeaponId::Pistol,trigger,.3f));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,.3f));
    REQUIRE(state.magazine==11);
    REQUIRE(!state.step(WeaponId::Pistol,idle,tick));
    REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    apricot_test::pass("aim transition, reload lowering and modal trigger consumption");
}

void invalid_time_and_replay() {
    auto state=drawn_pistol();
    REQUIRE(!state.step(WeaponId::Pistol,trigger,std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,std::numeric_limits<float>::infinity()));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,-1.f));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,0.f));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,1.f));
    REQUIRE(state.magazine==12);
    REQUIRE(std::isfinite(state.equip_blend));
    REQUIRE(std::isfinite(state.aim_blend));

    WeaponUseState first,second;
    int shots=0;
    for (int i=0;i<1400;++i) {
        const WeaponUseInput input{!(i>=800 && i<840),i%90<65,i%32==0,i%240==0};
        const bool fired=first.step(WeaponId::Pistol,input,tick);
        REQUIRE(fired==second.step(WeaponId::Pistol,input,tick));
        if (fired) ++shots;
        REQUIRE(first.magazine==second.magazine);
        REQUIRE(first.reserve==second.reserve);
        REQUIRE(first.reloading==second.reloading);
        REQUIRE(first.aim_blend==second.aim_blend);
        REQUIRE(first.equip_blend==second.equip_blend);
        REQUIRE(first.recoil==second.recoil);
        REQUIRE(first.shot_age==second.shot_age);
        REQUIRE(first.reload_progress()==second.reload_progress());
        REQUIRE(first.magazine>=0 && first.magazine<=12);
        REQUIRE(first.reserve>=0);
        REQUIRE(first.magazine+first.reserve+shots==60);
        REQUIRE(first.aim_blend>=0.f && first.aim_blend<=1.f);
        REQUIRE(first.equip_blend>=0.f && first.equip_blend<=1.f);
        REQUIRE(first.recoil>=0.f && first.recoil<=1.f);
    }
    // The script spends several seconds reloading; clicks during those windows
    // are intentionally discarded. Still require enough shots to need a reload.
    REQUIRE(shots>WeaponUseState::kMagazineCapacity);
    apricot_test::pass("invalid time cannot fire; replay and ammo invariants");
}

void paused_modal_cancels_without_time() {
    auto state=drawn_pistol();
    REQUIRE(state.step(WeaponId::Pistol,{true,true,true,false},.16f));
    REQUIRE(!state.step(WeaponId::Pistol,reload,tick));
    REQUIRE(state.reloading);
    const float cooldown=state.cooldown;
    REQUIRE(!state.step(WeaponId::Pistol,{false,true,true,true},0.f));
    REQUIRE(!state.reloading);
    REQUIRE_NEAR(state.reload_progress(),0.f,1e-6);
    REQUIRE_NEAR(state.equip_blend,0.f,1e-6);
    REQUIRE_NEAR(state.aim_blend,0.f,1e-6);
    REQUIRE_NEAR(state.recoil,0.f,1e-6);
    REQUIRE_NEAR(state.cooldown,cooldown,1e-6);
    REQUIRE(state.magazine==11 && state.reserve==48);
    REQUIRE(!state.step(WeaponId::Pistol,trigger,WeaponUseState::kDrawSeconds));
    REQUIRE(!state.step(WeaponId::Pistol,trigger,.3f));
    REQUIRE(state.magazine==11 && state.reserve==48);
    REQUIRE(!state.step(WeaponId::Pistol,idle,tick));
    REQUIRE(state.step(WeaponId::Pistol,trigger,tick));
    REQUIRE(!state.step(WeaponId::Unarmed,idle,0.f));
    REQUIRE(state.equipped==WeaponId::Unarmed);
    REQUIRE_NEAR(state.equip_blend,0.f,1e-6);
    apricot_test::pass("zero-time modal cancels reload, consumes presses and requires redraw");
}
} // namespace

int main() {
    draw_and_trigger_edges();
    magazine_and_reload();
    reload_conservation_and_cancel();
    modal_and_aim();
    invalid_time_and_replay();
    paused_modal_cancels_without_time();
    return apricot_test::done("weapon_use_tests");
}

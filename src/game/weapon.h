#pragma once
#include <cmath>
#include <cstdint>

namespace apricot {
enum class WeaponId : uint8_t { Unarmed, Pistol };
inline const char* weapon_name(WeaponId id) {
    return id==WeaponId::Pistol ? "PISTOL":"UNARMED";
}
// Selection is presentation-only until combat is introduced. The dead zone
// keeps a quick tap from changing the equipped item.
struct WeaponWheel {
    bool open=false;
    WeaponId equipped=WeaponId::Unarmed;
    WeaponId hovered=WeaponId::Unarmed;
    void begin() { open=true;hovered=equipped; }
    void point(float x,float y) {
        if (!open || !std::isfinite(x) || !std::isfinite(y) || x*x+y*y<.04f) return;
        hovered=x>=0 ? WeaponId::Pistol:WeaponId::Unarmed;
    }
    void close(bool confirm) { if(open && confirm) equipped=hovered;open=false; }
};
} // namespace apricot

#pragma once

#include <array>
#include <string_view>

#include "app/vehicle_lamp_mesh.h"

namespace apricot {

// The fleet cruisers' depth bands end exactly at their flat lenses, so a depth
// sampled on a lens lands on the band's edge, and which side it rounds to
// depends on the compiler: arm64 clang fuses the multiply-adds and lands
// inside, x86-64 gcc does not and lands one float step out, and those cruisers
// then refused to load on Windows. A tenth of a millimetre is far above that
// noise and far below any band, the thinnest of which is 4 mm.
inline constexpr float kVehicleLampBandSlack = 1e-4f;

struct VehicleHeadlightRegion {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, z0 = 0, z1 = 0;
    bool round = false;
    bool valid() const { return x1 > x0 && y1 > y0 && z1 > z0; }
    bool contains(glm::vec3 p) const {
        p.x = std::abs(p.x);
        if (!valid() || p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1 ||
            p.z < z0 - kVehicleLampBandSlack || p.z > z1 + kVehicleLampBandSlack) return false;
        const glm::vec2 q{(2*p.x-x0-x1)/(x1-x0), (2*p.y-y0-y1)/(y1-y0)};
        return !round || glm::dot(q, q) <= 1.0f;
    }
};

struct VehicleHeadlightProfile {
    int id = -1;
    std::array<VehicleHeadlightRegion, 2> regions{};
    bool exposed() const { return regions[0].valid(); }
};

// The shader includes this same table. Unknown meshes fail closed instead of
// silently inheriting a rectangle at an unrelated percentage of their bounds.
inline VehicleHeadlightProfile vehicle_headlight_profile(std::string_view path) {
#define HEADLIGHT_MODEL(name, number) if (path.find("/" #name "/") != std::string_view::npos) return {number, {{
#define HEADLIGHT_RECT(a,b,c,d,e,f) {float(a),float(b),float(c),float(d),float(e),float(f),false},
#define HEADLIGHT_ROUND(a,b,c,d,e,f) {float(a),float(b),float(c),float(d),float(e),float(f),true},
#define HEADLIGHT_END }}};
#include "../../assets/shaders/vehicle_headlight_profiles.inc"
#undef HEADLIGHT_MODEL
#undef HEADLIGHT_RECT
#undef HEADLIGHT_ROUND
#undef HEADLIGHT_END
    return {};
}

struct VehicleBrakelightProfile {
    int id = -1;
    std::array<VehicleHeadlightRegion, 3> regions{};
};

inline VehicleBrakelightProfile vehicle_brakelight_profile(std::string_view path) {
#define BRAKELIGHT_MODEL(name, number) if (path.find("/" #name "/") != std::string_view::npos) return {number, {{
#define BRAKELIGHT_RECT(a,b,c,d,e,f) {float(a),float(b),float(c),float(d),float(e),float(f),false},
#define BRAKELIGHT_ROUND(a,b,c,d,e,f) {float(a),float(b),float(c),float(d),float(e),float(f),true},
#define BRAKELIGHT_END }}};
#include "../../assets/shaders/vehicle_brakelight_profiles.inc"
#undef BRAKELIGHT_MODEL
#undef BRAKELIGHT_RECT
#undef BRAKELIGHT_ROUND
#undef BRAKELIGHT_END
    return {};
}

// Area-weighted centre of the actual exposed bulb(s) in each assembly, sampled
// on the cooked body. Source coordinates are then fitted exactly like the body.
inline bool vehicle_headlight_origin(const StaticEmesh& body,
                                     const VehicleHeadlightProfile& profile,
                                     std::size_t side, glm::vec3& out) {
    glm::vec3 sum{0};
    float total = 0;
    for (const auto& r : profile.regions) {
        if (!r.valid()) continue;
        glm::vec3 p{(side == 0 ? 1.0f : -1.0f)*(r.x0+r.x1)*0.5f,
                    (r.y0+r.y1)*0.5f, 0};
        if (!vehicle_body_surface_z(body, p.x, p.y, true, p.z) ||
            p.z < r.z0 - kVehicleLampBandSlack || p.z > r.z1 + kVehicleLampBandSlack) return false;
        const float area = (r.x1-r.x0)*(r.y1-r.y0)*(r.round ? 0.785398f : 1.0f);
        sum += p*area;
        total += area;
    }
    if (total <= 0) return false;
    out = sum/total;
    return true;
}

}  // namespace apricot

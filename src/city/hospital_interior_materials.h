#pragma once

#include <array>
#include <cstring>

namespace apricot::city {

struct HospitalInteriorMaterialDescriptor {
    const char* suffix;
    const char* path;
    bool fitted;
    float tile_span_m;
};

inline constexpr std::array<HospitalInteriorMaterialDescriptor, 17>
    kHospitalInteriorMaterials{{
        {" tex terrazzo", "textures/world/hospital/interior_detail/terrazzo.png", false, 3.0f},
        {" tex upholstery", "textures/world/hospital/interior_detail/upholstery.png", false, 0.5f},
        {" tex laminate", "textures/world/hospital/interior_detail/laminate.png", false, 1.5f},
        {" tex curtain", "textures/world/hospital/interior_detail/curtain.png", false, 0.75f},
        {" tex wallpaint", "textures/world/hospital/interior_detail/wallpaint.png", false, 2.5f},
        {" tex ceiling", "textures/world/hospital/interior_detail/ceiling.png", false, 2.4f},
        {" tex steel", "textures/world/hospital/interior_detail/steel.png", false, 1.0f},
        {" tex records-screen", "textures/world/hospital/interior_detail/records-screen.png", true, 0.0f},
        {" tex vitals-screen", "textures/world/hospital/interior_detail/vitals-screen.png", true, 0.0f},
        {" tex scale-screen", "textures/world/hospital/interior_detail/scale-screen.png", true, 0.0f},
        {" tex screen", "textures/world/hospital/interior_detail/screen.png", true, 0.0f},
        {" tex reception-sign", "textures/world/hospital/interior_detail/reception-sign.png", true, 0.0f},
        {" tex pharmacy-sign", "textures/world/hospital/interior_detail/pharmacy-sign.png", true, 0.0f},
        {" tex diagnostics-sign", "textures/world/hospital/interior_detail/diagnostics-sign.png", true, 0.0f},
        {" tex emergency-sign", "textures/world/hospital/interior_detail/emergency-sign.png", true, 0.0f},
        {" tex ward-sign", "textures/world/hospital/interior_detail/ward-sign.png", true, 0.0f},
        {" tex directory-sign", "textures/world/hospital/interior_detail/directory-sign.png", true, 0.0f},
    }};

inline int hospital_interior_material_index(const char* name) {
    if (name == nullptr) {
        return -1;
    }

    const std::size_t name_length = std::strlen(name);
    for (std::size_t i = 0; i < kHospitalInteriorMaterials.size(); ++i) {
        const char* suffix = kHospitalInteriorMaterials[i].suffix;
        const std::size_t suffix_length = std::strlen(suffix);
        if (name_length >= suffix_length &&
            std::strcmp(name + name_length - suffix_length, suffix) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace apricot::city

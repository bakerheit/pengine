#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace apricot {

// Standing human silhouette used by the original Probable Cause hitscan.
// Return metres along a unit ray, or -1 on a miss. Parallel axes are handled
// explicitly so rays on a box face never produce zero-times-infinity NaNs.
inline float weapon_ped_hit_distance(glm::vec3 origin,glm::vec3 direction,
                                     glm::vec3 feet,float max_distance) {
    if (!std::isfinite(max_distance) || max_distance<=0.f) return -1.f;
    for (int axis=0;axis<3;++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis]) ||
            !std::isfinite(feet[axis])) return -1.f;
    const float length2=glm::dot(direction,direction);
    if (std::fabs(length2-1.f)>.001f) return -1.f;

    const glm::vec3 lower=feet+glm::vec3{-.4f,0.f,-.4f};
    const glm::vec3 upper=feet+glm::vec3{.4f,1.85f,.4f};
    float near_distance=0.f,far_distance=max_distance;
    for (int axis=0;axis<3;++axis) {
        if (std::fabs(direction[axis])<1e-8f) {
            if (origin[axis]<lower[axis] || origin[axis]>upper[axis]) return -1.f;
            continue;
        }
        const float a=(lower[axis]-origin[axis])/direction[axis];
        const float b=(upper[axis]-origin[axis])/direction[axis];
        near_distance=std::max(near_distance,std::min(a,b));
        far_distance=std::min(far_distance,std::max(a,b));
        if (near_distance>far_distance) return -1.f;
    }
    // World geometry wins equal-distance contacts. Starting inside a person
    // counts at the origin, so close muzzle contact cannot tunnel through.
    return near_distance<max_distance ? near_distance:-1.f;
}

} // namespace apricot

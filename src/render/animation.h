#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pengine {

class Skeleton;

class Animation {
public:
    struct PosKey { float t; glm::vec3 v; };
    struct RotKey { float t; glm::quat q; };
    struct SclKey { float t; glm::vec3 v; };

    bool  load(const std::string& eanim_path, const Skeleton& skel);
    float duration() const { return duration_; }

    // Sample the animation at time t (seconds, can wrap around duration).
    // Fills out_local with one mat4 per bone of the skeleton:
    //   - if a channel exists for the bone: composed pos/rot/scale at t
    //   - otherwise: skel.bone(b).bind_local
    void sample(float t, const Skeleton& skel,
                std::vector<glm::mat4>& out_local) const;

private:
    struct Channel {
        std::string         bone_name;
        int                 bone_idx = -1;       // resolved against skel
        std::vector<PosKey> pos;
        std::vector<RotKey> rot;
        std::vector<SclKey> scl;
    };

    std::vector<Channel> channels_;
    float                duration_ = 0.f;
};

} // namespace pengine

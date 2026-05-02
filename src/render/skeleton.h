#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pengine {

struct Bone {
    int32_t     parent = -1;
    std::string name;
    glm::mat4   inv_bind   {1.f}; // mesh-space → bone-space at bind time
    glm::mat4   bind_local {1.f}; // local-to-parent at bind time (default pose)
};

class Skeleton {
public:
    bool load(const std::string& eskel_path);

    int  bone_count()                    const { return static_cast<int>(bones_.size()); }
    const Bone& bone(int i)              const { return bones_[static_cast<std::size_t>(i)]; }
    int  find_bone(const std::string& nm) const;

    // Walks the hierarchy. For each bone:
    //   world[b] = world[parent] * local_pose[b]
    //   skin [b] = world[b]      * inv_bind[b]
    // local_poses must be size bone_count(); writes to out_skin (resized).
    void compute_skin_matrices(const std::vector<glm::mat4>& local_poses,
                                std::vector<glm::mat4>& out_skin) const;

private:
    std::vector<Bone> bones_;
};

} // namespace pengine

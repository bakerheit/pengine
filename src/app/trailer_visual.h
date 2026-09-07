#pragma once
#include "game/tractor_trailer.h"
#include "scene/scene.h"
namespace apricot {
class Renderer;
class TrailerVisual {
public:
    bool init(Renderer& renderer,Scene& scene);
    void sync(Scene& scene,const TrailerState& previous,const TrailerState& current,float alpha,float lights=0,float brake=0) const;
    void destroy(Scene& scene);
private:
    NodeId body_=kInvalidId;
    std::array<NodeId,2> lamps_{kInvalidId,kInvalidId};
    std::array<NodeId,4> wheels_{kInvalidId,kInvalidId,kInvalidId,kInvalidId};
    std::array<NodeId,2> legs_{kInvalidId,kInvalidId};
    float wheel_scale_=1;
};
}

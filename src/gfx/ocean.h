#pragma once
#include "gfx/sky.h"
namespace apricot {
// Depth-tested sea surface at the same sea level used by terrain and maps.
class Ocean {
public:
    ~Ocean() { destroy(); }
    bool init();
    void destroy();
    void render(const Camera&,const SkyEnv&,const HeadlightRig&,
                const CanopyLightRig&,float time,float water_level_m);
private:
    Shader shader_;
    GLuint vao_=0;
};
}

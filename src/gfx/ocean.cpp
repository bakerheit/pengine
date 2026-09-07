#include "gfx/ocean.h"
#include "gfx/gl_state.h"
#include "terrain/heightmap.h"
namespace apricot {
bool Ocean::init() {
    if (!shader_.build_from_files("shaders/ocean.vert","shaders/ocean.frag")) return false;
    if (!vao_) glGenVertexArrays(1,&vao_);
    return vao_!=0;
}
void Ocean::destroy() {
    shader_.destroy();
    if (vao_) { glDeleteVertexArrays(1,&vao_); gl_state::on_vertex_array_deleted(vao_); vao_=0; }
}
void Ocean::render(const Camera& camera,const SkyEnv& env,const HeadlightRig& lights,
                    const CanopyLightRig& canopy,float time,float water_level_m) {
    if (!vao_ || !shader_.valid()) return;
    shader_.bind();
    shader_.set_mat4("u_view_proj",camera.projection()*camera.view());
    shader_.set_float("u_sea_level",water_level_m);
    shader_.set_float("u_time",time);
    shader_.set_vec3("u_sky_reflection",env.sky_bottom);
    apply_lighting(shader_,env,camera.position,lights,canopy);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    gl_state::bind_vertex_array(vao_);
    glDrawArrays(GL_TRIANGLES,0,6);
    glEnable(GL_CULL_FACE);
}
}

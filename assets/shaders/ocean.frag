#version 330 core
in vec3 v_world_pos;
out vec4 frag_color;
uniform float u_time;
uniform vec3 u_sky_reflection;
#include "lighting.glsl"
void main() {
    vec2 p=v_world_pos.xz;
    float distance_to_camera=length(v_world_pos-u_cam_pos);
    float detail=1.0-smoothstep(100.0,800.0,distance_to_camera);
    float a=dot(p,vec2(.17,.09))+u_time*.65;
    float b=dot(p,vec2(-.08,.22))-u_time*.48;
    vec3 normal=normalize(vec3((cos(a)*.13+cos(b)*.045)*detail,1,
        (cos(a)*.07+cos(b)*.12)*detail));
    vec3 view=normalize(u_cam_pos-v_world_pos);
    float fresnel=.08+.65*pow(1.0-max(dot(view,normal),0.0),4.0);
    vec3 water=vec3(.025,.20,.28)*(1.0+.06*sin(a+b)*detail);
    vec3 lit=apply_lighting(water,normal,v_world_pos);
    lit=mix(lit,u_sky_reflection*.70,fresnel);
    vec3 half_vector=normalize(view+normalize(u_light_dir));
    lit+=u_light_color*pow(max(dot(normal,half_vector),0.0),110.0)*.55;
    frag_color=vec4(apply_fog(lit,v_world_pos),1);
}

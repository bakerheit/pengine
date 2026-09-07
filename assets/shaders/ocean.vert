#version 330 core
uniform mat4 u_view_proj;
uniform vec3 u_cam_pos;
uniform float u_sea_level;
out vec3 v_world_pos;
void main() {
    const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),
        vec2(-1,-1),vec2(1,1),vec2(-1,1));
    v_world_pos=vec3(u_cam_pos.x,u_sea_level,u_cam_pos.z)+
        vec3(corners[gl_VertexID].x,0,corners[gl_VertexID].y)*10000.0;
    gl_Position=u_view_proj*vec4(v_world_pos,1);
}

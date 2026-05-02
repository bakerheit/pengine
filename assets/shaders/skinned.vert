#version 330 core

layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_uv;
layout(location = 3) in vec4  a_tangent;
layout(location = 4) in uvec4 a_bone_idx;
layout(location = 5) in vec4  a_bone_weight;

uniform mat4 u_model;
uniform mat4 u_view_proj;
uniform mat3 u_normal_mat;     // for non-skinned diffuse normal (we override with skinned)

const int MAX_BONES = 64;
uniform mat4 u_bones[MAX_BONES];

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    mat4 skin = u_bones[a_bone_idx.x] * a_bone_weight.x
              + u_bones[a_bone_idx.y] * a_bone_weight.y
              + u_bones[a_bone_idx.z] * a_bone_weight.z
              + u_bones[a_bone_idx.w] * a_bone_weight.w;

    vec4 sp = skin * vec4(a_pos, 1.0);
    vec3 sn = mat3(skin) * a_normal;

    vec4 world_pos = u_model * sp;
    v_world_pos    = world_pos.xyz;
    v_normal       = normalize(mat3(u_model) * sn);
    v_uv           = a_uv;
    gl_Position    = u_view_proj * world_pos;
}

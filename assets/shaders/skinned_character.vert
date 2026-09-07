#version 330 core

layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_uv;
layout(location = 3) in vec4  a_tangent;
layout(location = 4) in uvec4 a_bone_idx;
layout(location = 5) in vec4  a_bone_weight;

uniform mat4 u_model;
uniform mat4 u_view_proj;

const int MAX_BONES = 64;
uniform vec4 u_dq_real[MAX_BONES];
uniform vec4 u_dq_dual[MAX_BONES];

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;

vec3 quat_rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
    vec4 reference = u_dq_real[a_bone_idx.x];
    vec4 real_part = a_bone_weight.x * reference;
    vec4 dual_part = a_bone_weight.x * u_dq_dual[a_bone_idx.x];
    uint indices[3] = uint[3](a_bone_idx.y, a_bone_idx.z, a_bone_idx.w);
    float weights[3] = float[3](a_bone_weight.y, a_bone_weight.z,
                                a_bone_weight.w);
    for (int influence = 0; influence < 3; ++influence) {
        vec4 candidate = u_dq_real[indices[influence]];
        float signed_weight = dot(reference, candidate) < 0.0
            ? -weights[influence] : weights[influence];
        real_part += signed_weight * candidate;
        dual_part += signed_weight * u_dq_dual[indices[influence]];
    }

    float magnitude = length(real_part);
    if (magnitude < 1e-8) {
        real_part = vec4(0.0, 0.0, 0.0, 1.0);
        dual_part = vec4(0.0);
        magnitude = 1.0;
    }
    real_part /= magnitude;
    dual_part /= magnitude;

    vec3 skinned_position = quat_rotate(real_part, a_pos) + 2.0 *
        (real_part.w * dual_part.xyz - dual_part.w * real_part.xyz +
         cross(real_part.xyz, dual_part.xyz));
    vec3 skinned_normal = quat_rotate(real_part, a_normal);

    vec4 world_position = u_model * vec4(skinned_position, 1.0);
    v_world_pos = world_position.xyz;
    v_normal = normalize(transpose(inverse(mat3(u_model))) * skinned_normal);
    v_uv = a_uv;
    gl_Position = u_view_proj * world_position;
}

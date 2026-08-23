#version 330 core

// The ONE vertex shader for opaque world geometry.
//
// There is no non-instanced sibling on purpose. The naive A/B path in the debug
// overlay draws one instance per node through this exact shader, so the toggle
// measures BATCHING and nothing else — a second shader would quietly make the
// comparison a shader comparison too.
//
// Per-vertex layout is gfx/mesh.h's TerrainVertex. Per-instance layout is
// pinned by gfx/instance.h; change one and you must change all three.
layout(location = 0)  in vec3 a_pos;
layout(location = 1)  in vec3 a_normal;
layout(location = 2)  in vec2 a_uv;
// location 3 is reserved for a tangent, so adding normal mapping later does not
// renumber the instance block underneath everyone.

// --- per instance (divisor 1) ----------------------------------------------
layout(location = 4)  in mat4 a_inst_model;      // occupies 4,5,6,7
// Columns of the world normal matrix (xyz used, w = pad). A full inverse-
// transpose, not mat3(model): the scenery is non-uniformly scaled boxes, and
// mat3(model) skews their normals so a stretched box lights like a wedge.
layout(location = 8)  in vec4 a_inst_nrm0;
layout(location = 9)  in vec4 a_inst_nrm1;
layout(location = 10) in vec4 a_inst_nrm2;
layout(location = 11) in vec4 a_inst_tint;
layout(location = 12) in vec2 a_inst_uv_scale;

uniform mat4 u_view_proj;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_tint;

void main() {
    vec4 world  = a_inst_model * vec4(a_pos, 1.0);
    v_world_pos = world.xyz;
    v_normal    = mat3(a_inst_nrm0.xyz, a_inst_nrm1.xyz, a_inst_nrm2.xyz) * a_normal;
    // uv_scale is folded in HERE rather than in the fragment stage so the
    // tiling interpolates and the texture lookup stays one instruction.
    v_uv        = a_uv * a_inst_uv_scale;
    v_tint      = a_inst_tint;
    gl_Position = u_view_proj * world;
}

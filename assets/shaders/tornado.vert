#version 330 core

// Three authored metre-space funnel shells. Motion bends and wrinkles the
// surfaces in world space; no camera distance or viewport value scales them.
layout(location = 0) in vec3  a_local_position_m;
layout(location = 1) in vec2  a_funnel_uv;
layout(location = 2) in float a_layer;

uniform mat4  u_view_proj;
uniform vec3  u_center_ground;
uniform float u_time;

out vec2  v_funnel_uv;
out float v_layer;
out vec3  v_world_position;
out vec2  v_radial_direction;

vec2 rotate_2d(vec2 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

void main() {
    float height = a_funnel_uv.y;
    float layer_speed = mix(1.15, 1.85, a_layer);
    float twist = u_time * layer_speed + height * mix(5.5, 9.0, a_layer);

    vec3 local = a_local_position_m;
    local.xz = rotate_2d(local.xz, twist);

    // Integer circumferential frequencies keep both copies of the UV seam in
    // the same place. The wrinkle stays under half a metre at the 15 m crown.
    float wrinkle_phase = a_funnel_uv.x * 150.7964474 +
                          height * 31.0 - u_time * (2.1 + a_layer);
    float wrinkle = 1.0 + 0.024 * sin(wrinkle_phase);
    local.xz *= wrinkle;

    // Slow axis wander sells the rotating column without changing its fixed
    // height or authored radius. It grows toward the broad top and stays below
    // one metre even at full intensity.
    float sway_weight = height * height * 0.72;
    vec2 sway = vec2(
        sin(u_time * 0.47 + height * 4.8 + a_layer * 2.7),
        cos(u_time * 0.39 + height * 5.6 + a_layer * 3.1)
    ) * sway_weight;
    local.xz += sway;

    vec3 world = u_center_ground + local;
    v_funnel_uv = a_funnel_uv;
    v_layer = a_layer;
    v_world_position = world;
    v_radial_direction = normalize(local.xz - sway);
    gl_Position = u_view_proj * vec4(world, 1.0);
}

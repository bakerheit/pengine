#version 330 core

in vec2  v_funnel_uv;
in float v_layer;
in vec3  v_world_position;
in vec2  v_radial_direction;

uniform vec3  u_camera_position;
uniform float u_intensity;
uniform float u_time;

out vec4 frag_color;

void main() {
    const float two_pi = 6.28318530718;
    float angle = v_funnel_uv.x * two_pi;
    float height = v_funnel_uv.y;

    // Interference between broad and fine rotating bands gives each shell
    // torn gaps and dense ropes without a screen-space texture.
    float broad = 0.5 + 0.5 * sin(angle * 7.0 + height * 24.0 -
                                  u_time * (1.7 + v_layer * 0.8));
    float fine = 0.5 + 0.5 * sin(angle * 19.0 - height * 43.0 +
                                 u_time * (3.1 - v_layer * 0.7));
    float cross_band = 0.5 + 0.5 * sin(angle * 3.0 + height * 71.0 +
                                       broad * 3.0 - u_time * 1.2);
    float smoke = smoothstep(0.18, 0.88,
                             broad * 0.45 + fine * 0.30 + cross_band * 0.35);

    vec3 view_delta = u_camera_position - v_world_position;
    float view_distance = length(view_delta);
    vec3 to_camera = view_distance > 0.0001
        ? view_delta / view_distance
        : vec3(0.0, 1.0, 0.0);
    vec3 radial = normalize(vec3(v_radial_direction.x, 0.0,
                                 v_radial_direction.y));
    float silhouette = 1.0 - abs(dot(radial, to_camera));
    silhouette = mix(0.72, 1.28, silhouette);

    float foot_fade = smoothstep(0.0, 0.035, height);
    float crown_fade = 1.0 - 0.55 * smoothstep(0.88, 1.0, height);
    float layer_density = mix(0.105, 0.155, v_layer);
    float density = mix(0.48, 1.0, smoke) * silhouette;
    float alpha = u_intensity * layer_density * density *
                  foot_fade * crown_fade;

    if (alpha < 0.006) discard;

    vec3 ground_dust = vec3(0.16, 0.145, 0.13);
    vec3 storm_grey = vec3(0.30, 0.31, 0.32);
    vec3 color = mix(ground_dust, storm_grey,
                     smoothstep(0.08, 0.72, height));
    color += vec3(0.035) * smoke * (1.0 - v_layer * 0.35);
    frag_color = vec4(color, alpha);
}

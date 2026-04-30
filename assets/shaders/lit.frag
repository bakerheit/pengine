#version 330 core

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_diffuse;
uniform vec3      u_light_dir;   // world-space, normalized, toward the light
uniform vec3      u_light_color;
uniform vec3      u_ambient;
uniform vec3      u_cam_pos;

out vec4 frag_color;

void main() {
    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_light_dir);
    vec3 V = normalize(u_cam_pos - v_world_pos);
    vec3 H = normalize(L + V);

    vec3 albedo  = texture(u_diffuse, v_uv).rgb;
    float NdotL  = max(dot(N, L), 0.0);
    float NdotH  = max(dot(N, H), 0.0);

    vec3 ambient  = u_ambient * albedo;
    vec3 diffuse  = u_light_color * albedo * NdotL;
    vec3 specular = u_light_color * pow(NdotH, 64.0) * 0.3;

    frag_color = vec4(ambient + diffuse + specular, 1.0);
}

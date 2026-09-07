#version 330 core

// Shared world material: local damage is composited before normal lighting.
#include "lighting.glsl"
#include "vehicle_damage.glsl"
#include "vehicle_headlights.glsl"

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_tint;
in vec4 v_terrain_weights;
flat in float v_vehicle_lamp;
flat in int v_headlight_profile;
in vec3 v_lamp_source_position;

uniform sampler2D u_diffuse;
uniform bool u_glass;
uniform bool u_receives_snow;
uniform float u_material_specular_scale = 1.0;
out vec4 frag_color;

void main() {
    if (u_glass) {
        vec3 N=normalize(v_normal);
        vec3 V=normalize(u_cam_pos-v_world_pos);
        if (dot(N,V)<0.0) N=-N;
        float fresnel=0.04+0.96*pow(1.0-max(dot(N,V),0.0),5.0);
        vec3 R=reflect(-V,N);
        // Soft sky/ground reflection, with a small moving sun glint. No opaque
        // painted window stripes: the actual cab remains visible underneath.
        vec3 reflection=mix(u_ambient*vec3(.30,.34,.32),
            u_ambient*vec3(.85,1.05,1.25)+u_light_color*.12,smoothstep(-.1,.65,R.y));
        float glint=pow(max(dot(R,normalize(u_light_dir)),0.0),180.0);
        vec3 glass=mix(vec3(.12,.20,.23)*u_ambient,reflection,.7)+u_light_color*glint*1.5;
        float opacity=clamp(.16+fresnel*.48+glint*.22,.16,.72);
        frag_color=vec4(apply_fog(glass,v_world_pos),opacity);
        return;
    }
    if (v_vehicle_lamp >= 0.0) {
        bool front = v_vehicle_lamp < 2.0;
        float side = mod(v_vehicle_lamp, 2.0) < 0.5 ? 1.0 : -1.0;
        if (v_vehicle_lamp >= 4.0) {
            vec3 p=v_lamp_source_position;
            bool red=v_vehicle_lamp<5.0;
            float x=red ? -p.x : p.x;
            bool lightbar = v_headlight_profile == 14 &&
                x>=0.11 && x<=0.81 && p.y>=1.685 && p.y<=1.84 &&
                p.z>=-.27 && p.z<=.01;
            lightbar = lightbar || (v_headlight_profile == 23 &&
                x>=.06 && x<=.79 && p.y>=1.65 && p.y<=1.78 &&
                p.z>=-.235 && p.z<=.015);
            lightbar = lightbar || (v_headlight_profile == 24 &&
                x>=.08 && x<=.76 && p.y>=1.62 && p.y<=1.77 &&
                p.z>=-.18 && p.z<=.12);
            lightbar = lightbar || (v_headlight_profile == 25 &&
                x>=.05 && x<=.70 && p.y>=1.61 && p.y<=1.77 &&
                p.z>=-.35 && p.z<=-.14);
            lightbar = lightbar || (v_headlight_profile == 26 &&
                x>=.06 && x<=.72 && p.y>=1.74 && p.y<=1.88 &&
                p.z>=-.20 && p.z<=.10);
            lightbar = lightbar || (v_headlight_profile == 27 &&
                x>=.05 && x<=.76 && p.y>=1.56 && p.y<=1.70 &&
                p.z>=-.28 && p.z<=.02);
            if (!lightbar) discard;
        } else if (front) {
            if (v_lamp_source_position.x * side <= 0.0 || v_damage_normal.z < 0.15 ||
                !vehicle_headlight_contains(v_lamp_source_position, v_headlight_profile)) discard;
        } else {
            if (v_lamp_source_position.x * side <= 0.0 || v_damage_normal.z > -0.15 ||
                !vehicle_brakelight_contains(v_lamp_source_position, v_headlight_profile)) discard;
        }
    }
    vec4 source = texture(u_diffuse, v_uv);
    if (dot(v_terrain_weights,vec4(1))>.5) {
        float grain=clamp(dot(source.rgb,vec3(.333))*2.4+.2,.4,1.4);
        source.rgb=source.rgb*v_terrain_weights.z + grain*(
            vec3(.34,.35,.35)*v_terrain_weights.x +
            vec3(.44,.39,.31)*v_terrain_weights.y +
            vec3(.72,.64,.43)*v_terrain_weights.w);
    }
    // The geometric outline and the painted red texels must agree. This keeps
    // chrome rims, white reverse cells and amber pixels out of the brake glow.
    if (v_vehicle_lamp >= 2.0 && v_vehicle_lamp < 4.0 &&
        (source.r < source.g * 1.6 || source.r < source.b * 1.5 || source.r < 0.08)) discard;
    // Scraped-metal colours must not make a damaged red lens emit white light.
    vec3 albedo = v_vehicle_lamp >= 0.0
        ? apply_vehicle_damage(source.rgb) * v_tint.rgb
        : apply_vehicle_damage(source.rgb * v_tint.rgb);
    float material_specular = u_material_specular_scale;
    // Keep zero cover on a literal branch: clear weather must not perturb the
    // existing material path at all. Snow catches broad upward-facing world
    // surfaces and becomes matte as it hides the finish underneath.
    if (u_receives_snow && u_snow_cover > 0.0 &&
        v_vehicle_lamp < 0.0 && v_tint.a <= 1.0) {
        float accumulation = snow_accumulation(normalize(v_normal).y);
        albedo = apply_snow_cover(albedo, accumulation);
        material_specular *= 1.0 - accumulation * 0.82;
    }
    vec3 lit = apply_lighting(albedo, v_normal, v_world_pos,
                              material_specular);
    // Emissive lamp power remains independent of the car's paint color.
    lit += albedo * max(v_tint.a - 1.0, 0.0);
    float alpha = source.a * clamp(v_tint.a, 0.0, 1.0);
    frag_color = vec4(apply_fog(lit, v_world_pos), alpha);
}

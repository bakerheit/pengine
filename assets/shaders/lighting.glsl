// Shared lighting + fog. THE one implementation, #include'd by every lit
// fragment shader in the engine.
//
// This file is not a standalone shader: it has no #version and no main(). It is
// pasted in by the loader's #include pass (gfx/shader.cpp), which also keeps a
// line map so a compile error still reports THIS file and THIS line rather than
// an offset into a concatenated blob.
//
// The uniforms below are written by apply_lighting() in gfx/sky.cpp from one
// SkyEnv. Any shader that includes this file gets the whole set; a shader that
// leaves one unset is a shader lit by garbage, so apply_lighting() sets every
// one of them every time rather than assuming a default.

uniform vec3  u_light_dir;          // world space, normalised, TOWARD the light
uniform vec3  u_light_color;
uniform vec3  u_ambient;
uniform vec3  u_cam_pos;
uniform float u_specular_strength;  // 0 = matte; ~0.3 = world/metal
uniform float u_snow_cover;         // 0 = exact material no-op, 1 = full cover

// Accumulation is a material operation rather than a light. Only the opaque
// outdoor-world shader calls it, so water and moving characters stay clean.
// Vertical walls are below the first threshold; shallow ledges get a dusting.
float snow_accumulation(float normal_up) {
    if (u_snow_cover <= 0.0) return 0.0;
    float upward = smoothstep(0.35, 0.85, clamp(normal_up, 0.0, 1.0));
    return clamp(u_snow_cover, 0.0, 1.0) * upward;
}

vec3 apply_snow_cover(vec3 albedo, float accumulation) {
    if (accumulation <= 0.0) return albedo;
    const vec3 snow_albedo = vec3(0.82, 0.86, 0.90);
    return mix(albedo, snow_albedo, accumulation);
}

// Player-car headlamps. The host always writes both spots; intensity zero is
// the daylight off switch. Positions and directions are world space.
uniform vec3  u_headlight_pos[2];
uniform vec3  u_headlight_dir[2];
uniform vec3  u_headlight_color;
uniform vec2  u_headlight_intensity;
uniform float u_headlight_range;
uniform float u_headlight_inner_cos;
uniform float u_headlight_outer_cos;

uniform samplerBuffer u_traffic_lights;
uniform usamplerBuffer u_traffic_cells;
uniform usamplerBuffer u_traffic_indices;
uniform int u_traffic_columns;
uniform int u_traffic_rows;
uniform vec4 u_traffic_depth_plane;

// Halloway Gas canopy. Six downward spots share colour/cone/range but keep
// their authored positions, producing overlapping pools rather than one flat
// ambient rectangle across the lot.
uniform vec3  u_canopy_light_pos[6];
uniform vec3  u_canopy_light_dir;
uniform vec3  u_canopy_light_color;
uniform float u_canopy_light_intensity;
uniform float u_canopy_light_range;
uniform float u_canopy_light_inner_cos;
uniform float u_canopy_light_outer_cos;

// Distance fog. Layered ONTO the sky env, and an exact no-op when disabled:
// u_fog_end <= u_fog_start returns the input colour untouched, bit for bit.
// That is the contract weather relies on — a clear day must look identical
// whether or not the fog code path exists.
uniform vec3  u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform float u_fog_density;        // 0 = off, 1 = full strength at u_fog_end

// Blinn-Phong against the single directional light the sky env produces.
// `normal_ws` need not be normalised; `albedo` is already tinted.
vec3 apply_lighting(vec3 albedo, vec3 normal_ws, vec3 world_pos,
                    float surface_specular) {
    vec3 N = normalize(normal_ws);
    vec3 L = normalize(u_light_dir);
    vec3 V = normalize(u_cam_pos - world_pos);
    vec3 H = normalize(L + V);

    float n_dot_l = max(dot(N, L), 0.0);
    float n_dot_h = max(dot(N, H), 0.0);

    vec3 ambient  = u_ambient * albedo;
    vec3 diffuse  = u_light_color * albedo * n_dot_l;
    // Specular is gated on n_dot_l so a surface facing away from the light
    // cannot pick up a highlight — otherwise back faces glint at grazing angles
    // and the whole scene reads as wet plastic at sunset.
    vec3 specular = u_light_color * pow(n_dot_h, 64.0) * u_specular_strength
                    * step(0.0001, n_dot_l) * surface_specular;

    vec3 result = ambient + diffuse + specular;

    // Twin soft-edged spot lights. Linear range squared gives a smooth falloff
    // without the singular hot pixel an inverse-square light gets at its own
    // lamp position. Each spot still uses the real surface normal and view
    // vector, so terrain shape and the car paint read under the beam.
    for (int i = 0; i < 2; ++i) {
        if (u_headlight_intensity[i] <= 0.0) continue;
        vec3 to_light = u_headlight_pos[i] - world_pos;
        float distance_to_light = length(to_light);
        if (distance_to_light >= u_headlight_range || distance_to_light < 0.001) {
            continue;
        }

        vec3 headlight_L = to_light / distance_to_light;
        vec3 from_lamp = -headlight_L;
        float cone_dot = dot(from_lamp, normalize(u_headlight_dir[i]));
        if (cone_dot <= u_headlight_outer_cos) continue;
        float cone = smoothstep(u_headlight_outer_cos,
                                u_headlight_inner_cos, cone_dot);
        float range_fade = 1.0 - distance_to_light / u_headlight_range;
        float attenuation = cone * range_fade * range_fade *
                            u_headlight_intensity[i];

        float lamp_diffuse = max(dot(N, headlight_L), 0.0);
        vec3 lamp_H = normalize(headlight_L + V);
        float lamp_specular = pow(max(dot(N, lamp_H), 0.0), 48.0) * 0.18 *
                              step(0.0001, lamp_diffuse) * surface_specular;
        result += u_headlight_color *
                  (albedo * lamp_diffuse + lamp_specular) * attenuation;
    }

    // 64-pixel tiles with 24 logarithmic depth bands, matching
    // tiled_light_grid.h. Lists are variable length, with no overflow cap.
    if (u_traffic_columns > 0) {
        float depth = dot(u_traffic_depth_plane,vec4(world_pos,1.0));
        if (depth >= 0.15 && depth <= 256.0) {
            int slice = clamp(int(log(depth / 0.15) / log(256.0 / 0.15) * 24.0),0,23);
            ivec2 tile = clamp(ivec2(gl_FragCoord.xy) / 64,ivec2(0),
                               ivec2(u_traffic_columns-1,u_traffic_rows-1));
            uvec2 cell = texelFetch(u_traffic_cells,(slice*u_traffic_rows+tile.y)*u_traffic_columns+tile.x).rg;
            for(uint j=0u;j<cell.y;++j) {
                uint address=cell.x+j;
                uint id=texelFetch(u_traffic_indices,int(address)).r;
                int a=int(id)*3;
                vec4 pr=texelFetch(u_traffic_lights,a);
                vec3 delta=pr.xyz-world_pos;
                float distance2=dot(delta,delta);
                if(distance2>=pr.w*pr.w || distance2<0.000001) continue;
                vec4 dp=texelFetch(u_traffic_lights,a+1);
                vec4 co=texelFetch(u_traffic_lights,a+2);
                float inverse_distance=inversesqrt(distance2);
                vec3 lamp_L=delta*inverse_distance;
                float cone_dot=dot(-lamp_L,dp.xyz);
                if(cone_dot<=co.w || dp.w<=0.0) continue;
                float diffuse_factor=max(dot(N,lamp_L),0.0);
                if(diffuse_factor<=0.0) continue;
                float fade=1.0-distance2*inverse_distance/pr.w;
                float inner_cos=1.0-(1.0-co.w)/3.0;
                float power=smoothstep(co.w,inner_cos,cone_dot)*fade*fade*dp.w*
                    (1.0-smoothstep(200.0,256.0,depth));
                vec3 lamp_H=normalize(lamp_L+V);
                float shine=pow(max(dot(N,lamp_H),0.0),48.0)*0.18*surface_specular;
                result+=co.rgb*(albedo*diffuse_factor+shine)*power;
            }
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (u_canopy_light_intensity <= 0.0) continue;
        vec3 to_light = u_canopy_light_pos[i] - world_pos;
        float distance_to_light = length(to_light);
        if (distance_to_light >= u_canopy_light_range ||
            distance_to_light < 0.001) {
            continue;
        }

        vec3 canopy_L = to_light / distance_to_light;
        vec3 from_lamp = -canopy_L;
        float cone_dot = dot(from_lamp, normalize(u_canopy_light_dir));
        if (cone_dot <= u_canopy_light_outer_cos) continue;
        float cone = smoothstep(u_canopy_light_outer_cos,
                                u_canopy_light_inner_cos, cone_dot);
        float range_fade = 1.0 - distance_to_light / u_canopy_light_range;
        float attenuation = cone * range_fade * range_fade *
                            u_canopy_light_intensity;

        float canopy_diffuse = max(dot(N, canopy_L), 0.0);
        vec3 canopy_H = normalize(canopy_L + V);
        float canopy_specular =
            pow(max(dot(N, canopy_H), 0.0), 40.0) * 0.16 *
            step(0.0001, canopy_diffuse) * surface_specular;
        result += u_canopy_light_color *
                  (albedo * canopy_diffuse + canopy_specular) * attenuation;
    }

    return result;
}

// Existing terrain, character and other shader paths keep their own finish.
vec3 apply_lighting(vec3 albedo, vec3 normal_ws, vec3 world_pos) {
    return apply_lighting(albedo, normal_ws, world_pos, 1.0);
}

// Smooth distance haze between u_fog_start and u_fog_end metres, scaled by
// u_fog_density. The eased shoulder keeps nearby props crisp while the far end
// reaches a solid atmospheric wall that can safely hide distance culling.
vec3 apply_fog(vec3 lit, vec3 world_pos) {
    if (u_fog_end <= u_fog_start || u_fog_density <= 0.0) return lit;
    float d = length(u_cam_pos - world_pos);
    float f = clamp((d - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
    f = f * f * (3.0 - 2.0 * f);
    return mix(lit, u_fog_color, f * u_fog_density);
}

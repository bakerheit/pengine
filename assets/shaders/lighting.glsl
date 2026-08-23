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
vec3 apply_lighting(vec3 albedo, vec3 normal_ws, vec3 world_pos) {
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
                    * step(0.0001, n_dot_l);

    return ambient + diffuse + specular;
}

// Linear distance haze between u_fog_start and u_fog_end metres, scaled by
// u_fog_density. Disabled -> returns `lit` unchanged.
vec3 apply_fog(vec3 lit, vec3 world_pos) {
    if (u_fog_end <= u_fog_start || u_fog_density <= 0.0) return lit;
    float d = length(u_cam_pos - world_pos);
    float f = clamp((d - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
    return mix(lit, u_fog_color, f * u_fog_density);
}

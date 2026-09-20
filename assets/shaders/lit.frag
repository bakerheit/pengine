#version 330 core

// Shared world material: local damage is composited before normal lighting.
#include "lighting.glsl"
#include "vehicle_damage.glsl"
#include "vehicle_headlights.glsl"
#include "vehicle_snow.glsl"

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_tint;
in vec4 v_terrain_weights;
in vec4 v_windshield;
flat in float v_vehicle_lamp;
flat in int v_headlight_profile;
in vec3 v_lamp_source_position;

uniform sampler2D u_diffuse;
uniform bool u_glass;
uniform bool u_receives_snow;
uniform float u_material_specular_scale = 1.0;
out vec4 frag_color;

// Each texel pair is {start.xyz,width}, {end.xyz,remaining visual cover}.
// Height interpolation prevents a plowed road from clearing a bridge or roof.
uniform sampler2D u_snow_clearance;
uniform int u_snow_clearance_count;
uniform vec4 u_snow_clearance_bounds;
uniform float u_snow_clearance_height_tolerance;
// Static world-space cells hold complete roof lists. Open paved lots have no
// covering roof; top faces stay snowy because only points BELOW the underside
// qualify. Rotated roof corners use the same narrow phase as suspension snow.
uniform samplerBuffer u_snow_shelter_roofs;
uniform usamplerBuffer u_snow_shelter_cells;
uniform usamplerBuffer u_snow_shelter_indices;
uniform int u_snow_shelter_columns;
uniform int u_snow_shelter_rows;
uniform vec2 u_snow_shelter_origin;
uniform float u_snow_shelter_cell_size;
bool sheltered_from_snow(vec3 position) {
    if(u_snow_shelter_columns==0 || u_snow_shelter_rows==0) return false;
    ivec2 cell=ivec2(floor((position.xz-u_snow_shelter_origin)/u_snow_shelter_cell_size));
    if(cell.x<0 || cell.y<0 || cell.x>=u_snow_shelter_columns || cell.y>=u_snow_shelter_rows)
        return false;
    uvec2 span=texelFetch(u_snow_shelter_cells,cell.y*u_snow_shelter_columns+cell.x).xy;
    for(uint i=0u;i<span.y;++i) {
        int roof=int(texelFetch(u_snow_shelter_indices,int(span.x+i)).x)*4;
        vec4 anchor=texelFetch(u_snow_shelter_roofs,roof+2);
        if(position.y>=anchor.z) continue;
        vec4 broad=texelFetch(u_snow_shelter_roofs,roof+3);
        if(any(lessThan(position.xz,broad.xy)) || any(greaterThan(position.xz,broad.zw))) continue;
        vec4 local=texelFetch(u_snow_shelter_roofs,roof);
        vec4 axes=texelFetch(u_snow_shelter_roofs,roof+1);
        vec2 delta=position.xz-anchor.xy;
        vec2 point=vec2(dot(delta,axes.xy),dot(delta,axes.zw));
        if(all(greaterThanEqual(point,local.xy)) && all(lessThanEqual(point,local.zw)))
            return true;
    }
    return false;
}

float local_snow_cover(vec3 position) {
    if(sheltered_from_snow(position)) return 0.0;
    float cover=u_snow_cover;
    if (u_snow_clearance_count==0 ||
        position.x<u_snow_clearance_bounds.x || position.z<u_snow_clearance_bounds.y ||
        position.x>u_snow_clearance_bounds.z || position.z>u_snow_clearance_bounds.w)
        return cover;
    for (int i=0;i<u_snow_clearance_count;++i) {
        vec4 a=texelFetch(u_snow_clearance,ivec2(i*2,0),0);
        vec4 b=texelFetch(u_snow_clearance,ivec2(i*2+1,0),0);
        // Cheap rejection before the projection/divide for distant swaths.
        if (any(lessThan(position.xz,min(a.xz,b.xz)-vec2(a.w))) ||
            any(greaterThan(position.xz,max(a.xz,b.xz)+vec2(a.w)))) continue;
        vec2 delta=b.xz-a.xz;
        float t=clamp(dot(position.xz-a.xz,delta)/max(dot(delta,delta),1e-8),0.0,1.0);
        vec3 closest=mix(a.xyz,b.xyz,t);
        vec2 offset=position.xz-closest.xz;
        if (dot(offset,offset)<=a.w*a.w &&
            abs(position.y-closest.y)<=u_snow_clearance_height_tolerance)
            cover=min(cover,b.w);
    }
    return cover;
}


void main() {
    // Some legacy body triangles extend beyond the painted pane. Their metal
    // keeps ordinary snow shading even though they carry pane coordinates.
    bool windshield=v_windshield.w>.5 &&
        all(greaterThanEqual(v_windshield.xy,vec2(0.0))) &&
        all(lessThanEqual(v_windshield.xy,vec2(1.0)));
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
        if(u_snow_cover>0.0 && windshield) {
            float snow=local_snow_cover(v_world_pos)*
                windshield_snow_remaining(v_windshield.xyz);
            vec3 snowy=apply_lighting(vec3(.82,.86,.90),N,v_world_pos,0.0);
            glass=mix(glass,snowy,snow);
            opacity=mix(opacity,1.0,snow);
        }
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
            // Car 5-NEXT PATROL. Its source units are NOT metres: the catalog
            // squashes this imported body, so the bar sits much higher up the
            // Y axis than any cruiser's. One box per lens cell, so the chrome
            // ribs between them stay dark instead of lighting with the bank.
            // Generated by tools/car5_next_police_spec.py glsl(); the cook
            // validator fails if the mesh and these drift apart.
            lightbar = lightbar || (v_headlight_profile == 29 &&
                x>=0.0450 && x<=0.2067 && p.y>=2.131 && p.y<=2.318 &&
                p.z>=-0.110 && p.z<=0.290);
            lightbar = lightbar || (v_headlight_profile == 29 &&
                x>=0.2417 && x<=0.4033 && p.y>=2.131 && p.y<=2.318 &&
                p.z>=-0.110 && p.z<=0.290);
            lightbar = lightbar || (v_headlight_profile == 29 &&
                x>=0.4383 && x<=0.6000 && p.y>=2.131 && p.y<=2.318 &&
                p.z>=-0.110 && p.z<=0.290);
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
    // The Shū's curved lamp receivers meet red hood facets. Only neutral lens
    // texels emit; the shared geometric circle alone would also light paint.
    if (v_vehicle_lamp >= 0.0 && v_vehicle_lamp < 2.0 && v_headlight_profile == 22 &&
        (source.r > source.g * 1.35 || source.r > source.b * 1.35)) discard;
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
        if(windshield)
            accumulation=u_snow_cover*windshield_snow_remaining(v_windshield.xyz);
        if (accumulation > 0.0)
            accumulation *= local_snow_cover(v_world_pos) / u_snow_cover;
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

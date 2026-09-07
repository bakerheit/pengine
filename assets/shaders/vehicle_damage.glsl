// Color-neutral RGBA overlays, projected in the undeformed body frame.
// No separate polygons/depth offsets, no UV dependence, no health tint and
// no time-based random seed. Blank atlas texels leave base paint unchanged.
uniform sampler2D u_vehicle_damage;
flat in vec4 v_damage_zones;
flat in vec2 v_damage_stamps;
in vec3 v_damage_position;
in vec3 v_damage_normal;

vec3 damage_zone_values(float packed) {
    return vec3(mod(packed, 256.0), mod(floor(packed / 256.0), 256.0),
                floor(packed / 65536.0)) / 255.0;
}

vec4 damage_tile(vec2 point, float variant, float angle) {
    float c = cos(angle), s = sin(angle);
    vec2 uv = mat2(c, -s, s, c) * point * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return vec4(0.0);
    // Fixed low-resolution texel clusters keep the PSX read even if the
    // authored atlas is larger. Explicit LOD avoids neighboring-cell bleed.
    uv = (floor(uv * 64.0) + 0.5) / 64.0;
    uv = clamp(uv, vec2(0.5 / 64.0), vec2(1.0 - 0.5 / 64.0));
    vec2 cell = vec2(mod(variant, 4.0), 1.0 - floor(variant / 4.0));
    return textureLod(u_vehicle_damage, (cell + uv) / vec2(4.0, 2.0), 0.0);
}

vec3 damage_patch(vec3 paint, vec3 centre, vec3 extent,
                  float variant, float angle, float severity) {
    if (severity <= 0.001) return paint;
    vec3 q = (v_damage_position - centre) / extent;
    float edge = max(abs(q.x), max(abs(q.y), abs(q.z)));
    if (edge >= 1.0) return paint;
    float footprint = 1.0 - smoothstep(0.72, 1.0, edge);
    vec3 weights = pow(abs(normalize(v_damage_normal)), vec3(6.0));
    weights /= max(dot(weights, vec3(1.0)), 0.0001);
    // Premultiplied mixing between projections prevents dark fringes where
    // a fender changes angle. Mostly planar panels need only one lookup.
    vec4 decal = vec4(0.0);
    if (weights.x > 0.01) {
        vec4 t = damage_tile(q.zy, variant, angle);
        decal += vec4(t.rgb * t.a, t.a) * weights.x;
    }
    if (weights.y > 0.01) {
        vec4 t = damage_tile(q.xz, variant, angle);
        decal += vec4(t.rgb * t.a, t.a) * weights.y;
    }
    if (weights.z > 0.01) {
        vec4 t = damage_tile(q.xy, variant, angle);
        decal += vec4(t.rgb * t.a, t.a) * weights.z;
    }
    float opacity = footprint * smoothstep(0.015, 0.65, severity) * 0.88;
    decal.rgb = vec3(dot(decal.rgb, vec3(0.299, 0.587, 0.114)));
    return paint * (1.0 - decal.a * opacity) + decal.rgb * opacity;
}

vec3 apply_vehicle_damage(vec3 paint) {
    if (all(equal(v_damage_zones, vec4(0.0))) &&
        all(lessThan(mod(floor(v_damage_stamps / 1024.0), 32.0), vec2(0.5))))
        return paint;
    // Regional patches retain older damage after either of the two detailed
    // impact records is replaced. They never reach the opposite side/roof.
    for (int group = 0; group < 4; ++group) {
        vec3 strengths = damage_zone_values(v_damage_zones[group]);
        if (max(strengths.x, max(strengths.y, strengths.z)) <= 0.001) continue;
        for (int part = 0; part < 3; ++part) {
            float p = 1.0 - float(part);
            bool end = group < 2;
            float sign_side = mod(float(group), 2.0) == 0.0 ? 1.0 : -1.0;
            vec3 centre = end ? vec3(p * 0.60, -0.28, sign_side * 0.86)
                              : vec3(sign_side * 0.86, -0.22, p * 0.58);
            vec3 extent = end ? vec3(0.49, 0.48, 0.35)
                              : vec3(0.35, 0.48, 0.44);
            float index = float(group * 3 + part);
            paint = damage_patch(paint, centre, extent, mod(index, 7.0),
                                 (mod(index, 3.0) - 1.0) * 0.17,
                                 strengths[part] * 0.65);
        }
    }
    for (int i = 0; i < 2; ++i) {
        float packed = v_damage_stamps[i];
        float severity = mod(floor(packed / 1024.0), 32.0) / 31.0;
        if (severity <= 0.001) continue;
        vec2 cq = vec2(mod(packed, 32.0), mod(floor(packed / 32.0), 32.0));
        vec2 contact = -(cq / 31.0 * 2.0 - 1.0);
        float angleq = mod(floor(packed / 32768.0), 16.0);
        float angle = angleq / 15.0 * 6.28318530718 - 3.14159265359;
        float radiusq = mod(floor(packed / 524288.0), 4.0);
        float heightq = mod(floor(packed / 2097152.0), 4.0);
        float glancing = mod(floor(packed / 8388608.0), 2.0);
        vec2 motion = abs(vec2(cos(angle), sin(angle)));
        float radius = mix(0.24, 0.62, radiusq / 3.0);
        vec3 extent = vec3(radius * (1.0 + glancing * motion.x * 1.6),
                           0.34 + radiusq * 0.055,
                           radius * (1.0 + glancing * motion.y * 1.6));
        // Exclude severity from variant/rotation: an existing mark must not
        // change pattern as its damage strength grows.
        float variant = mod(cq.x + cq.y * 3.0 + radiusq + heightq * 2.0, 7.0);
        paint = damage_patch(paint, vec3(contact.x, heightq / 3.0 * 2.0 - 1.0,
                                        contact.y), extent, variant,
                             glancing > 0.5 ? 0.0 : angle * 0.2, severity);
    }
    return paint;
}

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
layout(location = 3) in vec4 a_material_weights;

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
layout(location = 13) in vec4 a_inst_body_damage0;
layout(location = 14) in vec4 a_inst_body_damage1;
layout(location = 15) in vec4 a_inst_deform_frame;

uniform mat4 u_view_proj;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_tint;
out vec4 v_terrain_weights;
flat out float v_vehicle_lamp;
flat out int v_headlight_profile;
out vec3 v_lamp_source_position;
out float v_body_damage;
out vec2 v_body_coord;
out float v_body_height;
out float v_loose_panel;
out float v_impact_mark;
out float v_impact_glancing;
out vec2 v_impact_axis;
// Flat records are decoded per fragment: coarse vehicle triangles must not
// smear a small impact over a whole door. Coordinates precede deformation.
flat out vec4 v_damage_zones;
flat out vec2 v_damage_stamps;
out vec3 v_damage_position;
out vec3 v_damage_normal;

vec3 unpack_damage_triple(float packed) {
    float value = clamp(packed, 0.0, 16777215.0);
    float high = floor(value / 65536.0);
    float remainder = value - high * 65536.0;
    float middle = floor(remainder / 256.0);
    float low = remainder - middle * 256.0;
    return vec3(low, middle, high) / 255.0;
}

void unpack_dent_stamp(float packed, out vec2 contact, out float severity,
                       out vec2 motion, out float radius, out float height,
                       out float glancing) {
    float value = clamp(packed, 0.0, 16777215.0);
    float xq = mod(value, 32.0);
    float zq = mod(floor(value / 32.0), 32.0);
    float severity_q = mod(floor(value / 1024.0), 32.0);
    float angle_q = mod(floor(value / 32768.0), 16.0);
    float radius_q = mod(floor(value / 524288.0), 4.0);
    float height_q = mod(floor(value / 2097152.0), 4.0);
    glancing = mod(floor(value / 8388608.0), 2.0);
    // Stored coordinates use the physics frame. Both source X and Z reverse
    // through the legacy body fit, so flip contact and motion together.
    contact = -(vec2(xq, zq) / 31.0 * 2.0 - 1.0);
    severity = severity_q / 31.0;
    float angle = angle_q / 15.0 * 6.28318530718 - 3.14159265359;
    motion = -vec2(cos(angle), sin(angle));
    radius = radius_q / 3.0;
    height = height_q / 3.0;
}

void main() {
    v_vehicle_lamp = a_inst_uv_scale.x <= -2.0 ? -a_inst_uv_scale.x - 2.0 : -1.0;
    v_headlight_profile = int(a_inst_uv_scale.y) - 1;
    v_lamp_source_position = a_pos;
    vec3 local_pos = a_pos;
    bool damage_frame = a_inst_deform_frame.x > 0.0 &&
                        a_inst_deform_frame.y > 0.0;
    v_damage_zones = damage_frame ? a_inst_body_damage0 : vec4(0.0);
    v_damage_stamps = damage_frame ? a_inst_body_damage1.xy : vec2(0.0);
    v_damage_position = vec3(
        (a_pos.x - a_inst_body_damage1.z) * a_inst_deform_frame.x,
        (a_pos.y - a_inst_deform_frame.z) * a_inst_deform_frame.w,
        (a_pos.z - a_inst_body_damage1.w) * a_inst_deform_frame.y);
    v_damage_normal = a_normal;
    v_body_damage = 0.0;
    v_body_coord = vec2(0.0);
    v_body_height = 0.0;
    v_loose_panel = 0.0;
    v_impact_mark = 0.0;
    v_impact_glancing = 0.0;
    v_impact_axis = vec2(0.0, 1.0);
    float peak_damage = max(max(a_inst_body_damage0.x,
                                a_inst_body_damage0.y),
                            max(a_inst_body_damage0.z,
                                a_inst_body_damage0.w));
    if (peak_damage > 0.0001 &&
        a_inst_deform_frame.x > 0.0 && a_inst_deform_frame.y > 0.0) {
        // Legacy car meshes face native +Z and native +X becomes chassis-left
        // after their 180-degree fit. Work in normalised source-mesh space so
        // the same twelve damage areas fit every traffic body.
        float nx = clamp((a_pos.x - a_inst_body_damage1.z) *
                         a_inst_deform_frame.x, -1.0, 1.0);
        float nz = clamp((a_pos.z - a_inst_body_damage1.w) *
                         a_inst_deform_frame.y, -1.0, 1.0);
        float ny = a_inst_deform_frame.w > 0.0
            ? clamp((a_pos.y - a_inst_deform_frame.z) *
                    a_inst_deform_frame.w, -1.0, 1.0)
            : 0.0;
        vec3 front = unpack_damage_triple(a_inst_body_damage0.x);
        vec3 rear = unpack_damage_triple(a_inst_body_damage0.y);
        vec3 side_left = unpack_damage_triple(a_inst_body_damage0.z);
        vec3 side_right = unpack_damage_triple(a_inst_body_damage0.w);
        float front_left = front.x;
        float front_center = front.y;
        float front_right = front.z;
        float rear_left = rear.x;
        float rear_center = rear.y;
        float rear_right = rear.z;
        float side_left_front = side_left.x;
        float side_left_middle = side_left.y;
        float side_left_rear = side_left.z;
        float side_right_front = side_right.x;
        float side_right_middle = side_right.y;
        float side_right_rear = side_right.z;

        float left_band = smoothstep(-0.12, 0.82, nx);
        float right_band = smoothstep(-0.12, 0.82, -nx);
        float center_band = 1.0 - smoothstep(0.02, 0.68, abs(nx));
        float longitudinal_front = smoothstep(-0.12, 0.82, nz);
        float longitudinal_rear = smoothstep(-0.12, 0.82, -nz);
        float longitudinal_middle =
            1.0 - smoothstep(0.02, 0.68, abs(nz));
        float front_damage = max(front_left * left_band,
            max(front_center * center_band, front_right * right_band));
        float rear_damage = max(rear_left * left_band,
            max(rear_center * center_band, rear_right * right_band));
        float front_surface = smoothstep(0.05, 0.92, nz);
        float rear_surface = smoothstep(0.05, 0.92, -nz);
        float middle = 1.0 - smoothstep(0.18, 0.72, abs(nz));
        float left_surface = smoothstep(0.12, 0.92, nx);
        float right_surface = smoothstep(0.12, 0.92, -nx);

        float left_side_damage = max(
            side_left_front * longitudinal_front,
            max(side_left_middle * longitudinal_middle,
                side_left_rear * longitudinal_rear));
        float right_side_damage = max(
            side_right_front * longitudinal_front,
            max(side_right_middle * longitudinal_middle,
                side_right_rear * longitudinal_rear));
        float left_damage = max(left_side_damage * middle,
                                max(front_left * front_surface,
                                    rear_left * rear_surface));
        float right_damage = max(right_side_damage * middle,
                                 max(front_right * front_surface,
                                     rear_right * rear_surface));

        // Light hits stay tight to the contacted skin; a deep hit broadens
        // into the surrounding panel. Front, rear and side panels use slightly
        // different falloffs so every collision does not make the same dent.
        float front_shape = mix(pow(front_surface, 3.4), front_surface,
                                smoothstep(0.24, 0.82, front_damage));
        float rear_shape = mix(pow(rear_surface, 2.8), rear_surface,
                               smoothstep(0.22, 0.78, rear_damage));
        float left_shape = mix(pow(left_surface, 3.0), left_surface,
                               smoothstep(0.20, 0.76, left_damage));
        float right_shape = mix(pow(right_surface, 3.0), right_surface,
                                smoothstep(0.20, 0.76, right_damage));

        // Displacements are fractions of the source half-extents. They remain
        // physically consistent after each model's fit transform and only move
        // body vertices inward, so original culling bounds stay conservative.
        float half_x = 1.0 / a_inst_deform_frame.x;
        float half_z = 1.0 / a_inst_deform_frame.y;
        local_pos.z -= front_damage * front_shape * half_z * 0.22;
        local_pos.z += rear_damage * rear_shape * half_z * 0.20;
        local_pos.x -= left_damage * left_shape * half_x * 0.18;
        local_pos.x += right_damage * right_shape * half_x * 0.18;

        // Deep localized damage loosens the panel instead of continuing to
        // shrink the whole body forever. These masks MUST depend only on source
        // position, not vertex normal. The legacy bodies duplicate vertices at
        // hood/fender/door seams with different normals; normal-based motion
        // pulled those coincident vertices apart and opened holes in the shell.
        // Smooth positional masks keep every copy of a seam welded while the
        // height/length regions still give each panel its own response.
        float upper_shell = smoothstep(-0.30, 0.18, ny);
        float side_shell = smoothstep(0.24, 0.76, abs(nx));
        float end_shell = smoothstep(0.34, 0.82, abs(nz));
        float end_panel_height = smoothstep(-0.34, 0.02, ny) *
                                 (1.0 - smoothstep(0.58, 0.88, ny));
        float side_panel_height = smoothstep(-0.62, -0.12, ny) *
                                  (1.0 - smoothstep(0.34, 0.68, ny));
        float front_loose = smoothstep(0.56, 0.92, front_damage) *
                            front_shape * upper_shell * end_panel_height;
        float rear_loose = smoothstep(0.56, 0.92, rear_damage) *
                           rear_shape * upper_shell * end_panel_height;
        float left_loose = smoothstep(0.58, 0.94, left_damage) *
                           left_shape * side_shell * side_panel_height;
        float right_loose = smoothstep(0.58, 0.94, right_damage) *
                            right_shape * side_shell * side_panel_height;
        // These values drive the exposed-seam material below, but do not pull
        // the combined legacy shell apart. Actual detached panels need an
        // inner body layer; without one, geometric peeling exposes the sky.
        v_loose_panel = clamp(max(max(front_loose, rear_loose),
                                  max(left_loose, right_loose)), 0.0, 1.0);

        // Two persistent impact stamps sit on top of the broad regional
        // crush. A direct narrow hit makes a compact bowl, a broad hit spreads
        // compression across a panel, and a glancing hit lays an elongated
        // crease along its travel direction. Panel masks then decide how that
        // force is allowed to move this particular piece of bodywork.
        float low_panel = 1.0 - smoothstep(-0.48, -0.02, ny);
        float mid_panel = smoothstep(-0.62, -0.18, ny) *
                          (1.0 - smoothstep(0.30, 0.66, ny));
        float high_panel = smoothstep(0.34, 0.72, ny);
        float centre_length = 1.0 - smoothstep(0.28, 0.68, abs(nz));
        float quarter_length = smoothstep(0.28, 0.72, abs(nz));
        for (int stamp_index = 0; stamp_index < 2; ++stamp_index) {
            float packed_stamp = stamp_index == 0
                ? a_inst_body_damage1.x : a_inst_body_damage1.y;
            vec2 stamp_contact;
            float stamp_severity;
            vec2 stamp_motion;
            float stamp_radius;
            float stamp_height;
            float stamp_glancing;
            unpack_dent_stamp(packed_stamp, stamp_contact, stamp_severity,
                              stamp_motion, stamp_radius, stamp_height,
                              stamp_glancing);
            if (stamp_severity <= 0.0001) continue;

            vec2 delta = vec2(nx, nz) - stamp_contact;
            vec2 axis = normalize(stamp_motion);
            vec2 across_axis = vec2(-axis.y, axis.x);
            float along = abs(dot(delta, axis));
            float across = abs(dot(delta, across_axis));
            float radius = mix(0.13, 0.48, stamp_radius);
            float direct_field = 1.0 - smoothstep(radius, radius * 1.75,
                                                  length(delta));
            float crease_field =
                (1.0 - smoothstep(radius * 0.32, radius * 0.74, across)) *
                (1.0 - smoothstep(radius * 1.4, radius * 3.2, along));
            float expected_height = stamp_height * 2.0 - 1.0;
            float height_match = 1.0 - smoothstep(0.30, 0.82,
                                                   abs(ny - expected_height));
            float field = mix(direct_field, crease_field, stamp_glancing) *
                          height_match * stamp_severity;
            if (field <= 0.0001) continue;

            float end_hit = step(abs(stamp_contact.x),
                                 abs(stamp_contact.y));
            float side_hit = 1.0 - end_hit;
            float end_sign = stamp_contact.y < 0.0 ? -1.0 : 1.0;
            float side_sign = stamp_contact.x < 0.0 ? -1.0 : 1.0;
            float compact = 1.0 - stamp_radius;
            float crush_depth = field * mix(0.075, 0.155, compact) *
                                mix(1.0, 0.58, stamp_glancing);

            // Bumpers compress and sag. Hoods/trunks form a raised fold next
            // to the depression. Doors take a long crease. Quarter panels
            // wrap inward around the wheel arch. High central hits cave roofs.
            float bumper = end_hit * end_shell * low_panel;
            float hood_or_trunk = end_hit * upper_shell * end_panel_height;
            float door = side_hit * side_shell * mid_panel * centre_length;
            float quarter = side_hit * side_shell * mid_panel * quarter_length;
            float roof = upper_shell * high_panel * centre_length *
                         smoothstep(0.50, 0.84, stamp_height);

            local_pos.z -= end_sign * crush_depth * half_z *
                           (bumper * 1.25 + hood_or_trunk * 0.78);
            local_pos.x -= side_sign * crush_depth * half_x *
                           (door * 1.05 + quarter * 1.30);
            local_pos.y -= bumper * field * half_z * 0.040;
            local_pos.y += hood_or_trunk * crease_field * stamp_severity *
                           half_z * 0.030;
            local_pos.y -= quarter * field * half_x * 0.045;
            local_pos.y -= roof * direct_field * stamp_severity *
                           half_z * 0.105;

            if (field > v_impact_mark) {
                v_impact_mark = field;
                v_impact_glancing = stamp_glancing;
                v_impact_axis = axis;
            }
        }

        // Interpolated into the fragment stage for localized paint wear. Use
        // the pre-deformation source coordinate so scratches stay attached to
        // the same panel as its dent grows.
        v_body_damage = clamp(max(
            max(front_damage * front_surface,
                rear_damage * rear_surface),
            max(left_damage * left_surface,
                right_damage * right_surface)), 0.0, 1.0);
        v_body_damage = max(v_body_damage, v_impact_mark);
        v_body_coord = vec2(nx, nz);
        v_body_height = ny;
    }

    vec4 world  = a_inst_model * vec4(local_pos, 1.0);
    v_world_pos = world.xyz;
    v_normal    = mat3(a_inst_nrm0.xyz, a_inst_nrm1.xyz, a_inst_nrm2.xyz) * a_normal;
    // uv_scale is folded in HERE rather than in the fragment stage so the
    // tiling interpolates and the texture lookup stays one instruction.
    bool terrain=a_inst_uv_scale.x>0.0 && a_inst_uv_scale.y<0.0;
    v_terrain_weights=terrain ? a_material_weights : vec4(0);
    v_uv        = v_vehicle_lamp >= 0.0 ? a_uv : a_uv *
        (terrain ? abs(a_inst_uv_scale) : a_inst_uv_scale);
    v_tint      = a_inst_tint;
    gl_Position = u_view_proj * world;
    // Glow is drawn later with GL_EQUAL against the body's exact depth.
    // Never pull it toward the camera: a clip-space bias grows with distance
    // and can reveal the lens through folded panels or nearby occluders.
}

#pragma once
#include <array>
#include <cmath>
#include "gfx/primitives.h"

namespace apricot {
// Shared unit-volume builder for the museum's own exhibit meshes. Every mesh
// in this header stays inside the same +/-0.5 box the authored StartPart
// placement convention assumes, so an exhibit is scaled by its part record
// exactly like a box fixture is.
struct LoomMeshBuilder {
    MeshData mesh;
    static constexpr float tau = 6.2831853f;

    void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 facing,
                  glm::vec2 uv_a = {0, 0}, glm::vec2 uv_b = {1, 0}, glm::vec2 uv_c = {1, 1}) {
        if (glm::dot(glm::cross(b - a, c - a), facing) < 0) { std::swap(b, c); std::swap(uv_b, uv_c); }
        const auto cross = glm::cross(b - a, c - a);
        // A degenerate sliver has no normal to normalise. Skip it rather than
        // ship a NaN: one NaN vertex takes the whole draw call with it.
        if (glm::dot(cross, cross) < 1e-14f) return;
        const auto normal = glm::normalize(cross);
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec2 uv[3]{uv_a, uv_b, uv_c};
        const glm::vec3 p[3]{a, b, c};
        for (int i = 0; i < 3; ++i) {
            mesh.vertices.push_back({p[i], normal, uv[i], {1, 0, 0, 0}});
            mesh.bounds.expand(p[i]);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
    }

    void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 facing) {
        triangle(a, b, c, facing, {0, 0}, {1, 0}, {1, 1});
        triangle(a, c, d, facing, {0, 0}, {1, 1}, {0, 1});
    }

    // Axis-aligned box between two corners. Exhibits are assembled from a few
    // of these plus the swept forms below, which keeps every mesh watertight.
    void box(glm::vec3 lo, glm::vec3 hi) {
        const glm::vec3 c = (lo + hi) * 0.5f;
        for (int axis = 0; axis < 3; ++axis) for (int side = 0; side < 2; ++side) {
            glm::vec3 n{0}; n[axis] = side ? 1.f : -1.f;
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            glm::vec3 corner[4];
            for (int i = 0; i < 4; ++i) {
                corner[i] = c;
                corner[i][axis] = side ? hi[axis] : lo[axis];
                corner[i][u] = (i == 0 || i == 3) ? lo[u] : hi[u];
                corner[i][v] = (i < 2) ? lo[v] : hi[v];
            }
            quad(corner[0], corner[1], corner[2], corner[3], n);
        }
    }

    // Surface of revolution about Y from a radius/height profile. Used by the
    // amphora, the cone and the sphere so they share one winding rule.
    void revolve(const glm::vec2* profile, std::size_t count, int sectors,
                 bool cap_bottom, bool cap_top) {
        for (std::size_t row = 0; row + 1 < count; ++row) for (int s = 0; s < sectors; ++s) {
            const float a = tau * static_cast<float>(s) / static_cast<float>(sectors);
            const float b = tau * static_cast<float>(s + 1) / static_cast<float>(sectors);
            const auto at = [&](std::size_t r, float angle) {
                return glm::vec3{profile[r].x * std::cos(angle), profile[r].y,
                                 profile[r].x * std::sin(angle)};
            };
            const auto tangent = profile[row + 1] - profile[row];
            const glm::vec3 facing{tangent.y * std::cos((a + b) * .5f), -tangent.x,
                                   tangent.y * std::sin((a + b) * .5f)};
            const float v0 = static_cast<float>(row) / static_cast<float>(count - 1);
            const float v1 = static_cast<float>(row + 1) / static_cast<float>(count - 1);
            const float u0 = static_cast<float>(s) / static_cast<float>(sectors);
            const float u1 = static_cast<float>(s + 1) / static_cast<float>(sectors);
            triangle(at(row, a), at(row, b), at(row + 1, b), facing, {u0, v0}, {u1, v0}, {u1, v1});
            triangle(at(row, a), at(row + 1, b), at(row + 1, a), facing, {u0, v0}, {u1, v1}, {u0, v1});
        }
        for (int end = 0; end < 2; ++end) {
            if (!(end ? cap_top : cap_bottom)) continue;
            const glm::vec2 rim = end ? profile[count - 1] : profile[0];
            if (rim.x <= 1e-5f) continue;
            for (int s = 0; s < sectors; ++s) {
                const float a = tau * static_cast<float>(s) / static_cast<float>(sectors);
                const float b = tau * static_cast<float>(s + 1) / static_cast<float>(sectors);
                triangle({0, rim.y, 0}, {rim.x * std::cos(a), rim.y, rim.x * std::sin(a)},
                         {rim.x * std::cos(b), rim.y, rim.x * std::sin(b)}, {0, end ? 1.f : -1.f, 0});
            }
        }
    }
};

// A small, deterministic classical amphora. Unit-volume bounds let it use the
// same authored placement/collision convention as the museum's other pieces.
inline MeshData make_loom_amphora() {
    MeshData out;
    constexpr float tau=6.2831853f;
    constexpr glm::vec4 rock{1,0,0,0};
    const auto triangle=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 facing) {
        if(glm::dot(glm::cross(b-a,c-a),facing)<0)std::swap(b,c);
        const auto normal=glm::normalize(glm::cross(b-a,c-a));
        const auto base=static_cast<uint32_t>(out.vertices.size());
        for(const auto p:{a,b,c}) {
            out.vertices.push_back({p,normal,{p.x+.5f,p.y+.5f},rock});
            out.bounds.expand(p);
        }
        out.indices.insert(out.indices.end(),{base,base+1,base+2});
    };
    // Radius / height profile: foot, tapered belly, shoulder, neck, flared lip
    // and an inset dark interior. The vessel is visibly open at the mouth.
    const std::array<glm::vec2,15> profile{{
        {.15f,-.5f},{.18f,-.47f},{.14f,-.40f},{.24f,-.31f},
        {.32f,-.18f},{.34f,-.04f},{.31f,.10f},{.23f,.21f},
        {.13f,.29f},{.13f,.40f},{.21f,.44f},{.21f,.49f},
        {.16f,.49f},{.10f,.40f},{.10f,.29f}}};
    for(std::size_t row=0;row+1<profile.size();++row)for(int sector=0;sector<24;++sector) {
        const float a=tau*static_cast<float>(sector)/24;
        const float b=tau*static_cast<float>(sector+1)/24;
        const auto p=[&](std::size_t r,float angle) {
            return glm::vec3{profile[r].x*std::cos(angle),profile[r].y,profile[r].x*std::sin(angle)};
        };
        const auto tangent=profile[row+1]-profile[row];
        const glm::vec3 facing{tangent.y*std::cos((a+b)*.5f),-tangent.x,
            tangent.y*std::sin((a+b)*.5f)};
        triangle(p(row,a),p(row,b),p(row+1,b),facing);
        triangle(p(row,a),p(row+1,b),p(row+1,a),facing);
    }
    for(int sector=0;sector<24;++sector) {
        const float a=tau*static_cast<float>(sector)/24,b=tau*static_cast<float>(sector+1)/24;
        triangle({0,-.5f,0},{.15f*std::cos(a),-.5f,.15f*std::sin(a)},
            {.15f*std::cos(b),-.5f,.15f*std::sin(b)},{0,-1,0});
        triangle({0,.29f,0},{.10f*std::cos(a),.29f,.10f*std::sin(a)},
            {.10f*std::cos(b),.29f,.10f*std::sin(b)},{0,1,0});
    }
    // Two open handles, formed from swept hexagonal tubes.
    for(float side:{-1.f,1.f})for(int step=0;step<16;++step) {
        const auto point=[&](int s,int ring) {
            const float t=static_cast<float>(s)/16;
            const float angle=static_cast<float>(ring)*tau/6;
            const glm::vec3 center{side*(.15f+.32f*std::sin(t*tau*.5f)),.32f-.44f*t,0};
            const auto tangent=glm::normalize(glm::vec3{side*.32f*tau*.5f*std::cos(t*tau*.5f),-.44f,0});
            const glm::vec3 normal{-tangent.y,tangent.x,0};
            return center+.032f*(normal*std::cos(angle)+glm::vec3{0,0,1}*std::sin(angle));
        };
        for(int ring=0;ring<6;++ring) {
            const auto a=point(step,ring),b=point(step+1,ring);
            const auto c=point(step+1,ring+1),d=point(step,ring+1);
            const float t=(static_cast<float>(step)+.5f)/16;
            const glm::vec3 center{side*(.15f+.32f*std::sin(t*tau*.5f)),.32f-.44f*t,0};
            const auto facing=(a+b+c+d)*.25f-center;
            triangle(a,b,c,facing);triangle(a,c,d,facing);
        }
    }
    return out;
}

// A round bob, planet or finial. Twenty sectors by fourteen rings reads as
// smooth at the sizes the museum uses and costs less than a shared sphere
// sized for a hero prop.
inline MeshData make_loom_sphere() {
    LoomMeshBuilder b;
    constexpr int rings = 14;
    std::array<glm::vec2, rings + 1> profile{};
    for (int i = 0; i <= rings; ++i) {
        const float t = static_cast<float>(i) / rings;
        const float angle = (t - .5f) * 3.14159265f;
        profile[static_cast<std::size_t>(i)] = {.5f * std::cos(angle), .5f * std::sin(angle)};
    }
    b.revolve(profile.data(), profile.size(), 20, false, false);
    return b.mesh;
}

// Nose cone. Apex up, so a rocket stacks body then cone with no rotation.
inline MeshData make_loom_cone() {
    LoomMeshBuilder b;
    // A short shoulder under the apex stops the tip shading as a hard spike.
    const std::array<glm::vec2, 6> profile{{{.5f, -.5f}, {.47f, -.28f}, {.40f, -.02f},
                                            {.29f, .22f}, {.14f, .40f}, {0, .5f}}};
    b.revolve(profile.data(), profile.size(), 20, true, false);
    return b.mesh;
}

// A locomotive driving wheel: tyre, flange, twelve spokes and a hub, lying in
// the XY plane so it takes the same yaw/roll a box fixture would. Flat discs
// were what made the old exhibit read as a balloon rather than a wheel.
inline MeshData make_loom_spoked_wheel() {
    LoomMeshBuilder b;
    constexpr int sectors = 24;
    constexpr float tau = 6.2831853f;
    const auto ring = [&](float inner, float outer, float half_depth) {
        for (int s = 0; s < sectors; ++s) {
            const float a = tau * static_cast<float>(s) / sectors;
            const float c = tau * static_cast<float>(s + 1) / sectors;
            const auto at = [&](float r, float angle, float z) {
                return glm::vec3{r * std::cos(angle), r * std::sin(angle), z};
            };
            const glm::vec3 mid{std::cos((a + c) * .5f), std::sin((a + c) * .5f), 0};
            b.quad(at(outer, a, -half_depth), at(outer, c, -half_depth),
                   at(outer, c, half_depth), at(outer, a, half_depth), mid);
            b.quad(at(inner, a, -half_depth), at(inner, c, -half_depth),
                   at(inner, c, half_depth), at(inner, a, half_depth), -mid);
            for (int face = 0; face < 2; ++face) {
                const float z = face ? half_depth : -half_depth;
                const glm::vec3 n{0, 0, face ? 1.f : -1.f};
                b.quad(at(inner, a, z), at(outer, a, z), at(outer, c, z), at(inner, c, z), n);
            }
        }
    };
    ring(.40f, .50f, .055f);   // tyre
    ring(.47f, .50f, .085f);   // flange, proud on one running face
    ring(0.f, .09f, .075f);    // hub
    for (int spoke = 0; spoke < 12; ++spoke) {
        const float a = tau * static_cast<float>(spoke) / 12;
        const glm::vec3 radial{std::cos(a), std::sin(a), 0};
        const glm::vec3 side{-std::sin(a), std::cos(a), 0};
        const glm::vec3 depth{0, 0, .038f};
        const glm::vec3 near_hub = radial * .085f, far_rim = radial * .41f;
        for (int face = 0; face < 2; ++face) {
            const glm::vec3 z = face ? depth : -depth;
            b.quad(near_hub - side * .030f + z, far_rim - side * .022f + z,
                   far_rim + side * .022f + z, near_hub + side * .030f + z,
                   {0, 0, face ? 1.f : -1.f});
        }
        for (int edge = 0; edge < 2; ++edge) {
            const float sign = edge ? 1.f : -1.f;
            b.quad(near_hub + side * sign * .030f - depth, far_rim + side * sign * .022f - depth,
                   far_rim + side * sign * .022f + depth, near_hub + side * sign * .030f + depth,
                   side * sign);
        }
    }
    return b.mesh;
}

// A sauropod skull: long tapering snout, a raised nasal arch, orbits cut as
// recesses and a hinged lower jaw. Built from named cross sections along the
// snout so the silhouette still reads from the far end of the hall, which a
// box never did.
inline MeshData make_loom_skull() {
    LoomMeshBuilder b;
    // x runs muzzle (-0.5) to occiput (+0.5). Each station is half width,
    // floor and roof of the cranium at that station.
    struct Station { float x, half_width, floor, roof; };
    const std::array<Station, 7> body{{
        {-.50f, .080f, -.12f, .02f},
        {-.34f, .105f, -.15f, .06f},
        {-.16f, .130f, -.17f, .20f},   // nasal arch
        {-.02f, .150f, -.19f, .12f},
        {.16f, .195f, -.20f, .22f},    // orbit
        {.34f, .205f, -.18f, .26f},
        {.50f, .150f, -.10f, .16f},
    }};
    for (std::size_t i = 0; i + 1 < body.size(); ++i) {
        const auto& a = body[i];
        const auto& c = body[i + 1];
        for (int side = 0; side < 2; ++side) {
            const float s = side ? 1.f : -1.f;
            b.quad({a.x, a.roof, s * a.half_width}, {c.x, c.roof, s * c.half_width},
                   {c.x, c.floor, s * c.half_width}, {a.x, a.floor, s * a.half_width}, {0, 0, s});
        }
        b.quad({a.x, a.roof, -a.half_width}, {c.x, c.roof, -c.half_width},
               {c.x, c.roof, c.half_width}, {a.x, a.roof, a.half_width}, {0, 1, 0});
        b.quad({a.x, a.floor, -a.half_width}, {c.x, c.floor, -c.half_width},
               {c.x, c.floor, c.half_width}, {a.x, a.floor, a.half_width}, {0, -1, 0});
    }
    b.quad({-.5f, body[0].roof, -body[0].half_width}, {-.5f, body[0].roof, body[0].half_width},
           {-.5f, body[0].floor, body[0].half_width}, {-.5f, body[0].floor, -body[0].half_width},
           {-1, 0, 0});
    b.quad({.5f, body[6].roof, -body[6].half_width}, {.5f, body[6].roof, body[6].half_width},
           {.5f, body[6].floor, body[6].half_width}, {.5f, body[6].floor, -body[6].half_width},
           {1, 0, 0});
    // Orbits and the nasal fenestra, sunk into the side walls as dark recesses.
    for (int side = 0; side < 2; ++side) {
        const float s = side ? 1.f : -1.f;
        for (const glm::vec3 hole : {glm::vec3{.18f, .07f, .075f}, glm::vec3{-.20f, .02f, .045f}}) {
            const float r = hole.z, depth = .055f;
            b.box({hole.x - r, hole.y - r, s > 0 ? .12f : -.12f - depth},
                  {hole.x + r, hole.y + r, s > 0 ? .12f + depth : -.12f});
        }
    }
    // Lower jaw, hung slightly open the way a mount is posed.
    b.box({-.47f, -.30f, -.075f}, {.34f, -.20f, .075f});
    b.box({.24f, -.30f, -.115f}, {.42f, -.06f, .115f});
    for (int tooth = 0; tooth < 9; ++tooth) {
        const float x = -.44f + static_cast<float>(tooth) * .062f;
        for (int side = 0; side < 2; ++side) {
            const float z = side ? .052f : -.078f;
            b.box({x, -.20f, z}, {x + .026f, -.15f, z + .026f});
            b.box({x, -.17f, z}, {x + .026f, -.12f, z + .026f});
        }
    }
    return b.mesh;
}

// A thin ring for the hall's armillary. Swept from a hexagonal tube, the same
// construction the amphora handles use, so it stays cheap and closed.
inline MeshData make_loom_ring() {
    LoomMeshBuilder b;
    constexpr int steps = 40, sides = 6;
    constexpr float tau = 6.2831853f, radius = .47f, thickness = .030f;
    const auto point = [&](int step, int ring) {
        const float t = tau * static_cast<float>(step % steps) / steps;
        const float a = tau * static_cast<float>(ring % sides) / sides;
        const glm::vec3 centre{radius * std::cos(t), radius * std::sin(t), 0};
        const glm::vec3 outward{std::cos(t), std::sin(t), 0};
        return centre + thickness * (outward * std::cos(a) + glm::vec3{0, 0, 1} * std::sin(a));
    };
    for (int step = 0; step < steps; ++step) for (int ring = 0; ring < sides; ++ring) {
        const float t = tau * (static_cast<float>(step) + .5f) / steps;
        const glm::vec3 centre{radius * std::cos(t), radius * std::sin(t), 0};
        const auto a = point(step, ring), c = point(step + 1, ring);
        const auto d = point(step + 1, ring + 1), e = point(step, ring + 1);
        b.quad(a, c, d, e, (a + c + d + e) * .25f - centre);
    }
    return b.mesh;
}
} // namespace apricot

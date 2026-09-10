#include "physics/ragdoll.h"

#include <algorithm>
#include <cmath>

#include "physics/breakaway_contact.h"
#include "physics/terrain_collider.h"

namespace apricot {
namespace {

constexpr int J(RagdollJoint j) { return static_cast<int>(j); }

// How far through the wrong side a joint may sit before anything is done about
// it at all. The strength of the correction is RagdollTuning::hinge_relaxation,
// scaled by speed; see the hinge block in ragdoll_step().
constexpr float kHingeTolerance = 0.01f;


glm::vec3 safe_normalize(glm::vec3 v, glm::vec3 fallback) {
    const float len = glm::length(v);
    return len > 1e-5f ? v / len : fallback;
}

// The figure's own forward, rebuilt every step from the pelvis and the chest.
// It has to be derived rather than stored: a body that has rolled twice has no
// relationship left to the heading it was walking on, and a bulge constraint
// measured against a stale axis puts the knees on backwards precisely when the
// body is tumbling, which is when anybody is looking.
glm::vec3 body_forward(const RagdollState& s) {
    const glm::vec3 across = s.position[J(RagdollJoint::HipR)] -
                             s.position[J(RagdollJoint::HipL)];
    const glm::vec3 up = s.position[J(RagdollJoint::Chest)] -
                         s.position[J(RagdollJoint::Hips)];
    return safe_normalize(glm::cross(across, up), {0.0f, 0.0f, -1.0f});
}

// One Gauss-Seidel sweep over links[first, end). Split out because the solver
// runs it twice per iteration with different starting points.
void solve_links(RagdollState& state, const RagdollFigure& figure,
                 std::size_t first) {
    for (std::size_t i = first; i < figure.links.size(); ++i) {
        const RagdollLink& link = figure.links[i];
        const std::size_t a = link.a;
        const std::size_t b = link.b;
        const float wa = figure.inv_mass[a];
        const float wb = figure.inv_mass[b];
        const float w = wa + wb;
        if (w <= 0.0f) continue;
        const glm::vec3 axis = state.position[b] - state.position[a];
        const float len = glm::length(axis);
        if (len < 1e-6f) continue;
        const float want = std::clamp(len, link.min_m, link.max_m);
        if (std::fabs(want - len) < 1e-7f) continue;
        const glm::vec3 push = axis * ((len - want) / len / w) * link.stiffness;
        state.position[a] += push * wa;
        state.position[b] -= push * wb;
    }
}

}  // namespace

const char* ragdoll_joint_name(RagdollJoint j) {
    switch (j) {
        case RagdollJoint::Hips:      return "hips";
        case RagdollJoint::Spine:     return "spine";
        case RagdollJoint::Chest:     return "chest";
        case RagdollJoint::Neck:      return "neck";
        case RagdollJoint::Head:      return "head";
        case RagdollJoint::HeadTip:   return "head_tip";
        case RagdollJoint::ClavicleL: return "clavicle_l";
        case RagdollJoint::ShoulderL: return "shoulder_l";
        case RagdollJoint::ElbowL:    return "elbow_l";
        case RagdollJoint::WristL:    return "wrist_l";
        case RagdollJoint::ClavicleR: return "clavicle_r";
        case RagdollJoint::ShoulderR: return "shoulder_r";
        case RagdollJoint::ElbowR:    return "elbow_r";
        case RagdollJoint::WristR:    return "wrist_r";
        case RagdollJoint::HipL:      return "hip_l";
        case RagdollJoint::KneeL:     return "knee_l";
        case RagdollJoint::AnkleL:    return "ankle_l";
        case RagdollJoint::ToeL:      return "toe_l";
        case RagdollJoint::HipR:      return "hip_r";
        case RagdollJoint::KneeR:     return "knee_r";
        case RagdollJoint::AnkleR:    return "ankle_r";
        case RagdollJoint::ToeR:      return "toe_r";
        case RagdollJoint::Count:     break;
    }
    return "?";
}

glm::vec3 RagdollState::centre() const {
    // The pelvis, not the average of every node. An average drifts toward
    // whichever end has more joints — the arms, which have six between them —
    // so a body with its arms flung out reports a centre that is not in it.
    return position[J(RagdollJoint::Hips)];
}

bool RagdollState::face_up() const {
    const glm::vec3 across = position[J(RagdollJoint::HipR)] -
                             position[J(RagdollJoint::HipL)];
    const glm::vec3 up = position[J(RagdollJoint::Chest)] -
                         position[J(RagdollJoint::Hips)];
    // Chest normal. Points out of the front of the torso; if it points at the
    // sky the body is on its back.
    return glm::cross(across, up).y < 0.0f;
}

void ragdoll_launch(RagdollState& state, const RagdollFigure& figure,
                    const std::array<glm::vec3, kRagdollJointCount>& world,
                    glm::vec3 velocity, glm::vec3 spin) {
    state.position = world;
    state.contact.fill(glm::vec3{0.0f});
    state.elapsed_s = 0.0f;
    state.still_s = 0.0f;
    state.active = figure.valid;

    // Rigid-body motion about the pelvis: everybody gets the launch, and the
    // spin adds whatever their offset from the centre earns them. That is what
    // makes an outflung arm move faster than a hip, and it is the difference
    // between a body that tumbles and one that slides along keeping its pose.
    const glm::vec3 centre = world[J(RagdollJoint::Hips)];
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        state.velocity[k] = velocity + glm::cross(spin, world[k] - centre);
    }
}

void ragdoll_step(RagdollState& state, const RagdollFigure& figure,
                  const RagdollTuning& tuning, const TerrainCollider* world,
                  const RagdollVehicle* car, float dt) {
    if (!state.active || !figure.valid || dt <= 0.0f) return;

    state.elapsed_s += dt;

    // The velocity of whatever each node is resting ON. Zero for the ground;
    // the car's own velocity for a body still draped over a bonnet.
    std::array<glm::vec3, kRagdollJointCount> surface_velocity{};

    // --- contact planes, ONCE per step ------------------------------------
    //
    // Probed here and reused by every substep and iteration below.
    // probe_down() marches the height field and tests every prop box, so at
    // twenty nodes this is the single most expensive thing a ragdoll does; per
    // substep would be four times it and per iteration sixteen.
    //
    // Per substep WAS tried, on the theory that a node at the speed cap covers
    // 0.22 m in a step and a plane that far behind would be wrong on a slope.
    // It changed the worst bone stretch by nothing at all — 0.01558 m either
    // way — so the coarse version is the honest one to keep. This is a cache
    // of a pure function over an interval it is near enough constant on, not a
    // cache of state.
    std::array<glm::vec3, kRagdollJointCount> plane_point{};
    std::array<glm::vec3, kRagdollJointCount> plane_normal{};
    std::array<float, kRagdollJointCount> plane_grip{};
    if (world != nullptr) {
        for (int i = 0; i < kRagdollJointCount; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            if (figure.inv_mass[k] <= 0.0f) continue;
            const auto ground = world->probe_down(
                state.position[k] + glm::vec3{0.0f, tuning.probe_lift_m, 0.0f},
                tuning.probe_reach_m);
            if (!ground.hit) continue;
            plane_point[k] = ground.point;
            plane_normal[k] = ground.normal;
            plane_grip[k] = ground.grip;
        }
    }

    // How hard the hinge limits pull this step. Decided ONCE, off the speed the
    // step starts at, so it cannot flicker between substeps and put a joint
    // into a shorter version of the same argument.
    // Which joints are already lying on something.
    //
    // A hinge is an anatomical limit and the ground is a fact, so where the
    // two disagree the ground wins and the limit steps aside. Without this a
    // knee resting on the pavement is lifted to the correct side every
    // substep and dropped again by gravity — sixteen corrections a frame, each
    // one real work against gravity, which is the flopping.
    std::array<bool, kRagdollJointCount> resting{};
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (plane_grip[k] <= 0.0f) continue;
        resting[k] = glm::dot(state.position[k] - plane_point[k],
                              plane_normal[k]) -
                         figure.radius_m[k] <
                     tuning.resting_clearance_m;
    }

    // The limits are enforced for a bounded time, then the body keeps the
    // pose it landed in. See RagdollTuning::hinge_hold_s.
    const bool enforce_hinges = state.elapsed_s <= tuning.hinge_hold_s;

    // --- substep -----------------------------------------------------------
    const int substeps = std::max(1, tuning.substeps);
    const float h = dt / static_cast<float>(substeps);
    const float retain = std::pow(std::max(0.0f, 1.0f - tuning.linear_damping),
                                  h);
    std::array<glm::vec3, kRagdollJointCount> before{};

    for (int sub = 0; sub < substeps; ++sub) {
    // Integrate. Gravity and damping, then a speed clamp: a car doing 25 m/s
    // through a hip is a legitimate launch, but a constraint projection that
    // disagrees with the ground on the same substep is not, and without a cap
    // the two can trade a point back and forth into the sky.
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (figure.inv_mass[k] <= 0.0f) continue;
        glm::vec3 v = state.velocity[k] * retain;
        v.y -= tuning.gravity_mps2 * h;
        const float speed = glm::length(v);
        if (speed > tuning.max_speed_mps) v *= tuning.max_speed_mps / speed;
        state.velocity[k] = v;
        before[k] = state.position[k];
        state.position[k] += v * h;
        state.contact[k] = glm::vec3{0.0f};
        // Cleared with the contact it belongs to. Every path that writes one
        // writes the other, so a stale pair cannot be read today — but they
        // are one fact and separating their lifetimes is how that stops being
        // true.
        surface_velocity[k] = glm::vec3{0.0f};
    }

    // --- project -----------------------------------------------------------
    // A FIXED count, in array order, every substep. See the header.
    for (int it = 0; it < tuning.iterations; ++it) {
        // ORDER MATTERS, AND THIS IS THE ORDER: hinges, contacts, lengths.
        //
        // Gauss-Seidel gives whatever runs last the final say, so this is a
        // ranking of which constraint may not be violated, weakest first.
        //
        // Bone length is last because it is the only one whose violation is
        // unmistakable: a centimetre of pavement showing through a shoulder is
        // invisible, a limb that grew eight centimetres is a rubber arm.
        // Running contacts last cost exactly that, measured — 0.079 m of
        // stretch at a 40 m/s launch — because the ground pushed a node out
        // and nothing came after to put the bone back.
        //
        // Hinges are FIRST, and that is the second half of the same lesson.
        // Solved after contacts they win against the ground, and a body lying
        // face-down has its knees told to bend downward into the pavement
        // while the pavement pushes them back: the two fight forever, the
        // corpse never goes still, and it ends up propped half a metre in the
        // air on a leg that is being levered into the ground. Measured, that
        // was a pelvis resting at 0.640 m and every single body timing out at
        // max_active_s instead of settling. A hinge is a joint LIMIT, and a
        // limit is the thing that gives when a body is jammed against the
        // world.
        // Hinges. The joint is SWUNG AROUND the limb's own axis to the
        // correct side, never pushed toward it.
        //
        // Pushing along the body's forward vector is the obvious version and
        // it is wrong twice over. It fights the bone lengths, because the knee
        // is pinned to a circle around the hip-to-ankle axis and a push off
        // that circle is just work for the link pass to undo. And it goes
        // fully degenerate when the limb happens to lie along the body's
        // forward axis — a body face-down with its legs out — where the push
        // has no component around the circle at all and the constraint simply
        // stops working. Measured, that left knees a full 0.129 m through the
        // wrong side, which is a leg bent backwards and is exactly the
        // artefact this constraint exists to prevent.
        //
        // Rotating about the axis stays on the circle, so both bones keep
        // their length exactly and the link pass has nothing to clean up.
        const glm::vec3 forward = body_forward(state);
        const std::size_t hinge_count =
            enforce_hinges ? figure.bulges.size() : 0u;
        for (std::size_t hi = 0; hi < hinge_count; ++hi) {
            const RagdollBulge& hinge = figure.bulges[hi];
            const std::size_t j = hinge.joint;
            if (hinge.a == hinge.b) continue;
            if (figure.inv_mass[j] <= 0.0f) continue;
            // The floor outranks anatomy. See resting_clearance_m.
            if (resting[j]) continue;

            const glm::vec3 root = state.position[hinge.a];
            glm::vec3 axis = state.position[hinge.b] - root;
            const float span = glm::length(axis);
            if (span < 1e-5f) continue;
            axis /= span;

            // The joint's offset from the limb axis: the radius of the circle
            // it is free to swing on.
            const glm::vec3 centre =
                root + axis * glm::dot(state.position[j] - root, axis);
            const glm::vec3 radial = state.position[j] - centre;
            const float radius = glm::length(radial);
            // A limb held straight has no circle to swing on, and no visible
            // bend to get wrong either.
            if (radius < 1e-4f) continue;

            // Which way "bent correctly" points, with the part along the limb
            // removed — that part is not a direction the joint can move in.
            glm::vec3 want = forward * hinge.sign;
            want -= axis * glm::dot(want, axis);
            const float want_len = glm::length(want);
            if (want_len < 1e-4f) continue;  // limb along the body axis
            want /= want_len;

            // How far through the wrong side, in metres. A body at rest
            // settles with its knees a few millimetres off, and correcting
            // that forever is what stops it ever going still.
            const float depth = -glm::dot(radial, want);
            if (depth <= kHingeTolerance) continue;

            const glm::vec3 turned = glm::normalize(
                glm::mix(radial / radius, want, tuning.hinge_relaxation));
            const glm::vec3 move =
                (centre + turned * radius) - state.position[j];
            state.position[j] += move;

            // ... AND THE SAME MOVE TO THE SUBSTEP'S STARTING POSITION, so the
            // velocity recovered at the end of the substep does not see it.
            //
            // Without this line the constraint is an energy pump, and not a
            // subtle one: velocity comes out of (position - before) / h, h is
            // two milliseconds, and swinging a knee a fifth of a metre around
            // its circle therefore reads as ninety metres per second. Measured,
            // bodies were still doing twenty-two metres per second after nine
            // seconds on the ground and never settled at all, while the same
            // scene with the hinges removed came to rest in 1.5 s every time.
            //
            // A bone stopping a limb IS an impulse and must reach the velocity;
            // an anatomical limit tidying up which way a knee faces is not.
            before[j] += move;
        }

        // Contacts, after the hinges and before the lengths.
        for (int i = 0; i < kRagdollJointCount; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            if (figure.inv_mass[k] <= 0.0f) continue;
            if (plane_grip[k] <= 0.0f) continue;
            const float clearance =
                glm::dot(state.position[k] - plane_point[k], plane_normal[k]) -
                figure.radius_m[k];
            if (clearance >= 0.0f) continue;
            state.position[k] -= plane_normal[k] * clearance;
            state.contact[k] = plane_normal[k] * plane_grip[k];
            surface_velocity[k] = glm::vec3{0.0f};
        }

        if (car != nullptr) {
            for (int i = 0; i < kRagdollJointCount; ++i) {
                const std::size_t k = static_cast<std::size_t>(i);
                if (figure.inv_mass[k] <= 0.0f) continue;
                if (std::fabs(state.position[k].y - car->position.y) >
                    car->height_m)
                    continue;
                const BreakawayContact hit = breakaway_contact(
                    car->position, car->orientation, car->half_extents,
                    state.position[k], figure.radius_m[k]);
                if (!hit.hit) continue;
                // The normal points from the contact INTO the car — that is
                // the convention the pole guard is pinned on — so leaving the
                // footprint means moving against it.
                state.position[k] -= hit.normal * hit.penetration;
                state.contact[k] = -hit.normal;
                // Carried. A body on a bonnet moves with the bonnet; without
                // this the car drives out from under it and it drops on the
                // spot, which is the tell that nothing is really touching.
                surface_velocity[k] = car->velocity;
            }
        }

        solve_links(state, figure, 0);

        // ... and the rigid tail again. The pelvis is an over-constrained
        // triangle inside a web of torso braces, and one sweep leaves its
        // shortest bones out by 8% of their length at a hard launch — small in
        // metres, but it is the hip, and a hip that changes width is a walk
        // cycle that never lines up again. A second sweep of nothing but the
        // bones takes that to 3%, for twenty-one extra constraint solves.
        solve_links(state, figure, figure.rigid_first);
    }

    // Velocity from the positions the solve actually produced, so a
    // constraint that moved a point IS a change in that point's speed. This
    // is the whole reason the projection above never touches velocity.
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (figure.inv_mass[k] <= 0.0f) continue;
        state.velocity[k] = (state.position[k] - before[k]) / h;
    }

    // --- friction ----------------------------------------------------------
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        const glm::vec3 contact = state.contact[k];
        const float grip = glm::length(contact);
        if (grip <= 1e-5f) continue;
        const glm::vec3 normal = contact / grip;
        // Against the surface's OWN motion, which is zero for the ground and
        // the car's velocity for a body draped over a bonnet. One expression
        // covers both, and a body on a moving car is carried rather than
        // scrubbed to a halt against it.
        const glm::vec3 relative = state.velocity[k] - surface_velocity[k];
        const float along = glm::dot(relative, normal);
        const glm::vec3 tangent = relative - normal * along;
        const float keep = std::clamp(
            1.0f - tuning.ground_friction * grip, 0.0f, 1.0f);
        // Bounce comes off the normal component only, and only while the node
        // is still moving INTO the surface.
        const float bounce = along < 0.0f ? -along * tuning.restitution : along;
        state.velocity[k] =
            tangent * keep + normal * bounce + surface_velocity[k];
    }
    }  // substep

    // --- settling ----------------------------------------------------------
    float fastest = 0.0f;
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        fastest = std::max(fastest, glm::length(state.velocity[k]));
    }
    const float still = tuning.still_speed_mps *
                        std::max(0.25f, figure.stature_m);
    if (fastest <= still) {
        state.still_s += dt;
    } else {
        state.still_s = 0.0f;
    }
}

void ragdoll_anchor_xz(RagdollState& state, glm::vec2 target_xz, float blend) {
    if (!state.active) return;
    const glm::vec3 hips = state.position[J(RagdollJoint::Hips)];
    const glm::vec3 shift{(target_xz.x - hips.x) * blend, 0.0f,
                          (target_xz.y - hips.z) * blend};
    // Every node by the same vector, so no length, no limit and no hinge sees
    // any change at all — the figure is moved, not deformed.
    for (int i = 0; i < kRagdollJointCount; ++i)
        state.position[static_cast<std::size_t>(i)] += shift;
}

bool ragdoll_settled(const RagdollState& state, const RagdollTuning& tuning) {
    if (!state.active) return true;
    return state.still_s >= tuning.still_hold_s ||
           state.elapsed_s >= tuning.max_active_s;
}

}  // namespace apricot

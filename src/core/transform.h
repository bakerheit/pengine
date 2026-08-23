#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace apricot {

// Position / rotation / scale, in that composition order (T * R * S).
//
// Rotation is a quaternion, not Euler angles. A rally car pitches, rolls and
// yaws simultaneously on every jump; Euler storage gimbal-locks on the exact
// manoeuvre the game is about. Euler input is available as a setter for
// authoring and debug UI, but it never round-trips back out.
struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4{1.0f}, position);
        m *= glm::mat4_cast(rotation);
        return glm::scale(m, scale);
    }

    void set_euler_deg(float pitch, float yaw, float roll) {
        rotation = glm::quat(glm::vec3{glm::radians(pitch), glm::radians(yaw),
                                       glm::radians(roll)});
    }

    // Basis vectors. -Z is forward, matching the GL convention the camera and
    // projection maths already use; mixing that up puts the car in reverse.
    glm::vec3 forward() const { return rotation * glm::vec3{0.0f, 0.0f, -1.0f}; }
    glm::vec3 right() const   { return rotation * glm::vec3{1.0f, 0.0f, 0.0f}; }
    glm::vec3 up() const      { return rotation * glm::vec3{0.0f, 1.0f, 0.0f}; }

    // Apply this transform to a point: scaled, then rotated, then translated —
    // the same T * R * S order matrix() builds, spelled out so it is obvious
    // that it is the same order.
    glm::vec3 transform_point(const glm::vec3& p) const {
        return position + (rotation * (scale * p));
    }

    // A direction, so the translation is skipped. Scale still applies, which
    // means this does NOT preserve length under non-uniform scale and is the
    // wrong function for a normal — normals need the inverse transpose.
    glm::vec3 transform_direction(const glm::vec3& v) const {
        return rotation * (scale * v);
    }

    // Rotation only. The right one for an axis, a velocity you want rotated
    // but not rescaled, or a unit basis vector.
    glm::vec3 rotate(const glm::vec3& v) const { return rotation * v; }

    // The inverse transform. EXACT for uniform scale; an approximation
    // otherwise, because inverting a non-uniform scale through a rotation
    // introduces shear and a position/rotation/scale triple has nowhere to put
    // shear. When the scale is non-uniform and the answer has to be right, use
    // glm::inverse(t.matrix()) and stay in matrix space.
    Transform inverse() const {
        Transform out;
        out.scale = 1.0f / scale;
        out.rotation = glm::conjugate(rotation);
        out.position = out.rotation * (out.scale * -position);
        return out;
    }
};

// COMPOSITION. `parent` is the outer transform, `child` the inner one, and the
// result is equivalent to parent.matrix() * child.matrix().
//
// GET THIS ORDER RIGHT. Reversed composition is the single most expensive
// cheap bug in a scene graph, because it is not obviously wrong on screen: with
// a parent at the origin it is exactly right, with a small parent rotation it
// is slightly off, and only once something is far from the origin AND rotated
// does the error become large enough to see. By then the wrong order is load-
// bearing in three other systems. tests/math_tests.cpp pins it against the
// matrix product, in both directions, with a deliberately asymmetric pair so a
// reversal cannot pass by accident.
//
// Read it as "child expressed in parent's space":
//
//     world = combine(parent_world, child_local);
//
// EXACT for uniform parent scale. Under NON-UNIFORM parent scale combined with
// a child rotation the true product contains shear, which no
// position/rotation/scale triple can represent, and this returns the closest
// shear-free transform. That is not a bug in the composition; it is a fact
// about TRS. If you need the sheared result, compose the matrices.
inline Transform combine(const Transform& parent, const Transform& child) {
    Transform out;
    // The child's offset is scaled by the parent, then rotated by it, then
    // moved to the parent's position — the parent's own T * R * S applied to
    // the child's origin. Any other grouping is one of the wrong orders.
    out.position = parent.transform_point(child.position);
    out.rotation = parent.rotation * child.rotation;
    out.scale = parent.scale * child.scale;
    return out;
}

// Spelled `parent * child`, matching the matrix product it stands for so the
// two forms cannot drift apart in a reader's head.
inline Transform operator*(const Transform& parent, const Transform& child) {
    return combine(parent, child);
}

// The child transform that, composed under `parent`, reproduces `world`.
// Same uniform-scale caveat as inverse().
inline Transform relative_to(const Transform& world, const Transform& parent) {
    return combine(parent.inverse(), world);
}

}  // namespace apricot

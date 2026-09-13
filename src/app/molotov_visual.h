#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "app/fire_sprite_sheet.h"
#include "game/fire.h"
#include "game/molotov.h"
#include "scene/scene.h"

namespace apricot {
class Renderer;

// The molotov's geometry: the bottle in the hand, the bottle in the air, and
// the fire it leaves behind. Presentation only — every decision about where
// the fire IS was already made in game/fire.h, and this file does not get to
// disagree with it.
//
// Split from WeaponVisual rather than added to it because the pistol's parts
// are all rigidly parented to one palm socket, and two of these three things
// are not parented to anything: a thrown bottle is in world space, and a fire
// stays where it was lit after the player has walked away from it.

// The sub-rectangle of the flame atlas one frame occupies.
//
// THE SHEET IS FLIPPED ON LOAD. gfx/texture.h sets stbi's vertical flip — it
// is the UV convention the imported model paint was authored in and it is not
// negotiable for one texture — so file row 0 ends up at the TOP of UV space.
// The flame's base is at the bottom of its cell IN THE FILE, which is the LOW
// v edge after the flip. Getting this backwards draws the fire upside down,
// which at a glance reads as "the sprite sheet is broken".
struct FireSpriteUv {
    glm::vec2 base{0.0f};  // u at the left edge, v at the flame's foot
    glm::vec2 tip{1.0f};   // u at the right edge, v at the flame's top
};

inline FireSpriteUv fire_sprite_uv(std::size_t frame) {
    const std::size_t wrapped = kFireSheetFrames ? frame % kFireSheetFrames : 0;
    // Split out so the row is plainly an integer cell index rather than a
    // division that happens to be used as a float.
    const std::size_t column_index = wrapped % kFireSheetColumns;
    const std::size_t row_index = wrapped / kFireSheetColumns;
    const auto column = static_cast<float>(column_index);
    const auto row = static_cast<float>(row_index);
    const auto columns = static_cast<float>(kFireSheetColumns);
    const auto rows = static_cast<float>(kFireSheetRows);
    FireSpriteUv uv;
    uv.base = {(column + kFireSheetInset) / columns,
               1.0f - (row + 1.0f - kFireSheetInset) / rows};
    uv.tip = {(column + 1.0f - kFireSheetInset) / columns,
              1.0f - (row + kFireSheetInset) / rows};
    return uv;
}

// The flame atlas and the quads that address it, owned once and shared by
// everything that draws fire.
//
// ONE QUAD MESH PER FRAME, with that frame's UVs baked in. The instance format
// carries a per-instance uv_SCALE and no uv_OFFSET, so a sprite atlas cannot be
// addressed per instance without widening a three-way contract across
// gfx/instance.h, gfx/mesh.cpp and the vertex shader — a change that reaches
// every draw in the engine for one effect. A hundred and thirty-two four-vertex
// meshes cost a few kilobytes and no shader change at all. The price is that
// two flames on different frames land in different batches; a whole burning
// street is still under two hundred draws sharing one texture.
class FireSprites {
public:
    bool init(Renderer& renderer);
    void destroy();

    bool valid() const { return material_ != kInvalidId && !frames_.empty(); }
    MaterialId material() const { return material_; }
    std::size_t frame_count() const { return frames_.size(); }
    // Wraps, so a caller may hand in a running frame counter without owning
    // the modulo. An uninitialised atlas returns kInvalidId and the caller
    // draws nothing, which is what a missing texture should look like.
    MeshId frame(std::size_t index) const {
        return frames_.empty() ? kInvalidId : frames_[index % frames_.size()];
    }

private:
    Renderer* renderer_ = nullptr;
    MaterialId material_ = kInvalidId;
    std::vector<MeshId> frames_;
};

class MolotovVisual {
public:
    bool init(Renderer& renderer, Scene& scene, const FireSprites& sprites);
    void destroy(Scene& scene);

    // The bottle in the player's hand. `hand_world` is the same rigid palm
    // socket WeaponVisual uses; null (or any weapon but the molotov, or an
    // empty hand between throws) hides every piece. `eye` billboards the lit
    // rag, which is the one part of the bottle that is a sprite.
    void sync_held(Scene& scene, WeaponId weapon, const glm::mat4* hand_world,
                   const MolotovUseState& use, glm::vec3 eye);

    // Bottles in flight, in world space.
    void sync_projectiles(Scene& scene, const MolotovProjectiles& shots,
                          glm::vec3 eye);

    // Where the held bottle actually is in the world, so the throw leaves from
    // the hand rather than from the player's navel. False when nothing is held.
    bool release_point(glm::vec3& out) const;

    std::size_t visible_projectile_count() const { return visible_projectiles_; }

private:
    struct Bottle {
        std::vector<NodeId> parts;   // glass, fuel, rag — rigid, parented
        std::vector<Transform> local;
        NodeId flame = kInvalidId;   // the lit rag, billboarded in world space
    };
    void build_bottle(Scene& scene, Bottle& bottle);
    void place_flame(Scene& scene, NodeId node, glm::vec3 world, glm::vec3 eye,
                     float size, float heat, std::size_t frame);

    Renderer* renderer_ = nullptr;
    const FireSprites* sprites_ = nullptr;
    MeshId bottle_mesh_ = kInvalidId;
    MeshId cube_mesh_ = kInvalidId;
    Bottle held_;
    std::vector<Bottle> flying_;
    Transform hand_;
    bool held_visible_ = false;
    std::size_t visible_projectiles_ = 0;
};

// Burning ground. One pool of billboards, re-pointed at whichever cells of the
// FireField are alight this frame; nothing is created or destroyed while a
// fire burns.
class FireVisual {
public:
    // Two cards per burning cell, on different frames of the atlas and at
    // different sizes. One card is a poster and reads as one the moment the
    // player walks round it; two at different depths and phases give a cell
    // enough parallax to pass for a volume. Three did not look better.
    static constexpr std::size_t kCardsPerCell = 2;

    // Atlas frames a second. The supplied sheet is 132 frames of one
    // continuous burn, so at this rate the whole take is five and a half
    // seconds long — comfortably longer than a cell lives, which means most
    // cells never visibly loop at all.
    static constexpr float kFramesPerSecond = 24.0f;

    bool init(Renderer& renderer, Scene& scene, const FireSprites& sprites);
    void destroy(Scene& scene);
    void sync(Scene& scene, const FireField& fire, glm::vec3 eye);

    std::size_t visible_card_count() const { return visible_cards_; }

private:
    Renderer* renderer_ = nullptr;
    const FireSprites* sprites_ = nullptr;
    std::vector<NodeId> cards_;
    std::size_t visible_cards_ = 0;
};

}  // namespace apricot

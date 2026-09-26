#include "app/molotov_visual.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include <glm/gtc/quaternion.hpp>

#include "app/molotov_bottle_mesh.h"
#include "core/asset_root.h"
#include "core/rng.h"
#include "gfx/primitives.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

// The cooked bottle keeps the Quequis kitchen's metre scale, which is a litre
// of soda. A molotov is a beer bottle: it has to fit a fist and it has to
// leave one without looking like the player threw a watering can.
constexpr float kMolotovBottleHeightM = 0.27f;
constexpr float kBottleScale = kMolotovBottleHeightM / kMolotovBottleSourceHeightM;
constexpr float kBottleRadiusM = kMolotovBottleSourceRadiusM * kBottleScale;

// Bottle green, and dark. The glass is opaque geometry here rather than the
// engine's transparent material: a thrown bottle is seen for a second against
// sky or tarmac and never against anything worth seeing through, and the glass
// pass draws after opaque characters, which is a sorting cost for nothing.
constexpr glm::vec4 kGlass{.09f, .20f, .11f, 1.f};
// Fuel sitting in the bottom half, seen through the glass, so it is dark
// amber rather than the colour petrol is in a jar. One extra box, and it is
// what makes the silhouette read as full rather than as an empty bottle.
constexpr glm::vec4 kFuel{.34f, .21f, .05f, 1.f};
constexpr glm::vec4 kRag{.74f, .70f, .58f, 1.f};

// How big a flame card is, in metres, at full heat. The atlas leaves a clear
// margin around the flame, so the CARD is meaningfully bigger than the fire
// the player sees inside it — a card sized to the cell draws a fire two thirds
// of a cell wide with a gap between it and the next one.
constexpr float kCellCardWidthM = 1.75f;
constexpr float kCellCardHeightM = 2.05f;

// The lit rag: the same atlas at a hundredth of the size.
constexpr float kRagFlameM = 0.17f;

// And the same rag once the bottle is in the air, where it is the ONLY thing
// the player can see. A dark green bottle a quarter of a metre long, thrown
// across a road at dawn, is about forty pixels of the same colour as the road;
// the flame streaming off it is what says a molotov is in flight, so in the
// air it is nearly twice the size it is in the hand.
constexpr float kFlightFlameM = 0.32f;

// Where the rag burns, in bottle-local metres. BELOW the lip of the neck, not
// level with it: the atlas leaves a clear margin under every flame, so a card
// anchored at the top of the glass draws its fire floating a few centimetres
// clear of the bottle it is supposed to be coming out of.
constexpr glm::vec3 kRagLocal{0.f, kMolotovBottleHeightM * 0.86f, 0.f};

float unit_roll(uint64_t bits) {
    return static_cast<float>(bits >> 40) / 16777215.f;
}

// Where the bottle sits relative to the palm socket. ONE definition, because
// both the drawn bottle and the point the throw launches from have to agree:
// two copies of this pose is how a bottle leaves the hand from a spot the
// player can see it is not in.
//
// Palm space: +Y is up out of the fist and -Z is forward, the same basis
// WeaponVisual's pistol is built in. The bottle stands in the hand with its
// base below the palm centre, tipped forward so the lit rag is clear of the
// knuckles rather than inside them.
Transform molotov_grip() {
    Transform grip;
    grip.position = {0.f, -.075f, .012f};
    grip.set_euler_deg(-14.f, 0.f, 0.f);
    return grip;
}

// Turn a world position and the eye into the yaw that points a card's +Z at
// the camera. YAW ONLY: a fire stands up, and a card that pitched to face a
// camera looking down from a rooftop would lie over on the road.
glm::quat billboard_yaw(glm::vec3 world, glm::vec3 eye) {
    const glm::vec3 to_eye = eye - world;
    const float reach = std::sqrt(to_eye.x * to_eye.x + to_eye.z * to_eye.z);
    // Directly overhead: any yaw is as good as any other, and atan2(0, 0) is
    // not a number worth arguing with.
    if (!(reach > 1e-4f)) return glm::quat{1.f, 0.f, 0.f, 0.f};
    return glm::angleAxis(std::atan2(to_eye.x, to_eye.z), glm::vec3{0.f, 1.f, 0.f});
}

// One frame of the atlas as a quad: one metre wide, one metre tall, standing
// on its own origin so scaling it grows the flame UPWARD out of the ground
// rather than sinking half of it into the road.
//
// DOUBLE SIDED. The alpha pass inherits back-face culling from the opaque
// setup, and a billboard whose yaw comes out a half turn wrong is then simply
// invisible — a bug that looks like the texture failing to load. Two extra
// triangles per frame buys immunity to the whole class, and the mirrored
// second card below needs the back faces anyway.
MeshData make_fire_card(std::size_t frame) {
    const FireSpriteUv uv = fire_sprite_uv(frame);
    constexpr glm::vec4 solid{1.f, 0.f, 0.f, 0.f};
    const glm::vec3 corner[4] = {
        {-.5f, 0.f, 0.f}, {.5f, 0.f, 0.f}, {.5f, 1.f, 0.f}, {-.5f, 1.f, 0.f}};
    const glm::vec2 texel[4] = {
        uv.base, {uv.tip.x, uv.base.y}, uv.tip, {uv.base.x, uv.tip.y}};
    MeshData mesh;
    mesh.vertices.reserve(8);
    mesh.indices.reserve(12);
    for (int side = 0; side < 2; ++side) {
        const glm::vec3 normal{0.f, 0.f, side == 0 ? 1.f : -1.f};
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back({corner[i], normal, texel[i], solid});
            mesh.bounds.expand(corner[i]);
        }
        if (side == 0)
            mesh.indices.insert(mesh.indices.end(),
                {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
        else
            mesh.indices.insert(mesh.indices.end(),
                {base, base + 2u, base + 1u, base, base + 3u, base + 2u});
    }
    return mesh;
}

}  // namespace

bool FireSprites::init(Renderer& renderer) {
    destroy();
    renderer_ = &renderer;
    Texture atlas;
    if (!atlas.load_file(asset_path(kFireSheetAsset))) return false;
    // Alpha blended, no specular and no snow. Snow settling on a flame would
    // be funny once; the lit shader skips it for emissive tints anyway, and
    // saying so here means the material does not depend on that holding.
    material_ = renderer.add_material(std::move(atlas), /*alpha_blended=*/true,
                                      /*specular_scale=*/0.f, Renderer::DepthBias(),
                                      /*receives_snow=*/false);
    if (material_ == kInvalidId) return false;
    frames_.reserve(kFireSheetFrames);
    for (std::size_t i = 0; i < kFireSheetFrames; ++i) {
        const MeshId mesh = renderer.add_mesh(make_fire_card(i));
        if (mesh == kInvalidId) { destroy(); return false; }
        frames_.push_back(mesh);
    }
    return true;
}

void FireSprites::destroy() {
    if (renderer_)
        for (const MeshId mesh : frames_) renderer_->remove_mesh(mesh);
    frames_.clear();
    // Materials are append-only by design (see gfx/renderer.h); the atlas is
    // created once at startup and outlives everything that samples it.
    material_ = kInvalidId;
    renderer_ = nullptr;
}

// ---------------------------------------------------------------------------

bool MolotovVisual::init(Renderer& renderer, Scene& scene, const FireSprites& sprites) {
    destroy(scene);
    renderer_ = &renderer;
    sprites_ = &sprites;
    bottle_mesh_ = renderer.add_mesh(make_molotov_bottle());
    cube_mesh_ = renderer.add_mesh(make_box(glm::vec3{.5f}));
    if (bottle_mesh_ == kInvalidId || cube_mesh_ == kInvalidId) return false;
    build_bottle(scene, held_);
    flying_.resize(MolotovProjectiles::kCapacity);
    for (auto& bottle : flying_) build_bottle(scene, bottle);
    return true;
}

void MolotovVisual::build_bottle(Scene& scene, Bottle& bottle) {
    // Rebuilt per copy rather than cached: this runs five times at startup and
    // never again, and the bounds are the only part actually wanted here.
    const auto glass = make_molotov_bottle();
    const auto cube = make_box(glm::vec3{.5f});
    constexpr float height = kMolotovBottleHeightM;
    constexpr float radius = kBottleRadiusM;
    const auto add = [&](MeshId mesh, const AABB& bounds, glm::vec3 centre,
                         glm::vec3 size, glm::vec4 tint) {
        Transform local;
        local.position = centre;
        local.scale = size;
        Renderable renderable;
        renderable.mesh = mesh;
        renderable.material = renderer_->white_material();
        renderable.tint = tint;
        const NodeId node = scene.create(renderable, local, bounds);
        if (auto* value = scene.get(node)) value->visible = false;
        bottle.parts.push_back(node);
        bottle.local.push_back(local);
    };
    // The glass itself, base at the part origin so every offset below is
    // measured up the bottle from where it stands.
    add(bottle_mesh_, glass.bounds, {0, 0, 0}, glm::vec3{kBottleScale}, kGlass);
    // Fuel. THE BOX IS SQUARE AND THE BOTTLE IS ROUND, so the width is set by
    // the box's DIAGONAL against the bottle's narrowest point — its waist —
    // and not by its side against the widest. Sized off the body radius the
    // corners stand proud of the glass and the bottle grows four yellow fins.
    add(cube_mesh_, cube.bounds, {0, height * .28f, 0},
        {radius * 1.18f, height * .46f, radius * 1.18f}, kFuel);
    // The rag, stuffed in the neck and hanging over one side.
    add(cube_mesh_, cube.bounds, {radius * .18f, height * .99f, 0},
        {radius * .60f, height * .12f, radius * .42f}, kRag);

    if (!sprites_ || !sprites_->valid()) return;
    Renderable flame;
    flame.mesh = sprites_->frame(0);
    flame.material = sprites_->material();
    bottle.flame = scene.create(flame, Transform{}, make_fire_card(0).bounds);
    if (auto* value = scene.get(bottle.flame)) value->visible = false;
}

void MolotovVisual::place_flame(Scene& scene, NodeId node, glm::vec3 world,
                                glm::vec3 eye, float size, float heat,
                                std::size_t frame) {
    auto* value = scene.get(node);
    if (!value || !sprites_ || !sprites_->valid()) return;
    value->visible = true;
    value->renderable.mesh = sprites_->frame(frame);
    // tint.a below 1 fades the card out; above 1 it is the lit shader's
    // emissive boost and the card draws at full opacity. ONE number spans
    // both, which is what lets a flame brighten as it takes hold and go
    // translucent as it dies without a second control for either.
    value->renderable.tint = {1.f, .96f, .90f,
                              .55f + 1.75f * std::clamp(heat, 0.f, 1.f)};
    Transform card;
    card.position = world;
    card.rotation = billboard_yaw(world, eye);
    card.scale = {size, size, size};
    scene.set_transform(node, card);
}

void MolotovVisual::sync_held(Scene& scene, WeaponId weapon,
                              const glm::mat4* hand_world,
                              const MolotovUseState& use, glm::vec3 eye) {
    // A bottle that is still being drawn, or that has just left the hand, is
    // not in the hand. `armed()` is the sim's own answer to that question; the
    // presentation must not invent a second one.
    held_visible_ = weapon == WeaponId::Molotov && hand_world &&
                    use.equip_blend > .02f && use.armed();
    if (held_visible_) {
        hand_.position = glm::vec3{(*hand_world)[3]};
        hand_.rotation = glm::normalize(glm::quat_cast(glm::mat3{*hand_world}));
    }
    Transform grip = molotov_grip();
    // Coming out of the draw, the bottle rises into the grip instead of
    // appearing there. equip_blend is the sim's draw clock, so this cannot
    // drift from the animation that plays alongside it.
    const float draw = std::clamp(use.equip_blend, 0.f, 1.f);
    grip.position.y -= (1.f - draw) * .22f;
    const Transform bottle = hand_ * grip;
    for (std::size_t i = 0; i < held_.parts.size(); ++i) {
        if (auto* node = scene.get(held_.parts[i])) node->visible = held_visible_;
        if (!held_visible_) continue;
        scene.set_transform(held_.parts[i], bottle * held_.local[i]);
    }
    if (held_.flame == kInvalidId) return;
    if (!held_visible_) {
        if (auto* node = scene.get(held_.flame)) node->visible = false;
        return;
    }
    // The rag's frame counter runs off throw_age, which is simulation time and
    // therefore replays. A wall clock here would be the one clock below App.
    const auto frame = static_cast<std::size_t>(
        std::max(0.f, use.throw_age) * FireVisual::kFramesPerSecond);
    place_flame(scene, held_.flame, bottle.transform_point(kRagLocal), eye,
                kRagFlameM, .85f, frame);
}

void MolotovVisual::sync_projectiles(Scene& scene, const MolotovProjectiles& shots,
                                     glm::vec3 eye) {
    visible_projectiles_ = 0;
    const auto& live = shots.shots();
    for (std::size_t i = 0; i < flying_.size(); ++i) {
        const bool show = i < live.size() && live[i].live;
        if (show) ++visible_projectiles_;
        Transform body;
        if (show) {
            body.position = live[i].position;
            body.rotation = glm::angleAxis(live[i].spin_turns * kTwoPi,
                                           glm::normalize(live[i].spin_axis));
        }
        for (std::size_t part = 0; part < flying_[i].parts.size(); ++part) {
            if (auto* node = scene.get(flying_[i].parts[part])) node->visible = show;
            if (!show) continue;
            scene.set_transform(flying_[i].parts[part], body * flying_[i].local[part]);
        }
        if (flying_[i].flame == kInvalidId) continue;
        if (!show) {
            if (auto* node = scene.get(flying_[i].flame)) node->visible = false;
            continue;
        }
        // The rag stays alight in the air, and the flame stays UPRIGHT while
        // the bottle tumbles under it: fire goes up whatever the thing
        // carrying it is doing, and a flame spinning end over end reads as a
        // sprite glued to the model.
        const auto frame = static_cast<std::size_t>(
            live[i].age * FireVisual::kFramesPerSecond +
            static_cast<float>(live[i].throw_id % 64u));
        place_flame(scene, flying_[i].flame, body.transform_point(kRagLocal),
                    eye, kFlightFlameM, 1.f, frame);
    }
}

bool MolotovVisual::release_point(glm::vec3& out) const {
    if (!held_visible_) return false;
    out = (hand_ * molotov_grip()).transform_point(kRagLocal);
    return true;
}

void MolotovVisual::destroy(Scene& scene) {
    const auto drop = [&](Bottle& bottle) {
        for (const NodeId node : bottle.parts) scene.remove(node);
        if (bottle.flame != kInvalidId) scene.remove(bottle.flame);
        bottle = Bottle{};
    };
    drop(held_);
    for (auto& bottle : flying_) drop(bottle);
    flying_.clear();
    if (renderer_) {
        if (bottle_mesh_ != kInvalidId) renderer_->remove_mesh(bottle_mesh_);
        if (cube_mesh_ != kInvalidId) renderer_->remove_mesh(cube_mesh_);
    }
    bottle_mesh_ = cube_mesh_ = kInvalidId;
    held_visible_ = false;
    visible_projectiles_ = 0;
    sprites_ = nullptr;
    renderer_ = nullptr;
}

// ---------------------------------------------------------------------------

bool FireVisual::init(Renderer& renderer, Scene& scene, const FireSprites& sprites) {
    destroy(scene);
    renderer_ = &renderer;
    sprites_ = &sprites;
    if (!sprites.valid()) return false;
    // ALLOCATED ONCE, UP FRONT, for the field's whole capacity. Creating nodes
    // as a fire grows would put scene churn on exactly the frames that already
    // have the most going on.
    const AABB bounds = make_fire_card(0).bounds;
    cards_.reserve(FireField::kCapacity * kCardsPerCell);
    for (std::size_t i = 0; i < FireField::kCapacity * kCardsPerCell; ++i) {
        Renderable renderable;
        renderable.mesh = sprites.frame(0);
        renderable.material = sprites.material();
        const NodeId node = scene.create(renderable, Transform{}, bounds);
        if (auto* value = scene.get(node)) value->visible = false;
        cards_.push_back(node);
    }
    return true;
}

void FireVisual::sync(Scene& scene, const FireField& fire, glm::vec3 eye) {
    visible_cards_ = 0;
    if (!sprites_ || !sprites_->valid()) return;
    for (std::size_t cell = 0; cell < FireField::kCapacity; ++cell) {
        const auto draw = fire.draw(cell);
        for (std::size_t card = 0; card < kCardsPerCell; ++card) {
            const std::size_t index = cell * kCardsPerCell + card;
            if (index >= cards_.size()) break;
            auto* node = scene.get(cards_[index]);
            if (!node) continue;
            node->visible = draw.visible;
            if (!draw.visible) continue;
            ++visible_cards_;

            // Everything about a card is keyed on its cell's own hash and its
            // own index, so no two cells play the same frame at the same
            // moment. Without that a grid of flames flickers in lockstep and
            // reads as a grid, which is the one thing it must never do.
            const uint64_t key = draw.variation ^
                                 (0x9E3779B97F4A7C15ull * (card + 1u));
            const float offset = unit_roll(splitmix64_mix(key)) *
                                 static_cast<float>(kFireSheetFrames);
            const float lean = unit_roll(splitmix64_mix(key ^ 0x51u)) - .5f;
            const float nudge = unit_roll(splitmix64_mix(key ^ 0xA3u)) - .5f;
            const auto frame = static_cast<std::size_t>(
                draw.age * kFramesPerSecond + offset);
            node->renderable.mesh = sprites_->frame(frame);
            node->renderable.tint = {1.f, .96f, .92f, .45f + 1.9f * draw.heat};

            // The second card is smaller, offset, and MIRRORED. A negative x
            // scale flips the winding, which is part of why the card mesh is
            // double sided; without the mirror two cards of the same take at
            // slightly different sizes read as one flame with a halo.
            const float shrink = card == 0 ? 1.f : .74f;
            const float width = kCellCardWidthM * shrink * (.55f + .45f * draw.heat);
            const float height = kCellCardHeightM * shrink * (.42f + .58f * draw.heat);
            Transform pose;
            pose.position = draw.position + glm::vec3{
                nudge * FireField::kCellSizeM * .38f, 0.f,
                lean * FireField::kCellSizeM * .38f};
            pose.rotation = billboard_yaw(pose.position, eye);
            pose.scale = {card == 0 ? width : -width, height, width};
            scene.set_transform(cards_[index], pose);
        }
    }
}

void FireVisual::destroy(Scene& scene) {
    for (const NodeId node : cards_) scene.remove(node);
    cards_.clear();
    visible_cards_ = 0;
    sprites_ = nullptr;
    renderer_ = nullptr;
}

// ---------------------------------------------------------------------------

bool WreckBlastVisual::init(Renderer& renderer, Scene& scene, const FireSprites& sprites) {
    destroy(scene);
    renderer_ = &renderer;
    sprites_ = &sprites;
    if (!sprites.valid()) return false;
    // One alpha-blended white material and one unit cube for the debris; the
    // tint carries the colour and the fade, as game/blood_particles.h does it.
    Texture white;
    if (!white.make_white()) return false;
    chunk_material_ = renderer.add_material(std::move(white), /*alpha_blended=*/true);
    const MeshData chunk = make_box(glm::vec3{.5f});
    chunk_mesh_ = renderer.add_mesh(chunk);
    if (chunk_mesh_ == kInvalidId || chunk_material_ == kInvalidId) return false;
    // Allocated once for the pool's whole capacity, like the burning ground.
    const AABB card_bounds = make_fire_card(0).bounds;
    cards_.reserve(WreckExplosion::kCapacity);
    chunks_.reserve(WreckExplosion::kCapacity);
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i) {
        Renderable card;
        card.mesh = sprites.frame(0);
        card.material = sprites.material();
        Renderable piece;
        piece.mesh = chunk_mesh_;
        piece.material = chunk_material_;
        for (auto [renderable, bounds, pool] :
             {std::tuple{card, card_bounds, &cards_}, std::tuple{piece, chunk.bounds, &chunks_}}) {
            const NodeId node = scene.create(renderable, Transform{}, bounds);
            if (auto* value = scene.get(node)) {
                value->visible = false;
                // A blast is seen from across the city; the default cull would
                // drop it before a wreck falling out of the sky got close.
                value->max_draw_distance = 900.f;
            }
            pool->push_back(node);
        }
    }
    return true;
}

void WreckBlastVisual::sync(Scene& scene, const WreckExplosion& blast, glm::vec3 eye) {
    visible_cards_ = visible_debris_ = 0;
    if (!sprites_ || !sprites_->valid()) return;
    for (std::size_t i = 0; i < cards_.size() && i < chunks_.size(); ++i) {
        const WreckParticleDraw draw = blast.draw(i);
        auto* card = scene.get(cards_[i]);
        auto* chunk = scene.get(chunks_[i]);
        if (!card || !chunk) continue;
        const bool flame = draw.visible && draw.kind != WreckParticleKind::Debris;
        card->visible = flame;
        chunk->visible = draw.visible && draw.kind == WreckParticleKind::Debris;
        if (!draw.visible) continue;

        if (!flame) {
            ++visible_debris_;
            chunk->renderable.tint = draw.tint;
            Transform piece;
            piece.position = draw.position;
            piece.scale = draw.scale;
            piece.rotation = glm::angleAxis(draw.spin,
                glm::normalize(glm::vec3{0.42f, 0.78f, 0.47f}));
            scene.set_transform(chunks_[i], piece);
            continue;
        }

        // Keyed on the slot, so no two particles of one blast play the same
        // frame at the same moment and read as a grid, which is the same rule
        // the burning ground follows.
        ++visible_cards_;
        const uint64_t key = splitmix64_mix(0x9E3779B97F4A7C15ull * (i + 1u) ^ 0xB1A57ull);
        const float offset = unit_roll(key) * static_cast<float>(kFireSheetFrames);
        const bool smoke = draw.kind == WreckParticleKind::Smoke;
        const float rate = smoke ? kSmokeFramesPerSecond : kFlameFramesPerSecond;
        card->renderable.mesh =
            sprites_->frame(static_cast<std::size_t>(draw.age * rate + offset));
        const float fade = std::clamp(draw.tint.a, 0.f, 1.f);
        float width, height;
        if (smoke) {
            // The flame take tinted down to soot and kept translucent: the
            // licks of the flame become rolling edges on a dark plume.
            card->renderable.tint = {draw.tint.r, draw.tint.g, draw.tint.b, fade};
            width = draw.scale.x * 1.15f;
            height = draw.scale.x * 1.25f;
        } else {
            // Above 1 the tint's alpha is the lit shader's emissive boost, so a
            // fresh flame burns bright and a dying one goes translucent on the
            // same number, exactly as place_flame() does for the lit rag.
            card->renderable.tint = {1.f, .90f, .80f, .35f + 2.0f * fade};
            width = draw.scale.x * 1.25f;
            height = draw.scale.x * 1.55f;
        }
        // The card stands on its base; the particle is its middle.
        Transform pose;
        pose.position = draw.position - glm::vec3{0.f, height * .45f, 0.f};
        pose.rotation = billboard_yaw(draw.position, eye);
        pose.scale = {(key & 1u) ? width : -width, height, width};
        scene.set_transform(cards_[i], pose);
    }
}

void WreckBlastVisual::destroy(Scene& scene) {
    for (const NodeId node : cards_) scene.remove(node);
    for (const NodeId node : chunks_) scene.remove(node);
    cards_.clear();
    chunks_.clear();
    if (renderer_ && chunk_mesh_ != kInvalidId) renderer_->remove_mesh(chunk_mesh_);
    chunk_mesh_ = kInvalidId;
    // Materials are append-only (see gfx/renderer.h).
    chunk_material_ = kInvalidId;
    visible_cards_ = visible_debris_ = 0;
    sprites_ = nullptr;
    renderer_ = nullptr;
}

}  // namespace apricot

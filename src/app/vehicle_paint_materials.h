#pragma once
// The host half of a respray: decode the atlas a car wears, build its paint
// mask from the profile table (game/vehicle_paint_profiles.h), recolour it on
// the CPU (game/vehicle_paint.h), and upload the result into a bounded pool of
// paintable materials (Renderer::add_paintable_material).
//
// Twelve slots, allocated once, because the material table never frees:
//  - Slot 0 is the PREVIEW. The booth repaints it when the picked colour
//    changes, and the spray reveal steps it toward the ordered colour. Only the
//    car being driven ever wears it, and only while the booth is open or a
//    spray runs.
//  - Slots 1..11 are OWNED by a (recolour atlas, colour) pair, so two cars in
//    the same paint share one. An owned slot is only ever repainted while no
//    live body wears it. Only the App knows every live body — the car being
//    driven and each parked one — so acquire() takes that set as an argument
//    rather than keeping a list that could go stale.
//
// Nothing here may be named in player_car_visual.cpp or game_ui.cpp: the tool
// labs compile those two files without this one.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/vehicle_paint.h"
#include "scene/scene.h"

namespace apricot {

class Renderer;

class PaintMaterialPool {
public:
    static constexpr std::size_t kSlots = 12;
    // Decoded atlases and their masks, least recently used out.
    static constexpr std::size_t kCachedAtlases = 4;

    bool init(Renderer& renderer);

    // The atlas a respray of `worn_atlas` recolours (a car wearing
    // car5/taxi.png is resprayed from car5/body.png), or nullptr when the
    // atlas has no paint profile.
    static const char* recolour_atlas(std::string_view worn_atlas);

    // A representative colour of the factory paint: the mask's base.
    std::optional<PaintColor> factory_colour(std::string_view worn_atlas);

    // Repaints slot 0. kInvalidId when the atlas has no profile or the upload
    // failed.
    MaterialId preview(std::string_view worn_atlas, PaintColor colour);
    MaterialId preview_material() const { return slots_[0].id; }

    // The owned slot already holding this paint, or kInvalidId. Never paints.
    MaterialId find(std::string_view worn_atlas, PaintColor colour) const;
    // find(), or else a slot no live body wears, painted now. kInvalidId when
    // every owned slot is worn.
    MaterialId acquire(std::string_view worn_atlas, PaintColor colour,
                       const std::vector<MaterialId>& live);
    static std::size_t live_refs(MaterialId id, const std::vector<MaterialId>& live);
    bool owns(MaterialId id) const;
    bool is_preview(MaterialId id) const { return id != kInvalidId && id == slots_[0].id; }

    // For --paint-check: decode every profiled atlas from disk, build its mask
    // with the C++ port, and hold it to the stats the lab measured when the
    // table was generated. A texture recook that moved the paint shows up here.
    bool check_profile_stats(std::string& report);

private:
    struct Atlas {
        std::string path;
        PaintImage image;
        PaintMask mask;
        uint64_t used = 0;
    };
    struct Slot {
        MaterialId id = kInvalidId;
        std::string atlas;
        PaintColor colour{};
        bool keyed = false;
    };

    // Valid until the next call that may decode.
    const Atlas* atlas_for(std::string_view worn_atlas);
    bool composite(const Atlas& atlas, PaintColor colour, Slot& slot);

    Renderer* renderer_ = nullptr;
    std::array<Slot, kSlots> slots_{};
    std::vector<Atlas> cache_;
    uint64_t uses_ = 0;
    PaintImage scratch_;
};

}  // namespace apricot

#include "app/vehicle_paint_materials.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "core/asset_root.h"
#include "core/log.h"
#include "game/vehicle_paint_profiles.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

bool decode_atlas(const char* path, PaintImage& out) {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    if (!decode_rgba_file(asset_path(path), width, height, rgba)) return false;
    out.width = width;
    out.height = height;
    out.bottom_up = true;
    out.rgba = std::move(rgba);
    return true;
}

// The mask a respray of this profile's atlas uses: its own gate, bounded by the
// stock atlas's mask when it is an alternate livery.
bool build_profile_mask(const PaintAtlasProfile& profile, const PaintImage& image, PaintMask& out) {
    if (!profile.params) return false;
    if (!profile.region_atlas) return build_paint_mask(image, *profile.params, nullptr, out);
    PaintImage region_image;
    PaintMask region;
    if (!profile.region_params || !decode_atlas(profile.region_atlas, region_image) ||
        !build_paint_mask(region_image, *profile.region_params, nullptr, region)) {
        return false;
    }
    return build_paint_mask(image, *profile.params, &region, out);
}

}  // namespace

bool PaintMaterialPool::init(Renderer& renderer) {
    renderer_ = &renderer;
    cache_.clear();
    for (Slot& slot : slots_) {
        slot = Slot{};
        slot.id = renderer.add_paintable_material(256, 256);
        if (slot.id == kInvalidId) {
            AP_ERROR("respray: could not create the paint material pool");
            return false;
        }
    }
    return true;
}

const char* PaintMaterialPool::recolour_atlas(std::string_view worn_atlas) {
    const PaintAtlasProfile* profile = find_paint_atlas_profile(worn_atlas);
    return profile && profile->params ? profile->atlas : nullptr;
}

const PaintMaterialPool::Atlas* PaintMaterialPool::atlas_for(std::string_view worn_atlas) {
    const PaintAtlasProfile* profile = find_paint_atlas_profile(worn_atlas);
    if (!profile || !profile->params) return nullptr;
    ++uses_;
    for (Atlas& cached : cache_) {
        if (cached.path == profile->atlas) {
            cached.used = uses_;
            return &cached;
        }
    }
    Atlas fresh;
    fresh.path = profile->atlas;
    fresh.used = uses_;
    if (!decode_atlas(profile->atlas, fresh.image) ||
        !build_profile_mask(*profile, fresh.image, fresh.mask)) {
        AP_ERROR("respray: could not build the paint mask for %s", profile->atlas);
        return nullptr;
    }
    if (cache_.size() < kCachedAtlases) {
        cache_.push_back(std::move(fresh));
        return &cache_.back();
    }
    const auto oldest = std::min_element(cache_.begin(), cache_.end(),
        [](const Atlas& a, const Atlas& b) { return a.used < b.used; });
    *oldest = std::move(fresh);
    return &*oldest;
}

bool PaintMaterialPool::composite(const Atlas& atlas, PaintColor colour, Slot& slot) {
    if (!renderer_ || slot.id == kInvalidId ||
        !recolour_paint(atlas.image, atlas.mask, colour, scratch_) ||
        !renderer_->update_paintable_material(slot.id, scratch_.width, scratch_.height,
                                              scratch_.rgba)) {
        slot.keyed = false;
        AP_ERROR("respray: recolour or upload of %s failed", atlas.path.c_str());
        return false;
    }
    slot.atlas = atlas.path;
    slot.colour = colour;
    slot.keyed = true;
    return true;
}

std::optional<PaintColor> PaintMaterialPool::factory_colour(std::string_view worn_atlas) {
    const Atlas* atlas = atlas_for(worn_atlas);
    if (!atlas) return std::nullopt;
    return atlas->mask.base_srgb;
}

MaterialId PaintMaterialPool::preview(std::string_view worn_atlas, PaintColor colour) {
    Slot& slot = slots_[0];
    const char* recolour = recolour_atlas(worn_atlas);
    if (!recolour || slot.id == kInvalidId) return kInvalidId;
    if (slot.keyed && slot.atlas == recolour && slot.colour == colour) return slot.id;
    const Atlas* atlas = atlas_for(worn_atlas);
    return atlas && composite(*atlas, colour, slot) ? slot.id : kInvalidId;
}

MaterialId PaintMaterialPool::find(std::string_view worn_atlas, PaintColor colour) const {
    const char* recolour = recolour_atlas(worn_atlas);
    if (!recolour) return kInvalidId;
    for (std::size_t i = 1; i < kSlots; ++i) {
        const Slot& slot = slots_[i];
        if (slot.keyed && slot.atlas == recolour && slot.colour == colour) return slot.id;
    }
    return kInvalidId;
}

MaterialId PaintMaterialPool::acquire(std::string_view worn_atlas, PaintColor colour,
                                      const std::vector<MaterialId>& live) {
    if (const MaterialId found = find(worn_atlas, colour); found != kInvalidId) return found;
    if (!recolour_atlas(worn_atlas)) return kInvalidId;
    Slot* pick = nullptr;
    for (std::size_t i = 1; i < kSlots; ++i) {
        Slot& slot = slots_[i];
        if (slot.id == kInvalidId || live_refs(slot.id, live) != 0) continue;
        if (!slot.keyed) {
            pick = &slot;
            break;
        }
        if (!pick) pick = &slot;
    }
    if (!pick) return kInvalidId;
    const Atlas* atlas = atlas_for(worn_atlas);
    return atlas && composite(*atlas, colour, *pick) ? pick->id : kInvalidId;
}

std::size_t PaintMaterialPool::live_refs(MaterialId id, const std::vector<MaterialId>& live) {
    return static_cast<std::size_t>(std::count(live.begin(), live.end(), id));
}

bool PaintMaterialPool::owns(MaterialId id) const {
    if (id == kInvalidId) return false;
    for (std::size_t i = 1; i < kSlots; ++i)
        if (slots_[i].id == id) return true;
    return false;
}

bool PaintMaterialPool::check_profile_stats(std::string& report) {
    report.clear();
    std::size_t checked = 0, failed = 0;
    for (std::size_t i = 0; i < paint_atlas_profile_count(); ++i) {
        const PaintAtlasProfile& profile = paint_atlas_profile(i);
        if (profile.alias_of) continue;
        PaintImage image;
        PaintMask mask;
        if (!decode_atlas(profile.atlas, image) || !build_profile_mask(profile, image, mask)) {
            ++failed;
            report += profile.atlas;
            report += ": could not decode the atlas or build its mask\n";
            continue;
        }
        ++checked;
        const PaintAtlasStats& expect = profile.expect;
        const double painted_tol = std::max(4.0, 0.001 * static_cast<double>(expect.painted_texels));
        const double weight_tol = 0.005 * static_cast<double>(expect.weight_sum_q8);
        const bool painted_ok = std::fabs(static_cast<double>(mask.painted_texels) -
                                          static_cast<double>(expect.painted_texels)) <= painted_tol;
        const bool weight_ok = std::fabs(static_cast<double>(mask.weight_sum_q8) -
                                         static_cast<double>(expect.weight_sum_q8)) <= weight_tol;
        const auto near = [](uint8_t a, uint8_t b) { return std::abs(int{a} - int{b}) <= 1; };
        const bool base_ok = near(mask.base_srgb.r, expect.base_srgb.r) &&
                             near(mask.base_srgb.g, expect.base_srgb.g) &&
                             near(mask.base_srgb.b, expect.base_srgb.b);
        if (painted_ok && weight_ok && base_ok) continue;
        ++failed;
        char line[320];
        std::snprintf(line, sizeof line,
            "%s: painted %u (expect %u), weight %llu (expect %llu), base %d,%d,%d (expect %d,%d,%d)\n",
            profile.atlas, mask.painted_texels, expect.painted_texels,
            static_cast<unsigned long long>(mask.weight_sum_q8),
            static_cast<unsigned long long>(expect.weight_sum_q8),
            mask.base_srgb.r, mask.base_srgb.g, mask.base_srgb.b,
            expect.base_srgb.r, expect.base_srgb.g, expect.base_srgb.b);
        report += line;
    }
    if (checked == 0) report += "no paint profile was checked\n";
    AP_INFO("respray: %zu paint profiles held to their lab stats, %zu failed", checked, failed);
    return failed == 0 && checked > 0;
}

}  // namespace apricot

#include "game/vehicle_paint_profiles.h"

#include <algorithm>
#include <iterator>

namespace apricot {
namespace {

#include "game/vehicle_paint_profiles.inc"

const PaintAtlasProfile* find_exact(std::string_view atlas) {
    for (const PaintAtlasProfile& profile : kPaintAtlasProfiles) {
        if (atlas == profile.atlas) return &profile;
    }
    return nullptr;
}

}  // namespace

std::size_t paint_atlas_profile_count() { return std::size(kPaintAtlasProfiles); }

const PaintAtlasProfile& paint_atlas_profile(std::size_t i) {
    return kPaintAtlasProfiles[std::min(i, std::size(kPaintAtlasProfiles) - 1)];
}

const PaintAtlasProfile* find_paint_atlas_profile(std::string_view atlas) {
    const PaintAtlasProfile* profile = find_exact(atlas);
    if (profile && profile->alias_of) profile = find_exact(profile->alias_of);
    // One hop only: an alias of an alias is a table bug, and the suite fails it.
    return profile && !profile->alias_of ? profile : nullptr;
}

uint64_t paint_profiles_json_digest() { return kPaintProfilesJsonDigest; }

}  // namespace apricot

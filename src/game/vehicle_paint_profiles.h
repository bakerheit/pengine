#pragma once
// Vehicle paint profiles: for every atlas a drivable car can wear, the mask
// params a respray builds that atlas's paint mask with (game/vehicle_paint.h).
//
// The table is generated. Its source is tools/paint_profiles/*.json, one paint
// family per file, tuned with tools/paint_lab.py; `python3
// tools/paint_profiles.py emit` writes vehicle_paint_profiles.inc, which the
// .cpp compiles. Never edit the .inc: vehicle_paint_profiles_tests holds it to
// the JSON by digest and fails on a hand edit or a JSON edit that was not
// regenerated.
//
// A table of numbers can be internally consistent and wrong about the art, so
// two by-hand audits need numpy and the real PNGs: `tools/paint_profiles.py
// check` re-derives every atlas's `expect` stats with the lab after a
// texture cook (rects belong to one bake), and `lamps` fails if any mask
// reaches a brake or lightbar lens.
//
// Every drivable car needs a profile for the atlas it wears, or an entry in
// kPaintProfilePending (app/vehicle_paint_catalog.h); the suite fails
// otherwise and says which to do.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "game/vehicle_paint.h"

namespace apricot {

enum class PaintGrade : uint8_t { Exact, Good, Rough };

// What the lab measured on the real atlas when the table was generated.
struct PaintAtlasStats {
    uint32_t painted_texels;  // weight >= .5
    uint64_t weight_sum_q8;   // sum of nearbyint(weight * 255)
    PaintColor base_srgb;
};

struct PaintAtlasProfile {
    // Asset-relative, as the catalog spells it: "textures/vehicles/car8/mail.png".
    const char* atlas;
    // nullptr, or the atlas a respray recolours in this one's place (a car
    // wearing car5/taxi.png is resprayed from car5/body.png). An alias holds
    // no params or stats of its own.
    const char* alias_of;
    // nullptr, or the stock atlas whose mask bounds this alternate livery's:
    // mask = min(own gate, region mask).
    const char* region_atlas;
    const PaintMaskParams* params;         // this atlas's, with its override merged in
    const PaintMaskParams* region_params;  // region_atlas's; set exactly when region_atlas is
    uint16_t width, height;
    PaintAtlasStats expect;
    PaintGrade grade;
};

std::size_t paint_atlas_profile_count();
// i < paint_atlas_profile_count(). Ordered by atlas path.
const PaintAtlasProfile& paint_atlas_profile(std::size_t i);
// The profile a respray of `atlas` uses: an exact path match, with an alias
// followed to the profile it names. nullptr when the atlas has none.
const PaintAtlasProfile* find_paint_atlas_profile(std::string_view atlas);
// FNV-1a 64 of the JSON set the compiled table was generated from: each file
// name, a NUL, then the file's bytes, in file-name order, carriage returns
// skipped.
uint64_t paint_profiles_json_digest();

}  // namespace apricot

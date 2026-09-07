#pragma once

#include <cstddef>

#include "city/start_area.h"

namespace apricot {
namespace city {

// First reusable roadside billboard. The site sits in the empty block directly
// south of Halloway Street and faces local +Z, toward traffic on the arterial.
// The art panel preserves the supplied 1280:588 aspect ratio exactly.
inline constexpr StartSite kNessBillboardSite{
    "Ness & Ness Billboard", {25.2943f, -73.8585f}, kGridCos, kGridSin,
    {0.0f, 3.0f}, 14.5f, 3.2f, kStartAreaGroundM, 900.0f};

inline constexpr StartPart kNessBillboardParts[] = {
    {"billboard footing west", {-4.0f, 3.0f}, 0.0f, 1.5f, 0.35f, 1.5f,
     StartFinish::Concrete, true},
    {"billboard footing east", {4.0f, 3.0f}, 0.0f, 1.5f, 0.35f, 1.5f,
     StartFinish::Concrete, true},
    {"billboard post west", {-4.0f, 3.0f}, 0.35f, 0.52f, 4.20f, 0.52f,
     StartFinish::Steel, true},
    {"billboard post east", {4.0f, 3.0f}, 0.35f, 0.52f, 4.20f, 0.52f,
     StartFinish::Steel, true},
    {"billboard brace west", {-2.0f, 3.0f}, 0.9f, 0.22f, 4.0f, 0.22f,
     StartFinish::Steel, false, 0.0f, 0.0f, -28.0f},
    {"billboard brace east", {2.0f, 3.0f}, 0.9f, 0.22f, 4.0f, 0.22f,
     StartFinish::Steel, false, 0.0f, 0.0f, 28.0f},
    {"billboard backing", {0.0f, 3.0f}, 4.55f, 12.8f, 6.15f, 0.38f,
     StartFinish::DarkRoof, true},
    {"billboard catwalk", {0.0f, 2.5f}, 4.15f, 13.4f, 0.16f, 1.25f,
     StartFinish::Steel, true},
    {"billboard face ness and ness", {0.0f, 3.22f}, 4.85f, 12.0f,
     5.5125f, 0.04f, StartFinish::White, false},
    {"billboard frame bottom", {0.0f, 3.28f}, 4.55f, 12.8f, 0.30f, 0.12f,
     StartFinish::Steel, false},
    {"billboard frame top", {0.0f, 3.28f}, 10.3625f, 12.8f, 0.30f, 0.12f,
     StartFinish::Steel, false},
    {"billboard frame west", {-6.2f, 3.28f}, 4.85f, 0.40f, 5.5125f,
     0.12f,
     StartFinish::Steel, false},
    {"billboard frame east", {6.2f, 3.28f}, 4.85f, 0.40f, 5.5125f,
     0.12f,
     StartFinish::Steel, false},
};

inline constexpr std::size_t kNessBillboardPartCount =
    sizeof(kNessBillboardParts) / sizeof(kNessBillboardParts[0]);

static_assert(kNessBillboardPartCount == 13u,
              "Ness billboard structure lost an authored part");

// A second roadside fixture in the matching block east of the Ness board.
// This one uses a single urban monopole and a wide V-yoke instead of two posts
// and a maintenance catwalk. It faces the same north-side traffic corridor,
// so the supplied 1850:850 art is readable during normal driving.
inline constexpr StartSite kPinnatyTaxiBillboardSite{
    "Pinnaty Taxi Billboard", {120.7684f, -63.8238f}, kGridCos, kGridSin,
    {0.0f, 2.5f}, 13.5f, 4.0f, kStartAreaGroundM, 900.0f};

inline constexpr StartPart kPinnatyTaxiBillboardParts[] = {
    {"taxi billboard footing", {0.0f, 2.5f}, 0.0f, 2.6f, 0.35f, 2.6f,
     StartFinish::Concrete, true},
    {"taxi billboard monopole", {0.0f, 2.5f}, 0.35f, 0.90f, 5.20f, 0.90f,
     StartFinish::Steel, true},
    {"taxi billboard pylon collar", {0.0f, 2.5f}, 4.65f, 1.35f, 0.65f,
     1.10f, StartFinish::DarkRoof, false},
    {"taxi billboard yoke west", {-2.0f, 2.5f}, 4.35f, 0.26f, 4.55f,
     0.26f, StartFinish::Steel, false, 0.0f, 0.0f, -58.0f},
    {"taxi billboard yoke east", {2.0f, 2.5f}, 4.35f, 0.26f, 4.55f,
     0.26f, StartFinish::Steel, false, 0.0f, 0.0f, 58.0f},
    {"taxi billboard backing", {0.0f, 2.5f}, 5.25f, 12.0f, 5.75f, 0.34f,
     StartFinish::DarkRoof, true},
    {"billboard face pinnaty taxi", {0.0f, 2.70f}, 5.55f, 11.1f, 5.10f,
     0.04f, StartFinish::White, false},
    {"taxi billboard frame bottom", {0.0f, 2.72f}, 5.25f, 12.0f, 0.30f,
     0.12f, StartFinish::Steel, false},
    {"taxi billboard frame top", {0.0f, 2.72f}, 10.70f, 12.0f, 0.30f,
     0.12f, StartFinish::Steel, false},
    {"taxi billboard frame west", {-5.85f, 2.72f}, 5.55f, 0.30f, 5.10f,
     0.12f, StartFinish::Steel, false},
    {"taxi billboard frame east", {5.85f, 2.72f}, 5.55f, 0.30f, 5.10f,
     0.12f, StartFinish::Steel, false},
    {"taxi billboard crown west", {-3.0f, 2.5f}, 11.0f, 5.4f, 0.18f,
     0.22f, StartFinish::DarkRoof, false},
    {"taxi billboard crown east", {3.0f, 2.5f}, 11.0f, 5.4f, 0.18f,
     0.22f, StartFinish::DarkRoof, false},
};

inline constexpr std::size_t kPinnatyTaxiBillboardPartCount =
    sizeof(kPinnatyTaxiBillboardParts) /
    sizeof(kPinnatyTaxiBillboardParts[0]);

static_assert(kPinnatyTaxiBillboardPartCount == 13u,
              "Pinnaty Taxi billboard structure lost an authored part");

}  // namespace city
}  // namespace apricot

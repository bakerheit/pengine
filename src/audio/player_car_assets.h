#pragma once

#include "audio/synth.h"

namespace apricot {

// Checked-in recorded runtime paths. Centralised so the game and the asset
// regression tests cannot silently disagree about which files ship live.
SfxOverridePaths player_car_audio_overrides();

}  // namespace apricot

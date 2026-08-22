#pragma once

#include <string>
#include <string_view>

namespace apricot {

// Root directory for on-disk assets, resolved once on first call and cached.
//
// apricot generates its world procedurally, so this tree holds only the things
// that genuinely cannot be computed: shader source, and fonts for the debug
// overlay. There is no asset cooker and there is no mesh on disk.
//
// The app anchors its working directory to the folder containing `assets/`
// before anything loads, so in-game this returns the RELATIVE root "assets" —
// portable across dev checkouts and packaged builds. Baking an absolute path
// into the binary works right up until someone else extracts the package.
// When there is no ./assets in the working directory — test binaries run from
// the build dir and never anchor — it falls back to the absolute APRICOT_-
// ASSETS_DIR baked in at compile time.
//
// Do NOT call this from a namespace-scope initialiser: that runs before the
// working directory is anchored and caches the wrong root for the process
// lifetime. Keep path tables as relative literals and join at load time.
const std::string& asset_root();

// Convenience join. `rel` is relative to the assets root and must not start
// with '/', e.g. asset_path("shaders/terrain.vert").
std::string asset_path(std::string_view rel);

}  // namespace apricot

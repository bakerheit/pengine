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
// RESOLUTION ORDER, first hit wins. Every one of these is a layout the engine
// is genuinely launched from, and the resolver logs which one it took at INFO
// so a wrong-assets bug is one line of the log rather than an afternoon:
//
//   1. $APRICOT_ASSETS            explicit override, for tools and packagers
//   2. ./assets                   the app anchors its working directory here
//   3. <exe dir>/assets           a flat portable drop
//   4. <exe dir>/../assets        build/bin/apricot -> build/assets
//   5. <exe dir>/../share/apricot/assets   a Unix install prefix
//   6. <exe dir>/../Resources/assets       a macOS .app bundle
//   7. walking up from the working directory   test binaries, ad-hoc launches
//   8. APRICOT_ASSETS_DIR         compile-time absolute path to the checkout
//
// Strategy 2 returns the RELATIVE string "assets" on purpose, so a packaged
// build stays relocatable. Baking an absolute path into the binary works right
// up until someone else extracts the package.
//
// A MISSING ASSETS TREE IS NOT FATAL. If nothing resolves, this warns once and
// returns the compile-time fallback anyway. The engine's world is procedural;
// it can run without a font. Aborting here would mean a missing debug overlay
// takes the whole game down, which is exactly backwards.
//
// Do NOT call this from a namespace-scope initialiser: that runs before the
// working directory is anchored and caches the wrong root for the process
// lifetime. Keep path tables as relative literals and join at load time.
const std::string& asset_root();

// Convenience join. `rel` is relative to the assets root and must not start
// with '/', e.g. asset_path("shaders/terrain.vert").
std::string asset_path(std::string_view rel);

// Which of the strategies above produced asset_root(), as a short stable tag
// ("env", "cwd", "exe", "exe-parent", "share", "bundle", "cwd-walk",
// "compiled-in", "none"). Resolves the root if it has not been resolved yet.
// For diagnostics and the debug overlay; do not branch engine behaviour on it.
const char* asset_root_strategy();

// False when NO strategy found a real directory and asset_root() is a guess.
// Loaders should report their own missing file normally; this exists so a
// caller can say "no assets tree at all" once instead of once per file.
bool assets_found();

}  // namespace apricot

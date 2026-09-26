#!/usr/bin/env bash
#
# Windows builds, cross-compiled here with MinGW-w64, and the Windows test PC.
#
#   tools/build_windows.sh              build + package build-win/dist/ProbableCause-<VERSION>-Windows-x64.zip
#   tools/build_windows.sh --push       ...then copy it to the PC and unpack it on its Desktop
#   tools/build_windows.sh --push-only  push the package already built
#   tools/build_windows.sh --test       cross-build every headless suite and run them on the PC
#
# The package is apricot.exe next to a copy of assets/. The exe links libgcc,
# libstdc++ and winpthread statically, so it needs no DLL beside it, and it
# finds its assets through asset_root()'s "<exe dir>/assets" strategy.
# assets/models/ is gitignored, so only a checkout holding the cooked models
# can make a playable package; this refuses to package without them.
#
# --test is the Windows counterpart of ctest in tools/ci.sh. The suites read
# the assets of the package already pushed for this VERSION rather than carry
# a second copy, so push first. They build in their own tree without -g: with
# debug info each static .exe is ~200 MB, and there are over two hundred.
# Two suites stay on the Mac: version_hook_tests is a bash script, and
# vehicle_paint_profiles_tests reads src/ and tools/ through the checkout's
# compile-time path, which the PC does not have.
#
# The PC runs OpenSSH with PowerShell as its shell; probablecause's
# tools/win_remote/ set that up. Key-only and never prompting, so a PC that is
# off or asleep fails in seconds instead of hanging.
#
# Env:  APRICOT_WIN_BUILD       package build dir (default <repo>/build-win)
#       APRICOT_WIN_TEST_BUILD  suite build dir (default <repo>/build-win-tests)
#       APRICOT_DEPS_DIR        fetched dependency sources to reuse instead of
#                               cloning (default <repo>/build/_deps if present)
#       PC_HOST                 user@host of the PC (default: tools/.pc_host,
#                               gitignored; use its .local name, its IP moves)
#       PC_KEY                  ssh key (default ~/.ssh/pcgame_winbox)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${APRICOT_WIN_BUILD:-$REPO/build-win}"
TEST_BUILD="${APRICOT_WIN_TEST_BUILD:-$REPO/build-win-tests}"
DEPS="${APRICOT_DEPS_DIR:-$REPO/build/_deps}"
VER="$(tr -d '[:space:]' < "$REPO/VERSION")"
PACKAGE="ProbableCause-${VER}-Windows-x64"
ZIP="$BUILD/dist/$PACKAGE.zip"
SUITES_NAME="apricot-suites-${VER}"

case "${1:-}" in
  "")          DO_BUILD=1; DO_PUSH=0; DO_TEST=0 ;;
  --push)      DO_BUILD=1; DO_PUSH=1; DO_TEST=0 ;;
  --push-only) DO_BUILD=0; DO_PUSH=1; DO_TEST=0 ;;
  --test)      DO_BUILD=0; DO_PUSH=0; DO_TEST=1 ;;
  *) echo "usage: tools/build_windows.sh [--push | --push-only | --test]"; exit 1 ;;
esac

# configure <dir> [cmake args...]: once per tree, reusing fetched sources so
# SDL and friends are not cloned again. Their builds stay per tree.
configure() {
  local dir="$1" dep src
  shift
  [ -f "$dir/CMakeCache.txt" ] && return 0
  command -v x86_64-w64-mingw32-g++ >/dev/null \
    || { echo "x86_64-w64-mingw32-g++ not found: brew install mingw-w64"; exit 1; }
  local args=()
  for dep in GLAD GLM IMGUI MINIAUDIO SDL2 STB; do
    src="$DEPS/$(echo "$dep" | tr '[:upper:]' '[:lower:]')-src"
    if [ -d "$src" ]; then args+=("-DFETCHCONTENT_SOURCE_DIR_$dep=$src"); fi
  done
  echo ">> configuring $dir"
  cmake -S "$REPO" -B "$dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ${args[@]+"${args[@]}"} "$@" >/dev/null
}

# connect: find the PC and fail fast when it is not answering.
connect() {
  if [ -z "${PC_HOST:-}" ] && [ -f "$REPO/tools/.pc_host" ]; then
    PC_HOST="$(tr -d '[:space:]' < "$REPO/tools/.pc_host")"
  fi
  [ -n "${PC_HOST:-}" ] || { echo "set PC_HOST=user@host or save it to tools/.pc_host"; exit 1; }
  SSH_OPTS=(-i "${PC_KEY:-$HOME/.ssh/pcgame_winbox}" -o StrictHostKeyChecking=accept-new
            -o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=5 -o ServerAliveCountMax=3)
  ssh "${SSH_OPTS[@]}" "$PC_HOST" "exit" >/dev/null 2>&1 \
    || { echo ">> $PC_HOST unreachable (off, asleep, or not on this network)"; exit 1; }
}

# pc <powershell>: run a command on the PC.
pc() { ssh "${SSH_OPTS[@]}" "$PC_HOST" "$@"; }

# unpack <zip> <folder>: copy the zip to the PC's Desktop and replace <folder>
# with its contents. Windows 10 ships bsdtar, far faster than Expand-Archive.
unpack() {
  local zip="$1" folder="$2" name
  name="$(basename "$zip")"
  echo ">> copying $name to $PC_HOST"
  scp "${SSH_OPTS[@]}" "$zip" "$PC_HOST:Desktop/"
  pc "\$d = Join-Path \$env:USERPROFILE 'Desktop'; \
      Remove-Item -Recurse -Force (Join-Path \$d '$folder') -ErrorAction SilentlyContinue; \
      tar -xf (Join-Path \$d '$name') -C \$d; \
      if (\$LASTEXITCODE -ne 0) { exit \$LASTEXITCODE }; \
      Remove-Item (Join-Path \$d '$name')"
}

if [ "$DO_BUILD" = 1 ]; then
  [ -d "$REPO/assets/models" ] \
    || { echo "assets/models/ is missing (it is gitignored); copy it from a checkout that has it"; exit 1; }
  configure "$BUILD"
  echo ">> building apricot.exe"
  cmake --build "$BUILD" --target apricot
  # A stripped exe ships; the unstripped one stays in $BUILD/bin for addr2line.
  echo ">> packaging $PACKAGE"
  rm -rf "$BUILD/dist" && mkdir -p "$BUILD/dist/$PACKAGE"
  x86_64-w64-mingw32-strip -o "$BUILD/dist/$PACKAGE/apricot.exe" "$BUILD/bin/apricot.exe"
  rsync -a --exclude='.DS_Store' "$REPO/assets/" "$BUILD/dist/$PACKAGE/assets/"
  ( cd "$BUILD/dist" && zip -r -q -1 "$PACKAGE.zip" "$PACKAGE" )
  echo ">> $ZIP ($(du -h "$ZIP" | cut -f1 | tr -d ' '))"
fi

if [ "$DO_PUSH" = 1 ]; then
  [ -f "$ZIP" ] || { echo "no package at $ZIP; build it first"; exit 1; }
  connect
  unpack "$ZIP" "$PACKAGE"
  pc "& (Join-Path \$env:USERPROFILE 'Desktop\\$PACKAGE\\apricot.exe') --version"
  echo ">> done. On the PC: Desktop\\$PACKAGE\\apricot.exe"
fi

if [ "$DO_TEST" = 1 ]; then
  configure "$TEST_BUILD" "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG" \
                          "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG"
  # Every registered suite that is one of our executables, less the two above.
  SUITES=()
  while IFS= read -r exe; do SUITES+=("$exe"); done < <(
    ctest --test-dir "$TEST_BUILD" --show-only=json-v1 | python3 -c '
import json, sys
skip = {"version_hook_tests", "vehicle_paint_profiles_tests"}
for test in json.load(sys.stdin)["tests"]:
    command = test.get("command") or []
    if test["name"] not in skip and command and command[0].endswith(".exe"):
        print(command[0])')
  TARGETS=()
  for exe in "${SUITES[@]}"; do TARGETS+=("$(basename "$exe" .exe)"); done
  echo ">> building ${#SUITES[@]} suites"
  cmake --build "$TEST_BUILD" --target "${TARGETS[@]}"

  STAGE="$TEST_BUILD/$SUITES_NAME"
  rm -rf "$STAGE" "$STAGE.zip" && mkdir -p "$STAGE/bin"
  for exe in "${SUITES[@]}"; do
    x86_64-w64-mingw32-strip -o "$STAGE/bin/$(basename "$exe")" "$exe"
  done
  cp "$REPO/tools/windows_suites.ps1" "$STAGE/"
  ( cd "$TEST_BUILD" && zip -r -q -1 "$SUITES_NAME.zip" "$SUITES_NAME" )

  connect
  pc "if (-not (Test-Path (Join-Path \$env:USERPROFILE 'Desktop\\$PACKAGE\\assets'))) { exit 3 }" \
    || { echo ">> $PACKAGE is not on the PC: run tools/build_windows.sh --push first"; exit 1; }
  unpack "$STAGE.zip" "$SUITES_NAME"
  echo ">> running the suites on $PC_HOST"
  pc "powershell -NoProfile -ExecutionPolicy Bypass \
        -File \"\$env:USERPROFILE\\Desktop\\$SUITES_NAME\\windows_suites.ps1\" \
        -Assets \"\$env:USERPROFILE\\Desktop\\$PACKAGE\\assets\"; exit \$LASTEXITCODE"
fi

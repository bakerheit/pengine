# Cross-compile apricot for 64-bit Windows with MinGW-w64, from macOS or Linux.
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#
# tools/build_windows.sh wraps this, packages the result and can push it to the
# Windows test PC. Homebrew's `mingw-w64` and Debian's `g++-mingw-w64-x86-64`
# both put the prefixed compilers on PATH, which is all this file assumes.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_mingw x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${_mingw}-gcc)
set(CMAKE_CXX_COMPILER ${_mingw}-g++)
set(CMAKE_RC_COMPILER  ${_mingw}-windres)

# Every dependency is fetched and built from source, so nothing should ever be
# found on the build machine: host programs yes (python for glad's generator),
# host libraries and headers never.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# One self-contained .exe: libgcc, libstdc++ and winpthread linked statically,
# so the package needs no MinGW DLLs next to it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

cmake_minimum_required(VERSION 3.24)

# ============================================================================
#  pengine-apricot
#
#  Three targets, and the LINK GRAPH is what enforces the architecture:
#
#    apricot_sim   STATIC  glm + Threads. ZERO GL, ZERO SDL, ZERO audio device.
#                          All world/physics/game logic. Tests link only this.
#    apricot_host  STATIC  Everything that talks to hardware: window, input,
#                          GL rendering, audio device. Links apricot_sim.
#    apricot       EXE     src/app/ + src/main.cpp.
#
#  A dependency can only flow sim -> host -> exe. If you find yourself wanting
#  to include <SDL.h> from src/terrain/, the design is wrong, not the rule.
#  tools/guard_sim_purity.sh fails the build before CI ever gets there.
#
#  MODULE-OWNED CMAKE FRAGMENTS. This file declares the three targets with no
#  sources; every src/<module>/CMakeLists.txt calls target_sources() to attach
#  its own files. That is deliberate: parallel agents own separate modules and
#  must never have to edit the same CMake file.
# ============================================================================

# Single source of truth for the build version. It flows into the project()
# version and the APRICOT_VERSION macro logged at startup, so every build is
# identifiable from its first line of output.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APRICOT_VERSION LIMIT_COUNT 1)
string(STRIP "${APRICOT_VERSION}" APRICOT_VERSION)
if(NOT APRICOT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semver like 1.2.3 (got '${APRICOT_VERSION}')")
endif()

project(apricot VERSION ${APRICOT_VERSION} LANGUAGES C CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(APRICOT_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)

# ----------------------------------------------------------------------------
#  Dependencies
#
#  Pinned versions, fetched at configure time. No system packages, no vendored
#  blobs in the tree: a fresh clone + `cmake -S . -B build` is the whole setup.
# ----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# --- SDL2 -------------------------------------------------------------------
# SDL 2.30.10 declares `cmake_minimum_required(VERSION 3.0.0...3.10)`, and
# CMake 4.x hard-errors on any project asking for < 3.5. CMAKE_POLICY_VERSION_-
# MINIMUM is the sanctioned escape hatch: it raises the floor SDL is evaluated
# against without touching SDL's tree. Scoped to this block and unset straight
# after so it can never silently paper over OUR code.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# --- GLM (header-only maths; the ONE dependency apricot_sim is allowed) ------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# --- glad2 (GL 3.3 core loader, generated at configure time) ----------------
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl_core_33 STATIC API gl:core=3.3)

# --- stb (single-file headers) ----------------------------------------------
# No CMakeLists.txt in the repo, so MakeAvailable populates it and skips
# add_subdirectory. (The old FetchContent_Populate() one-arg form used to be
# needed for this; it was removed in CMake 4.)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# --- miniaudio (single-header audio device) ---------------------------------
# Header-only repo; same populate-without-configure story as stb.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(miniaudio)

# --- Dear ImGui (debug overlay) ---------------------------------------------
# Built as its OWN target so it escapes apricot's -Werror/-Wconversion. ImGui
# ships no CMakeLists.txt, so we list its TUs ourselves. The GL3 backend loads
# GL symbols through its own bundled loader and needs no glad wiring.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
# SYSTEM so ImGui's headers don't trip -Wconversion inside OUR translation
# units when src/app includes them.
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

# ----------------------------------------------------------------------------
#  Strict warnings — on OUR targets only, never on anything fetched above.
# ----------------------------------------------------------------------------
if(MSVC)
    set(APRICOT_STRICT_FLAGS /W4 /WX /utf-8 /EHsc)
else()
    set(APRICOT_STRICT_FLAGS
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor
    )
endif()

# ----------------------------------------------------------------------------
#  The three targets. Source-less on purpose — src/<module>/CMakeLists.txt
#  attaches sources via target_sources().
# ----------------------------------------------------------------------------

# --- apricot_sim: pure logic. glm + Threads and nothing else, forever. -------
add_library(apricot_sim STATIC)
target_include_directories(apricot_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(apricot_sim PUBLIC
    APRICOT_VERSION="${APRICOT_VERSION}"
    # Absolute fallback used only by binaries that never anchor their working
    # directory (the headless test suites). See core/asset_root.cpp.
    APRICOT_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_compile_options(apricot_sim PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_sim PUBLIC glm::glm Threads::Threads)

# --- apricot_host: everything that touches hardware. -------------------------
add_library(apricot_host STATIC)
target_include_directories(apricot_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(apricot_host SYSTEM PRIVATE
    ${stb_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}
)
target_compile_options(apricot_host PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_host PUBLIC
    apricot_sim
    SDL2::SDL2-static
    glad_gl_core_33
    imgui
)

if(APPLE)
    target_link_libraries(apricot_host PUBLIC
        "-framework OpenGL"
        "-framework CoreAudio"
        "-framework AudioUnit"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
endif()

if(MSVC)
    # Our TUs pull in <SDL.h> -> <windows.h>; PUBLIC so consumers get them too.
    target_compile_definitions(apricot_host PUBLIC
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
endif()

# --- apricot: the executable. ------------------------------------------------
add_executable(apricot)
target_compile_options(apricot PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot PRIVATE apricot_host)

# Sanitizers — opt in via -DAPRICOT_SANITIZE=ON.
if(APRICOT_SANITIZE AND NOT MSVC)
    foreach(t apricot_sim apricot_host)
        target_compile_options(${t} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${t} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()

add_subdirectory(src)
add_subdirectory(tests)

message(STATUS "apricot ${APRICOT_VERSION} (${CMAKE_BUILD_TYPE})")

cmake_minimum_required(VERSION 3.21)
project(pengine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# --- SDL2 -------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

# --- GLM (header-only) ------------------------------------------------------
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

# --- stb (single-file image loader, header only) ----------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# --- Assimp (meshconv only — never linked into engine) ----------------------
# Only OBJ, FBX and glTF importers; no exporters; no tests.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER            ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER            ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER           ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS                   OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL                       OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS            OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW             OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB                    OFF CACHE BOOL "" FORCE) # use system zlib (avoids macOS header conflict)
set(BUILD_SHARED_LIBS                    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.3.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(assimp)

# stb_impl.cpp is a pure C header expanded into C++; suppress all warnings on it.
set_source_files_properties(src/render/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

# --- Engine library ---------------------------------------------------------
add_library(pengine_core STATIC
    src/platform/window.cpp
    src/platform/input.cpp
    src/Application.cpp
    src/render/shader.cpp
    src/render/mesh.cpp
    src/render/texture.cpp
    src/render/camera.cpp
    src/render/model.cpp
    src/render/material.cpp
    src/render/stb_impl.cpp
)

target_include_directories(pengine_core PUBLIC src)
target_include_directories(pengine_core PRIVATE ${stb_SOURCE_DIR})

target_compile_definitions(pengine_core PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

target_link_libraries(pengine_core
    PUBLIC
        SDL2::SDL2-static
        glm::glm
        glad_gl_core_33
)

if(APPLE)
    target_link_libraries(pengine_core PUBLIC "-framework OpenGL")
endif()

# Strict warnings on engine code only (not on third_party).
if(MSVC)
    target_compile_options(pengine_core PRIVATE /W4 /WX)
else()
    target_compile_options(pengine_core PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor
    )
endif()

# Sanitizers in Debug.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(pengine_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pengine_core PUBLIC -fsanitize=address,undefined)
endif()

# --- meshconv (uses Assimp; must NEVER be linked into pengine_core/pengine) --
add_executable(meshconv tools/meshconv/main.cpp)
target_include_directories(meshconv PRIVATE src)
target_compile_definitions(meshconv PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
target_link_libraries(meshconv PRIVATE assimp::assimp)
# meshconv is a host tool; no sanitizers, no strict warnings needed.
set_target_properties(meshconv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Executable -------------------------------------------------------------
add_executable(pengine src/main.cpp)
target_link_libraries(pengine PRIVATE pengine_core)

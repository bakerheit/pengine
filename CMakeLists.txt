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

# --- Engine library ---------------------------------------------------------
add_library(pengine_core STATIC
    src/platform/window.cpp
    src/platform/input.cpp
    src/Application.cpp
)

target_include_directories(pengine_core PUBLIC src)

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

# --- Executable -------------------------------------------------------------
add_executable(pengine src/main.cpp)
target_link_libraries(pengine PRIVATE pengine_core)

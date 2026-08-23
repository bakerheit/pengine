# gfx -> apricot_host. The only code in the engine allowed to call GL.
#
# Note what is NOT listed here: instance.h, primitives.h, sky_env.h and
# rain_field.h are header-only and GL-free on purpose, so the headless test
# suites can include them and exercise the real generators without a context.
target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera.cpp
)

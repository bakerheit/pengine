# gfx -> apricot_host. The only code in the engine allowed to call GL.
#
# Note what is NOT listed here: instance.h, primitives.h, sky_env.h,
# rain_field.h and glyph_atlas.h are header-only and GL-free on purpose, so the
# headless suites can include them and exercise the real generators without a
# window or a context. Keep them that way.
target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sky.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/precip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hud.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer.cpp
    # The far side of src/road/'s seam (PENG-27/PENG-32). src/road/ bakes six
    # plain vertex arrays and stops; this is the only place a road becomes a GPU
    # resource. It lives here and not there because that module owns no handle
    # and must never learn how to.
    ${CMAKE_CURRENT_SOURCE_DIR}/road_meshes.cpp
)

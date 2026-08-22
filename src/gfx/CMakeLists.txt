# gfx -> apricot_host. The only code in the engine allowed to call GL.
target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera.cpp
)

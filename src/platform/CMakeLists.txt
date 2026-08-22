# platform -> apricot_host. The window, the GL context, and the translation of
# raw device events into core::InputFrame.
target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input.cpp
)

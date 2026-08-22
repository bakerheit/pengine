# app -> the apricot executable. Wiring only: it owns the frame loop and the
# debug overlay, and holds no engine logic of its own.
target_sources(apricot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay.cpp
)

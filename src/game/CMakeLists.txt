# game -> apricot_sim. The pilot game's sim-side rules.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/character.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/climb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/save_game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/conditions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/wanted_system.cpp
)

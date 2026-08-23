# game -> apricot_sim. Rally rules: checkpoint route, lap timing, replay tape,
# ghost playback, conditions and the HUD's data model. No rendering: the HUD
# here is numbers, and something in src/gfx/ draws them.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/best_lap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/conditions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ghost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rally.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rally_hud.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/route.cpp
)

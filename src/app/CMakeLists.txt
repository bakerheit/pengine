# app -> the apricot executable. Wiring only: it owns the frame loop and the
# debug overlay, and holds no engine logic of its own.
#
# world.cpp is the host half of terrain streaming -- build, upload, deliver,
# free -- and nothing more. Which chunks exist, at what level, in what order and
# for how long is terrain/Streamer, which is sim-side precisely so a headless
# suite can drive it around a world for two thousand steps.
#
# It replaced demo_scene.{h,cpp} in PENG-27. That was a 420 m disc and 1400
# boxes, and its own header said in capitals that it was placeholder and meant
# to be deleted.
target_sources(apricot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_cutscene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_save.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cutscene_player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bank_interaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/boat_interaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/trailer_interaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/trailer_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/character_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/game_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/house_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/signal_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/traffic_horn_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player_car_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/police_gameplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/police_pursuit_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/traffic_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/character_identity_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vehicle_effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vehicle_interaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/weapon_visual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/weapon_check.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world.cpp
)

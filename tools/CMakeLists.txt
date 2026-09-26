# Reproducible vehicle documentation figures using the production simulation.
add_executable(apricot_vehicle_performance vehicle_performance.cpp)
target_compile_options(apricot_vehicle_performance PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_vehicle_performance PRIVATE apricot_sim)

# Native asset QA tool. It links the real host renderer so a preview exercises
# the same cooked mesh, texture, shader, culling and draw path as the game.
add_executable(apricot_asset_lab asset_lab.cpp)
target_include_directories(apricot_asset_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_asset_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_asset_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_asset_lab PRIVATE apricot_host)

# Focused skeletal QA. Unlike the static Asset Lab mode, this samples .eanim
# continuously and draws the bind mesh through the production skinning shader.
add_executable(apricot_character_lab character_lab.cpp)
target_include_directories(apricot_character_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_character_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_character_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_character_lab PRIVATE apricot_host)

# Render the production map at repeatable zoom levels and window shapes.
add_executable(apricot_map_lab map_lab.cpp ${CMAKE_SOURCE_DIR}/src/app/game_ui.cpp)
target_include_directories(apricot_map_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_map_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_map_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_map_lab PRIVATE apricot_host)

add_executable(apricot_intro_lab intro_lab.cpp ${CMAKE_SOURCE_DIR}/src/app/game_ui.cpp)
target_include_directories(apricot_intro_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_intro_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_intro_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_intro_lab PRIVATE apricot_host)
add_executable(apricot_mistral_driver_lab
    mistral_driver_lab.cpp ${CMAKE_SOURCE_DIR}/src/app/character_visual.cpp
    ${CMAKE_SOURCE_DIR}/src/app/player_car_visual.cpp)
target_include_directories(apricot_mistral_driver_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_mistral_driver_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_mistral_driver_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_mistral_driver_lab PRIVATE apricot_host)

add_executable(apricot_boat_driver_lab
    boat_driver_lab.cpp ${CMAKE_SOURCE_DIR}/src/app/character_visual.cpp)
target_include_directories(apricot_boat_driver_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_boat_driver_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_boat_driver_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_boat_driver_lab PRIVATE apricot_host)

# Standalone scene authoring and playback. Shares world loading without App.
# world.cpp builds the Bellwether town through bellwether.cpp, so the lab needs
# both halves of World.
add_executable(apricot_cutscene_lab cutscene_lab.cpp ${CMAKE_SOURCE_DIR}/src/app/world.cpp
    ${CMAKE_SOURCE_DIR}/src/app/bellwether.cpp)
target_include_directories(apricot_cutscene_lab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(apricot_cutscene_lab SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_compile_options(apricot_cutscene_lab PRIVATE ${APRICOT_STRICT_FLAGS})
target_link_libraries(apricot_cutscene_lab PRIVATE apricot_host)
if(APPLE)
    set(CUTSCENE_APP "${CMAKE_BINARY_DIR}/bin/Cutscene Studio.app")
    add_custom_command(TARGET apricot_cutscene_lab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CUTSCENE_APP}/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:apricot_cutscene_lab>"
            "${CUTSCENE_APP}/Contents/MacOS/apricot_cutscene_lab"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/cutscene_info.plist"
            "${CUTSCENE_APP}/Contents/Info.plist"
        VERBATIM)
endif()

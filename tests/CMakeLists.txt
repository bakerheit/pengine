# Headless test suites.
#
# Every suite links apricot_sim and ONLY apricot_sim. That is not a
# convenience, it is the architecture assertion: if a test ever needs a window,
# a GL context or an audio device to run, the logic under test is in the wrong
# library. Keep it that way and `ctest` stays green on a headless runner.

# Register a suite in one line:
#     apricot_add_suite(rng_determinism_tests)
# Extra sources (rare — prefer putting logic in a sim module) go after the name:
#     apricot_add_suite(foo_tests helper.cpp)
function(apricot_add_suite name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_options(${name} PRIVATE ${APRICOT_STRICT_FLAGS})
    target_link_libraries(${name} PRIVATE apricot_sim)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
apricot_add_suite(route1_elevation_tests)
apricot_add_suite(route1_traffic_tests)
apricot_add_suite(ostend_road_tests)
apricot_add_suite(traffic_signal_damage_tests)
apricot_add_suite(house_door_tests)

apricot_add_suite(rng_determinism_tests)
apricot_add_suite(fixed_step_tests)
apricot_add_suite(vehicle_interaction_tests)
apricot_add_suite(aircraft_tests)
apricot_add_suite(boat_tests)
apricot_add_suite(boat_driver_pose_tests)

# The engine's central claim: a recorded InputFrame tape replays bit for bit.
# Written against step_vehicle, TerrainCollider, InputFrame and FixedStep and
# NOTHING from game/, so the guarantee outlives whichever game is the sample.
# It already has: the rally that used to be the sample is gone and this suite
# did not move.
apricot_add_suite(sim_determinism_tests)

# game/ (PENG-23). What survived the rally: session conditions, which are a
# pure function of (seed, sim step) and feed VehicleTuning::grip_scale.
apricot_add_suite(conditions_tests)
apricot_add_suite(snowpack_tests)
apricot_add_suite(snow_clearance_tests)
apricot_add_suite(snow_shelter_tests)
apricot_add_suite(snow_shelter_render_tests)
apricot_add_suite(snowplow_traffic_tests)
apricot_add_suite(snowplow_service_tests)
apricot_add_suite(snowplow_visual_tests)
apricot_add_suite(vehicle_snow_shader_tests)
apricot_add_suite(vehicle_snow_mesh_tests)
apricot_add_suite(dev_weather_menu_tests)
apricot_add_suite(weather_hazards_tests)
apricot_add_suite(character_controller_tests)
apricot_add_suite(ui_flow_tests)
apricot_add_suite(minimap_tests)
apricot_add_suite(road_name_tests)
apricot_add_suite(audio_intro_runtime_tests)
apricot_add_suite(audio_rain_runtime_tests)
apricot_add_suite(cutscene_document_tests)
apricot_add_suite(cutscene_playback_tests)
apricot_add_suite(save_game_tests)
apricot_add_suite(cutscene_action_tests)
apricot_add_suite(delivery_mission_tests)
apricot_add_suite(delivery_cutscene_tests)

# gfx (PENG-5). These reach into src/gfx/ headers that are deliberately GL-free
# and header-only -- sky_env.h, rain_field.h, glyph_atlas.h, primitives.h,
# instance.h -- so the real generators the renderer uploads get exercised here
# rather than a hand-written copy of them. Nothing below needs a context.
apricot_add_suite(sky_env_tests)
apricot_add_suite(tiled_light_grid_tests)
apricot_add_suite(rain_field_tests)
apricot_add_suite(tire_track_field_tests)
apricot_add_suite(tornado_geometry_tests)
apricot_add_suite(glyph_atlas_tests)
apricot_add_suite(render_geometry_tests)
apricot_add_suite(render_batch_contract_tests)
apricot_add_suite(emesh_reader_tests)
apricot_add_suite(skeletal_animation_tests)
apricot_add_suite(math_tests)
apricot_add_suite(chase_camera_tests)
apricot_add_suite(input_latch_tests)
apricot_add_suite(rng_distribution_tests)
apricot_add_suite(terrain_determinism_tests)
apricot_add_suite(state_terrain_tests)

# city (PENG-41). Pinatty's map: district polygons, character parameters,
# landmarks and the terrain operators height_at() evaluates. The structural
# invariants are static_asserts in src/city/ -- that is the point of the map
# being compiled data -- so this suite pins the things a compiler cannot see:
# that the bucket index changes no answer, that the operators are pure, and
# that the districts are measurably different places on the ground. It PRINTS
# its measurements, because a map that passes every assertion can still be
# boring and only the numbers will tell you.
apricot_add_suite(city_map_tests)
apricot_add_suite(building_creator_tests)
apricot_add_suite(start_area_tests)
apricot_add_suite(authored_city_layout_tests)
apricot_add_suite(miandi_layout_tests)
apricot_add_suite(miandi_streets_tests)
apricot_add_suite(miandi_calle_ocho_tests)
apricot_add_suite(miandi_bayfront_tests)
apricot_add_suite(miandi_calle_noche_tests)
apricot_add_suite(miandi_context_tests)
apricot_add_suite(miandi_presentation_tests)
apricot_add_suite(miandi_resort_frontage_tests)
apricot_add_suite(miandi_mariposa_motel_tests)
apricot_add_suite(miandi_night_lighting_tests)
apricot_add_suite(miandi_ocean_drive_tests)
apricot_add_suite(miandi_port_sol_tests)
apricot_add_suite(miandi_prism_works_tests)
apricot_add_suite(miandi_sunwave_hotel_tests)
apricot_add_suite(miandi_integration_tests)
apricot_add_suite(tidewater_farm_tests)
apricot_add_suite(pinatty_infill_tests)
apricot_add_suite(marina_tests)
apricot_add_suite(bank_vault_tests)
apricot_add_suite(construction_site_tests)
apricot_add_suite(construction_neighbor_materials_tests)
apricot_add_suite(construction_neighbor_equipment_tests)
apricot_add_suite(construction_street_detail_tests)
apricot_add_suite(hospital_campus_tests)

# The road network (roads.h, spines.cpp) and whether it is a place you can
# drive. This suite runs the REAL chain end to end -- map_spines() into a real
# RoadGraph into a real ribbon bake, on the real terrain -- and it is where the
# drape-at-every-LOD measurement lives, because that measurement is only worth
# anything taken over the vertices the baker actually emitted.
apricot_add_suite(city_roads_tests)
apricot_add_suite(florangia_highway_tests)
apricot_add_suite(terrain_mesh_tests)
# LOD (PENG-27). The coarse lattice is a strict subset of the fine one, so the
# same-level seam is untouched; the DIFFERENT-level crack is real, and this
# suite measures it against the skirt that has to cover it rather than
# asserting a skirt merely exists.
apricot_add_suite(terrain_lod_tests)
apricot_add_suite(scene_cull_tests)
apricot_add_suite(streamer_tests)
# Streaming with LOD (PENG-27). Rings, in-place refits, cold fill and teleport,
# and the mesh lifetime -- the last of which no node-count assertion can see,
# because the node counts balance perfectly while the GPU fills up.
apricot_add_suite(streamer_lod_tests)
apricot_add_suite(terrain_collision_tests)
apricot_add_suite(vehicle_tests)
# Characterization matrix for extreme player inputs across every live handling
# profile. Outcome labels are intentionally observational until a human accepts
# them; determinism and finite state are hard requirements from day one.
apricot_add_suite(driving_extremes_tests)

# road (PENG-32). The road graph, the ribbon bake and the lane graph, all
# sim-side: the bake emits plain vertex arrays and owns no GPU resource, which
# is exactly why these suites can run it headless. tests/road_fixture.h holds
# the hand-made spine set standing in for the map tables (PENG-41).
apricot_add_suite(road_graph_tests)
apricot_add_suite(road_ribbon_tests)
apricot_add_suite(lane_graph_tests)

# audio (PENG-8). All three link only apricot_sim: gain routing, PCM
# generation and the whole render loop are device-free by construction, so
# these measure real waveforms — spectra, zero crossings, seams — on a runner
# with no sound card. tests/audio_analysis.h holds the measurement tools.
apricot_add_suite(audio_mixer_tests)
apricot_add_suite(audio_synth_tests)
apricot_add_suite(audio_engine_tone_tests)
apricot_add_suite(audio_vehicle_runtime_tests)
apricot_add_suite(traffic_horn_audio_tests)
apricot_add_suite(audio_city_runtime_tests)

# city (PENG-29). The Pinatty decision layers lifted out of probablecause, with
# the tests that were pinning them there. They came across because a lifted
# algorithm with no test is a liability: these are the only thing that says the
# behaviour survived the move.
apricot_add_suite(ped_separation_tests)
apricot_add_suite(ped_reaction_tests)
apricot_add_suite(character_punch_tests)
apricot_add_suite(character_getup_tests)
apricot_add_suite(traffic_ai_tests)
apricot_add_suite(police_ai_tests)
apricot_add_suite(police_offense_tests)
apricot_add_suite(police_combat_tests)
apricot_add_suite(police_officer_tests)
apricot_add_suite(police_arrest_tests)
apricot_add_suite(police_character_pose_tests)
apricot_add_suite(wanted_system_tests)
apricot_add_suite(weather_tests)
apricot_add_suite(lightning_tests)
apricot_add_suite(objective_runtime_tests)
apricot_add_suite(road_author_tests)
apricot_add_suite(road_fixture_tests)
apricot_add_suite(vehicle_driver_pose_tests)
apricot_add_suite(mistral_soft_top_tests)
apricot_add_suite(emergency_lighting_tests)
apricot_add_suite(island_coast_tests)
apricot_add_suite(building_access_tests)
apricot_add_suite(police_siren_tests)

# traffic (PENG-26). The ambient population: whether a city's worth of agents
# fits in a 120 Hz step, and whether sub-rate scheduling survives contact with
# a streamed world. traffic_determinism_tests carries the negative controls --
# the index-keyed sub-rate schedule is required to DIVERGE -- and
# traffic_bench measures. tests/traffic_harness.h is the rig both drive.
apricot_add_suite(traffic_determinism_tests)
apricot_add_suite(traffic_runtime_tests)
apricot_add_suite(police_runtime_tests)
apricot_add_suite(police_chase_tests)
apricot_add_suite(emergency_traffic_tests)
apricot_add_suite(civilian_maneuver_tests)
apricot_add_suite(traffic_junction_tests)
apricot_add_suite(traffic_behavior_tests)
apricot_add_suite(traffic_bench)

# Full-size commercial bus asset and selectable catalog contract.
apricot_add_suite(harrow_cityliner_tests)

# Terminal furnishings preserve clear entrance routes and readable signage.
apricot_add_suite(airport_terminal_tests)
apricot_add_suite(airport_circulation_tests)
apricot_add_suite(florangia_airport_tests)

# Quickbite drive-through turning and pedestrian clearances.
apricot_add_suite(quickbite_tests)
apricot_add_suite(quickbite_door_tests)
apricot_add_suite(tacomaco_tests)

# Real on-foot movement through the authored restaurant threshold and doorway.
apricot_add_suite(quickbite_interior_tests)
apricot_add_suite(interior_streaming_tests)

# Persistent leak depletion and engine failure.
apricot_add_suite(vehicle_mechanical_tests)

# Actual controller routes through the convenience-store entrance and aisles.
apricot_add_suite(gas_store_interior_tests)

apricot_add_suite(weapon_wheel_tests)

# New pawn-shop parcel clearance and actual customer/staff walking routes.
apricot_add_suite(pawn_shop_tests)
apricot_add_suite(gun_store_tests)

apricot_add_suite(repair_shop_tests)

# Nearby skyline landmarks preserve authored streets and foot access.
apricot_add_suite(neighborhood_towers_tests)
apricot_add_suite(construction_expansion_tests)
apricot_add_suite(skyscraper_window_lighting_tests)

# Emergency stations use real lowered curbs, vehicle bays and public entrances.
apricot_add_suite(emergency_stations_tests)

apricot_add_suite(neighborhood_bar_tests)
apricot_add_suite(loom_cultural_tests)
apricot_add_suite(north_airbase_tests)
apricot_add_suite(burgerpiz_tests)
apricot_add_suite(miandi_gas_station_tests)
apricot_add_suite(east_arm_plaza_tests)
apricot_add_suite(drunk_tests)
apricot_add_suite(alder_pip_tests)
apricot_add_suite(fang_venom_tests)

apricot_add_suite(vehicle_leak_warning_tests)

# Distinct sports/limo mass, lamps, body/door assets and actual driving.
apricot_add_suite(new_vehicle_models_tests)
apricot_add_suite(municipal_cruiser_91_tests)
apricot_add_suite(vehicle_model_tuning_tests)
apricot_add_suite(airport_parking_garage_tests)
apricot_add_suite(vehicle_sound_volume_tests)
apricot_add_suite(residential_neighborhood_tests)
apricot_add_suite(luxury_neighborhood_tests)
apricot_add_suite(westmere_streetscape_tests)
apricot_add_suite(traffic_signal_mesh_tests)
apricot_add_suite(sycamore_freeway_tests)

apricot_add_suite(tractor_trailer_tests)

# Pedestrians do more than walk: loitering, kerb waiting, panic and knockdown.
apricot_add_suite(ped_life_tests)
apricot_add_suite(ragdoll_tests)
apricot_add_suite(ragdoll_bench)

# The lifted PSX clip set, the crossfade, and the character state machine that
# calls city/character_punch.h and city/character_getup.h. app-side but
# header-only, so this links apricot_sim like every other suite.
apricot_add_suite(character_animation_tests)

# .pop.parked reaches the kerb: authored parked-car density per district.
apricot_add_suite(parked_density_tests)
apricot_add_suite(parked_corridor_tests)

apricot_add_suite(weapon_use_tests)
apricot_add_suite(weapon_visual_pose_tests)
apricot_add_suite(weapon_pose_tests)
apricot_add_suite(weapon_hit_tests)
apricot_add_suite(weapon_aim_tests)
apricot_add_suite(blood_particles_tests)
apricot_add_suite(ped_impact_pose_tests)

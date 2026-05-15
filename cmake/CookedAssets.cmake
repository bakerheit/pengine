# Wires meshconv into the build graph.
#
# Source art under assets/<type>/<pack>/... is cooked into engine binary
# formats under assets/models/<type>/<basename>{.emesh,.eskel,.eanim}.
#
# Three intent functions — one per asset shape:
#   cook_static_mesh(<source> <out_basename>)    → .emesh
#   cook_skinned_mesh(<source> <out_basename>)   → .emesh + .eskel + .eanim
#   cook_animation(<source> <out_basename>)      → .eanim
#
# The shape is a property of the source file, not the developer's intent.
# An FBX with a skinned mesh + bones + embedded animation produces all three
# files — that's true whether the source is conceptually "a character" or
# "an animation that happens to ship a placeholder mesh." A true Mixamo
# in-place export (no mesh at all) produces only .eanim. Pick the intent
# that matches what meshconv writes when you cook the source by hand.
#
# Add a new asset: append one line to the manifest below. The aggregate target
# `pengine_assets` depends on every cooked output; `pengine` depends on
# `pengine_assets`, so building the game cooks anything stale.

set(PENGINE_ASSETS_SRC_DIR ${CMAKE_SOURCE_DIR}/assets)
set(PENGINE_ASSETS_OUT_DIR ${CMAKE_SOURCE_DIR}/assets/models)
set(PENGINE_COOKED_ASSETS "")

function(cook_static_mesh src_rel out_basename)
    set(src_abs "${PENGINE_ASSETS_SRC_DIR}/${src_rel}")
    set(out_abs "${PENGINE_ASSETS_OUT_DIR}/${out_basename}")
    get_filename_component(out_dir "${out_abs}" DIRECTORY)

    add_custom_command(
        OUTPUT "${out_abs}.emesh"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND $<TARGET_FILE:meshconv> "${src_abs}" "${out_abs}"
        DEPENDS "${src_abs}" $<TARGET_FILE:meshconv>
        COMMENT "Cooking ${src_rel}"
        VERBATIM
    )

    list(APPEND PENGINE_COOKED_ASSETS "${out_abs}.emesh")
    set(PENGINE_COOKED_ASSETS "${PENGINE_COOKED_ASSETS}" PARENT_SCOPE)
endfunction()

function(cook_skinned_mesh src_rel out_basename)
    set(src_abs "${PENGINE_ASSETS_SRC_DIR}/${src_rel}")
    set(out_abs "${PENGINE_ASSETS_OUT_DIR}/${out_basename}")
    get_filename_component(out_dir "${out_abs}" DIRECTORY)

    add_custom_command(
        OUTPUT
            "${out_abs}.emesh"
            "${out_abs}.eskel"
            "${out_abs}.eanim"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND $<TARGET_FILE:meshconv> "${src_abs}" "${out_abs}"
        DEPENDS "${src_abs}" $<TARGET_FILE:meshconv>
        COMMENT "Cooking ${src_rel}"
        VERBATIM
    )

    list(APPEND PENGINE_COOKED_ASSETS
        "${out_abs}.emesh"
        "${out_abs}.eskel"
        "${out_abs}.eanim"
    )
    set(PENGINE_COOKED_ASSETS "${PENGINE_COOKED_ASSETS}" PARENT_SCOPE)
endfunction()

function(cook_animation src_rel out_basename)
    set(src_abs "${PENGINE_ASSETS_SRC_DIR}/${src_rel}")
    set(out_abs "${PENGINE_ASSETS_OUT_DIR}/${out_basename}")
    get_filename_component(out_dir "${out_abs}" DIRECTORY)

    add_custom_command(
        OUTPUT "${out_abs}.eanim"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND $<TARGET_FILE:meshconv> "${src_abs}" "${out_abs}"
        DEPENDS "${src_abs}" $<TARGET_FILE:meshconv>
        COMMENT "Cooking ${src_rel}"
        VERBATIM
    )

    list(APPEND PENGINE_COOKED_ASSETS "${out_abs}.eanim")
    set(PENGINE_COOKED_ASSETS "${PENGINE_COOKED_ASSETS}" PARENT_SCOPE)
endfunction()

# ============================================================================
# Asset manifest — source-of-truth for what gets shipped in the game.
# Adding an asset: append one line in the right section.
# ============================================================================

# --- Static meshes ----------------------------------------------------------
cook_static_mesh("vehicles/Vehicles_psx/Car 05/Car5.obj" "vehicles/car5")
cook_static_mesh("vehicles/Vehicles_psx/Car 08/Car8.obj" "vehicles/car8")
cook_static_mesh("vehicles/Vehicles_psx/Wheel/Wheel.obj" "vehicles/wheel")
cook_static_mesh("weapons/Glock 17 Gen 4/Glock17.fbx"    "weapons/glock17")

# --- Skinned meshes (produce .emesh + .eskel + .eanim) ---------------------
# Both characters and animation FBXs land here whenever the source carries a
# skinned mesh — meshconv writes the trio regardless of whether the source's
# conceptual purpose was "the character" or "the animation."
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_01.fbx"          "characters/character_01")
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_03.fbx"          "characters/ped_03")
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_07.fbx"          "characters/ped_07")
cook_skinned_mesh("characters/Characters_psx/Models/Male/Character_17_Police.fbx"   "characters/ped_17_police")
cook_skinned_mesh("characters/Characters_psx/Models/Female/Character_Female_03.fbx" "characters/ped_f03")
cook_skinned_mesh("characters/Characters_psx/Models/Female/Character_Female_07.fbx" "characters/ped_f07")
cook_skinned_mesh("characters/Characters_psx/Animations/Breathing Idle.fbx"         "characters/breathing_idle")
cook_skinned_mesh("characters/Characters_psx/Animations/Sprint.fbx"                 "characters/sprint")
cook_skinned_mesh("characters/Characters_psx/Animations/Walking.fbx"                "characters/walking")
cook_skinned_mesh("characters/Characters_psx/Animations/Dying Backwards.fbx"        "characters/dying_backwards")
cook_skinned_mesh("characters/Characters_psx/Animations/Dying Forwards.fbx"         "characters/dying_forwards")
cook_skinned_mesh("characters/Characters_psx/Animations/Hit By Car.fbx"             "characters/hit_by_car")

# --- True animation-only sources (Mixamo in-place exports, no mesh) --------
cook_animation("characters/Characters_psx/Animations/Pistol_Handgun Locomotion Pack/pistol idle.fbx" "characters/pistol_idle")
cook_animation("characters/Characters_psx/Animations/Pistol_Handgun Locomotion Pack/pistol run.fbx"  "characters/pistol_run")
cook_animation("characters/Characters_psx/Animations/Pistol_Handgun Locomotion Pack/pistol walk.fbx" "characters/pistol_walk")

# ============================================================================
# Aggregate target.
# Built by default (ALL). `pengine` depends on it so a build always cooks.
# ============================================================================

add_custom_target(pengine_assets ALL DEPENDS ${PENGINE_COOKED_ASSETS})
add_dependencies(pengine pengine_assets)

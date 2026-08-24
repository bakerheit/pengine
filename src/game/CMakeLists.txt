# game -> apricot_sim. The pilot game's sim-side rules.
#
# Currently only session conditions: time of day, weather, and the grip they
# leave the tyres. Pinatty (docs/design/pinatty.md) has no code here yet.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/conditions.cpp
)

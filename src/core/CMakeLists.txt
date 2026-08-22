# core -> apricot_sim. Foundations: logging, determinism, maths, the
# fixed-step clock and the InputFrame POD that every other module speaks.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/asset_root.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fixed_step.cpp
)

# audio is the one SPLIT module.
#
#   mixer.h + synth.cpp   pure PCM generation and gain maths -> apricot_sim
#   device.cpp            the miniaudio playback device      -> apricot_host
#
# The split is the point: gain maths and waveform generation are headless-
# testable and must never drag an audio device into the sim library.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/synth.cpp
)

target_sources(apricot_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_impl.c
)

# Third-party C expanded into one TU; it will never be warning-clean under our
# flags and it is not ours to fix.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/miniaudio_impl.c
    PROPERTIES COMPILE_OPTIONS "-w")

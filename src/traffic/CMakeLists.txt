# traffic -> apricot_sim. The ambient population: the analytic phantom schedule
# every lane carries, and the bounded active set instantiated out of it.
#
# It sits ABOVE road/ and city/ and below nothing. road/ answers where a lane
# is; city/ answers what a driver would decide; this answers who exists, where
# they are at step t, and which of them is worth stepping. Neither of the two
# below knows this module exists, and that is the direction the dependency has
# to run: a lane graph that knew about traffic could not be tested without it.
#
# Owns no GPU resource and never will. It writes transforms into a Scene, which
# is itself sim-side and hands the host layer opaque handles.
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ambient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crowd.cpp
)

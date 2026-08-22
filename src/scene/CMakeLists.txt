# scene -> apricot_sim. Node storage, transforms, culling and draw-batch
# planning. Plans draws; never issues them (that is gfx).
target_sources(apricot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/draw_batch.cpp
)

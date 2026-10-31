# GPX Ride 3D view: the Route3D prototype's world (terrain, road, houses,
# trees, sky, rider) as a Qt Quick item (QQuickFramebufferObject, OpenGL /
# OpenGL ES). Geometry headers are plain C++ (namespace route3d).
qt_add_library(watt_route3d STATIC
    route.h
    terrain.h
    world_noise.h
    vegetation.h
    settlement.h
    ghost_motion.h
    ghost_rider.h
    ghost_vertex.h
    ghost_runner.h
    glb_model.h glb_model.cpp
    materials.h
    Route3DScene.h Route3DScene.cpp
    Route3DItem.h Route3DItem.cpp
)
target_include_directories(watt_route3d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_route3d PUBLIC watt_core Qt6::Gui Qt6::OpenGL Qt6::Quick)

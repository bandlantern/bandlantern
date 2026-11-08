add_library(bl_app STATIC playback_controller.cpp)
target_include_directories(bl_app PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(bl_app PUBLIC bl_api)

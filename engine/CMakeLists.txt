add_subdirectory(api)
add_subdirectory(app)
add_subdirectory(tests)

add_executable(bandlantern-engine main.cpp)
target_link_libraries(bandlantern-engine PRIVATE bl_app)

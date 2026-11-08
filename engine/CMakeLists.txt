add_subdirectory(api)
add_subdirectory(app)
# Libraries added by separate pieces of work; each owns its own directory (docs/dev/WORKFLOW.md §2.1).
foreach(dir io buffer)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
    add_subdirectory(${dir})
  endif()
endforeach()
add_subdirectory(tests)

add_executable(bandlantern-engine main.cpp)
target_link_libraries(bandlantern-engine PRIVATE bl_app)

# SPSC ring tests (piece: engine/buffer). rt-alloc-tests replaces the global operator new, so it
# is its own executable.
add_executable(buffer-tests buffer_ring_test.cpp)
target_link_libraries(buffer-tests PRIVATE bl_buffer Catch2::Catch2WithMain)
catch_discover_tests(buffer-tests)

add_executable(rt-alloc-tests rt_alloc_test.cpp)
target_link_libraries(rt-alloc-tests PRIVATE bl_buffer bl_api Catch2::Catch2WithMain)
catch_discover_tests(rt-alloc-tests)

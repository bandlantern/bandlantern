find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(engine-tests hello_test.cpp)
target_link_libraries(engine-tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(engine-tests)

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(engine-tests hello_test.cpp api_frame_test.cpp api_server_test.cpp)
target_link_libraries(engine-tests PRIVATE bl_api Catch2::Catch2WithMain)
target_compile_definitions(engine-tests PRIVATE
  BL_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
  BL_API_SCHEMA="${PROJECT_SOURCE_DIR}/docs/api/schema/api.schema.json")
catch_discover_tests(engine-tests)

add_executable(io-tests io_sigmf_test.cpp io_wav_test.cpp)
target_link_libraries(io-tests PRIVATE bl_io Catch2::Catch2WithMain)
target_compile_definitions(io-tests PRIVATE BL_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata")
catch_discover_tests(io-tests)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(bl_io STATIC sigmf.cpp wav.cpp open_iq_file.cpp)
target_include_directories(bl_io PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(bl_io PUBLIC nlohmann_json::nlohmann_json)

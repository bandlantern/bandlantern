add_library(bl_buffer INTERFACE)
target_include_directories(bl_buffer INTERFACE ${PROJECT_SOURCE_DIR}/engine)
target_compile_features(bl_buffer INTERFACE cxx_std_20)

cmake_minimum_required(VERSION 3.25)
project(bandlantern VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /bigobj)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_subdirectory(engine)

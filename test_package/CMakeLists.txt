cmake_minimum_required(VERSION 3.0)

project(PackageTest CXX)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tconcurrent_test test.cpp)
conan_target_link_libraries(tconcurrent_test)

cmake_minimum_required(VERSION 3.4)

project(tconcurrent)

include(boost-config)

add_library(tconcurrent
  src/periodic_task.cpp
  src/thread_pool.cpp
)
set_property(TARGET tconcurrent PROPERTY CXX_STANDARD 14)
set_property(TARGET tconcurrent PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(tconcurrent PUBLIC include)
target_link_libraries(tconcurrent PUBLIC boost_system boost_thread)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

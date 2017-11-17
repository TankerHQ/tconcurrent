cmake_minimum_required(VERSION 3.4)

project(tconcurrent)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

find_package(Boost 1.61 REQUIRED COMPONENTS system thread context)

add_library(tconcurrent
  src/coroutine.cpp
  src/delay.cpp
  src/periodic_task.cpp
  src/thread_pool.cpp
)
target_include_directories(tconcurrent PUBLIC include)
target_link_libraries(tconcurrent PUBLIC
  Boost::system
  Boost::thread
  Boost::context
)
target_compile_definitions(tconcurrent PRIVATE
  TCONCURRENT_USE_THREAD_LOCAL=$<COMPILE_FEATURES:cxx_thread_local>
)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

cmake_minimum_required(VERSION 3.4)

project(tconcurrent)

find_package(Boost REQUIRED COMPONENTS system thread context)
include(curl-config)

add_library(tconcurrent
  src/coroutine.cpp
  src/delay.cpp
  src/periodic_task.cpp
  src/thread_pool.cpp

  src/curl/curl.cpp
  src/curl/read_all.cpp
)
set_property(TARGET tconcurrent PROPERTY CXX_STANDARD 14)
set_property(TARGET tconcurrent PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(tconcurrent PUBLIC include)
target_link_libraries(tconcurrent PUBLIC
  Boost::system
  Boost::thread
  Boost::context
  curl
)
target_compile_definitions(tconcurrent PRIVATE
  TCONCURRENT_USE_THREAD_LOCAL=$<COMPILE_FEATURES:cxx_thread_local>
)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

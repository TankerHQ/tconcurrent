cmake_minimum_required(VERSION 3.4)

project(tconcurrent)

include(boost-config)
include(curl-config)

add_library(tconcurrent
  src/coroutine.cpp
  src/periodic_task.cpp
  src/thread_pool.cpp

  src/curl/curl.cpp
  src/curl/read_all.cpp
)
set_property(TARGET tconcurrent PROPERTY CXX_STANDARD 14)
set_property(TARGET tconcurrent PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(tconcurrent PUBLIC include)
target_link_libraries(tconcurrent PUBLIC boost_system boost_thread boost_context curl)
target_compile_definitions(tconcurrent PRIVATE
  TCONCURRENT_USE_THREAD_LOCAL=$<COMPILE_FEATURES:cxx_thread_local>
)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

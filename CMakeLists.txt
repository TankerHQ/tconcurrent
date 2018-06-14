cmake_minimum_required(VERSION 3.4)

project(tconcurrent)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(Boost 1.61 REQUIRED COMPONENTS system thread context)
find_package(CURL REQUIRED)

add_library(tconcurrent
  src/async_wait.cpp
  src/barrier.cpp
  src/executor.cpp
  src/periodic_task.cpp
  src/stackful_coroutine.cpp
  src/stepper.cpp
  src/thread_pool.cpp

  src/curl/curl.cpp
  src/curl/read_all.cpp
)

target_include_directories(tconcurrent PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(tconcurrent PUBLIC
  # Keep components, to avoid linking with everything
  Boost::system
  Boost::thread
  Boost::context
  CONAN_PKG::libcurl
  CONAN_PKG::enum-flags
)

target_compile_definitions(tconcurrent PRIVATE
  TCONCURRENT_USE_THREAD_LOCAL=$<COMPILE_FEATURES:cxx_thread_local>
)

install(TARGETS tconcurrent
  EXPORT tconcurrent
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(DIRECTORY include
  DESTINATION .
)

install(EXPORT tconcurrent
  DESTINATION lib/cmake/tconcurrent
  FILE "tconcurrent-config.cmake"
  NAMESPACE tconcurrent::
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

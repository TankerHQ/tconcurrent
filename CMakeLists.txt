cmake_minimum_required(VERSION 3.4)

project(tconcurrent)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

find_package(Boost 1.61 REQUIRED COMPONENTS system thread context)

find_package(CURL REQUIRED)
add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}"
  IMPORTED_LOCATION "${CURL_LIBRARIES}")

add_library(tconcurrent
  src/coroutine.cpp
  src/delay.cpp
  src/periodic_task.cpp
  src/thread_pool.cpp

  src/curl/curl.cpp
  src/curl/read_all.cpp
)

target_include_directories(tconcurrent PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(tconcurrent PUBLIC
  Boost::system
  Boost::thread
  Boost::context
  curl
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
  add_subdirectory(test)
endif()

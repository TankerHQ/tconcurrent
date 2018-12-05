cmake_minimum_required(VERSION 3.4)

project(tconcurrent)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


set(tconcurrent_SRC
  src/barrier.cpp
  src/periodic_task.cpp
  src/stepper.cpp
)

set(tconcurrent_LIBS
)

if (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  list(APPEND tconcurrent_LIBS
    CONAN_PKG::Boost
    CONAN_PKG::enum-flags
  )
  list(APPEND tconcurrent_SRC
    src/async_wait_emscripten.cpp
    src/executor_emscripten.cpp
  )
else()
  find_package(Boost 1.61 REQUIRED COMPONENTS system thread context)

  list(APPEND tconcurrent_SRC
    src/async_wait.cpp
    src/executor.cpp
    src/stackful_coroutine.cpp
    src/thread_pool.cpp
  )
  list(APPEND tconcurrent_LIBS
    # Keep components, to avoid linking with everything
    Boost::system
    Boost::thread
    Boost::context
    CONAN_PKG::enum-flags
  )
endif()

add_library(tconcurrent ${tconcurrent_SRC})

target_include_directories(tconcurrent PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(tconcurrent PUBLIC ${tconcurrent_LIBS})

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

include(CTest)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()

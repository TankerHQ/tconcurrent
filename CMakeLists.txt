cmake_minimum_required(VERSION 3.4)

project(tconcurrent)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


set(tconcurrent_SRC
  include/tconcurrent/async.hpp
  include/tconcurrent/async_wait.hpp
  include/tconcurrent/barrier.hpp
  include/tconcurrent/cancelation_token.hpp
  include/tconcurrent/concurrent_queue.hpp
  include/tconcurrent/coroutine.hpp
  include/tconcurrent/detail/boost_fwd.hpp
  include/tconcurrent/detail/export.hpp
  include/tconcurrent/detail/shared_base.hpp
  include/tconcurrent/detail/util.hpp
  include/tconcurrent/executor.hpp
  include/tconcurrent/future.hpp
  include/tconcurrent/future_group.hpp
  include/tconcurrent/job.hpp
  include/tconcurrent/packaged_task.hpp
  include/tconcurrent/periodic_task.hpp
  include/tconcurrent/promise.hpp
  include/tconcurrent/semaphore.hpp
  include/tconcurrent/stackful_coroutine.hpp
  include/tconcurrent/stackless_coroutine.hpp
  include/tconcurrent/stepper.hpp
  include/tconcurrent/task_auto_canceler.hpp
  include/tconcurrent/thread_pool.hpp
  include/tconcurrent/when.hpp
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
  list(APPEND tconcurrent_SRC
    src/async_wait.cpp
    src/executor.cpp
    src/stackful_coroutine.cpp
    src/thread_pool.cpp
  )
  list(APPEND tconcurrent_LIBS
    CONAN_PKG::Boost
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

cmake_minimum_required(VERSION 3.10)

project(tconcurrent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

option(WITH_COVERAGE "Enable coverage" OFF)
option(TCONCURRENT_SANITIZER "Enable sanitizer support" OFF)

# CMAKE_C_FLAGS and the like are _strings_, not lists.
# So, we need a macro so that we can rewrite the values
# in place, and avoid appending the flags twice
macro(tconcurrent_add_flags var flags)
  string(FIND "${${var}}" ${flags} _res)
  if(${_res} EQUAL "-1")
    set(${var} "${${var}} ${flags}")
  endif()
endmacro()

if(${WITH_COVERAGE})
  if(WIN32)
    message(WARNING "WITH_COVERAGE ignored on Windows")
  else()
    message(STATUS "Building with coverage")
    tconcurrent_add_flags(CMAKE_C_FLAGS "--coverage")
    tconcurrent_add_flags(CMAKE_CXX_FLAGS "--coverage")
    tconcurrent_add_flags(CMAKE_EXE_LINKER_FLAGS "--coverage")
    tconcurrent_add_flags(CMAKE_SHARED_LINKER_FLAGS "--coverage")
    tconcurrent_add_flags(CMAKE_MODULE_LINKER_FLAGS "--coverage")
  endif()
endif()

find_package(Boost CONFIG REQUIRED)
find_package(enum-flags CONFIG REQUIRED)
find_package(function2 CONFIG REQUIRED)

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
  include/tconcurrent/task_canceler.hpp
  include/tconcurrent/thread_pool.hpp
  include/tconcurrent/when.hpp
  src/barrier.cpp
  src/periodic_task.cpp
  src/stackless_coroutine.cpp
  src/stepper.cpp
)

set(tconcurrent_LIBS
  Boost
  enum-flags::enum-flags
  function2::function2
)

if (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
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
endif()

add_library(tconcurrent ${tconcurrent_SRC})

if(TCONCURRENT_SANITIZER)
  target_compile_definitions(tconcurrent PUBLIC TCONCURRENT_SANITIZER)
endif()

target_include_directories(tconcurrent PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(tconcurrent PUBLIC ${tconcurrent_LIBS})

# In CMake 3.15, an optimization was introduced to avoid testing C++ supported features
# Unfortunately, some of those features (e.g. thread_local) require runtime components.
# Thus, the detection yields a wrong result on 32-bit iOS Simulators
# Those checks are based on a manual testing of all archs/ios versions
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.15 AND APPLE)
  set(_use_thread_local 1)
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "i386" AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.0)
    set(_use_thread_local 0)
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 8.0)
    set(_use_thread_local 0)
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm" AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 9.0)
    set(_use_thread_local 0)
  endif()
  target_compile_definitions(tconcurrent PRIVATE
    TCONCURRENT_USE_THREAD_LOCAL=${_use_thread_local}
  )
else()
  target_compile_definitions(tconcurrent PRIVATE
    TCONCURRENT_USE_THREAD_LOCAL=$<COMPILE_FEATURES:cxx_thread_local>
  )
endif()

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

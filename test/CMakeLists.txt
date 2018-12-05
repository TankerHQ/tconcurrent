add_library(tconcurrent_doctest_main STATIC
  main.cpp
)
target_link_libraries(tconcurrent_doctest_main PUBLIC CONAN_PKG::doctest)
if (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_compile_definitions(tconcurrent_doctest_main PUBLIC DOCTEST_CONFIG_NO_POSIX_SIGNALS)
endif()

set(test_tconcurrent_SRC
  test_async.cpp
  test_async_wait.cpp
  test_concurrent_queue.cpp
  test_future.cpp
  test_future_group.cpp
  test_job.cpp
  test_packaged_task.cpp
  test_periodic_task.cpp
  test_semaphore.cpp
  test_when.cpp
)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  list(APPEND test_tconcurrent_SRC
    test_coroutine.cpp
    test_thread_pool.cpp
  )
endif()

add_executable(test_tconcurrent ${test_tconcurrent_SRC})
target_link_libraries(test_tconcurrent tconcurrent tconcurrent_doctest_main)

add_test(NAME test_tconcurrent COMMAND test_tconcurrent)

if (TCONCURRENT_COROUTINES_TS)
  add_executable(test_coroutinests
    test_coroutine.cpp
  )
  target_link_libraries(test_coroutinests tconcurrent tconcurrent_doctest_main)
  target_compile_options(test_coroutinests PUBLIC -fcoroutines-ts)
  target_compile_definitions(test_coroutinests PUBLIC TCONCURRENT_COROUTINES_TS)
endif()

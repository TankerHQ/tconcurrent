enable_testing()

include(ExternalProject)
ExternalProject_Add(doctest_project
  PREFIX doctest
  SOURCE_DIR doctest/source
  DOWNLOAD_DIR doctest/source
  DOWNLOAD_NO_EXTRACT 1
  URL https://raw.githubusercontent.com/blastrock/doctest/threadsafe_singleheader/doctest/doctest.h
  URL_HASH SHA256=7bbe31706f2074d130acdb4bf24286b84bdd2a38ea5289a0053f4eacee878a17
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND "")
add_library(doctest INTERFACE)
set_property(TARGET doctest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/doctest/source)
add_dependencies(doctest doctest_project)

add_library(tconcurrent_doctest_main STATIC
  main.cpp
)
target_link_libraries(tconcurrent_doctest_main PUBLIC doctest)

add_executable(test_tconcurrent
  test_async.cpp
  test_async_wait.cpp
  test_concurrent_queue.cpp
  test_coroutine.cpp
  test_future.cpp
  test_future_group.cpp
  test_packaged_task.cpp
  test_periodic_task.cpp
  test_semaphore.cpp
  test_thread_pool.cpp
  test_when.cpp
)

add_executable(test_tconcurrent_curl
  test_curl.cpp
)

target_link_libraries(test_tconcurrent tconcurrent tconcurrent_doctest_main)
target_link_libraries(test_tconcurrent_curl tconcurrent tconcurrent_doctest_main)

add_test(NAME test_tconcurrent COMMAND test_tconcurrent)

include(catch-config)

add_library(tconcurrent_catch_main STATIC
  main.cpp
)
target_link_libraries(tconcurrent_catch_main PUBLIC catch)

add_executable(test_tconcurrent
  test.cpp
  test_coroutine.cpp
)

add_executable(test_tconcurrent_curl
  test_curl.cpp
)

target_link_libraries(test_tconcurrent tconcurrent tconcurrent_catch_main)
target_link_libraries(test_tconcurrent_curl tconcurrent tconcurrent_catch_main)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/results/test")
add_catch_test(NAME test_tconcurrent COMMAND test_tconcurrent)

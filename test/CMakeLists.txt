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

set_target_properties(test_tconcurrent PROPERTIES CXX_STANDARD 14)
set_target_properties(test_tconcurrent PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(test_tconcurrent_curl PROPERTIES CXX_STANDARD 14)
set_target_properties(test_tconcurrent_curl PROPERTIES CXX_EXTENSIONS OFF)

add_test(
  NAME test_tconcurrent
  COMMAND test_tconcurrent --reporter junit
                           --out "${CMAKE_BINARY_DIR}/test_tconcurrent.xml"
)
set_tests_properties(test_tconcurrent
  PROPERTIES
    TIMEOUT 60
)

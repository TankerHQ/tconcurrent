include(catch-config)

add_executable(test_tconcurrent
  main.cpp
  test.cpp
)

set_target_properties(test_tconcurrent PROPERTIES CXX_STANDARD 14)
set_target_properties(test_tconcurrent PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(test_tconcurrent tconcurrent catch)
add_test(test_tconcurrent test_tconcurrent)

enable_testing()

if (TANKER_ENVIRONMENT)
  include(catch-config)
else()
  include(ExternalProject)
  ExternalProject_Add(catch_project
    PREFIX catch
    SOURCE_DIR catch/source
    GIT_REPOSITORY https://github.com/philsquared/Catch
    GIT_TAG v1.5.0
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")
  add_library(catch INTERFACE)
  set_property(TARGET catch PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/catch/source/include)
  add_dependencies(catch catch_project)
endif()

add_library(tconcurrent_catch_main STATIC
  main.cpp
)
target_link_libraries(tconcurrent_catch_main PUBLIC catch)

add_executable(test_tconcurrent
  test.cpp
  test_coroutine.cpp
)

target_link_libraries(test_tconcurrent tconcurrent tconcurrent_catch_main)

if (TANKER_ENVIRONMENT)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/results/test")
  add_catch_test(NAME test_tconcurrent COMMAND test_tconcurrent)
else()
  add_test(NAME test_tconcurrent COMMAND test_tconcurrent)
endif()

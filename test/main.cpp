#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <tconcurrent/executor.hpp>

int main(int argc, char* argv[])
{
  doctest::Context context(argc, argv);
  auto const ret = context.run();

  tconcurrent::shutdown();
  return ret;
}

#include <doctest.h>

#include <tconcurrent/delay.hpp>

using namespace tconcurrent;

TEST_CASE("async_wait should get ready after a delay [waiting]")
{
  std::chrono::milliseconds const delay{100};
  auto before = std::chrono::steady_clock::now();
  auto fut = async_wait(delay);
  fut.wait();
  auto after = std::chrono::steady_clock::now();
  CHECK(delay < after - before);
}

TEST_CASE("async_wait should be instantly cancelable")
{
  std::chrono::milliseconds const delay{100};
  auto before = std::chrono::steady_clock::now();
  auto fut = async_wait(delay);
  fut.request_cancel();
  fut.wait();
  auto after = std::chrono::steady_clock::now();
  CHECK(delay > after - before);
}

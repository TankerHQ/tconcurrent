#include <doctest/doctest.h>

#include <tconcurrent/async.hpp>
#include <tconcurrent/promise.hpp>

using namespace tconcurrent;

TEST_CASE("async should run a task asynchronously")
{
  bool hasrun = false;
  auto fut = async([&] { hasrun = true; });
  CHECK_NOTHROW(fut.get());
  CHECK(hasrun);
}

TEST_CASE("async's cancelation token should expose cancelation requests")
{
  promise<void> prom;
  auto fut = async([&](cancelation_token& token) {
    prom.set_value({});
    while (!token.is_cancel_requested())
      ;
  });
  prom.get_future().get();
  fut.request_cancel();
  fut.get();
}

TEST_CASE("canceling async before it runs should prevent run")
{
  async([&] {
    auto fut = async([&] { REQUIRE(false); });
    fut.request_cancel();
    REQUIRE(fut.is_ready());
    CHECK_THROWS_AS(fut.get(), operation_canceled);
  })
      .get();
}

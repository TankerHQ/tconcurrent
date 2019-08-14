#undef __GTHREADS

#include <tconcurrent/lazy/async_wait.hpp>
#include <tconcurrent/lazy/sync_wait.hpp>
#include <tconcurrent/lazy/then.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/async_wait.hpp>

#include <doctest.h>

#include <iostream>

using namespace tconcurrent;
using namespace std::chrono_literals;

namespace
{
auto run_async()
{
  return [](auto p) {
    get_default_executor().post(
        [p = std::move(p)]() mutable { p.set_value(); });
  };
}

template <typename T>
auto async_algo(T task)
{
  return lazy::then(task, [] { return 10; });
}
}

TEST_CASE("lazy")
{
  auto n = run_async();
  auto f = async_algo(n);
  auto f2 = lazy::then(f, [](int i) { return 10 + i; });
  auto f3 = lazy::async_then(f2, [](auto& p, int i) {
    lazy::then(lazy::async_wait(get_default_executor(), 100ms),
               [i] { return i + 10; })(p);
  });
  auto f4 = lazy::async_then(f3, [](auto& p, int i) {
    lazy::then(lazy::async_wait(get_default_executor(), 100ms),
               [i] { return i + 10; })(p);
  });
  lazy::cancelation_token c;
  CHECK(lazy::sync_wait<int>(f4, c) == 40);
  async_wait(50ms).then([&](auto const&) { c.request_cancel(); });
  // async_wait(150ms).then([&](auto const&) { c.request_cancel(); });
  CHECK_THROWS_AS(lazy::sync_wait<int>(f4, c), lazy::operation_canceled);
}

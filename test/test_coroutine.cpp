#include <catch.hpp>

#include <tconcurrent/coroutine.hpp>
#include <tconcurrent/delay.hpp>
#include <tconcurrent/promise.hpp>
#include <tconcurrent/thread_pool.hpp>

using namespace tconcurrent;

TEST_CASE("coroutine empty", "[coroutine]")
{
  auto f = async_resumable([](auto& awaiter) {});
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine return", "[coroutine]")
{
  auto f = async_resumable([](auto& awaiter) { return 42; });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine throw", "[coroutine]")
{
  auto f = async_resumable([](auto& awaiter) { throw 42; });
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine wait ready", "[coroutine]")
{
  auto ready = make_ready_future();
  auto f = async_resumable([&](auto& await) {
    await(ready);
    return 42;
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine global wait ready", "[coroutine]")
{
  auto ready = make_ready_future();
  auto f = async_resumable([&](auto& await) {
    await(ready);
    return 42;
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait ready value", "[coroutine]")
{
  auto ready = make_ready_future(42);
  auto f = async_resumable([&](auto& await) { return await(ready); });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait", "[coroutine]")
{
  promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    return 42;
  });
  prom.set_value({});
  CHECK(42 == f.get());
}

TEST_CASE("coroutine nested", "[coroutine]")
{
  promise<int> prom;
  auto f = async_resumable([&](auto& await) {
    return await(
        async_resumable([&](auto& await) { return await(prom.get_future()); }));
  });
  prom.set_value(42);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait and throw", "[coroutine]")
{
  promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    throw 42;
  });
  prom.set_value({});
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine wait error", "[coroutine]")
{
  promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    return 42;
  });
  prom.set_exception(std::make_exception_ptr(42));
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine cancel propagation", "[coroutine][cancel]")
{
  unsigned called = 0;
  promise<void> prom;
  prom.get_cancelation_token().set_cancelation_callback([&] { ++called; });
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    return 42;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  f.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
  CHECK(1 == called);
  prom.set_value({});
  CHECK_THROWS_AS(f.get(), operation_canceled);
}

#include <catch.hpp>

#include <tconcurrent/coroutine.hpp>
#include <iostream>
#include <tconcurrent/delay.hpp>
#include <tconcurrent/promise.hpp>
#include <tconcurrent/thread_pool.hpp>

using namespace tconcurrent;

TEST_CASE("coroutine empty", "")
{
  auto f = async_resumable([](auto& awaiter) {});
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine return", "")
{
  auto f = async_resumable([](auto& awaiter) { return 42; });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine throw", "")
{
  auto f = async_resumable([](auto& awaiter) { throw 42; });
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine wait ready", "")
{
  auto ready = make_ready_future();
  auto f = async_resumable([&](auto& await) {
    await(ready);
    return 42;
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine global wait ready", "")
{
  auto ready = make_ready_future();
  auto f = async_resumable([&](auto& await) {
    tconcurrent::await(ready);
    return 42;
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait ready value", "")
{
  auto ready = make_ready_future(42);
  auto f = async_resumable([&](auto& await) { return await(ready); });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait", "")
{
  tconcurrent::promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    return 42;
  });
  prom.set_value(0);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine nested", "")
{
  tconcurrent::promise<int> prom;
  auto f = async_resumable([&](auto& await) {
    return await(
        async_resumable([&](auto& await) { return await(prom.get_future()); }));
  });
  prom.set_value(42);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait and throw", "")
{
  tconcurrent::promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    throw 42;
  });
  prom.set_value(0);
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine wait error", "")
{
  tconcurrent::promise<void> prom;
  auto f = async_resumable([&](auto& await) {
    await(prom.get_future());
    return 42;
  });
  prom.set_exception(std::make_exception_ptr(42));
  CHECK_THROWS_AS(f.get(), int);
}

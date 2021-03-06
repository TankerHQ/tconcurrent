#include <doctest/doctest.h>

#include <tconcurrent/async_wait.hpp>
#include <tconcurrent/coroutine.hpp>
#include <tconcurrent/lazy/sync_wait.hpp>
#include <tconcurrent/promise.hpp>
#include <tconcurrent/stepper.hpp>

#ifndef EMSCRIPTEN
#include <tconcurrent/thread_pool.hpp>
#endif

using namespace tconcurrent;

TEST_CASE("coroutine return")
{
  auto f = async_resumable([]() -> cotask<int> { TC_RETURN(42); });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine throw")
{
  auto f = async_resumable([]() -> cotask<void> {
    throw 42;
    TC_RETURN();
  });
  CHECK_THROWS_AS(f.get(), int);
}

#ifndef EMSCRIPTEN
TEST_CASE("coroutine on executor")
{
  thread_pool tp;
  tp.start(1);
  auto f = async_resumable("test", executor(tp), [&]() -> cotask<void> {
    CHECK(tp.is_in_this_context());
    TC_YIELD();
    CHECK(tp.is_in_this_context());
    TC_AWAIT(tc::async(tp, [] {}));
    CHECK(tp.is_in_this_context());
  });
  f.get();
}
#endif

TEST_CASE("coroutine run on main thread")
{
  auto const stack_frame = tc::async([] {
                             int dummy;
                             // This arithmetic operation silences a warning
                             // from clang saying that we are returning the
                             // address of a local variable.
                             auto const ptr =
                                 reinterpret_cast<intptr_t>(&dummy) + 1;
                             return ptr;
                           }).get();
  auto f = async_resumable([&]() -> cotask<void> {
    auto const result = tc::dispatch_on_thread_context([&] {
      int dummy;
      auto const nested_stack_frame = reinterpret_cast<intptr_t>(&dummy);

      auto const diff = nested_stack_frame - stack_frame;
      // check that the stack frame is close to the main one
      CHECK(std::abs(diff) < 0x1000);

      return 42;
    });
    CHECK(result == 42);
    TC_RETURN();
  });
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine run on main thread with void return type")
{
  // This is just a compilation test
  tc::dispatch_on_thread_context([] {});
}

namespace
{
struct move_only
{
  move_only() = default;
  move_only(move_only&&) = default;
  move_only& operator=(move_only&&) = default;
  move_only(move_only const&) = delete;
  move_only& operator=(move_only const&) = delete;
  void operator()()
  {
  }
};
}

TEST_CASE("coroutine run on main thread with a move-only lambda")
{
  // This is just a compilation test
  tc::dispatch_on_thread_context(move_only{});
}

TEST_CASE("coroutine wait ready")
{
  auto ready = make_ready_future();
  auto f = async_resumable([&]() -> cotask<int> {
    TC_AWAIT(std::move(ready));
    TC_RETURN(42);
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine global wait ready")
{
  auto f = async_resumable([&]() -> cotask<int> {
    TC_AWAIT(make_ready_future());
    TC_RETURN(42);
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait ready value")
{
  auto f = async_resumable(
      [&]() -> cotask<int> { TC_RETURN(TC_AWAIT(make_ready_future(42))); });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait ready move-only value")
{
  struct S
  {
    S(S const&) = delete;
    S& operator=(S const&) = delete;

    S(S&&) = default;
    S& operator=(S&&) = default;

    S() = default;
  };

  auto f = async_resumable(
      [&]() -> cotask<S> { TC_RETURN(TC_AWAIT(make_ready_future(S{}))); });
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine wait ready non default-constructible value")
{
  struct S
  {
    S(int)
    {
    }
  };

  auto f = async_resumable(
      [&]() -> cotask<S> { TC_RETURN(TC_AWAIT(make_ready_future(S{18}))); });
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine wait")
{
  promise<void> prom;
  auto f = async_resumable([&]() -> cotask<int> {
    TC_AWAIT(prom.get_future());
    TC_RETURN(42);
  });
  prom.set_value({});
  CHECK(42 == f.get());
}

// this one is there to trigger valgrind if lifetimes aren't respected
TEST_CASE("coroutine wait, cb on heap")
{
  promise<void> prom;
  auto const cb = [&, i = 42]() -> cotask<int> {
    TC_AWAIT(prom.get_future());
    TC_RETURN(i);
  };
  auto heap_cb = std::make_unique<decltype(cb)>(cb);

  auto f = async_resumable(*heap_cb);
  // expect no crash or valgrind error
  heap_cb = nullptr;

  prom.set_value({});
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait ready shared future")
{
  auto ready = make_ready_future(42).to_shared();
  auto f = async_resumable([&]() -> cotask<int> {
    CHECK(42 == TC_AWAIT(ready));
    CHECK(42 == TC_AWAIT(ready));
    TC_RETURN(42);
  });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine wait non-ready shared future")
{
  promise<int> prom;
  auto f = async_resumable([&]() -> cotask<int> {
    auto fut = prom.get_future().to_shared();
    CHECK(42 == TC_AWAIT(fut));
    CHECK(42 == TC_AWAIT(fut));
    TC_RETURN(42);
  });
  prom.set_value(42);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine nested")
{
  promise<int> prom;
  auto f = async_resumable([&]() -> cotask<int> {
    TC_RETURN(TC_AWAIT(async_resumable(
        [&]() -> cotask<int> { TC_RETURN(TC_AWAIT(prom.get_future())); })));
  });
  prom.set_value(42);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine nested cotask")
{
  promise<int> prom;
  auto f = async_resumable([&]() -> cotask<int> {
    TC_RETURN(TC_AWAIT(
        [&]() -> cotask<int> { TC_RETURN(TC_AWAIT(prom.get_future())); }()));
  });
  prom.set_value(42);
  CHECK(42 == f.get());
}

TEST_CASE("coroutine nested cotask<T&>")
{
  auto f = async_resumable([&]() -> cotask<void> {
             int i = 0;
             int& ii = TC_AWAIT([&]() -> cotask<int&> { TC_RETURN(i); }());
             CHECK(&i == &ii);
           }).get();
}

TEST_CASE("coroutine nested void cotask")
{
  promise<void> prom;
  auto f = async_resumable([&]() -> cotask<void> {
    TC_AWAIT([&]() -> cotask<void> { TC_AWAIT(prom.get_future()); }());
  });
  prom.set_value({});
  CHECK_NOTHROW(f.get());
}

TEST_CASE("coroutine wait and throw")
{
  promise<void> prom;
  auto f = async_resumable([&]() -> cotask<void> {
    TC_AWAIT(prom.get_future());
    throw 42;
  });
  prom.set_value({});
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine wait error")
{
  promise<void> prom;
  auto f = async_resumable([&]() -> cotask<int> {
    TC_AWAIT(prom.get_future());
    TC_RETURN(42);
  });
  prom.set_exception(std::make_exception_ptr(42));
  CHECK_THROWS_AS(f.get(), int);
}

TEST_CASE("coroutine cancel before run")
{
  unsigned called = 0;
  async([&] {
    auto f = async_resumable([&called]() -> cotask<int> {
      ++called;
      TC_RETURN(42);
    });
    f.request_cancel();
    REQUIRE(f.is_ready());
    CHECK_THROWS_AS(f.get(), operation_canceled);
  }).get();
  CHECK(0 == called);
}

TEST_CASE("coroutine cancel already started")
{
  stepper step;
  promise<void> never_ready_prom;
  auto never_ready = never_ready_prom.get_future();
  auto coroutine_future = async_resumable([&]() -> cotask<int> {
    step(2);
    TC_AWAIT(std::move(never_ready));
    TC_RETURN(42);
  });
  auto fut3 = async([&] {
    step(3);
    coroutine_future.request_cancel();
    REQUIRE(coroutine_future.is_ready());
    CHECK_THROWS_AS(coroutine_future.get(), operation_canceled);
  });
  step(1);
  fut3.get();
}

TEST_CASE("coroutine cancel already started from another coroutine")
{
  stepper step;
  promise<void> never_ready_prom;
  auto never_ready = never_ready_prom.get_future();
  auto coroutine_future = async_resumable([&]() -> cotask<int> {
    step(2);
    TC_AWAIT(std::move(never_ready));
    TC_RETURN(42);
  });
  auto fut3 = async_resumable([&]() -> cotask<void> {
    step(3);
    coroutine_future.request_cancel();
    REQUIRE(coroutine_future.is_ready());
    CHECK_THROWS_AS(coroutine_future.get(), operation_canceled);
    TC_RETURN();
  });
  step(1);
  fut3.get();
}

TEST_CASE("coroutine cancel propagation")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  prom.get_cancelation_token().push_cancelation_callback([&] { ++called; });
  // also test that we can pass mutable callback to async_resumable
  auto f = async_resumable([&fut]() mutable -> cotask<int> {
    TC_AWAIT(std::move(fut));
    TC_RETURN(42);
  });
  async([&] {
    f.request_cancel();
    CHECK(prom.get_cancelation_token().is_cancel_requested());
    CHECK(f.is_ready());
    CHECK(1 == called);
  }).get();
  prom.set_value({});
  CHECK_THROWS_AS(f.get(), operation_canceled);
}

TEST_CASE("coroutine yield")
{
  unsigned progress = 0;
  tc::promise<void> prom;
  auto fut = prom.get_future();
  auto fut1 = tc::async_resumable([&]() -> cotask<void> {
    ++progress;
    fut.wait();
    TC_YIELD();
    ++progress;
  });
  // this will be scheduled during the yield
  auto fut2 = tc::async([&] { CHECK(1 == progress); });
  prom.set_value({});
  fut2.get();
  fut1.get();
  CHECK(2 == progress);
}

TEST_CASE("coroutine yield cancel before yield")
{
  std::atomic<unsigned> progress{0};
  tc::promise<void> prom;
  auto fut = prom.get_future();
  auto fut1 = tc::async_resumable([&]() -> cotask<void> {
    ++progress;
    fut.wait();
    TC_YIELD();
    ++progress;
  });
  while (progress.load() != 1)
    ;
  fut1.request_cancel();
  prom.set_value({});
  CHECK(1 == progress);
  CHECK_THROWS_AS(fut1.get(), operation_canceled);
}

TEST_CASE("coroutine yield cancel on yield")
{
  std::atomic<unsigned> progress{0};
  auto prom = tc::promise<void>();
  tc::future<void> fut1;
  fut1 = tc::async_resumable([&]() -> cotask<void> {
    tc::async([&] {
      if (++progress != 2)
        CHECK(!"test failed");
      fut1.request_cancel();
      CHECK(fut1.is_ready());
      prom.set_value({});
    });
    if (++progress != 1)
      CHECK(!"test failed");
    TC_YIELD();
    ++progress;
  });
  prom.get_future().wait();
  CHECK(2 == progress);
  CHECK_THROWS_AS(fut1.get(), operation_canceled);
}

TEST_CASE("coroutine await move-only type")
{
  tc::promise<std::unique_ptr<int>> prom;
  auto fut = prom.get_future();
  auto finished = tc::async_resumable([&]() -> cotask<void> {
    auto const ptr = TC_AWAIT(std::move(fut));
    REQUIRE(ptr);
    CHECK(42 == *ptr);
  });
  prom.set_value(std::make_unique<int>(42));
  finished.get();
}

TEST_CASE("coroutine by lazy run_resumable")
{
  auto sender = lazy::run_resumable(
      get_default_executor(),
      {},
      [](int i) -> cotask<int> { TC_RETURN(i); },
      42);
  static_assert(std::is_same_v<decltype(sender)::value_types<std::tuple>,
                               std::tuple<int>>,
                "run_resumable must deduce the correct type from the lambda");
  lazy::cancelation_token c;
  CHECK(42 == lazy::sync_wait(std::move(sender), c));
}

TEST_CASE("coroutine void by lazy run_resumable")
{
  auto sender = lazy::run_resumable(
      get_default_executor(), {}, []() -> cotask<void> { TC_RETURN(); });
  static_assert(
      std::is_same_v<decltype(sender)::value_types<std::tuple>, std::tuple<>>,
      "run_resumable must deduce the correct type from the lambda");
  lazy::cancelation_token c;
  CHECK_NOTHROW(lazy::sync_wait(std::move(sender), c));
}

TEST_CASE("coroutine by lazy async_resumable")
{
  auto sender =
      lazy::async_resumable([i = 42]() -> cotask<int> { TC_RETURN(i); });
  static_assert(std::is_same_v<decltype(sender)::value_types<std::tuple>,
                               std::tuple<int>>,
                "run_resumable must deduce the correct type from the lambda");
  lazy::cancelation_token c;
  CHECK(42 == lazy::sync_wait(std::move(sender), c));
}

TEST_CASE("coroutine await lazy sender")
{
  auto sender =
      lazy::then(lazy::async(get_default_executor()), [] { return 42; });
  auto f = async_resumable(
      [sender]() mutable -> cotask<int> { TC_RETURN(TC_AWAIT(sender)); });
  CHECK(42 == f.get());
}

TEST_CASE("coroutine await lazy sender void")
{
  auto sender = lazy::async(get_default_executor());
  auto f =
      async_resumable([sender]() mutable -> cotask<void> { TC_AWAIT(sender); });
  CHECK_NOTHROW(f.get());
}

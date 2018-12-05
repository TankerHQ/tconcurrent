#include <doctest.h>

#include <tconcurrent/async.hpp>
#include <tconcurrent/async_wait.hpp>
#include <tconcurrent/periodic_task.hpp>
#include <tconcurrent/promise.hpp>
#ifndef EMSCRIPTEN
#include <tconcurrent/thread_pool.hpp>
#endif

#include <thread>

using namespace tconcurrent;

TEST_CASE("test periodic task construct")
{
  periodic_task pt;
}

TEST_CASE("test periodic task stop")
{
  periodic_task pt;
  pt.stop();
}

TEST_CASE("test periodic task [waiting]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&] { ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start();
  CHECK(pt.is_running());
  async_wait(std::chrono::milliseconds(450)).get();
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 >= called);
  CHECK(3 <= called);
}

TEST_CASE("test periodic task future [waiting]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&] {
    return async_wait(std::chrono::milliseconds(10))
        .and_then(get_synchronous_executor(), [&](tvoid) { ++called; });
  });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start();
  CHECK(pt.is_running());
  async_wait(std::chrono::milliseconds(500)).get();
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 >= called);
  CHECK(3 <= called);
}

TEST_CASE("test periodic task immediate [waiting]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&] { ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(50)).get();
  CHECK(pt.is_running());
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

#ifndef EMSCRIPTEN
TEST_CASE("test periodic task executor [waiting]")
{
  thread_pool tp;
  tp.start(1);

  bool fail{false};
  unsigned int called = 0;
  periodic_task pt;
  pt.set_executor(tp);
  pt.set_callback([&] {
    if (!tp.is_in_this_context())
      fail = true;
    ++called;
  });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start(periodic_task::start_immediately);
  CHECK(pt.is_running());
  async_wait(std::chrono::milliseconds(450)).get();
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 <= called);
  CHECK(5 >= called);
  CHECK(!fail);
}

TEST_CASE("test periodic task error stop [waiting]")
{
  thread_pool tp;
  tp.start(1);

  unsigned int called = 0;
  unsigned int goterror = 0;
  std::exception_ptr holdIt;

  tp.set_error_handler([&](std::exception_ptr const& e) {
    ++goterror;
    holdIt = e;
  });

  periodic_task pt;
  pt.set_executor(tp);
  pt.set_callback([&] {
    ++called;
    throw 18;
  });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(50)).get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
  REQUIRE(1 == goterror);
  CHECK_THROWS_AS(std::rethrow_exception(holdIt), int);
}

TEST_CASE("test periodic task future error stop [waiting]")
{
  thread_pool tp;
  tp.start(1);

  unsigned int called = 0;
  unsigned int goterror = 0;
  std::exception_ptr holdIt;

  tp.set_error_handler([&](std::exception_ptr const& e) {
    ++goterror;
    holdIt = e;
  });

  periodic_task pt;
  pt.set_executor(tp);
  pt.set_callback([&] {
    ++called;
    return make_exceptional_future<void>(18);
  });
  pt.set_period(std::chrono::milliseconds(1));
  pt.start();
  CHECK(pt.is_running());
  async_wait(std::chrono::milliseconds(50)).get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
  REQUIRE(1 == goterror);
  CHECK_THROWS_AS(std::rethrow_exception(holdIt), int);
}
#endif

TEST_CASE("test periodic task stop before start")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&] { ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start();
  CHECK(pt.is_running());
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(0 == called);
}

template <typename C>
void test_periodic_task_start_stop_spam(C&& cb)
{
  periodic_task pt;
  pt.set_callback(std::forward<C>(cb));
  pt.set_period(std::chrono::milliseconds(10));
  auto do_start_stop = [&] {
    for (unsigned int i = 0; i < 1000; ++i)
    {
      try
      {
        pt.start(periodic_task::start_immediately);
      }
      catch (...)
      {
      }
      pt.stop().get();
    }
  };
  static auto const NB_THREADS = 16;
  std::vector<std::thread> th;
  th.reserve(NB_THREADS);
  for (unsigned int i = 0; i < NB_THREADS; ++i)
    th.emplace_back(do_start_stop);
  for (auto& t : th)
    t.join();
  pt.stop().get();
}

TEST_CASE("test periodic task start stop spam [waiting]")
{
  // can't use catch in other threads...
  std::atomic<bool> call{false};
  std::atomic<bool> fail{false};

  test_periodic_task_start_stop_spam([&] {
    if (call.exchange(true))
      fail = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!call.exchange(false))
      fail = true;
  });

  CHECK(false == fail.load());
  CHECK(false == call.load());
}

TEST_CASE("test periodic task future start stop spam [waiting]")
{
  // can't use catch in other threads...
  std::atomic<bool> call{false};
  std::atomic<bool> fail{false};

  test_periodic_task_start_stop_spam([&] {
    if (call.exchange(true))
      fail = true;
    return async_wait(std::chrono::milliseconds(1))
        .then([&](future<void> const&) {
          if (!call.exchange(false))
            fail = true;
        });
  });

  CHECK(false == fail.load());
  CHECK(false == call.load());
}

TEST_CASE("test periodic task stop from inside [waiting]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&] {
    ++called;
    pt.stop();
  });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(10)).get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

#ifndef EMSCRIPTEN
TEST_CASE("test periodic single threaded task stop")
{
  thread_pool tp;
  tp.start(1);
  unsigned int called = 0;

  periodic_task pt;
  pt.set_executor(tp);
  pt.set_callback([&] { ++called; });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  CHECK_NOTHROW(async(tp, [&] { pt.stop().get(); }).get());
  CHECK(!pt.is_running());
  CHECK(0 < called);
}
#endif

TEST_CASE("test periodic task cancel [waiting]")
{
  promise<void> prom;

  periodic_task pt;
  pt.set_callback([&] { return prom.get_future(); });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  CHECK(!prom.get_cancelation_token().is_cancel_requested());
  async_wait(std::chrono::milliseconds(100)).get();
  auto stopfut = pt.stop();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
  CHECK(!stopfut.is_ready());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  stopfut.get();
  CHECK(!pt.is_running());
}

TEST_CASE("test periodic task cancel no-propagation [waiting]")
{
  periodic_task pt;
  pt.set_callback([&] {});
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(100)).get();
  auto stopfut = pt.stop();

  unsigned called = 0;
  auto fut2 = stopfut.then([&](cancelation_token& token, tc::future<void>) {
    CHECK(!token.is_cancel_requested());
    ++called;
  });

  fut2.get();
  CHECK(1 == called);
}

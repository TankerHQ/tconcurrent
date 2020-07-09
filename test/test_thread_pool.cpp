#include <doctest/doctest.h>

#include <tconcurrent/thread_pool.hpp>

#include <iostream>

using namespace tconcurrent;

TEST_CASE("test thread_pool do nothing")
{
  thread_pool tp;
  tp.run_thread();
}

TEST_CASE("test thread_pool start stop")
{
  thread_pool tp;
  tp.start(1);
  tp.stop();
}

TEST_CASE("test thread_pool run work")
{
  bool called = false;

  thread_pool tp;
  tp.start(1);
  tp.post([&] { called = true; });
  tp.stop();
  CHECK(called);
}

TEST_CASE("test thread_pool error work")
{
  bool called = false;

  thread_pool tp;
  tp.set_error_handler([&](std::exception_ptr const& e) {
    called = true;
    CHECK_THROWS_AS(std::rethrow_exception(e), int);
  });

  tp.start(1);
  tp.post([&] { throw 18; });
  tp.stop();
  CHECK(called);
}

TEST_CASE("test thread_pool task trace [waiting]")
{
  bool called = false;
  static auto const TaskName = "Little bobby";
  static auto const WaitTime = std::chrono::milliseconds(100);

  thread_pool tp;
  tp.set_task_trace_handler(
      [&](std::string const& name, std::chrono::steady_clock::duration dur) {
        CHECK(TaskName == name);
        CHECK(WaitTime <= dur);
        called = true;
      });

  tp.start(1);
  tp.post([] { std::this_thread::sleep_for(WaitTime); }, TaskName);
  tp.stop();
  CHECK(called);
}

TEST_CASE("is_in_this_context should work")
{
  thread_pool tp;
  CHECK_FALSE(tp.is_in_this_context());
  tp.post([&] { CHECK(get_default_executor().is_in_this_context()); });
}

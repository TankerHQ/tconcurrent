#include <tconcurrent/task_canceler.hpp>

#include <tconcurrent/async.hpp>

#include <doctest/doctest.h>

using namespace tconcurrent;

TEST_SUITE_BEGIN("task_canceler");

TEST_CASE("create and destroy task_canceler")
{
  task_canceler tc;
}

TEST_CASE("add a task that completes")
{
  task_canceler tc;
  auto fut = tc.run([] { return async([] {}); });
  fut.get();
}

TEST_CASE("terminate cancels tasks")
{
  task_canceler tc;

  promise<void> prom;
  auto tcfut =
      tc.run([fut = prom.get_future()]() mutable { return std::move(fut); });

  tc.terminate();

  CHECK(prom.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("destroying the task_canceler cancels tasks")
{
  promise<void> prom;

  {
    task_canceler tc;

    auto tcfut =
        tc.run([fut = prom.get_future()]() mutable { return std::move(fut); });
  }

  CHECK(prom.get_cancelation_token().is_cancel_requested());
}

TEST_SUITE_END();

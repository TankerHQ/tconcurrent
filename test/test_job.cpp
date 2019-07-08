#include <tconcurrent/job.hpp>

#include <tconcurrent/async_wait.hpp>
#include <tconcurrent/barrier.hpp>
#include <tconcurrent/stepper.hpp>

#include <doctest.h>

using namespace tconcurrent;

TEST_CASE("create and destroy job")
{
  job t([] { return make_ready_future(); });
}

TEST_CASE("trigger job once")
{
  int called = 0;
  job t([&] {
    ++called;
    return make_ready_future();
  });
  t.trigger().get();
  CHECK(called == 1);
}

TEST_CASE("trigger job with future once")
{
  int called = 0;
  job t([&] { return async([&] { ++called; }); });
  t.trigger().get();
  CHECK(called == 1);
}

TEST_CASE("trigger job a second time while it runs")
{
  barrier b1(2);

  int called = 0;
  job t([&] {
    return async([&] {
      if (called == 0)
        b1();
      ++called;
    });
  });
  auto fut1 = t.trigger();
  b1();
  auto fut2 = t.trigger();
  CHECK_NOTHROW(fut1.get());
  CHECK_NOTHROW(fut2.get());
  CHECK(called == 2);
}

TEST_CASE(
    "triggering job a third time during the first run should run the job only "
    "twice")
{
  barrier b1(2);

  int called = 0;
  job t([&] {
    return async([&] {
      // only block the first job
      if (called == 0)
        b1();
      ++called;
    });
  });
  auto fut1 = t.trigger();
  // one or both of the following two triggers should be discarded
  auto fut2 = t.trigger();
  auto fut3 = t.trigger();
  b1();
  CHECK_NOTHROW(fut1.get());
  CHECK_NOTHROW(fut2.get());
  CHECK_NOTHROW(fut3.get());
  // the job should have run once or twice depending on your luck, but not
  // three times
  CHECK(called < 3);
}

struct abort_test
{
};

TEST_CASE("trigger should forward the task exception")
{
  job t([&] {
    throw abort_test{};
    return make_ready_future();
  });

  auto fut = t.trigger();
  CHECK_THROWS_AS(fut.get(), abort_test);
}

TEST_CASE("trigger should forward the asynchronous task exception")
{
  job t([&] { return make_exceptional_future<void>(abort_test{}); });

  auto fut = t.trigger();
  CHECK_THROWS_AS(fut.get(), abort_test);
}

TEST_CASE("trigger_success should not get ready when the task fails")
{
  job t([&] {
    throw "fail";
    return make_ready_future();
  });

  auto fut = t.trigger_success();
  t.trigger().wait();
  CHECK(!fut.is_ready());
}

TEST_CASE(
    "trigger_success should not get ready when the task fails asynchronously")
{
  job t([&] { return make_exceptional_future<void>("fail"); });

  auto fut = t.trigger_success();
  t.trigger().wait();
  CHECK(!fut.is_ready());
}

TEST_CASE("trigger_success should get ready when the task succeeds")
{
  job t([&] { return make_ready_future(); });
  t.trigger_success().get();
}

TEST_CASE("trigger_success should not get ready until the task succeeds")
{
  bool doThrow = true;
  job t([&] {
    if (doThrow)
      throw "fail";
    return make_ready_future();
  });

  auto fut = t.trigger_success();
  t.trigger().wait();

  CHECK(!fut.is_ready());

  doThrow = false;
  t.trigger().get();
  CHECK(fut.is_ready());
}

TEST_CASE(
    "trigger_success should not get ready for a task that's already running")
{
  stepper step;

  int called = 0;
  job t([&] {
    ++called;
    if (called == 1)
    {
      step(1);
      step(4);
    }
    else if (called == 2)
    {
      step(6);
    }
    return make_ready_future();
  });

  auto fut = t.trigger_success();
  step(2);
  auto fut2 = t.trigger_success();
  step(3);
  fut.get();
  CHECK(!fut2.is_ready());
  step(5);
}

TEST_CASE("canceling should be instantaneous")
{
  async([&] {
    int called = 0;
    job t([&] { return async([&] { ++called; }); });
    auto fut = t.trigger();
    fut.request_cancel();
    CHECK(fut.is_ready());
    CHECK(called == 0);
  })
      .get();
}

TEST_CASE("job never runs more than once [waiting]")
{
  std::atomic<bool> failed{false};
  std::atomic<bool> calling{false};
  std::atomic<int> nbcall{0};

  tc::promise<void> done;

  std::shared_ptr<job> t = std::make_shared<job>([&] {
    if (calling)
    {
      failed = true;
    }
    calling = true;
    ++nbcall;
    tc::future<void> fut = async_wait(std::chrono::milliseconds(50));
    for (int i = 0; i < 10; ++i)
      fut = fut.and_then(tc::get_synchronous_executor(),
                         [](auto const&) {
                           return async_wait(std::chrono::milliseconds(50));
                         })
                .unwrap();
    return fut.and_then(tc::get_synchronous_executor(),
                        [&](auto const&) { calling = false; });
  });

  std::function<void(tc::future<void>)> const reschedule =
      [&](tc::future<void> fut) {
        if (nbcall < 10)
        {
          fut.then(reschedule);
          t->trigger();
        }
        else
        {
          t = nullptr;
          done.set_value({});
        }
      };

  tc::make_ready_future().then(reschedule);

  done.get_future().get();

  CHECK(!failed);
}

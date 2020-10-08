#include <tconcurrent/lazy/task_canceler.hpp>

#include <tconcurrent/executor.hpp>
#include <tconcurrent/lazy/async.hpp>
#include <tconcurrent/lazy/sink_receiver.hpp>
#include <tconcurrent/lazy/sync_wait.hpp>
#include <tconcurrent/lazy/then.hpp>

#include <doctest/doctest.h>

#include "utils.hpp"

using namespace tconcurrent;

TEST_SUITE_BEGIN("lazy::task_canceler");

TEST_CASE("create and destroy task_canceler")
{
  lazy::task_canceler tc;
}

TEST_CASE("add a task that completes")
{
  lazy::task_canceler tc;
  auto sender = tc.wrap(
      lazy::then(lazy::async(get_default_executor()), [] { return 42; }));
  static_assert(std::is_same_v<decltype(sender)::value_types<std::tuple>,
                               std::tuple<int>>,
                "tack_canceler must deduce the correct type from the sender");

  lazy::cancelation_token c;
  CHECK(sync_wait(sender, c) == 42);
}

TEST_CASE("terminate cancels tasks")
{
  lazy::task_canceler tc;

  bool canceled = false;

  auto sender = tc.wrap(make_sender<void>([&canceled](auto&& receiver) {
    receiver.get_cancelation_token()->set_canceler(
        // capture receiver to keep it alive
        [&canceled, receiver]() mutable {
          canceled = true;
          receiver.set_done();
        });
  }));

  sender.submit(lazy::sink_receiver{});

  tc.terminate();

  CHECK(canceled == true);
}

TEST_CASE("destroying the task_canceler cancels tasks")
{
  bool canceled = false;

  {
    lazy::task_canceler tc;
    auto sender = tc.wrap(make_sender<void>([&canceled](auto&& receiver) {
      receiver.get_cancelation_token()->set_canceler(
          // capture receiver to keep it alive
          [&canceled, receiver]() mutable {
            canceled = true;
            receiver.set_done();
          });
    }));

    sender.submit(lazy::sink_receiver{});
  }

  CHECK(canceled == true);
}

TEST_SUITE_END();

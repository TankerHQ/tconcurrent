#include <doctest.h>

#include <tconcurrent/future_group.hpp>

#include <algorithm>

using namespace tconcurrent;

TEST_CASE("test future_group empty")
{
  future_group group;
  CHECK(group.terminate().is_ready());
}

TEST_CASE("test future_group ready")
{
  future_group group;
  group.add(tc::make_ready_future(18));
  group.add(tc::make_ready_future(18.f));
  group.add(tc::make_exceptional_future<void>(
      std::make_exception_ptr(std::runtime_error("fail"))));
  CHECK(group.terminate().is_ready());
}

TEST_CASE("test future_group not ready")
{
  tc::promise<int> prom;
  future_group group;
  group.add(prom.get_future());
  auto fut = group.terminate();
  CHECK(!fut.is_ready());
  CHECK(prom.get_cancelation_token().is_cancel_requested());
  prom.set_value(18);
  CHECK(fut.is_ready());
}

TEST_CASE("test future_group adding future after termination")
{
  future_group group;
  group.add(tc::make_ready_future(18));
  CHECK(group.terminate().is_ready());
  CHECK_THROWS(group.add(tc::make_ready_future(42)));
}

TEST_CASE("test future_group double termination")
{
  future_group group;
  tc::promise<int> prom;
  group.add(prom.get_future());
  auto first_terminate = group.terminate();
  CHECK(!first_terminate.is_ready());
  prom.set_value({});
  CHECK(first_terminate.is_ready());

  CHECK(group.terminate().is_ready());
}

#include <doctest/doctest.h>

#include <tconcurrent/async.hpp>
#include <tconcurrent/packaged_task.hpp>

#include <thread>

using namespace tconcurrent;

TEST_CASE("packaged_task<void> should give a non-ready future")
{
  auto taskfut = package<void()>([] {});
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());
}

TEST_CASE("packaged_task<void> should set the future when it is run")
{
  auto taskfut = package<void()>([] {});
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  task();

  CHECK(future.is_ready());
  CHECK(future.has_value());
}

TEST_CASE("packaged_task should set the future when it is run")
{
  auto taskfut = package<int()>([] { return 42; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  task();

  CHECK(future.is_ready());
  CHECK(!future.has_exception());
  CHECK(42 == future.get());
}

// global to avoid gcc lambda capture bug
static const int val = 42;

TEST_CASE("packaged_task should copy value if returned by reference")
{
  auto taskfut = package<int()>([&]() -> const int& { return val; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  task();

  CHECK(42 == future.get());
}

TEST_CASE("packaged_task should support arguments")
{
  auto taskfut = package<int(int)>([](int i) { return i * 2; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  task(21);

  CHECK(42 == future.get());
}

TEST_CASE("packaged_task should handle exceptions")
{
  auto taskfut = package<void(int)>([](int i) { throw 42; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  CHECK_NOTHROW(task(21));

  CHECK(future.is_ready());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), int);
}

TEST_CASE("packaged_task that is not run should be reported as broken promise")
{
  auto future = package<void()>([] {}).second;
  REQUIRE(future.is_ready());
  CHECK_THROWS_AS(future.get(), broken_promise);
}

TEST_CASE(
    "packaged_task cancelable that is not run should be reported as broken "
    "promise")
{
  auto future = package_cancelable<void()>([] {}).second;
  REQUIRE(future.is_ready());
  CHECK_THROWS_AS(future.get(), broken_promise);
}

TEST_CASE("packaged_task should make future.get() block until it is run")
{
  auto taskfut = package<int(int)>([](int i) { return i * 2; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  std::thread th([&] { task(21); });

  CHECK(42 == future.get()); // may block
  th.join();
}

TEST_CASE("packaged_task should give a relevant cancelation_token")
{
  auto taskfut =
      package<void(bool)>([&](cancelation_token& token, bool cancelreq) {
        CHECK(cancelreq == token.is_cancel_requested());
      });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  SUBCASE("not canceled")
  {
    task(false);
  }

  SUBCASE("canceled")
  {
    future.request_cancel();
    task(true);
  }
}

TEST_CASE("cancelable packaged_task should not run if canceled")
{
  auto taskfut = package_cancelable<void()>([&]() { CHECK(false); });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);

  future.request_cancel();

  CHECK(future.is_ready());
  task(); // should do nothing
  CHECK_THROWS_AS(future.get(), operation_canceled);
}

TEST_CASE("packaged_task_result_type should be correct")
{
  {
    auto f = []() -> int { return {}; };
    static_assert(
        std::is_same<packaged_task_result_type<decltype(f)()>, int>::value,
        "packaged_task_result_type deduction error");
  }
  {
    auto f = []() -> void {};
    static_assert(
        std::is_same<packaged_task_result_type<decltype(f)()>, void>::value,
        "packaged_task_result_type deduction error");
  }
  {
    auto f = [](int, char*) -> float { return {}; };
    static_assert(
        std::is_same<packaged_task_result_type<decltype(f)(int, char*)>,
                     float>::value,
        "packaged_task_result_type deduction error");
  }
  {
    auto f = [](cancelation_token&) -> float { return {}; };
    static_assert(
        std::is_same<packaged_task_result_type<decltype(f)()>, float>::value,
        "packaged_task_result_type deduction error");
  }
  {
    auto f = [](cancelation_token&, int, char*) -> float { return {}; };
    static_assert(
        std::is_same<packaged_task_result_type<decltype(f)(int, char*)>,
                     float>::value,
        "packaged_task_result_type deduction error");
  }
}

TEST_CASE("sync should work")
{
  auto fut = sync([] { return 15; });
  CHECK(fut.is_ready());
  CHECK(15 == fut.get());
}

TEST_CASE("sync should work with void")
{
  auto fut = sync([] {});
  CHECK(fut.is_ready());
  CHECK_NOTHROW(fut.get());
}

TEST_CASE("sync should handle exceptions")
{
  auto fut = sync([] { throw 18; });
  CHECK(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), int);
}

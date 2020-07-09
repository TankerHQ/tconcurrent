#include <tconcurrent/future.hpp>
#include <tconcurrent/promise.hpp>
#ifndef EMSCRIPTEN
#include <tconcurrent/thread_pool.hpp>
#endif

#include <doctest/doctest.h>

#include <cstring>

#include <iostream>
#include <thread>

using namespace tconcurrent;

/////////////////////////
// ready future basics
/////////////////////////

TEST_CASE("ready future")
{
  auto future = make_ready_future(42);
  static_assert(std::is_same<decltype(future)::value_type, int>::value,
                "make_ready_future can't deduce int future type");
  CHECK(future.is_ready());
  CHECK(future.has_value());
  CHECK(!future.has_exception());
  CHECK(42 == future.get());
}

TEST_CASE("future should move its result out")
{
  auto future = make_ready_future(std::make_unique<int>(42));
  // move the unique_ptr out
  CHECK(42 == *future.get());
  // there is nothing left
  CHECK(!future.get());
}

TEST_CASE("future should be covertible to a shared future")
{
  auto future = make_ready_future(42);
  auto shared_future = future.to_shared();
  CHECK(!future.is_valid());
  CHECK(shared_future.is_ready());
  CHECK(42 == shared_future.get());
}

TEST_CASE("shared_future should not move its result out")
{
  auto future = make_ready_future(std::make_unique<int>(42)).to_shared();
  CHECK(42 == *future.get());
  CHECK(42 == *future.get());
}

TEST_CASE("shared_future should throw its exception twice")
{
  auto future = make_exceptional_future<void>(42).to_shared();
  CHECK_THROWS_AS(future.get(), int);
  CHECK_THROWS_AS(future.get(), int);
}

TEST_CASE("future<void> should work")
{
  auto future = make_ready_future();
  static_assert(std::is_same<decltype(future)::value_type, tvoid>::value,
                "make_ready_future can't deduce void future type");
  CHECK(future.is_ready());
  CHECK(future.has_value());
}

TEST_CASE("exceptional future")
{
  auto future = make_exceptional_future<int>(std::invalid_argument{"kaboom"});
  CHECK(future.is_ready());
  CHECK(!future.has_value());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), std::invalid_argument);
}

TEST_CASE("exceptional future<void>")
{
  auto future = make_exceptional_future<void>(std::invalid_argument{"kaboom"});
  CHECK(future.is_ready());
  CHECK(!future.has_value());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), std::invalid_argument);
}

TEST_CASE("future.wait_for should timeout properly [waiting]")
{
  promise<void> prom;
  auto fut = prom.get_future();
  auto before = std::chrono::steady_clock::now();
  fut.wait_for(std::chrono::milliseconds(100));
  auto after = std::chrono::steady_clock::now();
  CHECK(std::chrono::milliseconds(100) < after - before);
}

TEST_CASE("future should do nothing when cancel is requested after it is ready")
{
  auto fut = make_ready_future(18);
  // does nothing
  fut.request_cancel();
  CHECK(fut.has_value());
}

TEST_CASE("ready future should not have a cancel requested")
{
  unsigned called = 0;
  auto fut =
      make_ready_future().then([&](cancelation_token& token, future<void> fut) {
        ++called;
        CHECK(!token.is_cancel_requested());
      });
  fut.get();
  CHECK(1 == called);
}

/////////////////////////
// future then
/////////////////////////

TEST_CASE("then should work on a ready future")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.then([](future<int> val) { return long(val.get() * 2); });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "then can't deduce future type");
  CHECK(42 == fut2.get());
}

TEST_CASE("then should support mutable lambdas")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.then([i = 0](future<int> val) mutable {
    i = 1;
    return val.get() * 2;
  });
  CHECK(42 == fut2.get());
}

TEST_CASE("then should work on exceptional future")
{
  auto fut = make_exceptional_future<int>(21);
  auto fut2 = fut.then([&](future<int> const& val) {
    try
    {
      fut.get();
    }
    catch (int i)
    {
      return i * 2;
    }
    return 0;
  });
  CHECK(42 == fut2.get());
}

TEST_CASE("then should support throwing continuation")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.then([](future<int> val) { throw 42; });
  CHECK_THROWS_AS(fut2.get(), int);
}

TEST_CASE("then should work with non-ready future")
{
  promise<int> prom;
  auto fut2 =
      prom.get_future().then([](future<int> val) { return val.get() * 2; });

  std::thread th([&] { prom.set_value(21); });

  CHECK(42 == fut2.get());
  th.join();
}

TEST_CASE("and_then should work on a ready future")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.and_then([](int val) { return long(val * 2); });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "and_then can't deduce future type");
  CHECK(42 == fut2.get());
}

TEST_CASE("and_then should support mutable lambdas")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.and_then([i = 0](int val) mutable {
    i = 1;
    return val * 2;
  });
  CHECK(42 == fut2.get());
}

TEST_CASE("and_then should work on exceptional future")
{
  auto fut = make_exceptional_future<int>(21);
  auto fut2 = fut.and_then([&](int val) mutable {
    CHECK(false);
    return val * 2;
  });
  CHECK_THROWS_AS(fut2.get(), int);
}

TEST_CASE("and_then should support throwing continuations")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.and_then([](int val) { throw 42; });
  CHECK_THROWS_AS(fut2.get(), int);
}

/////////////////////////
// chain names
/////////////////////////

TEST_CASE("to_void should work on a ready future")
{
  auto fut = make_ready_future(42).to_void();
  static_assert(std::is_same<decltype(fut)::value_type, tvoid>::value,
                "to_void must give a void future");
  CHECK(fut.has_value());
}

TEST_CASE("to_void should work on a non-ready future")
{
  promise<int> prom;
  auto fut = prom.get_future().to_void();
  CHECK(!fut.is_ready());
  prom.set_value(42);
  CHECK(fut.is_ready());
}

/////////////////////////
// chain names
/////////////////////////

TEST_CASE("then should propagate the chain name to the returned future")
{
  auto const ChainName = "test test";
  auto fut =
      make_ready_future(21).update_chain_name(ChainName).then([](auto) {});
  CHECK(ChainName == fut.get_chain_name());
}

TEST_CASE("then should set task name to chain name")
{
  struct Executor
  {
    std::string name;
    void post(std::function<void()> f, std::string n)
    {
      name = n;
      f();
    }
  };

  auto const ChainName = "test test";
  Executor e;
  auto fut =
      make_ready_future(21).update_chain_name(ChainName).then(e, [](auto) {});
  fut.get();
  // only compare the beginning of the string because future appends the type of
  // the lambda to the name
  CHECK(ChainName == e.name.substr(0, std::strlen(ChainName)));
}

/////////////////////////
// promise
/////////////////////////

TEST_CASE("promise should work with a value")
{
  promise<int> prom;
  auto fut = prom.get_future();
  static_assert(std::is_same<decltype(fut)::value_type, int>::value,
                "promise and future don't have the same type");
  CHECK(!fut.is_ready());
  prom.set_value(42);
  CHECK(fut.is_ready());
  CHECK(42 == fut.get());
}

TEST_CASE("promise<void> should work with a value")
{
  promise<void> prom;
  auto fut = prom.get_future();
  static_assert(std::is_same<decltype(fut)::value_type, tvoid>::value,
                "promise and future don't have the same type");
  CHECK(!fut.is_ready());
  prom.set_value({});
  CHECK(fut.is_ready());
  CHECK_NOTHROW(fut.get());
}

TEST_CASE("promise should support exceptions")
{
  promise<void> prom;
  auto fut = prom.get_future();
  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(42));
  CHECK(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), int);
}

TEST_CASE("promise destroyed prematurely should set broken_promise exception")
{
  auto fut = promise<void>().get_future();
  REQUIRE(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), broken_promise);
}

/////////////////////////
// cancelation_token
/////////////////////////

TEST_CASE("canceling a promise's future should propagate to cancelation token")
{
  promise<void> prom;
  auto fut = prom.get_future();
  fut.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("canceling a promise's future should call cancelation callback")
{
  promise<void> prom;
  auto fut = prom.get_future();
  unsigned called = 0;
  prom.get_cancelation_token().push_cancelation_callback([&] { ++called; });

  fut.request_cancel();

  CHECK(1 == called);
}

TEST_CASE(
    "canceling a promise's future should call scoped cancelation callback")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  auto& token = prom.get_cancelation_token();

  {
    auto const scope = token.make_scope_canceler([&] { ++called; });
    fut.request_cancel();
    CHECK(1 == called);
  }

  // should do nothing
  fut.request_cancel();
  CHECK(1 == called);
}

TEST_CASE("setting a cancelation callback on a canceled promise should call it")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  auto& token = prom.get_cancelation_token();

  fut.request_cancel();

  token.make_scope_canceler([&] { ++called; });

  CHECK(1 == called);
}

TEST_CASE("scoped_cancelers should support nesting")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  auto& token = prom.get_cancelation_token();

  SUBCASE("canceling inner scope")
  {
    {
      auto scope = token.make_scope_canceler([&] {
        CHECK(1 == called);
        ++called;
      });
      {
        auto scope = token.make_scope_canceler([&] {
          CHECK(0 == called);
          ++called;
        });
        fut.request_cancel();
        CHECK(1 == called);
      }
    }
    CHECK(2 == called);
  }

  SUBCASE("canceling outer scope")
  {
    auto scope = token.make_scope_canceler([&] { ++called; });
    {
      auto scope = token.make_scope_canceler([&] { CHECK(false); });
    }
    fut.request_cancel();
    CHECK(1 == called);
  }
}

TEST_CASE(
    "it should be ok to set a value to a promise while a scope canceler is set")
{
  promise<void> prom;

  auto const canceler = prom.get_cancelation_token().make_scope_canceler([] {});

  prom.set_value({});

  prom = promise<void>();
}

/////////////////////////
// promise and cancel
/////////////////////////

TEST_CASE("promise should reuse cancelation token of given future")
{
  promise<void> prom;
  auto fut = prom.get_future();
  promise<int> prom2(fut);
  auto fut2 = prom2.get_future();

  fut2.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
}

/////////////////////////
// continuations and cancel
/////////////////////////

TEST_CASE("continuations and cancelation")
{
  unsigned called = 0;
  promise<int> prom;
  auto fut = prom.get_future();

  SUBCASE("then without cancel")
  {
    auto fut2 = fut.then([&](cancelation_token& token, future<int> const& fut) {
      ++called;
      CHECK(!token.is_cancel_requested());
    });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SUBCASE("then with cancel before execution")
  {
    auto fut2 = fut.then([&](cancelation_token& token, future<int> const& fut) {
      ++called;
      CHECK(token.is_cancel_requested());
    });
    fut2.request_cancel();
    CHECK(prom.get_cancelation_token().is_cancel_requested());
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SUBCASE("then with cancel during execution")
  {
    future<void> fut2 =
        fut.then([&](cancelation_token& token, future<int> const& fut) {
          ++called;
          CHECK(!token.is_cancel_requested());
          fut2.request_cancel();
          CHECK(token.is_cancel_requested());
        });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SUBCASE("and_then without cancel")
  {
    auto fut2 = fut.and_then([&](cancelation_token& token, int) {
      ++called;
      CHECK(!token.is_cancel_requested());
    });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SUBCASE("and_then with cancel before execution")
  {
    auto fut2 = fut.and_then([&](cancelation_token& token, int) { ++called; });
    fut2.request_cancel();
    prom.set_value(18);
    CHECK_THROWS_AS(fut2.get(), operation_canceled);
    CHECK(0 == called);
  }

  SUBCASE("and_then with cancel during execution")
  {
    future<void> fut2 = fut.and_then([&](cancelation_token& token, int) {
      ++called;
      CHECK(!token.is_cancel_requested());
      fut2.request_cancel();
      CHECK(token.is_cancel_requested());
    });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SUBCASE("cancel should propagate to a continuation set afterwards")
  {
    auto fut2 = prom.get_future().then([&](future<int> fut) {
      return fut.then([&](cancelation_token& token, future<int> fut) {
        ++called;
        CHECK(token.is_cancel_requested());
      });
    });
    fut2.request_cancel();
    prom.set_value(18);
    fut2.get().get();
    CHECK(1 == called);
  }
}

TEST_CASE(
    "break_cancelation_chain should prevent cancel propagation from first "
    "future to second")
{
  tc::promise<void> prom;
  auto fut1 = prom.get_future();
  fut1.request_cancel();

  unsigned called = 0;
  auto fut2 = fut1.then([&](cancelation_token& token, future<void> fut) {
                    ++called;
                    CHECK(token.is_cancel_requested());
                  })
                  .break_cancelation_chain()
                  .then([&](cancelation_token& token, future<void> fut) {
                    ++called;
                    CHECK(!token.is_cancel_requested());
                  });
  prom.set_value({});
  fut2.get();
  CHECK(2 == called);
}

TEST_CASE(
    "break_cancelation_chain should prevent cancel propagation from second "
    "future to first")
{
  tc::promise<void> prom;
  auto fut1 = prom.get_future();

  unsigned called = 0;
  auto fut2 = fut1.then([&](cancelation_token& token, future<void> fut) {
                    ++called;
                    CHECK(!token.is_cancel_requested());
                  })
                  .break_cancelation_chain();

  fut2.request_cancel();
  CHECK(!prom.get_cancelation_token().is_cancel_requested());

  prom.set_value({});
  fut2.get();
  CHECK(1 == called);
}

/////////////////////////
// future unwrapping
/////////////////////////

static_assert(
    std::is_same<future<int>,
                 decltype(std::declval<future<future<int>>>().unwrap())>::value,
    "incorrect unwrap signature");

static_assert(std::is_same<future<int>,
                           decltype(std::declval<shared_future<future<int>>>()
                                        .unwrap())>::value,
              "incorrect unwrap signature");

static_assert(std::is_same<shared_future<int>,
                           decltype(std::declval<future<shared_future<int>>>()
                                        .unwrap())>::value,
              "incorrect unwrap signature");

static_assert(
    std::is_same<shared_future<int>,
                 decltype(std::declval<shared_future<shared_future<int>>>()
                              .unwrap())>::value,
    "incorrect unwrap signature");

TEST_CASE("unwrap should work with nested ready futures")
{
  tc::promise<future<int>> prom;
  auto future = prom.get_future();

  auto unfut = future.unwrap();
  CHECK(!unfut.is_ready());
  prom.set_value(make_ready_future(42));
  CHECK(unfut.is_ready());
  CHECK(42 == unfut.get());
}

TEST_CASE("unwrap should work with nested ready futures of void")
{
  tc::promise<future<void>> prom;
  auto future = prom.get_future();

  auto unfut = future.unwrap();
  CHECK(!unfut.is_ready());
  prom.set_value(make_ready_future());
  CHECK(unfut.is_ready());
  CHECK_NOTHROW(unfut.get());
}

TEST_CASE("unwrap should handle errors in outer future")
{
  tc::promise<future<int>> prom;
  auto future = prom.get_future();

  auto unfut = future.unwrap();
  prom.set_exception(std::make_exception_ptr(42));
  CHECK(unfut.is_ready());
  CHECK_THROWS_AS(unfut.get(), int);
}

TEST_CASE("unwrap should handle errors in inner future")
{
  tc::promise<future<int>> prom;
  auto future = prom.get_future();

  auto unfut = future.unwrap();
  prom.set_value(make_exceptional_future<int>(42));
  CHECK(unfut.is_ready());
  CHECK_THROWS_AS(unfut.get(), int);
}

TEST_CASE(
    "unwrap should propagate cancelation to outer and inner future when "
    "canceled before the first future completes")
{
  promise<future<int>> prom;
  auto fut = prom.get_future();
  auto fut2 = fut.unwrap();

  fut2.request_cancel();

  CHECK(prom.get_cancelation_token().is_cancel_requested());
  promise<int> prom2;
  prom.set_value(prom2.get_future());
  CHECK(prom2.get_cancelation_token().is_cancel_requested());
}

TEST_CASE(
    "unwrap should propagate cancelation to outer and inner future when "
    "canceled after the first future completes")
{
  promise<future<int>> prom;
  auto fut = prom.get_future();
  auto fut2 = fut.unwrap();

  CHECK(!prom.get_cancelation_token().is_cancel_requested());
  promise<int> prom2;
  prom.set_value(prom2.get_future());

  fut2.request_cancel();

  CHECK(prom2.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("unwrap should work with future<shared_future>")
{
  shared_future<int> fut1{make_ready_future(18)};
  future<shared_future<int>> fut2{make_ready_future(fut1)};
  auto unwrapped = fut2.unwrap();
  CHECK(unwrapped.is_ready());
  CHECK(18 == unwrapped.get());
}

/////////////////////////
// future executor
/////////////////////////

#ifndef EMSCRIPTEN
TEST_CASE("then must support running on specified executor")
{
  thread_pool tp;
  tp.start(1);
  auto f = make_ready_future();

  f.then(executor(tp),
         [&](future<void> const&) { CHECK(tp.is_in_this_context()); })
      .get();
}

TEST_CASE("and_then must support running on specified executor")
{
  thread_pool tp;
  tp.start(1);
  auto f = make_ready_future();

  f.and_then(executor(tp), [&](tvoid) { CHECK(tp.is_in_this_context()); })
      .get();
}
#endif

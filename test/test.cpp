#include <catch.hpp>

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/promise.hpp>
#include <tconcurrent/delay.hpp>
#include <tconcurrent/periodic_task.hpp>
#include <tconcurrent/async.hpp>
#include <tconcurrent/when.hpp>
#include <tconcurrent/concurrent_queue.hpp>
#include <tconcurrent/semaphore.hpp>
#include <tconcurrent/future_group.hpp>

using namespace tconcurrent;

TEST_CASE("test ready future", "[future]")
{
  auto future = make_ready_future(42);
  static_assert(std::is_same<decltype(future)::value_type, int>::value,
                "make_ready_future can't deduce int future type");
  CHECK(future.is_ready());
  CHECK(future.has_value());
  CHECK(42 == future.get());
}

TEST_CASE("test void ready future", "[future]")
{
  auto future = make_ready_future();
  static_assert(std::is_same<decltype(future)::value_type, tvoid>::value,
                "make_ready_future can't deduce void future type");
  CHECK(future.is_ready());
  CHECK(future.has_value());
}

TEST_CASE("test exceptional ready future", "[future]")
{
  auto future = make_exceptional_future<int>("FAIL");
  CHECK(future.is_ready());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), char const*);
}

TEST_CASE("test exceptional ready future void", "[future]")
{
  auto future = make_exceptional_future<void>("FAIL");
  CHECK(future.is_ready());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), char const*);
}

TEST_CASE("test simple packaged task", "[packaged_task]")
{
  auto taskfut = package<void()>([]{});
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  CHECK_NOTHROW(task());

  CHECK(future.is_ready());
  CHECK(!future.has_exception());
}

TEST_CASE("test non void packaged task", "[packaged_task]")
{
  auto taskfut = package<int()>([]{ return 42; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  CHECK_NOTHROW(task());

  CHECK(future.is_ready());
  CHECK(!future.has_exception());
  CHECK(42 == future.get());
}

const int val = 42;

TEST_CASE("test reference packaged task", "[packaged_task]")
{
  auto taskfut = package<int()>([&]() -> const int& { return val; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  static_assert(
      std::is_same<std::remove_reference<decltype(future)>::type::value_type,
                   int>::value,
      "package can't handle reference return type");
  CHECK(!future.is_ready());

  CHECK_NOTHROW(task());

  CHECK(future.is_ready());
  CHECK(!future.has_exception());
  CHECK(42 == future.get());
}

TEST_CASE("test arguments packaged task", "[packaged_task]")
{
  auto taskfut = package<int(int)>([](int i){ return i*2; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  CHECK_NOTHROW(task(21));

  CHECK(future.is_ready());
  CHECK(!future.has_exception());
  CHECK(42 == future.get());
}

TEST_CASE("test exception packaged task", "[packaged_task]")
{
  auto taskfut = package<void(int)>([](int i){ throw 42; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  CHECK_NOTHROW(task(21));

  future.wait();
  CHECK(future.is_ready());
  CHECK(future.has_exception());
  CHECK_THROWS_AS(future.get(), int);
}

TEST_CASE("test unrun packaged task", "[packaged_task]")
{
  auto future = package<void()>([]{}).second;
  REQUIRE(future.is_ready());
  CHECK_THROWS_AS(future.get(), broken_promise);
}

TEST_CASE("test delayed packaged task", "[packaged_task]")
{
  auto taskfut = package<int(int)>([](int i){ return i*2; });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  std::thread th([&]{ task(21); });

  CHECK(42 == future.get()); // may block
  CHECK(future.is_ready());
  CHECK(!future.has_exception());
  th.join();
}

TEST_CASE("test packaged task cancel", "[packaged_task][cancel]")
{
  bool cancelreq;
  auto taskfut = package<int(int)>([&](cancelation_token& token, int i) {
    cancelreq = token.is_cancel_requested();
    return i * 2;
  });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  CHECK(!future.is_ready());

  SECTION("not canceled")
  {
    task(21);
    CHECK(42 == future.get());
    CHECK(!cancelreq);
  }

  SECTION("canceled")
  {
    future.request_cancel();
    task(21);
    CHECK(42 == future.get());
    CHECK(cancelreq);
  }
}

TEST_CASE("test sync", "[sync]")
{
  auto fut = sync([]{ return 15; });
  CHECK(fut.is_ready());
  CHECK(15 == fut.get());
}

TEST_CASE("test sync void", "[sync]")
{
  auto fut = sync([]{});
  CHECK(fut.is_ready());
  CHECK_NOTHROW(fut.get());
}

TEST_CASE("test sync throw", "[sync]")
{
  auto fut = sync([]{ throw 18; });
  CHECK(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), int);
}

TEST_CASE("test packaged task packaged_task_result_type", "[packaged_task]")
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
        std::is_same<packaged_task_result_type<decltype(f)()>,
                     float>::value,
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

TEST_CASE("test future unwrap", "[future][unwrap]")
{
  auto taskfut = package<future<int>(int)>([](int i)
                                           {
                                             return make_ready_future(i * 2);
                                           });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  auto unfut = future.unwrap();
  static_assert(std::is_same<decltype(unfut)::value_type, int>::value,
                "unwrap has an incorrect return type");
  CHECK(!unfut.is_ready());
  task(21);
  CHECK(unfut.is_ready());
  CHECK(42 == unfut.get());
}

TEST_CASE("test future unwrap void", "[future][unwrap]")
{
  auto taskfut = package<future<void>()>([]
                                         {
                                           return make_ready_future();
                                         });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  auto unfut = future.unwrap();
  static_assert(std::is_same<decltype(unfut)::value_type, tvoid>::value,
                "unwrap of void has an incorrect return type");
  CHECK(!unfut.is_ready());
  task();
  CHECK(unfut.is_ready());
  CHECK_NOTHROW(unfut.get());
}

TEST_CASE("test future unwrap error", "[future][unwrap]")
{
  auto taskfut = package<future<int>(int)>([](int i) -> future<int>
                                           {
                                             throw 42;
                                           });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  auto unfut = future.unwrap();
  static_assert(std::is_same<decltype(unfut)::value_type, int>::value,
                "unwrap has an incorrect return type");
  CHECK(!unfut.is_ready());
  task(21);
  CHECK(unfut.is_ready());
  CHECK(unfut.has_exception());
  CHECK_THROWS_AS(unfut.get(), int);
}

TEST_CASE("test future unwrap nested error", "[future][unwrap]")
{
  auto taskfut =
      package<future<int>(int)>([](int i)
                                {
                                  return make_exceptional_future<int>(42);
                                });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  auto unfut = future.unwrap();
  static_assert(std::is_same<decltype(unfut)::value_type, int>::value,
                "unwrap has an incorrect return type");
  CHECK(!unfut.is_ready());
  task(21);
  CHECK(unfut.is_ready());
  CHECK(unfut.has_exception());
  CHECK_THROWS_AS(unfut.get(), int);
}

SCENARIO("future can be used with specific executors", "[future][executor]")
{
  GIVEN("A ready future and an executor")
  {
    thread_pool tp;
    tp.start(1);
    auto f = make_ready_future();

    THEN(".then continuation is executed on the executor")
    {
      f.then(tp, [&](future<void> const&) {
         CHECK(tp.is_in_this_context());
       }).get();
    }
    THEN(".and_then continuation is executed on the executor")
    {
      f.and_then(tp, [&](tvoid) {
         CHECK(tp.is_in_this_context());
       }).get();
    }
  }
}

TEST_CASE("test future ready to_void", "[future]")
{
  auto fut = make_ready_future(42).to_void();
  CHECK(fut.is_ready());
}

TEST_CASE("test future to_void", "[future]")
{
  promise<int> prom;
  auto fut = prom.get_future().to_void();
  CHECK(!fut.is_ready());
  prom.set_value(42);
  CHECK(fut.is_ready());
}

TEST_CASE("test future to_void cancelation", "[future][cancel]")
{
  promise<int> prom;
  auto fut = prom.get_future().to_void();
  fut.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  CHECK(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), operation_canceled);
}

TEST_CASE("test simple async", "[async]")
{
  bool hasrun = false;
  auto fut = async([&]{ hasrun = true; });
  CHECK_NOTHROW(fut.get());
  CHECK(hasrun);
}

TEST_CASE("test non void async", "[async]")
{
  bool hasrun = false;
  auto fut = async([&]{ hasrun = true; return 42; });
  CHECK(42 == fut.get());
  CHECK(hasrun);
}

TEST_CASE("test reference async", "[async]")
{
  bool hasrun = false;
  auto fut = async([&]() -> int const& { hasrun = true; return val; });
  static_assert(std::is_same<decltype(fut)::value_type, int>::value,
                "async can't handle reference return type");
  CHECK(42 == fut.get());
  CHECK(hasrun);
}

TEST_CASE("test async cancelation_token not canceled", "[async][cancel]")
{
  auto fut = async(
      [&](cancelation_token& token) { CHECK(!token.is_cancel_requested()); });
  fut.get();
}

TEST_CASE("test async cancelation_token canceled", "[async][cancel]")
{
  auto fut = async([&](cancelation_token& token) {
    while (!token.is_cancel_requested())
      ;
  });
  fut.request_cancel();
  fut.get();
}

TEST_CASE("test is_in_this_context", "[executor]")
{
  auto fut = async([&]
                   {
                     return get_default_executor().is_in_this_context();
                   });
  CHECK(fut.get());
  CHECK_FALSE(get_default_executor().is_in_this_context());
}

TEST_CASE("test delay async", "[async_wait]")
{
  std::chrono::milliseconds const delay{100};
  auto before = std::chrono::steady_clock::now();
  auto fut = async_wait(delay);
  fut.wait();
  auto after = std::chrono::steady_clock::now();
  CHECK(delay < after - before);
}

TEST_CASE("test delay async cancel", "[async_wait][cancel]")
{
  std::chrono::milliseconds const delay{100};
  auto before = std::chrono::steady_clock::now();
  auto fut = async_wait(delay);
  fut.request_cancel();
  fut.wait();
  auto after = std::chrono::steady_clock::now();
  CHECK(delay > after - before);
}

TEST_CASE("test ready future then", "[future][then]")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.then([](future<int> val){ return long(val.get()*2); });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "then can't deduce future type");
  CHECK(42 == fut2.get());
}

TEST_CASE("test ready future then with chain name", "[future][executor][then]")
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
  CHECK(ChainName == e.name.substr(0, strlen(ChainName)));
}

TEST_CASE("test ready future then with chain name propagation",
          "[future][then]")
{
  auto const ChainName = "test test";
  auto fut =
      make_ready_future(21).update_chain_name(ChainName).then([](auto) {});
  CHECK(ChainName == fut.get_chain_name());
}

TEST_CASE("test not ready future then", "[future][then]")
{
  auto taskfut = package<int()>([]{ return 21; });
  auto& task = std::get<0>(taskfut);
  auto& fut = std::get<1>(taskfut);
  // also test mutable callback
  auto fut2 = fut.then([i = 0](future<int> val) mutable {
    i = 42;
    return long(val.get() * 2);
  });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "then can't deduce future type");

  std::thread th([&]{ task(); });

  CHECK(42 == fut2.get());
  th.join();
}

TEST_CASE("test ready future then error", "[future][then]")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.then([](future<int> val) { throw 42; });
  CHECK_THROWS_AS(fut2.get(), int);
}

TEST_CASE("test error future then", "[future][then]")
{
  bool called = false;
  auto fut = make_exceptional_future<int>(21);
  auto fut2 = fut.then([&](future<int> const& val)
                       {
                         called = true;
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
  static_assert(std::is_same<decltype(fut2)::value_type, int>::value,
                "then can't deduce future type");
  fut2.wait();
  CHECK(42 == fut2.get());
  CHECK(called);
}

TEST_CASE("test ready future and_then", "[future][then]")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.and_then([](int val){ return long(val*2); });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "and_then can't deduce future type");
  CHECK(42 == fut2.get());
}

TEST_CASE("test error future and_then", "[future][then]")
{
  bool called = false;
  auto fut = make_exceptional_future<int>(21);
  // also test mutable callback
  auto fut2 = fut.and_then([&, i = 0](int val) mutable {
    i = 42;
    called = true;
    return long(val * 2);
  });
  static_assert(std::is_same<decltype(fut2)::value_type, long>::value,
                "and_then can't deduce future type");
  fut2.wait();
  CHECK(fut2.has_exception());
  CHECK(!called);
}

TEST_CASE("test ready future and_then error", "[future][then]")
{
  auto fut = make_ready_future(21);
  auto fut2 = fut.and_then([](int val) { throw 42; });
  CHECK_THROWS_AS(fut2.get(), int);
}

TEST_CASE("test promise", "[promise]")
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

TEST_CASE("test void promise", "[promise]")
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

TEST_CASE("test error promise", "[promise]")
{
  promise<void> prom;
  auto fut = prom.get_future();
  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(42));
  CHECK(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), int);
}

TEST_CASE("test broken promise", "[promise]")
{
  auto fut = promise<void>().get_future();
  REQUIRE(fut.is_ready());
  CHECK_THROWS_AS(fut.get(), broken_promise);
}

TEST_CASE("test future timed wait", "[future][wait]")
{
  promise<void> prom;
  auto fut = prom.get_future();
  auto before = std::chrono::steady_clock::now();
  fut.wait_for(std::chrono::milliseconds(100));
  auto after = std::chrono::steady_clock::now();
  CHECK(std::chrono::milliseconds(100) < after - before);
}

TEST_CASE("test future cancel ready", "[future][cancel]")
{
  auto fut = make_ready_future(18);
  // does nothing
  fut.request_cancel();
  CHECK(fut.has_value());
}

TEST_CASE("test future promise cancel", "[future][cancel]")
{
  promise<void> prom;
  auto fut = prom.get_future();
  fut.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  CHECK_THROWS_AS(fut.get(), operation_canceled);
}

TEST_CASE("test future promise cancel callback", "[future][cancel]")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();

  prom.get_cancelation_token().push_cancelation_callback([&] { ++called; });
  fut.request_cancel();
  CHECK(1 == called);
  CHECK(prom.get_cancelation_token().is_cancel_requested());

  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  CHECK_THROWS_AS(fut.get(), operation_canceled);
}

TEST_CASE("test future promise cancel scope callback", "[future][cancel]")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  auto& token = prom.get_cancelation_token();

  {
    auto scope = token.make_scope_canceler([&] { ++called; });
    fut.request_cancel();
    CHECK(1 == called);
    CHECK(token.is_cancel_requested());
  }

  fut.request_cancel();
  CHECK(1 == called);

  {
    auto scope = token.make_scope_canceler([&] { ++called; });
    CHECK(2 == called);
    CHECK(token.is_cancel_requested());
  }

  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  CHECK_THROWS_AS(fut.get(), operation_canceled);
}

TEST_CASE("test future promise cancel nested scope callback",
          "[future][cancel]")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future();
  auto& token = prom.get_cancelation_token();

  SECTION("cancel inner")
  {
    auto scope = token.make_scope_canceler([&] { CHECK(1 == called); });
    {
      auto scope = token.make_scope_canceler([&] { ++called; });
      fut.request_cancel();
      CHECK(1 == called);
      CHECK(token.is_cancel_requested());
    }
  }

  SECTION("cancel outer")
  {
    auto scope = token.make_scope_canceler([&] { ++called; });
    {
      auto scope = token.make_scope_canceler([&] { CHECK(false); });
    }
    fut.request_cancel();
    CHECK(1 == called);
    CHECK(token.is_cancel_requested());
  }

  CHECK(!fut.is_ready());
  prom.set_exception(std::make_exception_ptr(operation_canceled{}));
  CHECK_THROWS_AS(fut.get(), operation_canceled);
}

TEST_CASE("test future promise cancel propagation", "[future][cancel]")
{
  promise<void> prom;
  auto fut = prom.get_future();
  promise<int> prom2(fut);
  auto fut2 = prom2.get_future();

  fut2.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("test future promise continuation cancel", "[future][cancel]")
{
  unsigned called = 0;
  promise<int> prom;
  auto fut = prom.get_future();

  SECTION("then no cancel")
  {
    auto fut2 = fut.then([&](cancelation_token& token, future<int> const& fut) {
      ++called;
      CHECK(!token.is_cancel_requested());
    });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SECTION("then cancel first")
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

  SECTION("then cancel second")
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

  SECTION("and_then no cancel")
  {
    auto fut2 =
        fut.and_then([&](cancelation_token& token, int) {
          ++called;
          CHECK(!token.is_cancel_requested());
        });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SECTION("and_then cancel first")
  {
    future<void> fut2 =
        fut.and_then([&](cancelation_token& token, int) {
          ++called;
        });
    fut2.request_cancel();
    CHECK(prom.get_cancelation_token().is_cancel_requested());
    prom.set_value(18);
    CHECK_THROWS_AS(fut2.get(), operation_canceled);
    CHECK(0 == called);
  }

  SECTION("and_then cancel second")
  {
    future<void> fut2 =
        fut.and_then([&](cancelation_token& token, int) {
          ++called;
          CHECK(!token.is_cancel_requested());
          fut2.request_cancel();
          CHECK(token.is_cancel_requested());
        });
    prom.set_value(18);
    fut2.get();
    CHECK(1 == called);
  }

  SECTION("and_then prevented on cancel")
  {
    future<void> fut2 =
        fut.and_then([&](cancelation_token& token, int) { ++called; });
    fut2.request_cancel();
    prom.set_value(18);
    CHECK_THROWS_AS(fut2.get(), operation_canceled);
    CHECK(0 == called);
  }
}

TEST_CASE("test future promise continuation complex cancel", "[future][cancel]")
{
  unsigned called = 0;
  promise<void> prom;
  auto fut = prom.get_future().then([&](future<void> fut) {
    return fut.then([&](cancelation_token& token, future<void> fut) {
      ++called;
      CHECK(token.is_cancel_requested());
    });
  });
  fut.request_cancel();
  prom.set_value({});
  fut.get().get();
  CHECK(1 == called);
}

TEST_CASE("test future ready continuation cancel token", "[future][cancel]")
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

TEST_CASE("test future from promise break cancel chain",
          "[promise][future][cancel][breakchain]")
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

TEST_CASE("test future from promise break cancel chain reversed",
          "[promise][future][cancel][breakchain]")
{
  tc::promise<void> prom;
  auto fut1 = prom.get_future();

  unsigned called = 0;
  auto fut2 = fut1.then([&](cancelation_token& token, future<void> fut) {
    ++called;
    CHECK(!token.is_cancel_requested());
  }).break_cancelation_chain();
  fut2.request_cancel();
  CHECK(!prom.get_cancelation_token().is_cancel_requested());
  prom.set_value({});
  fut2.get();
  CHECK(1 == called);
}

TEST_CASE("test future promise unwrap cancel", "[future][unwrap][cancel]")
{
  unsigned called = 0;
  promise<future<int>> prom;
  auto fut = prom.get_future();
  auto fut2 = fut.unwrap();

  fut2.request_cancel();
  CHECK(prom.get_cancelation_token().is_cancel_requested());

  promise<int> prom2(fut);
  prom.set_value(prom2.get_future());
  CHECK(prom2.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("test future promise unwrap late cancel", "[future][unwrap][cancel]")
{
  unsigned called = 0;
  promise<future<int>> prom;
  auto fut = prom.get_future();
  auto fut2 = fut.unwrap();

  CHECK(!prom.get_cancelation_token().is_cancel_requested());

  promise<int> prom2(fut);
  prom.set_value(prom2.get_future());
  fut2.request_cancel();
  CHECK(prom2.get_cancelation_token().is_cancel_requested());
}

TEST_CASE("test when_all", "[when_all]")
{
  auto const NB_FUTURES = 100;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (i % 2)
      promises[i].set_value({});

  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  CHECK(!all.is_ready());

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (!(i % 2))
      promises[i].set_value({});

  CHECK(all.is_ready());
  auto futs = all.get();
  CHECK(futures.size() == futs.size());
  for (auto const& fut : futs)
  {
    CHECK(fut.is_ready());
    CHECK(fut.has_value());
  }
}

TEST_CASE("test when_all empty", "[when_all]")
{
  std::vector<future<int>> futures;
  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  CHECK(all.is_ready());
  auto futs = all.get();
  CHECK(0 == futs.size());
}

TEST_CASE("test when_all cancel", "[when_all][cancel]")
{
  auto const NB_FUTURES = 100;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  auto all = when_all(
      std::make_move_iterator(futures.begin()),
      std::make_move_iterator(futures.end()));
  all.request_cancel();

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    CHECK(promises[i].get_cancelation_token().is_cancel_requested());

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    promises[i].set_value({});

  all.get();
}

// periodic task

TEST_CASE("test periodic task construct", "[periodic_task]")
{
  periodic_task pt;
}

TEST_CASE("test periodic task stop", "[periodic_task]")
{
  periodic_task pt;
  pt.stop();
}

TEST_CASE("test periodic task", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start();
  CHECK(pt.is_running());
  async_wait(std::chrono::milliseconds(450)).get();
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 >= called);
  CHECK(3 <= called);
}

TEST_CASE("test periodic task future", "[periodic_task]")
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

TEST_CASE("test periodic task immediate", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(50)).get();
  CHECK(pt.is_running());
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

TEST_CASE("test periodic task executor", "[periodic_task][executor]")
{
  thread_pool tp;
  tp.start(1);

  bool fail{false};
  unsigned int called = 0;
  periodic_task pt;
  pt.set_executor(&tp);
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

TEST_CASE("test periodic task error stop", "[periodic_task][executor]")
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
  pt.set_executor(&tp);
  pt.set_callback([&]{ ++called; throw 18; });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(50)).get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
  REQUIRE(1 == goterror);
  CHECK_THROWS_AS(std::rethrow_exception(holdIt), int);
}

TEST_CASE("test periodic task future error stop", "[periodic_task][executor]")
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
  pt.set_executor(&tp);
  pt.set_callback([&]
                  {
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

TEST_CASE("test periodic task stop before start", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; });
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
  auto do_start_stop = [&]
  {
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

TEST_CASE("test periodic task start stop spam", "[periodic_task]")
{
  // can't use catch in other threads...
  std::atomic<bool> call{false};
  std::atomic<bool> fail{false};

  test_periodic_task_start_stop_spam(
      [&]
      {
        if (call.exchange(true))
          fail = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!call.exchange(false))
          fail = true;
      });

  CHECK(false == fail.load());
  CHECK(false == call.load());
}

TEST_CASE("test periodic task future start stop spam", "[periodic_task]")
{
  // can't use catch in other threads...
  std::atomic<bool> call{false};
  std::atomic<bool> fail{false};

  test_periodic_task_start_stop_spam(
      [&]
      {
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

TEST_CASE("test periodic task stop from inside", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; pt.stop(); });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  async_wait(std::chrono::milliseconds(10)).get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

TEST_CASE("test periodic single threaded task stop", "[periodic_task]")
{
  thread_pool tp;
  tp.start(1);
  unsigned int called = 0;

  periodic_task pt;
  pt.set_executor(&tp);
  pt.set_callback([&]{ ++called; });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  CHECK_NOTHROW(async(tp, [&]{ pt.stop().get(); }).get());
  CHECK(!pt.is_running());
  CHECK(0 < called);
}

TEST_CASE("test periodic task cancel", "[periodic_task][cancel]")
{
  promise<void> prom;

  periodic_task pt;
  pt.set_callback([&]{ return prom.get_future(); });
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

TEST_CASE("test periodic task cancel no-propagation", "[periodic_task][cancel]")
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

SCENARIO("test concurrent_queue", "[concurrent_queue]")
{
  GIVEN("an empty queue")
  {
    concurrent_queue<int> q;
    THEN("it is empty")
    {
      CHECK(0 == q.size());
    }
    THEN("we can not pop")
    {
      CHECK(!q.pop().is_ready());
    }
    THEN("we can push")
    {
      q.push(1);
      q.push(2);
      q.push(3);
      CHECK(3 == q.size());
    }
    THEN("pushing unlocks a poper")
    {
      auto fut = q.pop();
      CHECK(!fut.is_ready());
      q.push(18);
      CHECK(fut.is_ready());
      CHECK(18 == fut.get());
    }
  }
  GIVEN("a queue with 3 values")
  {
    concurrent_queue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);
    THEN("it holds 3 values")
    {
      CHECK(3 == q.size());
    }
    THEN("we can pop")
    {
      CHECK(q.pop().is_ready());
      CHECK(2 == q.size());
    }
    THEN("we can pop in the same order")
    {
      CHECK(1 == q.pop().get());
      CHECK(2 == q.pop().get());
      CHECK(3 == q.pop().get());

      CHECK(0 == q.size());
    }
    THEN("we can push and pop in the same order")
    {
      CHECK(1 == q.pop().get());
      q.push(4);
      CHECK(2 == q.pop().get());
      q.push(5);
      CHECK(3 == q.pop().get());
      CHECK(4 == q.pop().get());
      CHECK(5 == q.pop().get());

      CHECK(0 == q.size());
    }
  }
}

SCENARIO("test semaphore", "[semaphore]")
{
  GIVEN("an empty semaphore")
  {
    semaphore sem{0};

    THEN("it is null")
    {
      CHECK(0 == sem.count());
    }

    THEN("we can release it")
    {
      sem.release();
      CHECK(1 == sem.count());
    }

    THEN("we can not acquire it")
    {
      auto fut = sem.acquire();
      CHECK(!fut.is_ready());
      CHECK(0 == sem.count());

      AND_WHEN("someone releases it")
      {
        sem.release();

        THEN("the acquire succeeds")
        {
          CHECK(fut.is_ready());
          CHECK(0 == sem.count());
        }
      }
    }
  }

  GIVEN("a semaphore 4-initialized")
  {
    semaphore sem{4};

    THEN("it holds 4")
    {
      CHECK(4 == sem.count());
    }

    THEN("we can release it")
    {
      sem.release();
      CHECK(5 == sem.count());
    }

    THEN("we can acquire it")
    {
      auto fut = sem.acquire();
      CHECK(fut.is_ready());
      CHECK(3 == sem.count());
    }

    // FIXME this is broken because future expects a copyable type
    //THEN("we can get a scope_lock")
    //{
    //  auto l = sem.get_scoped_lock();
    //}
  }
}

TEST_CASE("test thread_pool do nothing", "[thread_pool]")
{
  thread_pool tp;
  tp.run_thread();
}

TEST_CASE("test thread_pool start stop", "[thread_pool]")
{
  thread_pool tp;
  tp.start(1);
  tp.stop();
}

TEST_CASE("test thread_pool run work", "[thread_pool]")
{
  bool called = false;

  thread_pool tp;
  tp.start(1);
  tp.post([&]{ called = true; });
  tp.stop();
  CHECK(called);
}

TEST_CASE("test thread_pool error work", "[thread_pool]")
{
  bool called = false;

  thread_pool tp;
  tp.set_error_handler([&](std::exception_ptr const& e) {
    called = true;
    CHECK_THROWS_AS(std::rethrow_exception(e), int);
  });

  tp.start(1);
  tp.post([&]{ throw 18; });
  tp.stop();
  CHECK(called);
}

TEST_CASE("test thread_pool task trace", "[thread_pool]")
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
  tp.post([]{ std::this_thread::sleep_for(WaitTime); }, TaskName);
  tp.stop();
  CHECK(called);
}

TEST_CASE("test future_group empty", "[future_group]")
{
  future_group group;
  CHECK(group.terminate().is_ready());
}

TEST_CASE("test future_group ready", "[future_group]")
{
  future_group group;
  group.add(tc::make_ready_future(18));
  group.add(tc::make_ready_future(18.f));
  group.add(tc::make_exceptional_future<void>(
      std::make_exception_ptr(std::runtime_error("fail"))));
  CHECK(group.terminate().is_ready());
}

TEST_CASE("test future_group not ready", "[future_group]")
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

TEST_CASE("test future_group adding future after termination", "[future_group]")
{
  future_group group;
  group.add(tc::make_ready_future(18));
  CHECK(group.terminate().is_ready());
  CHECK_THROWS(group.add(tc::make_ready_future(42)));
}

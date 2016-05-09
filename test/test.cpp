#include <catch.hpp>

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/promise.hpp>
#include <tconcurrent/delay.hpp>
#include <tconcurrent/periodic_task.hpp>
#include <tconcurrent/async.hpp>
#include <tconcurrent/when.hpp>
#include <tconcurrent/concurrent_queue.hpp>
#include <tconcurrent/semaphore.hpp>

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
  static_assert(std::is_same<decltype(future)::value_type, void*>::value,
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

TEST_CASE("test future unwrap", "[future]")
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

TEST_CASE("test future unwrap void", "[future]")
{
  auto taskfut = package<future<void>()>([]
                                         {
                                           return make_ready_future();
                                         });
  auto& task = std::get<0>(taskfut);
  auto& future = std::get<1>(taskfut);
  auto unfut = future.unwrap();
  static_assert(std::is_same<decltype(unfut)::value_type, void*>::value,
                "unwrap of void has an incorrect return type");
  CHECK(!unfut.is_ready());
  task();
  CHECK(unfut.is_ready());
  CHECK_NOTHROW(unfut.get());
}

TEST_CASE("test future unwrap error", "[future]")
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

TEST_CASE("test future unwrap nested error", "[future]")
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
      f.and_then(tp, [&](void*) {
         CHECK(tp.is_in_this_context());
       }).get();
    }
  }
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
  auto bdl = async_wait(delay);
  bdl.fut.wait();
  auto after = std::chrono::steady_clock::now();
  CHECK(delay < after - before);
}

TEST_CASE("test delay async cancel", "[async_wait]")
{
  std::chrono::milliseconds const delay{100};
  auto before = std::chrono::steady_clock::now();
  auto bdl = async_wait(delay);
  bdl.cancel();
  bdl.fut.wait();
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

TEST_CASE("test not ready future then", "[future][then]")
{
  auto taskfut = package<int()>([]{ return 21; });
  auto& task = std::get<0>(taskfut);
  auto& fut = std::get<1>(taskfut);
  auto fut2 = fut.then([](future<int> val){ return long(val.get()*2); });
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
  auto fut2 = fut.and_then([&](int val)
                           {
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
  static_assert(std::is_same<decltype(fut)::value_type, void*>::value,
                "promise and future don't have the same type");
  CHECK(!fut.is_ready());
  prom.set_value(0);
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

TEST_CASE("test when_all", "[when_all]")
{
  auto const NB_FUTURES = 100;

  std::vector<promise<void>> promises(NB_FUTURES);
  std::vector<future<void>> futures;
  for (auto const& prom : promises)
    futures.push_back(prom.get_future());

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (i % 2)
      promises[i].set_value(0);

  auto all = when_all(futures.begin(), futures.end());
  CHECK(!all.is_ready());

  for (unsigned int i = 0; i < NB_FUTURES; ++i)
    if (!(i % 2))
      promises[i].set_value(0);

  auto& futs = all.get();
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
  auto all = when_all(futures.begin(), futures.end());
  CHECK(all.is_ready());
  auto& futs = all.get();
  CHECK(0 == futs.size());
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
  std::this_thread::sleep_for(std::chrono::milliseconds(450));
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 == called);
}

TEST_CASE("test periodic task error stop", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; throw 18; });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start(periodic_task::start_immediately);
  CHECK(pt.is_running());
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

TEST_CASE("test periodic task future", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]
                  {
                    return async_wait(std::chrono::milliseconds(10))
                        .fut.and_then([&](void*)
                                      {
                                        ++called;
                                      });
                  });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start();
  CHECK(pt.is_running());
  std::this_thread::sleep_for(std::chrono::milliseconds(450));
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(4 == called);
}

TEST_CASE("test periodic task future error stop", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]
                  {
                    ++called;
                    return make_exceptional_future<void>(18);
                  });
  pt.set_period(std::chrono::milliseconds(0));
  pt.start();
  CHECK(pt.is_running());
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

TEST_CASE("test periodic task immediate", "[periodic_task]")
{
  unsigned int called = 0;

  periodic_task pt;
  pt.set_callback([&]{ ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start(periodic_task::start_immediately);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(pt.is_running());
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(1 == called);
}

TEST_CASE("test periodic task executor", "[periodic_task][executor]")
{
  thread_pool tp;
  tp.start(1);

  unsigned int called = 0;
  periodic_task pt;
  pt.set_executor(&tp);
  pt.set_callback([&]{ CHECK(tp.is_in_this_context()); ++called; });
  pt.set_period(std::chrono::milliseconds(100));
  pt.start(periodic_task::start_immediately);
  CHECK(pt.is_running());
  std::this_thread::sleep_for(std::chrono::milliseconds(450));
  pt.stop().get();
  CHECK(!pt.is_running());
  CHECK(5 == called);
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
            .fut.and_then([&](void*)
                          {
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
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

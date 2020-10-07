#undef __GTHREADS

#include <tconcurrent/lazy/async.hpp>
#include <tconcurrent/lazy/async_wait.hpp>
#include <tconcurrent/lazy/sink_receiver.hpp>
#include <tconcurrent/lazy/sync_wait.hpp>
#include <tconcurrent/lazy/then.hpp>

#include <tconcurrent/coroutine.hpp>

#include <tconcurrent/async_wait.hpp>
#include <tconcurrent/thread_pool.hpp>

#include <doctest/doctest.h>

#include <iostream>

using namespace tconcurrent;

struct move_only
{
  move_only() = default;
  move_only(move_only const&) = delete;
  move_only& operator=(move_only const&) = delete;
  move_only(move_only&&) = default;
  move_only& operator=(move_only&&) = default;
};

namespace
{
template <typename F, typename Ret>
struct lambda_sender
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<Ret>;

  F f;

  template <typename R>
  void submit(R&& receiver) const
  {
    f(std::forward<R>(receiver));
  }
};

template <typename F>
struct lambda_sender<F, void> : lambda_sender<F, int>
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<>;
};

template <typename R, typename F>
auto make_sender(F&& f)
{
  return lambda_sender<std::decay_t<F>, R>{std::forward<F>(f)};
}
}

// sink_receiver

TEST_CASE("lazy sink_receiver")
{
  lazy::sink_receiver s;
  SUBCASE("value")
  s.set_value();
  SUBCASE("values")
  s.set_value(1, 2, 3);
  SUBCASE("done")
  s.set_done();
  // a sink is just a no-op, except for set_error which terminates the program,
  // so we just test it compiles without calling it
  if (false)
    s.set_error(42);
}

// sync_wait

TEST_CASE("lazy sync_wait with value")
{
  lazy::cancelation_token c;
  auto const val = lazy::sync_wait(
      make_sender<int>([](auto&& receiver) { receiver.set_value(42); }), c);
  CHECK(val == 42);
}

TEST_CASE("lazy sync_wait with error")
{
  lazy::cancelation_token c;
  CHECK_THROWS_AS(
      lazy::sync_wait(make_sender<int>([](auto&& receiver) {
                        receiver.set_error(std::make_exception_ptr(42));
                      }),
                      c),
      int);
}

TEST_CASE("lazy sync_wait with cancel")
{
  lazy::cancelation_token c;
  CHECK_THROWS_AS(
      lazy::sync_wait(
          make_sender<int>([](auto&& receiver) { receiver.set_done(); }), c),
      operation_canceled);
}

TEST_CASE("lazy sync_wait of void")
{
  lazy::cancelation_token c;
  lazy::sync_wait(
      make_sender<void>([](auto&& receiver) { receiver.set_value(); }), c);
}

TEST_CASE("lazy sync_wait of move only type")
{
  lazy::cancelation_token c;
  lazy::sync_wait(make_sender<move_only>(
                      [](auto&& receiver) { receiver.set_value(move_only{}); }),
                  c);
}

// then

TEST_CASE("lazy then returning value")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_value(21); });
  auto const twice = [](int v) { return v * 2; };
  auto const then = lazy::then(s1, twice);
  static_assert(
      std::is_same_v<decltype(then)::value_types<std::tuple>, std::tuple<int>>,
      "then must deduce the correct type from the lambda");
  auto const result = lazy::sync_wait(then, c);
  CHECK(result == 42);
}

TEST_CASE("lazy then receiving and returning void")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<void>([](auto&& receiver) { receiver.set_value(); });
  auto const s2 = []() {};
  lazy::sync_wait(lazy::then(s1, s2), c);
}

TEST_CASE("lazy then receiving and returning move only type")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<move_only>(
      [](auto&& receiver) { receiver.set_value(move_only{}); });
  auto const s2 = [](auto v) { return std::move(v); };
  lazy::sync_wait(lazy::then(s1, s2), c);
}

TEST_CASE("lazy then with error")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_value(21); });
  auto const twice = [](int v) -> int { throw v * 2; };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::then(s1, twice), c), int);
}

TEST_CASE("lazy then never run because of error")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<int>(
      [](auto&& receiver) { receiver.set_error(std::make_exception_ptr(42)); });
  auto const twice = [](int v) {
    CHECK(false);
    return 0;
  };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::then(s1, twice), c), int);
}

TEST_CASE("lazy then never run because of throw")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<int>([](auto&& receiver) { throw 42; });
  auto const twice = [](int v) {
    CHECK(false);
    return 0;
  };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::then(s1, twice), c), int);
}

TEST_CASE("lazy then never run because of abort")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_done(); });
  auto const twice = [](int v) {
    CHECK(false);
    return 0;
  };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::then(s1, twice), c),
                  operation_canceled);
}

// async_then

TEST_CASE("lazy async_then returning value")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_value(21); });
  auto const twice = [](auto r, int v) { r.set_value(v * 2); };
  auto const result = lazy::sync_wait(lazy::async_then<int>(s1, twice), c);
  CHECK(result == 42);
}

TEST_CASE("lazy async_then receiving and returning void")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<void>([](auto&& receiver) { receiver.set_value(); });
  auto const s2 = [](auto r) { r.set_value(); };
  lazy::sync_wait(lazy::async_then<>(s1, s2), c);
}

TEST_CASE("lazy async_then receiving and returning move only type")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<move_only>(
      [](auto&& receiver) { receiver.set_value(move_only{}); });
  auto const s2 = [](auto r, auto v) { r.set_value(std::move(v)); };
  lazy::sync_wait(lazy::async_then<move_only>(s1, s2), c);
}

TEST_CASE("lazy async_then with error")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_value(21); });
  auto const twice = [](auto r, int v) {
    r.set_error(std::make_exception_ptr(v * 2));
  };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, twice), c), int);
}

TEST_CASE("lazy async_then with throw")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_value(21); });
  auto const twice = [](auto r, int v) { throw v * 2; };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, twice), c), int);
}

TEST_CASE("lazy async_then never run because of error")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<int>(
      [](auto&& receiver) { receiver.set_error(std::make_exception_ptr(42)); });
  auto const twice = [](auto r, int v) { REQUIRE(false); };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, twice), c), int);
}

TEST_CASE("lazy async_then never run because of throw")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<int>([](auto&& receiver) { throw 42; });
  auto const twice = [](auto r, int v) { REQUIRE(false); };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, twice), c), int);
}

TEST_CASE("lazy async_then never run because of abort")
{
  lazy::cancelation_token c;
  auto const s1 =
      make_sender<int>([](auto&& receiver) { receiver.set_done(); });
  auto const twice = [](auto r, int v) { REQUIRE(false); };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, twice), c),
                  operation_canceled);
}

// async

TEST_CASE("lazy async")
{
  thread_pool tp;
  tp.start(1);
  lazy::cancelation_token c;
  auto const s1 = lazy::async(executor{tp});
  auto const s2 = lazy::then(s1, [&] { REQUIRE(tp.is_in_this_context()); });
  lazy::sync_wait(s2, c);
}

// async_wait

TEST_CASE("lazy async_wait [waiting]")
{
  std::chrono::milliseconds const delay{100};

  lazy::cancelation_token c;
  auto const s1 = lazy::async_wait(get_default_executor(), delay);

  auto before = std::chrono::steady_clock::now();
  lazy::sync_wait(s1, c);
  auto after = std::chrono::steady_clock::now();
  CHECK(delay < after - before);
}

// cancelation_token

TEST_CASE("is_cancel_requested is true after request_cancel")
{
  lazy::cancelation_token c;
  c.request_cancel();
  lazy::sync_wait(
      make_sender<void>([](auto&& receiver) {
        CHECK(receiver.get_cancelation_token()->is_cancel_requested());
        receiver.set_value();
      }),
      c);
}

TEST_CASE("canceler is called if cancel is already requested")
{
  lazy::cancelation_token c;
  c.request_cancel();
  auto const sender = make_sender<void>([](auto&& receiver) {
    receiver.get_cancelation_token()->set_canceler(
        [&] { receiver.set_done(); });
  });
  CHECK_THROWS_AS(lazy::sync_wait(sender, c), operation_canceled);
}

TEST_CASE("then does not run continuation if cancel was requested")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<void>([&](auto&& receiver) {
    c.request_cancel();
    receiver.set_value();
  });
  auto const s2 = []() { REQUIRE(false); };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::then(s1, s2), c), operation_canceled);
}

TEST_CASE("async_then does not run continuation if cancel was requested")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<void>([&](auto&& receiver) {
    c.request_cancel();
    receiver.set_value();
  });
  auto const s2 = [](auto&& receiver) { REQUIRE(false); };
  CHECK_THROWS_AS(lazy::sync_wait(lazy::async_then(s1, s2), c),
                  operation_canceled);
}

TEST_CASE("then does not run an obsolete canceler")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<void>([](auto&& receiver) {
    receiver.get_cancelation_token()->set_canceler([]() { REQUIRE(false); });
    receiver.set_value();
  });
  auto const s2 = [&]() { c.request_cancel(); };
  CHECK_NOTHROW(lazy::sync_wait(lazy::then(s1, s2), c));
}

TEST_CASE("async_then does not run an obsolete canceler")
{
  lazy::cancelation_token c;
  auto const s1 = make_sender<void>([](auto&& receiver) {
    receiver.get_cancelation_token()->set_canceler([]() { REQUIRE(false); });
    receiver.set_value();
  });
  auto const s2 = [&](auto&& receiver) {
    c.request_cancel();
    receiver.set_value();
  };
  CHECK_NOTHROW(lazy::sync_wait(lazy::async_then(s1, s2), c));
}

TEST_CASE("request_cancel should call canceler")
{
  lazy::cancelation_token c;
  // we use an internal of sync_wait to write this test because it is very
  // helpful
  lazy::detail::sync_state<tvoid> state;

  auto const s1 = make_sender<void>([](auto&& receiver) {
    receiver.get_cancelation_token()->set_canceler(
        [receiver = std::move(receiver)]() mutable { receiver.set_done(); });
  });
  s1.submit(lazy::detail::sync_receiver<void>{&state, &c});
  CHECK(state.data.index() == 0);
  c.request_cancel();
  CHECK(state.data.index() == 1);
}

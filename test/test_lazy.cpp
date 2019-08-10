#undef __GTHREADS

#include <tconcurrent/lazy/sr.hpp>

#include <tconcurrent/async.hpp>
#include <tconcurrent/async_wait.hpp>

#include <boost/asio/steady_timer.hpp>

#include <doctest.h>

#include <iostream>

using namespace tconcurrent;
using namespace std::chrono_literals;

namespace
{
auto run_async()
{
  return [](auto p) { async([p = std::move(p)]() mutable { p.set_value(); }); };
}

template <typename T>
auto async_algo(T task)
{
  return lazy::then(task, [] { return 10; });
}

template <typename P>
struct async_wait_data
{
  boost::asio::steady_timer timer;
  P prom;
  std::atomic<bool> fired{false};

  template <typename Delay>
  async_wait_data(boost::asio::io_service& io, Delay delay, P&& p)
    : timer(io, delay), prom(std::forward<P>(p))
  {
  }
};

auto async_wait_lazy(executor executor,
                     std::chrono::steady_clock::duration delay)
{
  return [executor = std::move(executor), delay](auto p) mutable {
    auto const data = std::make_shared<async_wait_data<decltype(p)>>(
        executor.get_io_service(), delay, std::move(p));

    data->prom.get_cancelation_token()->cancel = [data] {
      if (data->fired.exchange(true))
        return;

      data->timer.cancel();
      data->prom.set_error(std::make_exception_ptr(operation_canceled()));
    };

    data->timer.async_wait([data](boost::system::error_code const&) mutable {
      if (!data->fired.exchange(true))
        data->prom.set_value();
    });
  };
}

// struct map_promise
// {
//   P p_;
//   F fun_;
//   auto get_cancelation_token()
//   {
//     return p_.get_cancelation_token();
//   }
//   template <typename... V>
//   void set_value(V... vs)
//   {
//     fun_(p_, std::forward<V>(vs)...);
//   }
//   template <typename E>
//   void set_error(E&& e)
//   {
//     p_.set_error(std::forward<E>(e));
//   }
// };
//
// template <typename P, typename F>
// void map()
// {
//   return [](auto p) {};
// }
}

TEST_CASE("lazy")
{
  auto n = run_async();
  auto f = async_algo(n);
  auto f2 = lazy::then(f, [](int i) { return 10 + i; });
  auto f3 = lazy::then2(f2, [](auto p, int i) {
    lazy::then2(async_wait_lazy(get_default_executor(), 100ms),
                [i](auto p) mutable { p.set_value(i + 10); })(p);
  });
  auto f4 = lazy::then2(f3, [](auto p, int i) {
    lazy::then2(async_wait_lazy(get_default_executor(), 100ms),
                [i](auto p) mutable { p.set_value(i + 10); })(p);
  });
  lazy::cancelation_token c;
  // async_wait(50ms).then([&](auto const&) { c.request_cancel(); });
  async_wait(150ms).then([&](auto const&) { c.request_cancel(); });
  std::cout << "result: " << lazy::sync_wait<int>(f4, c) << std::endl;
}

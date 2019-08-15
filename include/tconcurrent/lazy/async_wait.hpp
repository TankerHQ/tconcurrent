#ifndef TCONCURRENT_LAZY_ASYNC_WAIT_HPP
#define TCONCURRENT_LAZY_ASYNC_WAIT_HPP

#include <tconcurrent/lazy/cancelation_token.hpp>

#include <tconcurrent/executor.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>

namespace tconcurrent
{
namespace lazy
{
namespace details
{
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
}

auto async_wait(executor executor, std::chrono::steady_clock::duration delay)
{
  return [&io_service = executor.get_io_service(), delay](auto p) mutable {
    auto const data = std::make_shared<details::async_wait_data<decltype(p)>>(
        io_service, delay, std::move(p));

    data->timer.async_wait([data](boost::system::error_code const&) mutable {
      if (data->fired.exchange(true))
        return;

      data->prom.set_value();
    });

    data->prom.get_cancelation_token()->set_canceler([data] {
      if (data->fired.exchange(true))
        return;

      data->timer.cancel();
      data->prom.set_done();
    });
  };
}
}
}

#endif

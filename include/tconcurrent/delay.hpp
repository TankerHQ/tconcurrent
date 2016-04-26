#ifndef TCONCURRENT_DELAY_HPP
#define TCONCURRENT_DELAY_HPP

#include <atomic>

#include <boost/asio/steady_timer.hpp>

#include <tconcurrent/promise.hpp>
#include <tconcurrent/future.hpp>
#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{

struct operation_canceled : std::exception
{
  const char* what() const BOOST_NOEXCEPT override
  {
    return "operation was canceled";
  }
};

struct cancelable_bundle
{
  using canceler = std::function<void()>;

  future<void> fut;
  canceler cancel;
};

namespace detail
{

struct async_wait_data
{
  boost::asio::steady_timer timer;
  promise<void> prom;
  std::atomic<bool> fired{false};

  template <typename Delay>
  async_wait_data(boost::asio::io_service& io, Delay delay)
    : timer(io, delay)
  {
  }
};

}

template <typename Delay>
cancelable_bundle async_wait(thread_pool& pool, Delay delay)
{
  if (pool.is_single_threaded())
  {
    auto const data =
        std::make_shared<detail::async_wait_data>(pool.get_io_service(), delay);

    data->timer.async_wait(
        [data](boost::system::error_code const&) mutable {
          if (!data->fired.exchange(true))
            data->prom.set_value(0);
        });

    return {data->prom.get_future(), [data]() mutable {
              if (data->fired.exchange(true))
                return;

              data->timer.cancel();
              data->prom.set_exception(
                  std::make_exception_ptr(operation_canceled()));
            }};
  }
  else
  {
    auto const timer = std::make_shared<boost::asio::steady_timer>(
        pool.get_io_service(), delay);

    auto taskfut = package<void(boost::system::error_code const&)>(
        [timer](boost::system::error_code const& error) {
          if (error == boost::asio::error::operation_aborted)
            // ugly throw, bad performance :(
            throw operation_canceled();
        });

    auto& task = std::get<0>(taskfut);
    auto& fut = std::get<1>(taskfut);

    timer->async_wait(task);

    return {fut, [timer] { timer->cancel(); }};
  }
}

template <typename Delay>
cancelable_bundle async_wait(Delay delay)
{
  return async_wait(get_default_executor(), delay);
}

}

#endif

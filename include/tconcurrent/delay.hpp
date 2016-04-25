#ifndef TCONCURRENT_DELAY_HPP
#define TCONCURRENT_DELAY_HPP

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

template <typename Delay>
cancelable_bundle async_wait(thread_pool& pool, Delay delay)
{
  auto const timer =
      std::make_shared<boost::asio::steady_timer>(pool.get_io_service(), delay);

  if (pool.is_single_threaded())
  {
    promise<void> prom;

    timer->async_wait(
        [timer, prom](boost::system::error_code const& error) mutable {
          if (error != boost::asio::error::operation_aborted)
            prom.set_value(0);
        });

    return {prom.get_future(), [timer, prom]() mutable {
              if (prom.get_future().is_ready())
                return;

              timer->cancel();
              prom.set_exception(std::make_exception_ptr(operation_canceled()));
            }};
  }
  else
  {
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

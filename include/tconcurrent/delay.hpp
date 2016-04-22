#ifndef TCONCURRENT_DELAY_HPP
#define TCONCURRENT_DELAY_HPP

#include <boost/asio/steady_timer.hpp>

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

  auto taskfut = package<void(boost::system::error_code const&)>(
      [timer](boost::system::error_code const& error)
      {
        if (error == boost::asio::error::operation_aborted)
          // ugly throw, bad performance :(
          throw operation_canceled();
      });

  auto& task = std::get<0>(taskfut);
  auto& fut  = std::get<1>(taskfut);

  timer->async_wait(task);

  return {fut,
          [timer]
          {
            timer->cancel();
          }};
}

template <typename Delay>
cancelable_bundle async_wait(Delay delay)
{
  return async_wait(get_default_executor(), delay);
}

}

#endif

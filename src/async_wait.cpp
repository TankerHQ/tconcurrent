#include <tconcurrent/async_wait.hpp>

#include <atomic>

#include <boost/asio/steady_timer.hpp>

#include <tconcurrent/promise.hpp>
#include <tconcurrent/packaged_task.hpp>

namespace tconcurrent
{

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

future<void> async_wait(thread_pool& pool,
                        std::chrono::steady_clock::duration delay)
{
  if (pool.is_single_threaded())
  {
    auto const data =
        std::make_shared<detail::async_wait_data>(pool.get_io_service(), delay);

    data->timer.async_wait(
        [data](boost::system::error_code const&) mutable {
          if (!data->fired.exchange(true))
          {
            data->prom.get_cancelation_token().pop_cancelation_callback();
            data->prom.set_value({});
          }
        });

    data->prom.get_cancelation_token().push_cancelation_callback([data] {
      if (data->fired.exchange(true))
        return;

      data->timer.cancel();
      data->prom.get_cancelation_token().pop_cancelation_callback();
      data->prom.set_exception(std::make_exception_ptr(operation_canceled()));
    });

    return data->prom.get_future();
  }
  else
  {
    auto token = std::make_shared<cancelation_token>();

    auto const timer = std::make_shared<boost::asio::steady_timer>(
        pool.get_io_service(), delay);

    auto taskfut = package<void(boost::system::error_code const&)>(
        [timer, token](boost::system::error_code const& error) {
          token->pop_cancelation_callback();
          if (error == boost::asio::error::operation_aborted)
            // TODO ugly throw, bad performance :(
            throw operation_canceled();
        },
        token);

    auto& task = std::get<0>(taskfut);
    auto& fut = std::get<1>(taskfut);

    token->push_cancelation_callback([timer] { timer->cancel(); });

    timer->async_wait(task);

    return std::move(fut);
  }
}

}

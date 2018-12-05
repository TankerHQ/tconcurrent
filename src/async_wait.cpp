#include <tconcurrent/async_wait.hpp>

#include <atomic>

#include <boost/asio/steady_timer.hpp>

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/promise.hpp>

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
  async_wait_data(boost::asio::io_service& io, Delay delay) : timer(io, delay)
  {
  }
};
}

future<void> async_wait(executor executor,
                        std::chrono::steady_clock::duration delay)
{
  auto const data = std::make_shared<detail::async_wait_data>(
      executor.get_io_service(), delay);

  data->prom.get_cancelation_token().push_cancelation_callback([data] {
    if (data->fired.exchange(true))
      return;

    data->timer.cancel();
    data->prom.get_cancelation_token().pop_cancelation_callback();
    data->prom.set_exception(std::make_exception_ptr(operation_canceled()));
  });

  data->timer.async_wait([data](boost::system::error_code const&) mutable {
    if (!data->fired.exchange(true))
    {
      data->prom.get_cancelation_token().pop_cancelation_callback();
      data->prom.set_value({});
    }
  });

  return data->prom.get_future();
}
}

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
namespace detail
{
template <typename Receiver>
struct async_wait_data
{
  boost::asio::steady_timer timer;
  Receiver receiver;
  std::atomic<bool> fired{false};

  template <typename Delay, typename ReceiverArg>
  async_wait_data(boost::asio::io_service& io,
                  Delay delay,
                  ReceiverArg&& receiver)
    : timer(io, delay), receiver(std::forward<ReceiverArg>(receiver))
  {
  }
};

struct async_wait_sender
{
  boost::asio::io_service* io_service;
  std::chrono::steady_clock::duration delay;

  template <typename R>
  void submit(R&& receiver)
  {
    auto const data = std::make_shared<
        detail::async_wait_data<std::decay_t<decltype(receiver)>>>(
        *io_service, delay, std::forward<decltype(receiver)>(receiver));

    data->timer.async_wait([data](boost::system::error_code const&) mutable {
      if (data->fired.exchange(true))
        return;

      data->receiver.set_value();
    });

    data->receiver.get_cancelation_token()->set_canceler([data] {
      if (data->fired.exchange(true))
        return;

      data->timer.cancel();
      data->receiver.set_done();
    });
  }

  template <template <typename...> class Tuple>
  using value_types = Tuple<>;
};
}

/** Make a sender that calls its receiver on \p executor after \p delay.
 */
auto async_wait(executor executor, std::chrono::steady_clock::duration delay)
{
  return detail::async_wait_sender{&executor.get_io_service(), delay};
}
}
}

#endif

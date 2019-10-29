#ifndef TCONCURRENT_LAZY_ASYNC_HPP
#define TCONCURRENT_LAZY_ASYNC_HPP

#include <atomic>
#include <memory>
#include <string>

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
template <typename Receiver>
struct async_data
{
  Receiver receiver;
  std::atomic<bool> fired{false};

  async_data(Receiver receiver) : receiver(std::move(receiver))
  {
  }
};

template <typename E>
struct async_sender
{
  template <template <typename...> class Tuple>
  using value_types = Tuple<>;

  E executor;
  std::string name;

  template <typename R>
  void submit(R&& receiver)
  {
    auto data = std::make_unique<detail::async_data<std::decay_t<R>>>(
        std::forward<R>(receiver));
    data->receiver.get_cancelation_token()->set_canceler(
        [data = data.get()]() mutable {
          if (data->fired.exchange(true))
            return;
          data->receiver.set_done();
        });
    executor.post(
        [data = std::move(data)]() mutable {
          if (data->fired.exchange(true))
            return;
          data->receiver.set_value();
        },
        std::move(name));
  };
};
}

template <typename E>
auto async(E&& executor, std::string name = {})
{
  return detail::async_sender<std::decay_t<E>>{std::forward<E>(executor),
                                               std::move(name)};
}
}
}

#endif

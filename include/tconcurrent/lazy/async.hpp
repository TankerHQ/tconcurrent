#ifndef TCONCURRENT_LAZY_ASYNC_HPP
#define TCONCURRENT_LAZY_ASYNC_HPP

#include <atomic>
#include <memory>

namespace tconcurrent
{
namespace lazy
{
namespace detail
{
template <typename P>
struct async_data
{
  P p;
  std::atomic<bool> fired{false};

  async_data(P&& p) : p(std::forward<P>(p))
  {
  }
};
}

template <typename E>
auto async(E&& executor)
{
  return [executor = std::forward<E>(executor)](auto&& p) mutable {
    auto data = std::make_unique<detail::async_data<std::decay_t<decltype(p)>>>(
        std::forward<decltype(p)>(p));
    data->p.get_cancelation_token()->set_canceler(
        [data = data.get()]() mutable {
          if (data->fired.exchange(true))
            return;
          data->p.set_done();
        });
    executor.post([data = std::move(data)]() mutable {
      if (data->fired.exchange(true))
        return;
      data->p.set_value();
    });
  };
}
}
}

#endif

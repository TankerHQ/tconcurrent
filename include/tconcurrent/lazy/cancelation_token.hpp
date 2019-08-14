#ifndef TCONCURRENT_LAZY_CANCELATION_TOKEN_HPP
#define TCONCURRENT_LAZY_CANCELATION_TOKEN_HPP

#include <functional>
#include <mutex>

namespace tconcurrent
{
namespace lazy
{
struct operation_canceled
{
};

struct cancelation_token
{
  std::mutex mutex;
  bool canceled = false;
  std::function<void()> cancel;

  void request_cancel()
  {
    auto canceler = [&] {
      std::lock_guard<std::mutex> l(mutex);
      return cancel;
    }();
    if (canceler)
      canceler();
  }
  void reset()
  {
    std::lock_guard<std::mutex> l(mutex);
    cancel = nullptr;
  }
};
}
}

#endif

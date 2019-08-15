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

class cancelation_token
{
public:
  using Canceler = std::function<void()>;

  void request_cancel()
  {
    auto canceler = [&] {
      std::lock_guard<std::mutex> l(_mutex);
      _canceled = true;
      return _cancel;
    }();
    if (canceler)
      canceler();
  }
  bool is_canceled() const
  {
    std::lock_guard<std::mutex> l(_mutex);
    return _canceled;
  }
  void set_canceler(Canceler canceler)
  {
    auto canceler_to_run = [&] {
      std::lock_guard<std::mutex> l(_mutex);
      _cancel = std::move(canceler);
      if (_canceled)
        return _cancel;
      else
        return Canceler();
    }();
    if (canceler_to_run)
      canceler_to_run();
  }
  void reset()
  {
    std::lock_guard<std::mutex> l(_mutex);
    _cancel = nullptr;
  }

private:
  mutable std::mutex _mutex;
  bool _canceled = false;
  Canceler _cancel;
};
}
}

#endif

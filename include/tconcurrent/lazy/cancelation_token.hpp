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
    lock_guard l(_mutex);
    _canceled = true;
    if (_cancel)
      _cancel();
  }
  bool is_canceled() const
  {
    lock_guard l(_mutex);
    return _canceled;
  }
  void set_canceler(Canceler canceler)
  {
    lock_guard l(_mutex);
    _cancel = std::move(canceler);
    if (_canceled)
      _cancel();
  }
  void reset()
  {
    lock_guard l(_mutex);
    _cancel = nullptr;
  }

private:
  using lock_guard = std::lock_guard<std::recursive_mutex>;
  mutable std::recursive_mutex _mutex;
  bool _canceled = false;
  Canceler _cancel;
};
}
}

#endif

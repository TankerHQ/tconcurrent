#ifndef TCONCURRENT_LAZY_CANCELATION_TOKEN_HPP
#define TCONCURRENT_LAZY_CANCELATION_TOKEN_HPP

#include <mutex>

#include <function2/function2.hpp>

namespace tconcurrent
{
namespace lazy
{
class cancelation_token
{
public:
  using Canceler = fu2::function<void()>;

  class scope_canceler
  {
  public:
    scope_canceler() = default;
    scope_canceler(cancelation_token* token, Canceler cb) : _token(token)
    {
      assert(cb);
      _token->set_canceler(std::move(cb));
    }

    ~scope_canceler()
    {
      _token->reset();
    }

    scope_canceler(scope_canceler&&) = default;
    scope_canceler& operator=(scope_canceler&&) = default;

  private:
    cancelation_token* _token;
  };

  void request_cancel()
  {
    lock_guard l(_mutex);
    if (_canceled)
      return;
    _canceled = true;
    // We must not call _cancel directly because it can call this object's
    // destructor. Instead, make a copy to keep the function alive the time of
    // the call.
    auto canceler = _cancel;
    if (canceler)
      canceler();
  }
  bool is_cancel_requested() const
  {
    lock_guard l(_mutex);
    return _canceled;
  }
  void set_canceler(Canceler canceler)
  {
    lock_guard l(_mutex);
    assert(!_cancel);
    _cancel = std::move(canceler);
    if (_canceled)
      _cancel();
  }
  void reset()
  {
    lock_guard l(_mutex);
    _cancel = nullptr;
  }

  scope_canceler make_scope_canceler(Canceler cb)
  {
    return scope_canceler(this, std::move(cb));
  }

private:
  // We need a recursive_mutex here because cancelers are called while the mutex
  // is held. A canceler can call set_done which usually calls
  // cancelation_token.reset().
  using lock_guard = std::lock_guard<std::recursive_mutex>;
  mutable std::recursive_mutex _mutex;
  bool _canceled = false;
  Canceler _cancel;
};
}
}

#endif

#ifndef TCONCURRENT_CANCELATION_TOKEN_HPP
#define TCONCURRENT_CANCELATION_TOKEN_HPP

#include <functional>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <cassert>

#include <boost/core/null_deleter.hpp>

namespace tconcurrent
{

struct operation_canceled : std::exception
{
  const char* what() const noexcept override
  {
    return "operation was canceled";
  }
};

class cancelation_token
{
public:
  using cancelation_callback = std::function<void()>;

  class scope_canceler
  {
  public:
    scope_canceler(cancelation_token* token, cancelation_callback cb)
      : _token(token)
    {
      assert(cb);
      _token->set_cancelation_callback(std::move(cb));
    }
    ~scope_canceler()
    {
      _token->set_cancelation_callback({});
    }

    scope_canceler(scope_canceler&&) = default;
    scope_canceler& operator=(scope_canceler&&) = default;

  private:
    std::unique_ptr<cancelation_token, boost::null_deleter> _token;
  };

  bool is_cancel_requested() const
  {
    scope_lock l(_mutex);
    return _is_cancel_requested;
  }

  void set_cancelation_callback(cancelation_callback cb)
  {
    auto f = [&] {
      scope_lock l(_mutex);
      _do_cancel = std::move(cb);
      if (_do_cancel && _is_cancel_requested)
        return _do_cancel;
      else
        return cancelation_callback{};
    }();
    if (f)
      f();
  }

  scope_canceler make_scope_canceler(cancelation_callback cb)
  {
    return scope_canceler(this, std::move(cb));
  }

  void request_cancel()
  {
    auto f = [&] {
      scope_lock l(_mutex);
      _is_cancel_requested = true;
      return _do_cancel;
    }();
    if (f)
      f();
  }

private:
  using mutex = std::mutex;
  using scope_lock = std::lock_guard<mutex>;

  mutable mutex _mutex;

  bool _is_cancel_requested{false};
  std::function<void()> _do_cancel;
};

using cancelation_token_ptr = std::shared_ptr<cancelation_token>;

}

#endif

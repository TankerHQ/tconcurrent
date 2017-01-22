#ifndef TCONCURRENT_CANCELATION_TOKEN_HPP
#define TCONCURRENT_CANCELATION_TOKEN_HPP

#include <deque>
#include <functional>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <cassert>

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
    scope_canceler() = default;
    scope_canceler(cancelation_token* token, cancelation_callback cb)
      : _token(token)
    {
      assert(cb);
      _token->push_cancelation_callback(std::move(cb));
    }

    scope_canceler(scope_canceler&&) = default;
    scope_canceler& operator=(scope_canceler&&) = default;

  private:
    struct deleter
    {
      void operator()(cancelation_token* token) const
      {
        token->pop_cancelation_callback();
      }
    };

    std::unique_ptr<cancelation_token, deleter> _token;
  };

  bool is_cancel_requested() const
  {
    scope_lock l(_mutex);
    return _is_cancel_requested;
  }

  void push_cancelation_callback(cancelation_callback cb)
  {
    auto current = [&] {
      scope_lock l(_mutex);
      _do_cancels.push_back(std::move(cb));
      return _is_cancel_requested ? _do_cancels.back() : cancelation_callback{};
    }();
    if (current)
      current();
  }

  void push_last_cancelation_callback(cancelation_callback cb)
  {
    auto current = [&] {
      scope_lock l(_mutex);
      _do_cancels.push_front(std::move(cb));
      return _is_cancel_requested && _do_cancels.size() == 1 ?
                 _do_cancels.back() :
                 cancelation_callback{};
    }();
    if (current)
      current();
  }

  void pop_cancelation_callback()
  {
    auto current = [&] {
      scope_lock l(_mutex);
      _do_cancels.pop_back();
      return _is_cancel_requested && !_do_cancels.empty() ?
                 _do_cancels.back() :
                 cancelation_callback{};
    }();
    if (current)
      current();
  }

  /** Set a cancelation callback for a scope duration
   *
   * This will set a cancelation callback that will be automatically unset when
   * the returned scope_canceler is destroyed.
   *
   * It is possible to nest cancelation callbacks like this:
   *
   *     {
   *       auto const c1 = make_scope_canceler(canceler1);
   *       // canceler1 will be called if a cancelation is requested here
   *       {
   *         auto const c2 = make_scope_canceler(canceler2);
   *         // canceler2 will be called if a cancelation is requested here
   *       }
   *       // canceler1 will be called if a cancelation is requested here
   *     }
   */
  scope_canceler make_scope_canceler(cancelation_callback cb)
  {
    return scope_canceler(this, std::move(cb));
  }

  void request_cancel()
  {
    auto f = [&] {
      scope_lock l(_mutex);
      _is_cancel_requested = true;
      return !_do_cancels.empty() ? _do_cancels.back() :
                                    cancelation_callback{};
    }();
    if (f)
      f();
  }

private:
  using mutex = std::mutex;
  using scope_lock = std::lock_guard<mutex>;

  mutable mutex _mutex;

  bool _is_cancel_requested{false};
  std::deque<cancelation_callback> _do_cancels;
};

using cancelation_token_ptr = std::shared_ptr<cancelation_token>;

}

#endif

#ifndef TCONCURRENT_LAZY_SENDER_RECEIVER_HPP
#define TCONCURRENT_LAZY_SENDER_RECEIVER_HPP

#include <mpark/variant.hpp>

#include <condition_variable>
#include <functional>
#include <mutex>

namespace tconcurrent
{
namespace lazy
{
struct cancelation_token
{
  bool canceled = false;
  std::function<void()> cancel;

  void request_cancel()
  {
    if (cancel)
      cancel();
  }
  void reset()
  {
    cancel = nullptr;
  }
};

namespace details
{
template <typename P, typename F>
struct then_promise
{
  P p_;
  F fun_;
  auto get_cancelation_token()
  {
    return p_.get_cancelation_token();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    p_.get_cancelation_token()->reset();
    try
    {
      p_.set_value(fun_(std::forward<V>(vs)...));
    }
    catch (...)
    {
      p_.set_error(std::current_exception());
    }
  }
  template <typename E>
  void set_error(E&& e)
  {
    p_.get_cancelation_token()->reset();
    p_.set_error(std::forward<E>(e));
  }
};

template <typename P, typename F>
struct then2_promise
{
  P p_;
  F fun_;
  auto get_cancelation_token()
  {
    return p_.get_cancelation_token();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    p_.get_cancelation_token()->reset();
    try
    {
      // XXX can't move p because we use it in catch, what should we do?
      fun_(p_, std::forward<V>(vs)...);
    }
    catch (...)
    {
      p_.set_error(std::current_exception());
    }
  }
  template <typename E>
  void set_error(E&& e)
  {
    p_.get_cancelation_token()->reset();
    p_.set_error(std::forward<E>(e));
  }
};

template <typename T>
struct sync_state
{
  std::mutex mtx;
  std::condition_variable cv;
  mpark::variant<mpark::monostate, std::exception_ptr, T> data;
};

template <class T>
struct sync_promise
{
  sync_state<T>* pst;
  cancelation_token* cancelation_token;
  auto get_cancelation_token()
  {
    return cancelation_token;
  }
  template <int I, typename... V>
  void _set(V... xs)
  {
    cancelation_token->reset();
    auto lk = std::unique_lock<std::mutex>{pst->mtx};
    pst->data.template emplace<I>(std::forward<V>(xs)...);
    pst->cv.notify_one();
  }
  template <typename... V>
  void set_value(V... vs)
  {
    _set<2>(std::forward<V>(vs)...);
  }
  template <typename E>
  void set_error(E&& e)
  {
    _set<1>(std::forward<E>(e));
  }
};

struct sink_promise
{
  template <typename... V>
  void set_value(V...)
  {
  }
  template <typename E>
  void set_error(E&& e)
  {
    std::terminate();
  }
};
}

template <typename T, typename F>
auto then(T task, F fun)
{
  return [=](auto p) mutable {
    task(details::then_promise<decltype(p), decltype(fun)>{p, fun});
  };
}

template <typename T, typename F>
auto then2(T&& task, F&& fun)
{
  return [task = std::forward<T>(task),
          fun = std::forward<F>(fun)](auto p) mutable {
    task(details::then2_promise<decltype(p), decltype(fun)>{std::move(p),
                                                            std::move(fun)});
  };
}

template <class T, class Task>
T sync_wait(Task task, cancelation_token& c)
{
  details::sync_state<T> state;

  task(details::sync_promise<T>{&state, &c});

  {
    auto lk = std::unique_lock<std::mutex>{state.mtx};
    state.cv.wait(lk, [&state] { return state.data.index() != 0; });
  }

  if (state.data.index() == 1)
    std::rethrow_exception(mpark::get<1>(state.data));
  return std::move(mpark::get<2>(state.data));
}
}
}

#endif

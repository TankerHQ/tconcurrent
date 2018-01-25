/*
 * Copyright 2015 Adobe Systems Incorporated Distributed under the MIT License
 * (see license at http://stlab.adobe.com/licenses.html)
 */

#ifndef PACKAGED_TASK_HPP
#define PACKAGED_TASK_HPP

#include <memory>
#include <tconcurrent/future.hpp>

namespace tconcurrent
{

namespace detail
{

struct packaged_task_result_type_2
{
  template <typename F, typename... Args>
  static decltype(std::declval<F>()(std::declval<Args>()...)) f(long);
  template <typename F, typename... Args>
  static decltype(std::declval<F>()(std::declval<cancelation_token&>(),
                                    std::declval<Args>()...))
  f(int);
};

template <typename Call>
struct packaged_task_result_type_; // not defined

template <typename F, typename... Args>
struct packaged_task_result_type_<F(Args...)>
{
  using type = decltype(packaged_task_result_type_2::f<F, Args...>(0));
};

}

template <typename Call>
using packaged_task_result_type =
    typename detail::packaged_task_result_type_<Call>::type;

namespace detail
{

template <typename...>
using void_t = void;

template <typename R>
struct package_caller
{
  template <typename F, typename... Args>
  static void do_call(shared_base<R>& s, F&& func, Args&&... args)
  {
    s.set(func(std::forward<Args>(args)...));
  }
};

template <>
struct package_caller<void>
{
  template <typename F, typename... Args>
  static void do_call(shared_base<tvoid>& s, F&& func, Args&&... args)
  {
    func(std::forward<Args>(args)...);
    s.set({});
  }
};

template <typename>
struct shared; // not defined

template <typename R, typename... Args>
struct shared<R(Args...)> : shared_base<shared_base_type<R>>
{
  using base_type = shared_base<shared_base_type<R>>;

  std::atomic<bool> _done{false};
  std::function<R(cancelation_token&, Args...)> _f;
  bool _cancelable;

  template <typename F>
  shared(
      bool cancelable,
      cancelation_token_ptr token,
      F&& f,
      void_t<decltype(std::declval<F>()(std::declval<Args>()...))>* = nullptr)
    : base_type(std::move(token)),
      _cancelable(cancelable),
      // TODO vs2015 doesn't accept a forward for f here...
      _f([f](cancelation_token&, auto&&... args) mutable {
        return f(std::forward<decltype(args)>(args)...);
      })
  {
  }

  template <typename F>
  shared(
      bool cancelable,
      cancelation_token_ptr token,
      F&& f,
      void_t<decltype(std::declval<F>()(std::declval<cancelation_token&>(),
                                        std::declval<Args>()...))>** = nullptr)
    : base_type(std::move(token)),
      _cancelable(cancelable),
      _f(std::forward<F>(f))
  {
    assert(_f);
  }

  template <typename... A>
  void operator()(A&&... args)
  {
    if (_done.exchange(true))
      return;
    try
    {
      if (_cancelable)
        this->_cancelation_token->pop_cancelation_callback();
      package_caller<R>::do_call(
          *this, _f, *this->_cancelation_token, std::forward<A>(args)...);
    }
    catch (...)
    {
      this->set_exception(std::current_exception());
    }
    _f = nullptr;
  }
};

template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token, bool cancelable)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>
{
  auto p = std::make_shared<detail::shared<S>>(
      cancelable, token, std::forward<F>(f));
  if (cancelable)
    token->push_cancelation_callback([p, token = token.get()] {
      if (p->_done.exchange(true))
        return;
      token->pop_cancelation_callback();
      p->set_exception(std::make_exception_ptr(operation_canceled{}));
      p->_f = nullptr;
    });
  return std::make_pair(packaged_task<S>(p),
                        future<detail::result_of_t_<S>>(p));
}

}

template <typename R, typename... Args>
class packaged_task<R(Args...)>
{
public:
  template <typename... A>
  void operator()(A&&... args) const
  {
    (*_p)(std::forward<A>(args)...);
  }

private:
  using shared_type = detail::shared<R(Args...)>;

  detail::promise_ptr<shared_type> _p;

  template <typename S, typename F>
  friend auto detail::package(
      F&& f, cancelation_token_ptr token, bool cancelable)
      -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;

  explicit packaged_task(std::shared_ptr<detail::shared<R(Args...)>> p)
    : _p(std::move(p))
  {
  }
};

template <typename S, typename F>
auto package(F&& f, cancelation_token_ptr token)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>
{
  return detail::package<S>(
      std::forward<F>(f), token, false);
}

template <typename S, typename F>
auto package(F&& f)
{
  return detail::package<S>(
      std::forward<F>(f), std::make_shared<cancelation_token>(), false);
}

template <typename S, typename F>
auto package_cancelable(F&& f)
{
  return detail::package<S>(
      std::forward<F>(f), std::make_shared<cancelation_token>(), true);
}

}

#endif

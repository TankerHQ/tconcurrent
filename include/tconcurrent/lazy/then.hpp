#ifndef TCONCURRENT_LAZY_THEN_HPP
#define TCONCURRENT_LAZY_THEN_HPP

#include <tconcurrent/lazy/cancelation_token.hpp>

namespace tconcurrent
{
namespace lazy
{
namespace details
{
template <typename P, typename F>
struct then_receiver
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
  void set_done()
  {
    p_.get_cancelation_token()->reset();
    p_.set_done();
  }
};

template <typename P, typename F>
struct async_then_receiver
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
  void set_done()
  {
    p_.get_cancelation_token()->reset();
    p_.set_done();
  }
};
}

template <typename T, typename F>
auto then(T task, F fun)
{
  return [=](auto&& p) mutable {
    task(details::then_receiver<std::decay_t<decltype(p)>, decltype(fun)>{
        std::forward<decltype(p)>(p), std::move(fun)});
  };
}

template <typename T, typename F>
auto async_then(T&& task, F&& fun)
{
  return [task = std::forward<T>(task),
          fun = std::forward<F>(fun)](auto&& p) mutable {
    task(details::async_then_receiver<std::decay_t<decltype(p)>, decltype(fun)>{
        std::forward<decltype(p)>(p), std::move(fun)});
  };
}
}
}

#endif

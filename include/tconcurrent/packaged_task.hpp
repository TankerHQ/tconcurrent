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

template <typename>
struct shared; // not defined

template <typename R, typename... Args>
struct shared<R(Args...)> : detail::shared_base<R>
{
  std::function<R(Args...)> _f;

  template <typename F>
  shared(F&& f)
    : _f(std::forward<F>(f))
  {
    assert(_f);
  }

  template <typename... A>
  void operator()(A&&... args)
  {
    try
    {
      this->set(_f(std::forward<A>(args)...));
    }
    catch (...)
    {
      this->set_exception(std::current_exception());
    }
    _f = nullptr;
  }
};

template <typename... Args>
struct shared<void(Args...)> : detail::shared_base<void*>
{
  std::function<void(Args...)> _f;

  template <typename F>
  shared(F&& f)
    : _f(std::forward<F>(f))
  {
  }

  template <typename... A>
  void operator()(A&&... args)
  {
    try
    {
      _f(std::forward<A>(args)...);
      this->set(0);
    }
    catch (...)
    {
      this->set_exception(std::current_exception());
    }
    _f = nullptr;
  }
};

}

template <typename R, typename... Args>
class packaged_task<R(Args...)>
{
public:
  packaged_task() = default;

  template <typename... A>
  void operator()(A&&... args) const
  {
    (*_p)(std::forward<A>(args)...);
  }

private:
  std::shared_ptr<detail::shared<R(Args...)>> _p;

  template <typename S, typename F>
  friend auto package(F&& f)
      -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>;

  explicit packaged_task(std::shared_ptr<detail::shared<R(Args...)>> p)
    : _p(std::move(p))
  {
  }
};

template <typename S, typename F>
auto package(F&& f)
    -> std::pair<packaged_task<S>, future<detail::result_of_t_<S>>>
{
  auto p = std::make_shared<detail::shared<S>>(std::forward<F>(f));
  return std::make_pair(packaged_task<S>(p),
                        future<detail::result_of_t_<S>>(p));
}

}

#endif

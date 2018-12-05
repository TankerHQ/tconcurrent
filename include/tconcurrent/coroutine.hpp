#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#ifdef TCONCURRENT_COROUTINES_TS
#include <tconcurrent/stackless_coroutine.hpp>
#else
#include <tconcurrent/stackful_coroutine.hpp>
#endif

namespace tconcurrent
{
template <typename F>
auto async_resumable(F&& cb)
{
  return async_resumable({}, get_default_executor(), std::forward<F>(cb));
}

template <typename F>
auto async_resumable(std::string const& name, F&& cb)
{
  return async_resumable(name, get_default_executor(), std::forward<F>(cb));
}
}

#endif

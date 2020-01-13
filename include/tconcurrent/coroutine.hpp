#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#ifdef TCONCURRENT_COROUTINES_TS
#include <tconcurrent/stackless_coroutine.hpp>
#else
#include <tconcurrent/stackful_coroutine.hpp>
#endif

namespace tconcurrent
{
/** Schedule a resumable function
 *
 * \param name (optional) the name of the task, for debugging purposes.
 * \param executor (optional) the executor on which the task will be run
 * \param cb the callback to run. Its signature should be:
 *
 *     cotask<T> func();
 *
 * \return a future<T> corresponding to the result of the callback
 */
template <typename E, typename F>
auto async_resumable(std::string const& name, E&& executor, F&& cb);

/// See auto async_resumable(std::string const& name, E&& executor, F&& cb)
template <typename F>
auto async_resumable(F&& cb)
{
  return async_resumable({}, get_default_executor(), std::forward<F>(cb));
}

/// See auto async_resumable(std::string const& name, E&& executor, F&& cb)
template <typename F>
auto async_resumable(std::string const& name, F&& cb)
{
  return async_resumable(name, get_default_executor(), std::forward<F>(cb));
}
}

#endif

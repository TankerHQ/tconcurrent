#ifndef TCONCURRENT_COROUTINE_HPP
#define TCONCURRENT_COROUTINE_HPP

#if TCONCURRENT_COROUTINES_TS
#include <tconcurrent/stackless_coroutine.hpp>
#else
#include <tconcurrent/stackful_coroutine.hpp>
#endif

namespace tconcurrent
{
namespace lazy
{
/** Return a sender that will run the resumable function.
 *
 * \p cb must be stateless! All state must be passed through \p args
 *
 * Note that the function will first run synchronously when the sender is
 * invoked. The executor is only used when resuming the coroutine execution.
 *
 * \param executor the executor on which the task will resume
 * \param name the name of the task, for debugging purposes.
 * \param cb the callback to run. Its signature should be:
 *
 *     cotask<T> func();
 *
 * \param args the arguments to pass to \p cb
 *
 * \return a sender that will run the callback and call the receiver with the
 * result
 */
template <typename E, typename F, typename... Args>
auto run_resumable(E&& executor, std::string name, F&& cb, Args&&... args);
}

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

/** Run a callable on the thread context.
 *
 * When called from a stackful coroutine, this function will switch to the
 * thread context and default stack, run the function, and switch back to the
 * coroutine context. When called from a stackless coroutine, or from out of a
 * coroutine, it will just call \p f.
 *
 * This function is not a suspension or cancelation point.
 *
 * \returns the value returned by \p f
 * \throws the exception thrown by \p f
 */
template <typename F>
auto dispatch_on_thread_context(F&& f);
}

#endif

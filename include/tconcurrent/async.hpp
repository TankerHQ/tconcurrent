#ifndef TCONCURRENT_ASYNC_HPP
#define TCONCURRENT_ASYNC_HPP

#include <tconcurrent/executor.hpp>
#include <tconcurrent/packaged_task.hpp>

namespace tconcurrent
{

/** Run a task on the given executor
 *
 * \param name (optional) the name of the task, for debugging purposes.
 * \param executor (optional) the executor on which the task will be run
 * \param f the callback to run which must have one of the following signatures:
 *
 *     T func();
 *     T func(cancelation_token&);
 *
 * \return a future<T> corresponding to the result of the function
 */
template <typename E, typename F>
auto async(std::string const& name, E&& executor, F&& f)
{
  using result_type = std::decay_t<packaged_task_result_type<F()>>;

  auto pack = package_cancelable<result_type()>(std::forward<F>(f));

  executor.post(std::move(std::get<0>(pack)),
                name + " (" + typeid(F).name() + ")");
  return std::move(std::get<1>(pack)).update_chain_name(name);
}

/// See async(std::string const& name, E&& executor, F&& f)
template <typename F>
auto async(std::string const& name, F&& f)
{
  return async(name, get_default_executor(), std::forward<F>(f));
}

/// See async(std::string const& name, E&& executor, F&& f)
template <typename E,
          typename F,
          typename = std::enable_if_t<
              !std::is_convertible<std::decay_t<E>, std::string>::value>>
auto async(E&& executor, F&& f)
{
  return async({}, std::forward<E>(executor), std::forward<F>(f));
}

/// See async(std::string const& name, E&& executor, F&& f)
template <typename F>
auto async(F&& f)
{
  return async({}, get_default_executor(), std::forward<F>(f));
}

/** Run f synchronously and returns a future containing the result
 *
 * Actually calls async(get_synchronous_executor(), f).
 */
template <typename F>
auto sync(F&& f)
{
  return async(get_synchronous_executor(), std::forward<F>(f));
}
}

#endif

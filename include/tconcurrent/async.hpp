#ifndef TCONCURRENT_ASYNC_HPP
#define TCONCURRENT_ASYNC_HPP

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{

template <typename E, typename F>
auto async(E&& executor, F&& f)
{
  using result_type = std::decay_t<packaged_task_result_type<F()>>;

  auto pack = package<result_type()>(std::forward<F>(f));

  executor.post(std::move(std::get<0>(pack)));
  return std::get<1>(pack);
}

template <typename F>
auto async(F&& f)
{
  return async(get_default_executor(), std::forward<F>(f));
}

}

#endif

#ifndef TCONCURRENT_ASYNC_HPP
#define TCONCURRENT_ASYNC_HPP

#include <tconcurrent/packaged_task.hpp>
#include <tconcurrent/thread_pool.hpp>

namespace tconcurrent
{

template <typename F>
auto async(F&& f) -> future<typename std::decay<decltype(f())>::type>
{
  using result_type = typename std::decay<decltype(f())>::type;

  auto pack = package<result_type()>(std::forward<F>(f));

  get_default_executor().post(std::move(std::get<0>(pack)));
  return std::get<1>(pack);
}

}

#endif
